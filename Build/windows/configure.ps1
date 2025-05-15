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

  Write-Host "Installing Microsoft Visual C++ 2010 Service Pack 1 Redistributable  via chocolatey"
  Start-Process -FilePath "choco" -ArgumentList "install vcredist2010 -y" -NoNewWindow -Wait

  Write-Host "Microsoft Visual C++ 2010 Service Pack 1 Redistributable installation completed."
}
Install-Yasm
Install-VisualStudio2022

