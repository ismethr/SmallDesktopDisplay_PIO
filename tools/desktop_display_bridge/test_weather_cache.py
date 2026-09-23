import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from bridge_settings import SettingsState
from weather_cache import WeatherCache, parse_weather


class WeatherTests(unittest.TestCase):
    def document(self, **changes):
        live = dict(temp='21.5', SD='60%', cityname='城关', weather='阴',
                    weathercode='d02', aqi='30', WD='南风', WS='1级')
        live.update(changes)
        return ('var dataSK = ' + json.dumps(live) +
                ';var cityDZ={"weatherinfo":{"city":"城关","weather":"阴"}};'
                'var dataFC={"f":[{"fd":"12","fc":"23"}]}')

    def test_live_weather_preserves_chinese_and_timestamp(self):
        result = parse_weather(self.document(), 0)
        self.assertEqual('城关', result['city'])
        self.assertEqual(2, result['code'])
        self.assertEqual(21.5, result['temp'])
        self.assertEqual(60, result['humidity'])
        self.assertEqual('南风1级', result['wind'])
        self.assertEqual(11, len(result['at']))

    def test_malformed_or_partial_weather_cannot_replace_valid_snapshot(self):
        for changes in [dict(temp='nan'), dict(SD='120%'), dict(weather='x'*100), dict(weathercode='bad')]:
            with self.assertRaises((ValueError, KeyError)):
                parse_weather(self.document(**changes))
        with self.assertRaises(ValueError):
            parse_weather('var dataSK = {}')

    def test_cached_snapshot_survives_network_failure_but_not_city_change(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = SettingsState(Path(directory) / 'settings.json')
            data = parse_weather(self.document(), 0)
            settings.path.with_name('weather-cache.json').write_text(
                json.dumps(dict(city='0', data=data)), encoding='utf-8')
            cache = WeatherCache(settings)
            payload = cache.payload()
            stop = threading.Event()
            def fail(*args):
                stop.set()
                raise OSError('offline')
            with patch('weather_cache.fetch_text', side_effect=fail):
                cache.run(stop)
            self.assertEqual(payload, cache.payload())
            self.assertTrue(payload.startswith(b'MSW1,'))
            settings.update(dict(weather_city_code='101010100'), 0)
            self.assertEqual(b'', cache.payload())

    def test_city_code_cannot_inject_url_path(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = SettingsState(Path(directory) / 'settings.json')
            for value in ['../other', 'http://other', 101010100, '123456789']:
                with self.assertRaises(ValueError):
                    settings.update(dict(weather_city_code=value), 0)


if __name__ == '__main__':
    unittest.main()
