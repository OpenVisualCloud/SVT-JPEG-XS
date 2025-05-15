<#
    Copyright(c) 2024 Intel Corporation
    SPDX-License-Identifier: BSD-2-Clause-Patent
#>

param(
    [string[]]$Args = $args
)

Set-Location -Path $PSScriptRoot

# Set defaults
$build = $true
$buildtype = "Debug"
$shared = "ON"
$GENERATOR = ""
$unittest = "OFF"
$vs = ""
$cmake_eflags = ""

# Set ASM_NASM environment variable to C:\Yasm\yasm.exe
$env:ASM_NASM = "C:\Yasm\yasm.exe"
Write-Host "ASM_NASM set to $($env:ASM_NASM)"
function Show-Help {
    Write-Host "PowerShell script to build SVT-AV1 on Windows"
    Write-Host "Usage: build.ps1 [2022|2019|2017|2015|clean] [release|debug] [nobuild] [test] [shared|static] [c-only]"
    exit 1
}

function Clean-BuildFolder {
    Write-Host "Cleaning build folder"
    Get-ChildItem -File | Where-Object { $_.Name -ne "build.ps1" } | Remove-Item -Force
    Get-ChildItem -Directory | Where-Object { $_.Name -ne "build.ps1" } | Remove-Item -Recurse -Force
    exit 0
}

# Parse arguments
$i = 0
while ($i -lt $Args.Count) {
    switch -Regex ($Args[$i].ToLower()) {
        "help|/help" { Show-Help }
        "clean" { Clean-BuildFolder }
        "2022" { Write-Host "Generating Visual Studio 2022 solution"; $GENERATOR = 'Visual Studio 17 2022'; $vs = "2022" }
        "2019" { Write-Host "Generating Visual Studio 2019 solution"; $GENERATOR = 'Visual Studio 16 2019'; $vs = "2019" }
        "2017" { Write-Host "Generating Visual Studio 2017 solution"; $GENERATOR = 'Visual Studio 15 2017 Win64'; $vs = "2017" }
        "2015" { Write-Host "Generating Visual Studio 2015 solution"; $GENERATOR = 'Visual Studio 14 2015 Win64'; $vs = "2015" }
        "2013" { Write-Host "Generating Visual Studio 2013 solution (not officially supported)"; $GENERATOR = 'Visual Studio 12 2013 Win64'; $vs = "2013" }
        "2012" { Write-Host "Generating Visual Studio 2012 solution (not officially supported)"; $GENERATOR = 'Visual Studio 11 2012 Win64'; $vs = "2012" }
        "2010" { Write-Host "Generating Visual Studio 2010 solution (not officially supported)"; $GENERATOR = 'Visual Studio 10 2010 Win64'; $vs = "2010" }
        "2008" { Write-Host "Generating Visual Studio 2008 solution (not officially supported)"; $GENERATOR = 'Visual Studio 9 2008 Win64'; $vs = "2008" }
        "ninja" { Write-Host "Generating Ninja files (not officially supported)"; $GENERATOR = 'Ninja' }
        "msys" { Write-Host "Generating MSYS Makefiles (not officially supported)"; $GENERATOR = 'MSYS Makefiles' }
        "mingw" { Write-Host "Generating MinGW Makefiles (not officially supported)"; $GENERATOR = 'MinGW Makefiles' }
        "unix" { Write-Host "Generating Unix Makefiles (not officially supported)"; $GENERATOR = 'Unix Makefiles' }
        "release" { $buildtype = "Release" }
        "debug" { $buildtype = "Debug" }
        "test" { $unittest = "ON" }
        "static" { $shared = "OFF" }
        "shared" { $shared = "ON" }
        "nobuild" { $build = $false }
        "lto" { $cmake_eflags += " -DSVT_AV1_LTO=ON" }
        default {
            Write-Host "Unknown argument '$($Args[$i])'"
            Show-Help
        }
    }
    $i++
}

# Remove CMake cache and files
if (Test-Path "CMakeCache.txt") { Remove-Item "CMakeCache.txt" -Force }
if (Test-Path "CMakeFiles") { Remove-Item "CMakeFiles" -Recurse -Force }

if ($GENERATOR -ne "") { $GENERATOR = "-G `"$GENERATOR`"" }

Write-Host "Building in $buildtype configuration"
if (-not $build) { Write-Host "Generating build files" }
if ($shared -eq "ON") { Write-Host "Building shared" } else { Write-Host "Building static" }
if ($unittest -eq "ON") { Write-Host "Building unit tests" }

$cmakeArgs = @("../..")
if ($GENERATOR -ne "") { $cmakeArgs += $GENERATOR }
if ($vs -eq "2019" -or $vs -eq "2022") {
    $cmakeArgs += "-A x64"
}
$cmakeArgs += "-DCMAKE_INSTALL_PREFIX=$env:SYSTEMDRIVE\svt-encoders"
$cmakeArgs += "-DBUILD_SHARED_LIBS=$shared"
$cmakeArgs += "-DBUILD_TESTING=$unittest"
if ($cmake_eflags -ne "") { $cmakeArgs += $cmake_eflags }

# Run CMake configure
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build if requested
if ($build) {
    & cmake --build . --config $buildtype
    exit $LASTEXITCODE
}