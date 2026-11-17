#!/bin/bash
# Parallax: build Parallax.vst3 (Release, Apple Silicon), run the tests, install for Ableton Live.
#   ./build_mac.sh              build + tests + install
#   SKIP_TESTS=1 ./build_mac.sh build + install only
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

command -v cmake >/dev/null || { echo "CMake is missing. Install it with:  brew install cmake   (or from cmake.org)"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode Command Line Tools are missing. Run:  xcode-select --install"; exit 1; }

BUILD="$ROOT/build-mac"
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

cmake -S . -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DPLX_BUILD_TESTS=ON
cmake --build "$BUILD" --config Release -j "$(sysctl -n hw.ncpu)" \
      --target Parallax_VST3 ParallaxTests PLXTestDrive_VST3 PLXTestLatency_VST3

TESTS="skipped"
if [ "${SKIP_TESTS:-0}" != "1" ]; then
    T="$(find "$BUILD" -path '*ParallaxTests_artefacts*' -type f -name ParallaxTests | head -1)"
    TESTS="passed"
    echo "== Engine tests"; "$T" "$BUILD" || TESTS="FAILED (see above)"
fi

OUT="$(find "$BUILD" -path '*Parallax_artefacts*' -type d -name Parallax.vst3 | head -1)"
codesign --force --deep --sign - "$OUT"

DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/Parallax.vst3"
cp -R "$OUT" "$DEST/"
xattr -dr com.apple.quarantine "$DEST/Parallax.vst3" 2>/dev/null || true

echo
echo "Tests:        $TESTS"
echo "Built:        $OUT"
echo "Installed:    $DEST/Parallax.vst3"
echo "In Live: Settings > Plug-Ins > enable 'Use VST3 Plug-In System Folders' and press Rescan."
