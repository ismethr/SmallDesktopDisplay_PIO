#!/bin/sh
set -eu
repository_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preview_output="${repository_root}/build/status_preview"
font_directory="${repository_root}/mac_status_display/.pio/libdeps/esp12e/TFT_eSPI/Fonts"
if [ ! -f "${font_directory}/Font16.c" ]; then
  echo "Build the status firmware first to install its pinned TFT_eSPI dependency." >&2
  exit 2
fi
mkdir -p "${preview_output}"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror \
  -I "${repository_root}/tools/status_preview" -I "${font_directory}" \
  -I "${repository_root}/mac_status_display/include" \
  "${repository_root}/tools/status_preview/preview.cpp" -o "${preview_output}/status-preview"
"${preview_output}/status-preview" "${preview_output}"
