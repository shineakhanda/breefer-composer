param(
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$mingwBin = 'C:\Qt\Tools\mingw1310_64\bin'
$qtRoot   = 'C:\Qt\6.12.0\mingw_64'
$ffmpeg   = Join-Path $root '.toolchain\ffmpeg\ffmpeg-master-latest-win64-lgpl-shared'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $ninja) {
    $ninja = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}

foreach ($path in @($mingwBin, "$qtRoot\bin", "$ffmpeg\bin")) {
    if (Test-Path $path) { $env:PATH = "$path;$env:PATH" }
}

$buildDir = Join-Path $root 'build'
if ($Clean -and (Test-Path $buildDir)) { Remove-Item $buildDir -Recurse -Force }

& $cmake -S $root -B $buildDir -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$ninja" `
    -DCMAKE_C_COMPILER="$mingwBin/gcc.exe" `
    -DCMAKE_CXX_COMPILER="$mingwBin/g++.exe" `
    -DCMAKE_BUILD_TYPE=Release `
    -DQT_ROOT="$qtRoot" `
    -DFFMPEG_ROOT="$ffmpeg"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

& $cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

Write-Host "Built $buildDir\breefer.exe" -ForegroundColor Green

if ($Run) {
    & "$buildDir\breefer.exe" (Join-Path $root 'assets\demo.bcproj')
}
