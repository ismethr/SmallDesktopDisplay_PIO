#ifndef MINIDISPLAY_LINE_READER_H
#define MINIDISPLAY_LINE_READER_H

#include <stddef.h>

namespace minidisplay {

// Splits a byte stream into '\n'-terminated lines ('\r' is ignored). A line
// longer than Capacity is discarded whole instead of being truncated.
template <size_t Capacity>
class LineReader {
 public:
  // Returns the completed, NUL-terminated line, valid until the next push(),
  // or nullptr while a line is still incomplete, empty or overlong.
  char *push(char next) {
    if (next == '\r') return nullptr;
    if (next != '\n') {
      if (length_ < Capacity) buffer_[length_++] = next;
      else overflow_ = true;
      return nullptr;
    }
    const bool complete = !overflow_ && length_ > 0;
    buffer_[length_] = '\0';
    length_ = 0;
    overflow_ = false;
    return complete ? buffer_ : nullptr;
  }

 private:
  char buffer_[Capacity + 1] = {};
  size_t length_ = 0;
  bool overflow_ = false;
};

}  // namespace minidisplay

#endif
