"""Host-side weather snapshots for the USB screen's offline clock page."""
from __future__ import annotations

import json
import math
import re
import threading
import time
import urllib.request
from pathlib import Path


def fetch_text(url: str) -> str:
    request = urllib.request.Request(url, headers={
        'User-Agent': 'Mozilla/5.0', 'Referer': 'http://www.weather.com.cn/'})
    with urllib.request.urlopen(request, timeout=8) as response:
        data = response.read(131073)
        if len(data) > 131072:
            raise ValueError('weather response too large')
        return data.decode('utf-8')


def js_object(text: str, marker: str):
    start = text.index(marker) + len(marker)
    start = text.index('{', start)
    return json.JSONDecoder().raw_decode(text[start:])[0]


def scalar_text(value, limit: int = 40) -> str:
    if not isinstance(value, (str, int, float)) or isinstance(value, bool):
        raise ValueError('invalid weather text')
    value = str(value)
    if any(ord(c) < 32 for c in value) or len(value) > limit:
        raise ValueError('invalid weather text length')
    return value


def parse_weather(text: str, now: float | None = None) -> dict:
    live = js_object(text, 'dataSK =')
    daily = js_object(text, '"weatherinfo":')
    forecast = js_object(text, '"f":[')
    temperature = float(live['temp'])
    humidity = int(str(live['SD']).rstrip('%'))
    code = int(str(live['weathercode']).removeprefix('n').removeprefix('d'))
    low, high = int(forecast['fd']), int(forecast['fc'])
    if (not math.isfinite(temperature) or not -60 <= temperature <= 60 or
            not 0 <= humidity <= 100 or not 0 <= code <= 999 or
            not -60 <= low <= high <= 60):
        raise ValueError('weather values out of range')
    try:
        aqi = int(live.get('aqi', -1))
    except (ValueError, TypeError):
        aqi = -1
    return dict(city=scalar_text(live.get('cityname') or daily['city'], 12),
                temp=temperature, humidity=humidity, code=code,
                aqi=aqi if 0 <= aqi <= 999 else -1,
                weather=scalar_text(live.get('weather') or daily['weather'], 24),
                low=low, high=high,
                wind=scalar_text(str(live.get('WD', '')) + str(live.get('WS', '')), 24),
                at=time.strftime('%m-%d %H:%M', time.localtime(time.time() if now is None else now)))


class WeatherCache:
    def __init__(self, settings):
        self.settings = settings
        self.path = settings.path.with_name('weather-cache.json')
        self.lock = threading.Lock()
        self.snapshot = None
        self.city = None
        try:
            if self.path.stat().st_size <= 4096:
                saved = json.loads(self.path.read_text(encoding='utf-8'))
                # Cache is only reused for the matching city, with the original timestamp.
                if saved['city'] == settings.get().weather_city_code and isinstance(saved['data'], dict):
                    self.snapshot, self.city = saved['data'], saved['city']
        except (OSError, ValueError, KeyError, TypeError):
            pass

    def payload(self) -> bytes:
        with self.lock:
            if self.city != self.settings.get().weather_city_code or self.snapshot is None:
                return b''
            value = json.dumps(self.snapshot, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
            return b'MSW1,' + value if len(value) <= 900 else b''

    def run(self, stop: threading.Event):
        attempted_city, next_fetch = None, 0.0
        while not stop.is_set():
            city = self.settings.get().weather_city_code
            if city != attempted_city or time.monotonic() >= next_fetch:
                attempted_city = city
                try:
                    resolved = city
                    if city == '0':
                        geo = fetch_text('http://wgeo.weather.com.cn/ip/')
                        match = re.search(r'id\s*=\s*["\'](\d{9})["\']', geo)
                        if not match:
                            raise ValueError('weather city could not be detected')
                        resolved = match.group(1)
                    value = parse_weather(fetch_text(
                        f'http://d1.weather.com.cn/weather_index/{resolved}.html?_={int(time.time())}'))
                    with self.lock:
                        self.snapshot, self.city = value, city
                    self.path.parent.mkdir(parents=True, exist_ok=True)
                    temporary = self.path.with_suffix('.tmp')
                    temporary.write_text(json.dumps(dict(city=city, data=value), ensure_ascii=False), encoding='utf-8')
                    temporary.replace(self.path)
                    next_fetch = time.monotonic() + 1800
                except (OSError, ValueError, KeyError, TypeError):
                    next_fetch = time.monotonic() + 60
            stop.wait(1)
