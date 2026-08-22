# Third-party notices

This file records provenance and rights that are separate from the repository's
GNU AGPL-3.0 licence. It is informational and does not replace the original
licence texts.

## Project lineage

This repository is a modified version of
[KittenCN/SmallDesktopDisplay_PIO](https://github.com/KittenCN/SmallDesktopDisplay_PIO),
which in turn credits
[chuxin520922/SmallDesktopDisplay](https://github.com/chuxin520922/SmallDesktopDisplay).
The Git history and original author notices are retained. The current branch's
prominent modification notice and date are in `README.md`.

## Vendored and simulator components

- `lib/TJpg_Decoder_ArrayOnly` contains a modified, array-only form of Bodmer's
  TJpg_Decoder and ChaN's Tiny JPEG Decompressor. Its retained licence and
  copyright texts are in `lib/TJpg_Decoder_ArrayOnly/LICENSE.txt`.
- The Windows simulator downloads SDL 3.4.14 during configuration. Its zlib
  licence, copyright and pinned archive hash are retained in
  `simulator/THIRD_PARTY_NOTICES.txt` and included in simulator packages.
- PlatformIO downloads the pinned build dependencies listed in `platformio.ini`.
  They are not stored in this repository. Their package licences include MIT
  (ArduinoJson, Button2, DHT sensor library, Thread, WiFiManager), Apache-2.0
  (Adafruit Unified Sensor), LGPL-2.1-or-later (Time) and the notices shipped by
  TFT_eSPI. Redistributors of binaries should retain the notices supplied with
  those packages.

## ChatGPT/Codex icon and names

`src/img/chatgpt_24.h` is a 24 x 24 monochrome rasterisation, at the original
aspect ratio, of CodexBar's `ProviderIcon-codex.svg`. CodexBar is copyright
Peter Steinberger and distributed under the
[MIT License](https://github.com/steipete/CodexBar/blob/main/LICENSE).

OpenAI, ChatGPT, Codex and the associated mark are trademarks or marks of their
respective owner. They are used only to identify interoperability and the data
shown by this independent community project. This project is not affiliated
with, sponsored by or endorsed by OpenAI. The software licences do not grant
trademark rights.

## Embedded artwork, fonts and weather images

The animation frame arrays, weather images and several bitmap/font resources
were inherited from the upstream repository. The available history does not
document a separate original source or artwork licence for every asset.
Character artwork and names may also be protected by third-party copyright or
trademark rights. Do not assume that the repository's software licence grants
rights to third-party characters or brands. Before commercial or branded
redistribution, independently verify the relevant rights or replace those
assets with material you are authorised to use. The animation replacement
tool is documented in `src/Animate/README.md`.
