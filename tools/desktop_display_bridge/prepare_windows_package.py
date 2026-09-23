"""Generate an ICO and copy distribution license notices for the installer."""
from importlib.metadata import PackageNotFoundError, distribution
from pathlib import Path
import sys
import re
import subprocess
import zipfile

from PIL import Image

root, staging = map(Path, sys.argv[1:])
with Image.open(root / 'tools/desktop_display_bridge/assets/MiniDisplayBridgeIcon.png') as icon:
    icon.save(staging / 'MiniDisplayBridge.ico', sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
for name in ('pystray', 'Pillow', 'psutil', 'pyserial', 'six', 'pyinstaller', 'certifi'):
    try:
        package = distribution(name)
    except PackageNotFoundError:
        if name == 'certifi':  # Optional: not installed in a minimal build venv.
            continue
        raise
    for file in package.files or []:
        if any(word in file.name.lower() for word in ('license', 'copying')) and '.dist-info' in str(file):
            source = Path(package.locate_file(file))
            if source.is_file():
                (staging / 'licenses' / (name + '-' + file.name + '.txt')).write_bytes(source.read_bytes())
(staging / 'licenses' / 'Python-LICENSE.txt').write_bytes(Path(sys.base_prefix, 'LICENSE.txt').read_bytes())

# Include the exact working sources used for the build, including new source
# files before their first commit. Git-ignored build products and credentials
# are excluded. This archive is delivered beside the installer.
version = re.search(r"StringStruct\('ProductVersion', '([^']+)'\)",
                    (root / 'tools/desktop_display_bridge/windows_version_info.txt').read_text()).group(1)
packages = root / 'build/packages'
packages.mkdir(parents=True, exist_ok=True)
files = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=root).decode('utf-8').split('\0')
with zipfile.ZipFile(packages / f'MiniDisplayBridge-{version}-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for relative in sorted(set(filter(None, files))):
        source = root / relative
        if source.is_file():
            archive.write(source, relative)
