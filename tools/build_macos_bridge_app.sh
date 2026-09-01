#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
repository_root="${script_dir:h}"
bridge_directory="${repository_root}/tools/desktop_display_bridge"
launcher="${bridge_directory}/macos_bridge_launcher.py"
smc_source="${bridge_directory}/macos_smc_temperature.c"
build_root="${repository_root}/build/macos_bridge_app"
helper_directory="${build_root}/helpers"
smc_binary="${helper_directory}/SmallDesktopDisplaySMC"
dist_directory="${build_root}/dist"
work_directory="${build_root}/work"
spec_directory="${build_root}/spec"
pyinstaller_config_directory="${build_root}/pyinstaller-config"
python_command="${PYTHON:-${repository_root}/.venv/bin/python}"
target_arch="${MACOS_BRIDGE_ARCH:-$(uname -m)}"
release_version="${BRIDGE_VERSION:-1.9.0}"

if [[ ! -x "${python_command}" ]]; then
  print -u2 "Python was not found: ${python_command}"
  print -u2 "Create .venv and install tools/desktop_display_bridge/requirements-build.txt first."
  exit 2
fi
if [[ ! -f "${launcher}" ]]; then
  print -u2 "macOS bridge launcher was not found: ${launcher}"
  exit 2
fi
if [[ ! -f "${smc_source}" ]]; then
  print -u2 "macOS SMC temperature helper source was not found: ${smc_source}"
  exit 2
fi
if [[ "${target_arch}" != "x86_64" && "${target_arch}" != "arm64" && "${target_arch}" != "universal2" ]]; then
  print -u2 "Unsupported MACOS_BRIDGE_ARCH: ${target_arch}"
  exit 2
fi
if [[ ! "${release_version}" =~ '^[0-9]+([.][0-9]+){0,2}$' ]]; then
  print -u2 "Unsupported BRIDGE_VERSION: ${release_version}"
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
  "${pyinstaller_config_directory}" \
  "${helper_directory}"
export PYINSTALLER_CONFIG_DIR="${pyinstaller_config_directory}"

compile_smc_helper() {
  local architecture="$1"
  local output="$2"
  local deployment_target="10.15"
  if [[ "${architecture}" == "arm64" ]]; then
    deployment_target="11.0"
  fi
  xcrun --sdk macosx clang -O2 -Wall -Wextra -Werror \
    -arch "${architecture}" \
    -mmacosx-version-min="${deployment_target}" \
    "${smc_source}" \
    -framework IOKit \
    -framework CoreFoundation \
    -o "${output}"
}

if ! command -v xcrun >/dev/null 2>&1; then
  print -u2 "xcrun was not found; Xcode Command Line Tools are required"
  exit 2
fi
if [[ "${target_arch}" == "universal2" ]]; then
  compile_smc_helper x86_64 "${helper_directory}/SmallDesktopDisplaySMC-x86_64"
  compile_smc_helper arm64 "${helper_directory}/SmallDesktopDisplaySMC-arm64"
  xcrun lipo -create \
    "${helper_directory}/SmallDesktopDisplaySMC-x86_64" \
    "${helper_directory}/SmallDesktopDisplaySMC-arm64" \
    -output "${smc_binary}"
else
  compile_smc_helper "${target_arch}" "${smc_binary}"
fi

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
  --add-binary "${smc_binary}:." \
  --add-data "${bridge_directory}/THIRD_PARTY_NOTICES.md:." \
  "${launcher}"

app_path="${dist_directory}/SmallDesktopDisplayBridge.app"
if [[ ! -d "${app_path}" ]]; then
  print -u2 "Expected app bundle was not produced: ${app_path}"
  exit 1
fi

info_plist="${app_path}/Contents/Info.plist"
if ! /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${release_version}" "${info_plist}" 2>/dev/null; then
  /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string ${release_version}" "${info_plist}"
fi
if ! /usr/libexec/PlistBuddy -c "Set :CFBundleVersion ${release_version}" "${info_plist}" 2>/dev/null; then
  /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string ${release_version}" "${info_plist}"
fi
if ! /usr/libexec/PlistBuddy -c "Add :LSUIElement bool true" "${info_plist}" 2>/dev/null; then
  /usr/libexec/PlistBuddy -c "Set :LSUIElement true" "${info_plist}"
fi
if [[ "$(plutil -extract LSUIElement raw -o - "${info_plist}")" != "true" ]]; then
  print -u2 "Failed to mark the app as a background UI agent"
  exit 1
fi

codesign --force --deep --sign - "${app_path}"

archive_path="${build_root}/SmallDesktopDisplayBridge-macos-${target_arch}.zip"
ditto -c -k --sequesterRsrc --keepParent "${app_path}" "${archive_path}"

print "macOS bridge app: ${app_path}"
print "archive: ${archive_path}"
file "${app_path}/Contents/MacOS/SmallDesktopDisplayBridge"
shasum -a 256 "${archive_path}"
