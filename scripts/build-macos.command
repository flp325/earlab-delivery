#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "请在 macOS 上运行此脚本。" >&2
    exit 1
fi
missing=0
if ! xcode-select -p >/dev/null 2>&1 ||
   ! xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1 ||
   ! xcrun --sdk macosx clang++ --version >/dev/null 2>&1 ||
   ! xcrun --find make >/dev/null 2>&1; then
    printf '%s\n' "Apple 编译工具或 macOS SDK 不可用。" \
        "打开「终端」，运行 xcode-select --install，在弹窗中完成安装。" \
        "如果已经安装完整 Xcode，请打开 Xcode 完成首次设置，并在 Settings → Locations 中选择 Command Line Tools。" >&2
    missing=1
fi

supported_version() {
    local version
    version="$("$1" --version 2>/dev/null)" || return 1
    [[ "$version" =~ ^(cmake|ctest)[[:space:]]version[[:space:]]([0-9]+)\.([0-9]+) ]] || return 1
    (( BASH_REMATCH[2] > 3 || (BASH_REMATCH[2] == 3 && BASH_REMATCH[3] >= 22) ))
}

# Finder may open Terminal without a Homebrew or CMake.app bin directory in PATH.
cmake_bin=""
for candidate in "$(command -v cmake || true)" /Applications/CMake.app/Contents/bin/cmake \
                 "$HOME/Applications/CMake.app/Contents/bin/cmake" /opt/homebrew/bin/cmake /usr/local/bin/cmake; do
    [[ -x "$candidate" ]] || continue
    ctest_bin="$(dirname "$candidate")/ctest"
    if supported_version "$candidate" && supported_version "$ctest_bin"; then
        cmake_bin="$candidate"
        break
    fi
done
if [[ -z "$cmake_bin" ]]; then
    printf '%s\n' "需要 CMake 3.22 或更高版本，以及同一套工具中的 CTest。" \
        "打开 https://cmake.org/download/ ，下载适用于这台 Mac 的 macOS .dmg，将 CMake.app 拖入「应用程序」。" \
        "已使用 Homebrew 的用户也可在终端运行 brew install cmake；更新旧版请运行 brew upgrade cmake。" >&2
    missing=1
fi
(( missing == 0 )) || exit 1
build="$root/build/macos-universal"
output="$root/dist/macos"
mkdir -p "$output"
"$cmake_bin" -S "$root/app" -B "$build" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
"$cmake_bin" --build "$build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
"$ctest_bin" --test-dir "$build" --output-on-failure
app="$build/EarLabEngine_artefacts/Release/EarLab Engine.app"
plist="$app/Contents/Info.plist"
plutil -lint "$plist"
exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print CFBundleExecutable' "$plist")"
version="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$plist")"
lipo "$exe" -verify_arch arm64 x86_64
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
"$exe" --check-trims

tmp="$(mktemp -d "$output/.package.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT
stage="$tmp/image"
mkdir -p "$stage"
ditto "$app" "$stage/EarLab Engine.app"
ln -s /Applications "$stage/Applications"
cp "$root/installer/LICENSE" "$stage/LICENSE.txt"
cp "$root/installer/THIRD-PARTY-NOTICES-macOS.txt" "$stage/THIRD-PARTY-NOTICES.txt"
cp "$root/third_party/JUCE/LICENSE.md" "$stage/JUCE-LICENSE.md"
cp "$root/MACOS.md" "$stage/MACOS.md"
name="EarLab-$version-macOS-universal.dmg"
hdiutil create -volname "EarLab $version" -fs HFS+ -format UDZO -srcfolder "$stage" "$tmp/$name"
hdiutil verify "$tmp/$name"
mv -f "$tmp/$name" "$output/$name"
(cd "$output" && shasum -a 256 "$name" > "$name.sha256")
echo "已生成 $output/$name"
if [[ "${1:-}" == --show-in-finder ]]; then
    open -R "$output/$name" || echo "请在 Finder 中打开上述路径查看 DMG。"
fi
