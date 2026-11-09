#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 3
ctest --test-dir build/linux --output-on-failure
mkdir -p dist/linux
cp build/linux/reaper_media_wave_thumbnails.so dist/linux/
file dist/linux/reaper_media_wave_thumbnails.so
