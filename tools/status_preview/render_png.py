"""Convert status-preview PPM frames to 2x PNGs and a labelled contact sheet."""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ORDER = (
    "waiting", "live", "low-quota", "cached", "missing", "maximum",
    "offline-unsynced", "offline", "midnight", "reconnected",
)
SCALE = 2
COLUMNS = 5
LABEL_HEIGHT = 22


def main(directory: Path) -> int:
    frames = [(name, directory / f"{name}.ppm") for name in ORDER]
    missing = [str(path) for _, path in frames if not path.is_file()]
    if missing:
        print("missing previews: " + ", ".join(missing), file=sys.stderr)
        return 1
    tile = 240 * SCALE
    rows = (len(frames) + COLUMNS - 1) // COLUMNS
    sheet = Image.new("RGB", (COLUMNS * (tile + 8) + 8, rows * (tile + LABEL_HEIGHT + 8) + 8), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for index, (name, path) in enumerate(frames):
        with Image.open(path) as source:
            image = source.convert("RGB").resize((tile, tile), Image.NEAREST)
        image.save(directory / f"{name}.png")
        x = 8 + (index % COLUMNS) * (tile + 8)
        y = 8 + (index // COLUMNS) * (tile + LABEL_HEIGHT + 8)
        draw.text((x + 2, y + 4), name, fill=(200, 200, 200))
        sheet.paste(image, (x, y + LABEL_HEIGHT))
    sheet.save(directory / "contact-sheet.png")
    print(f"wrote {len(frames)} PNG previews and {directory / 'contact-sheet.png'}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: render_png.py PREVIEW_DIRECTORY", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(Path(sys.argv[1])))
