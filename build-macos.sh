#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
arch="${1:-$(uname -m)}"
case "$arch" in arm64|x86_64|universal) ;; *) echo 'usage: build-macos.sh [arm64|x86_64|universal]' >&2; exit 2;; esac
architectures="$arch"
if [[ "$arch" == universal ]]; then architectures='arm64;x86_64'; fi
cmake -S . -B "build/macos-$arch" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_OSX_ARCHITECTURES="$architectures"
cmake --build "build/macos-$arch" --parallel 3
ctest --test-dir "build/macos-$arch" --output-on-failure
mkdir -p "dist/macos-$arch"
cp "build/macos-$arch/reaper_media_wave_thumbnails.dylib" "dist/macos-$arch/"
codesign --force --sign - "dist/macos-$arch/reaper_media_wave_thumbnails.dylib"
file "dist/macos-$arch/reaper_media_wave_thumbnails.dylib"
