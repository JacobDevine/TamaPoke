#!/bin/bash
# Fetches the pinned SDL2 source into android/third_party/SDL2 (gitignored).
# The Android build compiles it from source and takes SDLActivity's Java from it.
#
#   bash android/fetch_sdl.sh
#
# Pinned by version AND hash: updating SDL is a deliberate act, the same rule
# CLAUDE.md applies to the vendored esp-web-tools.
set -euo pipefail
VER=2.32.10
SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HERE/third_party/SDL2"

if [ -f "$DEST/.version" ] && [ "$(cat "$DEST/.version")" = "$VER" ]; then
  echo "SDL2 $VER already present at $DEST"
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/sdl.tar.gz" \
  "https://github.com/libsdl-org/SDL/releases/download/release-$VER/SDL2-$VER.tar.gz"
echo "$SHA256  $TMP/sdl.tar.gz" | sha256sum -c - >/dev/null \
  || { echo "SDL2 tarball hash mismatch -- refusing to use it" >&2; exit 1; }
mkdir -p "$TMP/x"
tar -xzf "$TMP/sdl.tar.gz" -C "$TMP/x"
rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
mv "$TMP/x/SDL2-$VER" "$DEST"
echo "$VER" > "$DEST/.version"
echo "SDL2 $VER -> $DEST"
