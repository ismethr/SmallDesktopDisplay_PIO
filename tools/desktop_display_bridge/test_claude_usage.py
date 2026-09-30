from __future__ import annotations

import io
import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import claude_usage as claude
import claude_cli
import desktop_display_bridge as bridge
from bridge_settings import DisplaySettings, validate_settings

NOW = 1700000000
DATA = {"five_hour": {"utilization": 27.4, "resets_at": "2023-11-14T23:13:20Z"},
        "seven_day": {"utilization": 58, "resets_at": "2023-11-20T00:00:00+00:00"}}


class ClaudeUsageTests(unittest.TestCase):
    def test_percent_is_quota_remaining_not_context_or_spend(self):
        state = claude.ClaudeUsageState()
        state.update({**DATA, "context_window": {"remaining_percentage": 1}}, NOW)
        result = state.get(NOW)
        self.assertEqual(72.6, result["five_hour"]["remaining_percent"])
        self.assertEqual(42, result["seven_day"]["remaining_percent"])
        self.assertFalse(result["stale"])

    def test_partial_windows_do_not_invent_quota(self):
        state = claude.ClaudeUsageState()
        state.update({"seven_day": DATA["seven_day"]}, NOW)
        self.assertIsNone(state.get(NOW)["five_hour"])
        self.assertTrue(state.get(NOW)["ok"])

    def test_zero_and_full_remaining_are_distinct_from_missing(self):
        for used in (0, 100):
            result = claude.parse_window({**DATA["five_hour"], "utilization": used}, NOW)
            self.assertEqual(100 - used, result["remaining_percent"])
        for value in (True, "10", -1, 101, float("nan"), float("inf"), None):
            self.assertIsNone(claude.parse_window({**DATA["five_hour"], "utilization": value}, NOW))

    def test_reset_timestamp_validation(self):
        for reset in ("garbage", 123, {}, "2023-11-14T23:13:20", "2023-01-01T00:00:00Z"):
            self.assertIsNone(claude.parse_window({"utilization": 10, "resets_at": reset}, NOW))
        self.assertIsNone(claude.parse_window({"utilization": 10, "resets_at": None}, NOW))

    def test_inactive_window_is_full_but_not_kept_indefinitely(self):
        state = claude.ClaudeUsageState()
        state.update({"five_hour": {"utilization": 0, "resets_at": None}}, NOW)
        self.assertEqual(100, state.get(NOW)["five_hour"]["remaining_percent"])
        self.assertIsNone(state.get(NOW + claude.STALE_SECONDS)["five_hour"])

    def test_network_failure_retains_data_only_until_reset(self):
        state = claude.ClaudeUsageState()
        state.update(DATA, NOW)
        state.set_error("network_error")
        self.assertEqual(72.6, state.get(NOW)["five_hour"]["remaining_percent"])
        self.assertTrue(state.get(NOW)["stale"])
        self.assertIsNone(state.get(NOW + 3600)["five_hour"])
        self.assertIsNotNone(state.get(NOW + 3600)["seven_day"])

    def test_data_ages_without_worker_running(self):
        state = claude.ClaudeUsageState()
        state.update(DATA, NOW)
        self.assertTrue(state.get(NOW + 301)["stale"])

    def test_logout_and_disabled_clear_previous_account(self):
        for code in ("login_required", "login_expired", "access_denied", "disabled"):
            state = claude.ClaudeUsageState()
            state.update(DATA, NOW)
            state.set_error(code)
            self.assertFalse(state.get(NOW)["ok"])
            self.assertIsNone(state.get(NOW)["fetched_at"])

    def test_snapshot_is_not_mutable_by_http_consumer(self):
        state = claude.ClaudeUsageState()
        state.update(DATA, NOW)
        state.get(NOW)["five_hour"]["remaining_percent"] = 999
        self.assertEqual(72.6, state.get(NOW)["five_hour"]["remaining_percent"])

    def test_aux_serial_crc_and_missing_window(self):
        state = claude.ClaudeUsageState()
        state.update({"seven_day": DATA["seven_day"]}, NOW)
        result = bridge.encode_claude_frame(state.get(NOW))
        body = b'MSA1,-1,420,0'
        self.assertEqual(b'$' + body + f'*{bridge.crc16_ccitt(body):04X}\n'.encode(), result)

    def test_credentials_are_opt_in_and_setting_requires_boolean(self):
        self.assertFalse(DisplaySettings().claude_enabled)
        for bad in ("true", 1, None):
            with self.assertRaises(ValueError):
                validate_settings({"claude_enabled": bad}, DisplaySettings())
        stop = mock.Mock()
        stop.is_set.side_effect = [False, True]
        with mock.patch.object(claude, "read_access_token") as read:
            claude.ClaudeUsageState().run(SimpleNamespace(get=lambda: DisplaySettings()), stop)
        read.assert_not_called()

    def test_read_token_is_bounded_and_does_not_rewrite_credentials(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(claude.os.environ, {"CLAUDE_CONFIG_DIR": directory}):
            path = Path(directory) / '.credentials.json'
            document = json.dumps({"claudeAiOauth": {"accessToken": "test-only-not-real", "scopes": ["user:profile"], "expiresAt": (NOW + 60) * 1000}})
            path.write_text(document)
            with mock.patch.object(claude.time, 'time', return_value=NOW):
                self.assertEqual('test-only-not-real', claude.read_access_token())
            self.assertEqual(document, path.read_text())
            with mock.patch.object(claude.time, 'time', return_value=NOW + 61):
                with self.assertRaisesRegex(claude.UsageError, 'login_expired'):
                    claude.read_access_token()
            path.write_bytes(b' ' * (claude.MAX_BYTES + 1))
            with self.assertRaises(claude.UsageError):
                claude.read_access_token()

    def test_authorization_is_fixed_destination_and_redirects_refused(self):
        response = io.BytesIO(json.dumps(DATA).encode())
        with mock.patch.object(claude.urllib.request, 'build_opener') as opener:
            opener.return_value.open.return_value = response
            self.assertEqual(DATA, claude.fetch_usage('test-only'))
            request = opener.return_value.open.call_args.args[0]
            self.assertEqual(claude.USAGE_URL, request.full_url)
            self.assertEqual('Bearer test-only', request.get_header('Authorization'))
        handler = claude.NoRedirect()
        self.assertIsNone(handler.redirect_request(request, None, 302, '', {}, 'https://example.com'))

    def test_error_body_does_not_reach_public_snapshot(self):
        for status, code in ((401, 'login_expired'), (403, 'access_denied'), (429, 'rate_limited'), (500, 'service_unavailable')):
            with mock.patch.object(claude.urllib.request, 'build_opener') as opener:
                opener.return_value.open.side_effect = urllib.error.HTTPError(claude.USAGE_URL, status, 'secret', {'Retry-After': '600'}, io.BytesIO(b'secret'))
                with self.assertRaises(claude.UsageError) as error:
                    claude.fetch_usage('test-only')
                self.assertEqual(code, str(error.exception))
                if status == 429:
                    self.assertEqual(600, error.exception.retry_after)

    def test_codexbar_style_retry_after_dates_and_bounds(self):
        self.assertEqual(600, claude.retry_after_seconds('Tue, 14 Nov 2023 22:23:20 GMT', NOW))
        self.assertEqual(300, claude.retry_after_seconds('nonsense', NOW))
        self.assertEqual(3600, claude.retry_after_seconds('99999', NOW))
        self.assertEqual(120, claude.retry_after_seconds('0', NOW))
        self.assertEqual(300, claude.retry_after_seconds('NaN', NOW))

    def test_missing_scope_and_unknown_expiry_never_sent(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(claude.os.environ, {"CLAUDE_CONFIG_DIR": directory}):
            path = Path(directory) / '.credentials.json'
            for oauth, code in [
                ({"accessToken": "test", "expiresAt": (NOW + 60) * 1000, "scopes": ["user:inference"]}, "missing_scope"),
                ({"accessToken": "test", "scopes": ["user:profile"]}, "login_expired"),
                ({"accessToken": "test", "expiresAt": float('nan')}, "login_expired"),
            ]:
                path.write_text(json.dumps({"claudeAiOauth": oauth}))
                with mock.patch.object(claude.time, 'time', return_value=NOW):
                    with self.assertRaisesRegex(claude.UsageError, code):
                        claude.read_access_token()

    def test_probe_only_runs_status_with_tools_hooks_and_mcp_disabled(self):
        args = claude_cli.probe_arguments(Path('claude.exe'))
        self.assertEqual('/status', args[-1])
        self.assertIn('--strict-mcp-config', args)
        self.assertEqual('', args[args.index('--allowed-tools') + 1])
        self.assertEqual('', args[args.index('--setting-sources') + 1])
        self.assertTrue(json.loads(args[args.index('--settings') + 1])['disableAllHooks'])
        with mock.patch.dict(claude_cli.os.environ, {'ANTHROPIC_API_KEY':'fake','CLAUDE_CODE_OAUTH_TOKEN':'fake','CLAUDE_CONFIG_DIR':'selected-profile'}):
            env = claude_cli.probe_environment()
        self.assertNotIn('ANTHROPIC_API_KEY', env)
        self.assertNotIn('CLAUDE_CODE_OAUTH_TOKEN', env)
        self.assertEqual('selected-profile', env['CLAUDE_CONFIG_DIR'])
        self.assertEqual('1', env['DISABLE_AUTOUPDATER'])

    def test_expired_credentials_are_delegated_and_reread_before_query(self):
        state = claude.ClaudeUsageState()
        stop = threading.Event()
        settings = SimpleNamespace(path=Path('unused/settings.json'), get=lambda: DisplaySettings(claude_enabled=True))
        def fetch(token):
            self.assertEqual('renewed-test-token', token)
            stop.set()
            return DATA
        with mock.patch.object(claude, 'read_access_token', side_effect=[claude.UsageError('login_expired'), 'renewed-test-token']), \
             mock.patch.object(claude, 'renew_credentials', return_value=True) as renew, \
             mock.patch.object(claude, 'fetch_usage', side_effect=fetch), \
             mock.patch.object(claude.time, 'time', return_value=NOW):
            state.run(settings, stop)
        renew.assert_called_once()
        self.assertTrue(state.get(NOW)['ok'])

    def test_failed_renewal_does_not_fetch_with_an_expired_token(self):
        state = claude.ClaudeUsageState()
        stop = mock.Mock()
        stop.is_set.side_effect = [False, False, True]
        settings = SimpleNamespace(path=Path('unused/settings.json'), get=lambda: DisplaySettings(claude_enabled=True))
        with mock.patch.object(claude, 'read_access_token', side_effect=claude.UsageError('login_expired')), \
             mock.patch.object(claude, 'renew_credentials', return_value=False), \
             mock.patch.object(claude, 'fetch_usage') as fetch:
            state.run(settings, stop)
        fetch.assert_not_called()
        self.assertEqual('login_expired', state.get()['error'])

    def test_opt_out_during_request_drops_result(self):
        stop = threading.Event()
        settings = mock.Mock()
        settings.get.side_effect = [DisplaySettings(claude_enabled=True), DisplaySettings()]
        def result(_token):
            stop.set()
            return DATA
        state = claude.ClaudeUsageState()
        with mock.patch.object(claude, 'read_access_token', return_value='test-only'), \
             mock.patch.object(claude, 'fetch_usage', side_effect=result), \
             mock.patch.object(claude.time, 'time', return_value=NOW):
            state.run(settings, stop)
        self.assertEqual('disabled', state.get(NOW)['error'])
        self.assertFalse(state.get(NOW)['ok'])


if __name__ == '__main__':
    unittest.main()
