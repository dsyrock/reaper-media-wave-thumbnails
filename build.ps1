$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path build,dist -Force | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -shared -static -DUNICODE -D_UNICODE -DNOMINMAX src/plugin.cpp -o build/reaper_media_wave_thumbnails.dll -lcomctl32 -lgdi32 -luser32
    if ($LASTEXITCODE -ne 0) { throw '编译失败' }
    Copy-Item build/reaper_media_wave_thumbnails.dll dist/reaper_media_wave_thumbnails.dll -Force
    Get-FileHash dist/reaper_media_wave_thumbnails.dll -Algorithm SHA256
} finally { Pop-Location }
