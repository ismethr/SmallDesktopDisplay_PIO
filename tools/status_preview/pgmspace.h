#pragma once
#include <cstring>
#ifndef PROGMEM
#define PROGMEM
#endif
#define pgm_read_ptr(address) (*(address))
#define memcpy_P(destination, source, count) std::memcpy(destination, source, count)
