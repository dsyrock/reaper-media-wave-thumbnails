param([Parameter(Mandatory=$true)][string]$TestResource)
$ErrorActionPreference='Stop'
$ini=Join-Path $TestResource 'reaper.ini'
$testExe=Join-Path $TestResource 'reaper.exe'
if (Get-Process reaper -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $testExe -and $_.MainWindowHandle -ne 0}) {throw '先正常退出独立测试实例'}
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public class MWIni {
[DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
[DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string s,string k,string d,StringBuilder v,int n,string p);
public static string Get(string s,string k,string p){var b=new StringBuilder(2048);GetPrivateProfileString(s,k,"0",b,2048,p);return b.ToString();}
}
'@
$count=[int][MWIni]::Get('reaper_explorer','NbShortcuts',$ini)
$backup=Join-Path $PSScriptRoot '../test-output/reaper-before-db.ini'
if(-not (Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $ini -Destination $backup}
[void][MWIni]::WritePrivateProfileString('reaper_explorer',('Shortcut'+$count),'MediaWaveThumbnailsTest.ReaperFileList',$ini)
[void][MWIni]::WritePrivateProfileString('reaper_explorer',('ShortcutT'+$count),'MediaWaveThumbnails test',$ini)
[void][MWIni]::WritePrivateProfileString('reaper_explorer','NbShortcuts',($count+1).ToString(),$ini)
Write-Output ('Database shortcut action: '+(42098+$count))
