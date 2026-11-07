param([string]$ReaperRoot='K:\REAPER-test')
$ErrorActionPreference='Stop'
$ReaperRoot=[IO.Path]::GetFullPath($ReaperRoot)
$exe=Join-Path $ReaperRoot 'reaper.exe'
if(-not (Test-Path -LiteralPath $exe)){throw "未找到 REAPER：$exe"}
if(Get-Process reaper -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe}){throw '请先正常退出此目录的 REAPER 再安装'}
$dll=Join-Path $PSScriptRoot 'dist/reaper_media_wave_thumbnails.dll'
if(-not (Test-Path -LiteralPath $dll)){$dll=Join-Path $PSScriptRoot 'reaper_media_wave_thumbnails.dll'}
if(-not (Test-Path -LiteralPath $dll)){throw '缺少已编译 DLL，请先执行 build.ps1'}
$plugins=Join-Path $ReaperRoot 'UserPlugins'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
$destination=Join-Path $plugins 'reaper_media_wave_thumbnails.dll'
if(Test-Path -LiteralPath $destination){Copy-Item -LiteralPath $destination -Destination ($destination+'.previous') -Force}
Copy-Item -LiteralPath $dll -Destination $destination -Force
Get-FileHash -LiteralPath $destination -Algorithm SHA256
