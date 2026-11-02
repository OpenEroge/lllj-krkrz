#!/usr/bin/env bash
# Download the prebuilt LGPL shared FFmpeg used by the movie backend
# (plugins/lllj_compat/src/FFmpegMoviePlayer.cpp) into third_party/ffmpeg/.
#
# Source: https://github.com/BtbN/FFmpeg-Builds (LGPL build: native wmv3/vc1/
# wmapro/h264/aac decoders, no GPL parts). BtbN prunes old autobuild releases,
# so the tarball is kept next to the extracted tree; if the pinned tag is gone,
# pick a newer n8.1 lgpl-shared asset and update TAG/ASSET/SHA256 below
# (soname major versions stay the same within 8.1).
set -euo pipefail
LLLJ="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$LLLJ/third_party/ffmpeg"

TAG="autobuild-2026-11-01-12-49"
ASSET="ffmpeg-n8.1.3-29-gad3b795b72-linux64-lgpl-shared-8.1"
SHA256="bcf24617e957f296e763e9f5d208065f27483f07aba433bc09d573f4d483c7fe"
URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/$TAG/$ASSET.tar.xz"

if [[ -f "$DEST/.asset" && "$(cat "$DEST/.asset")" == "$ASSET" ]]; then
	exit 0
fi

mkdir -p "$DEST"
TARBALL="$DEST/$ASSET.tar.xz"
if [[ ! -f "$TARBALL" ]] || ! echo "$SHA256  $TARBALL" | sha256sum -c --status; then
	echo "fetch_ffmpeg: downloading $URL" >&2
	curl -fL --retry 3 -o "$TARBALL.part" "$URL"
	mv "$TARBALL.part" "$TARBALL"
fi
echo "$SHA256  $TARBALL" | sha256sum -c --status || { echo "fetch_ffmpeg: sha256 mismatch for $TARBALL" >&2; exit 1; }

rm -rf "$DEST/include" "$DEST/lib"
tar -xJf "$TARBALL" -C "$DEST" --strip-components=1 "$ASSET/include" "$ASSET/lib" "$ASSET/LICENSE.txt"
echo "$ASSET" > "$DEST/.asset"
echo "fetch_ffmpeg: $ASSET -> $DEST" >&2
