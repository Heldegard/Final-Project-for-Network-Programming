<#
.SYNOPSIS
    Configure and build the LanKVM project with MinGW-w64 GCC, CMake and Ninja.

.DESCRIPTION
    Uses the toolchain under -ToolchainRoot (default D:\mygcc\mingw64),
    produces a statically linked executable so it can be copied to another
    machine without shipping libstdc++/libgcc DLLs.

.PARAMETER Config
    Build configuration: Release (default) or Debug.

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER Test
    Run ctest after a successful build.

.PARAMETER ToolchainRoot
    Root of the MinGW-w64 installation.

.PARAMETER BuildDir
    Override the build directory (default <project>\build\<config>).

.EXAMPLE
    .\build.ps1

.EXAMPLE
    .\build.ps1 -Config Debug -Clean -Test
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Test,
    [string]$ToolchainRoot = 'D:\mygcc\mingw64',
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $root ('build\' + $Config.ToLower())
}

$bin = Join-Path $ToolchainRoot 'bin'
$tools = [ordered]@{
    gcc   = Join-Path $bin 'gcc.exe'
    gpp   = Join-Path $bin 'g++.exe'
    cmake = Join-Path $bin 'cmake.exe'
    ninja = Join-Path $bin 'ninja.exe'
    ctest = Join-Path $bin 'ctest.exe'
}
foreach ($name in $tools.Keys) {
    if (-not (Test-Path -LiteralPath $tools[$name])) {
        throw "Missing $name at '$($tools[$name])'. Pass -ToolchainRoot <path>."
    }
}
$env:PATH = "$bin;$env:PATH"

$cmakeLists = Join-Path $root 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $cmakeLists)) {
    throw "No CMakeLists.txt in '$root'. The project skeleton has not been created yet."
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Host "Cleaning $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$configureArgs = @(
    '-S', $root,
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_C_COMPILER=$($tools.gcc)",
    "-DCMAKE_CXX_COMPILER=$($tools.gpp)",
    '-DCMAKE_EXE_LINKER_FLAGS=-static',
    '-DCMAKE_CXX_FLAGS=-Wall -Wextra',
    '-DCMAKE_C_FLAGS=-Wall -Wextra'
)

Write-Host "== configure ($Config) =="
& $tools.cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code $LASTEXITCODE" }

Write-Host '== build =='
& $tools.cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

if ($Test) {
    Write-Host '== test =='
    & $tools.ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "tests failed with exit code $LASTEXITCODE" }
}

Write-Host ''
Write-Host '== artifacts =='
Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter *.exe -ErrorAction SilentlyContinue |
    ForEach-Object { '{0,9:N0} KB  {1}' -f ($_.Length / 1KB), $_.FullName }
