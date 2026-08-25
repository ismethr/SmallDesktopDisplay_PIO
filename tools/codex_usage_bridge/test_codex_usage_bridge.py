from __future__ import annotations

import base64
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import codex_usage_bridge as bridge


def jwt_with_account(account_id: str) -> str:
    payload = json.dumps(
        {"https://api.openai.com/auth": {"chatgpt_account_id": account_id}},
        separators=(",", ":"),
    ).encode()
    encoded = base64.urlsafe_b64encode(payload).decode().rstrip("=")
    return f"header.{encoded}.signature"


class BridgeTests(unittest.TestCase):
    def test_load_credentials_uses_account_id_from_jwt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "auth.json"
            path.write_text(
                json.dumps(
                    {
                        "tokens": {
                            "access_token": "secret-token",
                            "id_token": jwt_with_account("account-123"),
                        }
                    }
                ),
                encoding="utf-8",
            )
            credentials = bridge.load_credentials(path)
        self.assertEqual("secret-token", credentials.access_token)
        self.assertEqual("account-123", credentials.account_id)

    def test_parse_usage_prefers_weekly_window_and_clamps_percent(self) -> None:
        snapshot = bridge.parse_usage_response(
            {
                "rate_limit": {
                    "primary_window": {
                        "used_percent": 12.2,
                        "reset_at": 2_000,
                        "limit_window_seconds": 18_000,
                    },
                    "secondary_window": {
                        "used_percent": 101.2,
                        "reset_at": 8_200,
                        "limit_window_seconds": 604_800,
                    },
                }
            },
            now=1_000,
        )
        self.assertTrue(snapshot.valid)
        self.assertEqual(100, snapshot.used_percent)
        self.assertEqual(0, snapshot.remaining_percent)
        self.assertEqual(604_800, snapshot.window_seconds)
        self.assertEqual(120, snapshot.reset_minutes)

    def test_parse_usage_accepts_weekly_only_primary_shape(self) -> None:
        snapshot = bridge.parse_usage_response(
            {
                "rate_limit": {
                    "primary_window": {
                        "used_percent": 31,
                        "reset_at": 4_600,
                        "limit_window_seconds": 604_800,
                    },
                    "secondary_window": None,
                }
            },
            now=1_000,
        )
        self.assertEqual(31, snapshot.used_percent)
        self.assertEqual(69, snapshot.remaining_percent)
        self.assertEqual(60, snapshot.reset_minutes)

    def test_parse_usage_array_prefers_weekly_over_monthly(self) -> None:
        snapshot = bridge.parse_usage_response(
            {
                "rate_limits": [
                    {
                        "used_percent": 73,
                        "limit_window_seconds": 30 * 86400,
                    },
                    {
                        "used_percent": 28,
                        "limit_window_seconds": 7 * 86400,
                    },
                    {
                        "used_percent": 9,
                        "limit_window_seconds": 5 * 3600,
                    },
                ]
            },
            now=1_000,
        )
        self.assertEqual(28, snapshot.used_percent)
        self.assertEqual(72, snapshot.remaining_percent)
        self.assertEqual(7 * 86400, snapshot.window_seconds)

    def test_fetch_sends_token_and_returns_sanitized_snapshot(self) -> None:
        response_body = json.dumps(
            {
                "rate_limit": {
                    "primary_window": {
                        "used_percent": 42,
                        "reset_at": 4_102_444_800,
                        "limit_window_seconds": 604_800,
                    }
                }
            }
        ).encode()

        class FakeResponse:
            status = 200

            def __enter__(self) -> "FakeResponse":
                return self

            def __exit__(self, *_args: object) -> None:
                pass

            def read(self, _limit: int) -> bytes:
                return response_body

        with tempfile.TemporaryDirectory() as directory:
            auth_file = Path(directory) / "auth.json"
            auth_file.write_text(
                json.dumps(
                    {"tokens": {"access_token": "local-secret", "account_id": "acct"}}
                ),
                encoding="utf-8",
            )
            with mock.patch.object(
                bridge.urllib.request, "urlopen", return_value=FakeResponse()
            ) as urlopen:
                snapshot = bridge.fetch_usage(
                    auth_file,
                    "https://chatgpt.com/test-usage",
                    timeout=2,
                )

        request = urlopen.call_args.args[0]
        self.assertEqual("Bearer local-secret", request.get_header("Authorization"))
        self.assertEqual("acct", request.get_header("Chatgpt-account-id"))
        self.assertEqual(42, snapshot.used_percent)
        safe_json = json.dumps(snapshot.as_dict())
        self.assertNotIn("local-secret", safe_json)
        self.assertNotIn("acct", safe_json)

    def test_error_keeps_last_good_value_as_stale(self) -> None:
        state = bridge.UsageState()
        state.set_success(bridge.UsageSnapshot(valid=True, used_percent=25, remaining_percent=75))
        state.set_error("temporary failure")
        snapshot = state.get()
        self.assertTrue(snapshot.valid)
        self.assertTrue(snapshot.stale)
        self.assertEqual(25, snapshot.used_percent)
        self.assertEqual("temporary failure", snapshot.error)

    def test_public_snapshot_hides_internal_error_details(self) -> None:
        snapshot = bridge.UsageSnapshot(error="credential missing: /Users/private/auth.json")
        payload = snapshot.as_public_dict()
        self.assertEqual("unavailable", payload["error"])
        self.assertNotIn("/Users/private", json.dumps(payload))

        stale = bridge.UsageSnapshot(valid=True, stale=True, error="upstream details")
        self.assertEqual("stale", stale.as_public_dict()["error"])

    def test_upstream_url_rejects_token_exfiltration_targets(self) -> None:
        for url in (
            "http://chatgpt.com/backend-api/wham/usage",
            "https://example.invalid/usage",
            "https://chatgpt.com@example.invalid/usage",
        ):
            with self.subTest(url=url):
                with self.assertRaises(bridge.BridgeError):
                    bridge.validate_upstream_url(url)

        bridge.validate_upstream_url("https://chatgpt.com/backend-api/wham/usage")
        bridge.validate_upstream_url("http://127.0.0.1:8767/test")

    def test_server_start_does_not_require_reverse_dns(self) -> None:
        state = bridge.UsageState()
        server = bridge.BridgeHTTPServer(
            ("127.0.0.1", 0), bridge.make_handler(state), bind_and_activate=False
        )
        server.server_address = ("127.0.0.1", 12345)
        with mock.patch.object(bridge.TCPServer, "server_bind") as bind:
            with mock.patch("socket.getfqdn", side_effect=AssertionError("unexpected DNS")):
                server.server_bind()
        bind.assert_called_once_with(server)
        self.assertEqual("127.0.0.1", server.server_name)
        self.assertEqual(12345, server.server_port)
        server.server_close()


if __name__ == "__main__":
    unittest.main()
