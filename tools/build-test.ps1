[CmdletBinding()]
param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++ first.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 tools are missing.' }
$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
if (-not (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe'))) { throw 'Visual Studio CMake component is missing.' }
$env:PATH = "$cmakeBin;$env:PATH"
& cmake -S $root -B (Join-Path $root 'build') -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
if ($SkipTests) {
    & cmake --build (Join-Path $root 'build') --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} else {
    & (Join-Path $root 'tests\validate.ps1') -Build (Join-Path $root 'build')
}
& cmake --install (Join-Path $root 'build') --config Release --prefix (Join-Path $root 'build\dist')
if ($LASTEXITCODE -ne 0) { throw 'Portable staging failed.' }
Write-Output "Ready: $root\build\dist\FileLockInspector.exe"
