import re
import hashlib
import struct
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


def read_vlw_glyphs(header: Path):
    source = header.read_text(encoding="utf-8")
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", source))
    if len(data) < 24:
        raise ValueError(f"{header} does not contain a VLW header")
    glyph_count = struct.unpack(">I", data[:4])[0]
    metrics_end = 24 + glyph_count * 28
    if len(data) < metrics_end:
        raise ValueError(f"{header} has a truncated VLW metrics table")

    glyphs = {}
    for index in range(glyph_count):
        record_start = 24 + index * 28
        codepoint, _, _, advance, _, _, _ = struct.unpack(
            ">7I", data[record_start : record_start + 28]
        )
        glyphs[chr(codepoint)] = advance
    ascent, descent = struct.unpack(">II", data[16:24])
    return glyphs, (ascent + descent) // 4


class DisplayFontCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.calendar_glyphs, cls.calendar_space_width = read_vlw_glyphs(
            REPOSITORY_ROOT / "src" / "font" / "font_td_20.h"
        )
        cls.weather_glyphs, _ = read_vlw_glyphs(
            REPOSITORY_ROOT / "src" / "font" / "ZdyLwFont_20.h"
        )

    def assert_calendar_text_supported(self, text):
        missing = sorted({char for char in text if char != " " and char not in self.calendar_glyphs})
        self.assertEqual([], missing, f"missing calendar glyphs: {missing!r}")

    def calendar_width(self, text):
        return sum(
            self.calendar_space_width if char == " " else self.calendar_glyphs[char]
            for char in text
        )

    def test_fixed_calendar_pages_and_statuses_are_supported(self):
        self.assert_calendar_text_supported("NTP WAIT" "0123456789月日周一二三四五六")
        for status in ("NTP WAIT", "12月31日 周六"):
            self.assertLessEqual(self.calendar_width(status), 150)

    def test_replacement_glyph_exists_in_weather_font(self):
        self.assertIn("-", self.weather_glyphs)

    def test_weather_wait_fallback_is_supported(self):
        missing = sorted(
            {char for char in "WEATHER WAIT" if char != " " and char not in self.weather_glyphs}
        )
        self.assertEqual([], missing, f"missing weather fallback glyphs: {missing!r}")

    def test_current_weather_city_is_supported(self):
        missing = sorted({char for char in "呈贡" if char not in self.weather_glyphs})
        self.assertEqual([], missing, f"missing current city glyphs: {missing!r}")

    def test_calendar_font_exactly_matches_its_manifest(self):
        manifest = (
            REPOSITORY_ROOT / "src" / "font" / "font_td_20_chars.txt"
        ).read_text(encoding="utf-8")
        expected = {char for char in manifest if char not in " \t\r\n"}
        self.assertEqual(expected, set(self.calendar_glyphs))

        source = (REPOSITORY_ROOT / "src" / "font" / "font_td_20.h").read_text(
            encoding="utf-8"
        )
        data = bytes(
            int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", source)
        )
        self.assertEqual(5046, len(data))
        self.assertEqual(
            "fbf481a10b6c4cc67ccc656d462540a1f2f859b79d61ae0df30fc0caf6c61c63",
            hashlib.sha256(data).hexdigest(),
        )


if __name__ == "__main__":
    unittest.main()
