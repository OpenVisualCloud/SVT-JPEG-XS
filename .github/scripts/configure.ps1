<#
 Copyright(c) 2024 Intel Corporation
 SPDX-License-Identifier: BSD-2-Clause-Patent
#>

function Install-Yasm {
  Write-Host "installing Yasm..."
  $yasmUrl = "https://www.tortall.net/projects/yasm/releases/yasm-1.3.0-win64.exe"
  $installDir = $env:YASM_PATH
  if (-not $installDir) {
    $installDir = "C:\Yasm"
  }
  if (-Not (Test-Path -Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir | Out-Null
  }

  Write-Host "Downloading Yasm..."
  Invoke-WebRequest -Uri $yasmUrl -OutFile "$installDir\yasm.exe"

  $envPath = [System.Environment]::GetEnvironmentVariable("Path", [System.EnvironmentVariableTarget]::Machine)
  if (-Not $envPath.Contains($installDir)) {
    [System.Environment]::SetEnvironmentVariable("Path", "$envPath;$installDir", [System.EnvironmentVariableTarget]::Machine)
    Write-Host "Yasm installation directory added to system PATH."
  } else {
    Write-Host "Yasm installation directory already exists in system PATH."
  }
  # Set ASM_NASM environment variable to the full path of yasm.exe
  [System.Environment]::SetEnvironmentVariable("ASM_NASM", "$installDir\yasm.exe", [System.EnvironmentVariableTarget]::Machine)
  Write-Host "ASM_NASM environment variable set to $installDir\yasm.exe"
  Write-Host "Yasm installation completed."
}

function Install-VisualStudio2022 {
  Write-Host "installing Visual Studio 2022..."
  $vsUrl = "https://aka.ms/vs/17/release/vs_community.exe"
  $installDir = "C:\VisualStudio2022"

  if (-Not (Test-Path -Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir | Out-Null
  }

  Write-Host "Downloading Visual Studio bootstrapper..."
  Invoke-WebRequest -Uri $vsUrl -OutFile "$installDir\vs_community.exe"

  $installArgs = "--quiet --wait --norestart --installPath $installDir --add Microsoft.VisualStudio.Workload.CoreEditor"

  Write-Host "Installing Visual Studio 2022..."
  Start-Process -FilePath "$installDir\vs_community.exe" -ArgumentList $installArgs -NoNewWindow -Wait

  Write-Host "Visual Studio 2022 installation completed."

  Write-Host "Installing Microsoft Visual C++ 2010 Service Pack 1 Redistributable via chocolatey"
  Start-Process -FilePath "choco" -ArgumentList "install vcredist2010 -y" -NoNewWindow -Wait

  Write-Host "Microsoft Visual C++ 2010 Service Pack 1 Redistributable installation completed."
}

function Install-Pacman {
  Write-Host "Installing pacman via Chocolatey..."
  $msys2Path = "${env:ProgramFiles}\MSYS2\usr\bin"
  Start-Process -FilePath "choco" -ArgumentList "install msys2 -y --params "/InstallDir:$msys2Path"" -NoNewWindow -Wait
  
  if (-Not (Test-Path -Path $msys2Path)) {
    $msys2Path = "${env:ProgramFiles(x86)}\MSYS2\usr\bin"
  }
  if (-Not (Test-Path -Path $msys2Path)) {
    Write-Error "MSYS2 installation not found."
    return
  }

  $env:Path += ";$msys2Path"
  [System.Environment]::SetEnvironmentVariable("Path", $env:Path, [System.EnvironmentVariableTarget]::Machine)

  Write-Host "Running pacman to install required packages..."
  & "$msys2Path\pacman.exe" -S --noconfirm make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-yasm mingw-w64-x86_64-diffutils
  Write-Host "Pacman and required packages installation completed."
}

Install-Yasm
Install-VisualStudio2022
Install-Pacman
