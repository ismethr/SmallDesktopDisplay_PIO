#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
repository_root="${script_dir:h}"
bridge_directory="${repository_root}/tools/desktop_display_bridge"
launcher="${bridge_directory}/macos_bridge_launcher.py"
build_root="${repository_root}/build/macos_bridge_app"
dist_directory="${build_root}/dist"
work_directory="${build_root}/work"
spec_directory="${build_root}/spec"
pyinstaller_config_directory="${build_root}/pyinstaller-config"
python_command="${PYTHON:-${repository_root}/.venv/bin/python}"
target_arch="${MACOS_BRIDGE_ARCH:-$(uname -m)}"

if [[ ! -x "${python_command}" ]]; then
  print -u2 "Python was not found: ${python_command}"
  print -u2 "Create .venv and install tools/desktop_display_bridge/requirements-build.txt first."
  exit 2
fi
if [[ ! -f "${launcher}" ]]; then
  print -u2 "macOS bridge launcher was not found: ${launcher}"
  exit 2
fi
if [[ "${target_arch}" != "x86_64" && "${target_arch}" != "arm64" && "${target_arch}" != "universal2" ]]; then
  print -u2 "Unsupported MACOS_BRIDGE_ARCH: ${target_arch}"
  exit 2
fi

"${python_command}" -c 'import PyInstaller' 2>/dev/null || {
  print -u2 "PyInstaller is not installed. Run:"
  print -u2 "  ${python_command} -m pip install -r tools/desktop_display_bridge/requirements-build.txt"
  exit 2
}

mkdir -p \
  "${dist_directory}" \
  "${work_directory}" \
  "${spec_directory}" \
  "${pyinstaller_config_directory}"
export PYINSTALLER_CONFIG_DIR="${pyinstaller_config_directory}"

"${python_command}" -m PyInstaller \
  --noconfirm \
  --clean \
  --windowed \
  --onedir \
  --name SmallDesktopDisplayBridge \
  --osx-bundle-identifier io.github.ismethr.SmallDesktopDisplayBridge \
  --target-arch "${target_arch}" \
  --distpath "${dist_directory}" \
  --workpath "${work_directory}" \
  --specpath "${spec_directory}" \
  --paths "${bridge_directory}" \
  --paths "${repository_root}/tools/codex_usage_bridge" \
  --hidden-import codex_usage_bridge \
  "${launcher}"

app_path="${dist_directory}/SmallDesktopDisplayBridge.app"
if [[ ! -d "${app_path}" ]]; then
  print -u2 "Expected app bundle was not produced: ${app_path}"
  exit 1
fi

codesign --force --deep --sign - "${app_path}"

archive_path="${build_root}/SmallDesktopDisplayBridge-macos-${target_arch}.zip"
ditto -c -k --sequesterRsrc --keepParent "${app_path}" "${archive_path}"

print "macOS bridge app: ${app_path}"
print "archive: ${archive_path}"
file "${app_path}/Contents/MacOS/SmallDesktopDisplayBridge"
shasum -a 256 "${archive_path}"
