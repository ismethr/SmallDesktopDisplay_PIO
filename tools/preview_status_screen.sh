#!/bin/sh
# Builds the real status-screen drawing code against a host raster adapter and
# checks layout, redraw and page-switching invariants. Windows: use
# tools/test_status_display.ps1, which also runs the protocol unit tests.
set -eu
repository_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preview_output="${repository_root}/build/status_preview"
firmware="${repository_root}/mac_status_display"
font_directory="${firmware}/.pio/libdeps/esp12e/TFT_eSPI/Fonts"
if [ ! -f "${font_directory}/Font32rle.c" ]; then
  echo "Build the status firmware first to install its pinned TFT_eSPI dependency." >&2
  exit 2
fi
mkdir -p "${preview_output}"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
  -I "${repository_root}/tools/status_preview" -I "${font_directory}" \
  -I "${firmware}/include" \
  "${repository_root}/tools/status_preview/preview.cpp" \
  "${firmware}/src/minidisplay_app.cpp" \
  "${firmware}/src/status_screen.cpp" \
  "${firmware}/src/country_flags.cpp" \
  "${firmware}/src/offline_screen.cpp" \
  -o "${preview_output}/status-preview"
"${preview_output}/status-preview" "${preview_output}"
