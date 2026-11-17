#!/bin/bash
# Double-click to install Parallax.vst3 for the current user (no admin password needed).
cd "$(dirname "$0")"
DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/Parallax.vst3"
cp -R "Parallax.vst3" "$DEST/"
xattr -cr "$DEST/Parallax.vst3" 2>/dev/null; codesign --force --deep --sign - "$DEST/Parallax.vst3" 2>/dev/null
echo "Parallax installed: $DEST/Parallax.vst3"
echo "In Ableton Live: Settings > Plug-Ins > Rescan."
read -n 1 -s -r -p "Press any key to close"
