#ifndef SDD_CLOCK_FONT_RENDERER_H
#define SDD_CLOCK_FONT_RENDERER_H

#include "../font/timeClockFont.h"

namespace sdd {

// The original weather-clock LineAtom renderer, shared with the USB screen.
// Keep the same digit cells, bitmap data, positions and colors as drawLineFont.
template <typename Display>
void drawClockDigit(Display &display, uint32_t x, uint32_t y, uint32_t digit,
                   uint32_t size, uint16_t color) {
  if (digit > 9 || size < 1 || size > 3) return;
  const LineAtom *atoms;
  uint8_t count;
  if (size == 1) {
    atoms = reinterpret_cast<const LineAtom *>(pgm_read_ptr(&smallLineFont[digit]));
    count = pgm_read_byte(&smallLineFont_size[digit]);
    display.fillRect(x, y, 9, 14, 0);
  } else if (size == 2) {
    atoms = reinterpret_cast<const LineAtom *>(pgm_read_ptr(&middleLineFont[digit]));
    count = pgm_read_byte(&middleLineFont_size[digit]);
    display.fillRect(x, y, 18, 30, 0);
  } else {
    atoms = reinterpret_cast<const LineAtom *>(pgm_read_ptr(&largeLineFont[digit]));
    count = pgm_read_byte(&largeLineFont_size[digit]);
    display.fillRect(x, y, 36, 90, 0);
  }
  for (uint8_t index = 0; index < count; ++index) {
    LineAtom atom;
    memcpy_P(&atom, atoms + index, sizeof(atom));
    display.drawFastHLine(x + atom.xValue, y + atom.yValue, atom.lValue, color);
  }
}

}  // namespace sdd
#endif
