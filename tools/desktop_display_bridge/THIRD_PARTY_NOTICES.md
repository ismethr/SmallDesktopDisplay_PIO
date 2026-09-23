# Third-party notices

Windows installers additionally distribute the unmodified official
[LibreHardwareMonitor 0.9.6](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/v0.9.6)
release under MPL-2.0. Its LICENSE and THIRD-PARTY-NOTICES are copied into the
installer's `licenses` directory, alongside the Python, pystray (LGPL-3.0),
Pillow, psutil, pyserial, six, certifi and PyInstaller license notices.
The bridge uses an independent read-only collector with the official library.
PawnIO is installed separately from its official release page; the bridge does
not silently install a kernel driver or change driver policy.
The corresponding bridge sources are delivered beside the installer as a ZIP.

The read-only AppleSMC structure and call sequence in
`macos_smc_temperature.c` are adapted from
[Stats](https://github.com/exelban/stats/tree/master/SMC).

MIT License

Copyright (c) 2019 Serhiy Mytrovtsiy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
