# Downloads the compiler of the project, llvm-mingw (clang for i686 Windows), into toolchain\llvm-mingw.
#   powershell -ExecutionPolicy Bypass -File tools/get-toolchain.ps1 [-Version 20261006]
param([string]$Version = "20261006")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "toolchain\llvm-mingw"
if (Test-Path (Join-Path $dest "bin\i686-w64-mingw32-clang++.exe")) {
    "toolchain is already in $dest"
    exit 0
}
$name = "llvm-mingw-$Version-ucrt-x86_64"
$url = "https://github.com/mstorsjo/llvm-mingw/releases/download/$Version/$name.zip"
$zip = Join-Path ([IO.Path]::GetTempPath()) "$name.zip"
New-Item -ItemType Directory -Force (Join-Path $root "toolchain") | Out-Null
"downloading $url"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = "SilentlyContinue"  # the progress bar slows the download down a lot in Windows PowerShell
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
Expand-Archive -Path $zip -DestinationPath (Join-Path $root "toolchain") -Force
Move-Item (Join-Path $root "toolchain\$name") $dest
Remove-Item $zip
"toolchain is in $dest"
