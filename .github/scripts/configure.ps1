<#
 Copyright(c) 2024 Intel Corporation
 SPDX-License-Identifier: BSD-2-Clause-Patent
#>

function Install-Nasm {
  Write-Host "installing NASM..."
  $nasmVersion = "3.01"
  $nasmUrl = "https://www.nasm.us/pub/nasm/releasebuilds/$nasmVersion/win64/nasm-$nasmVersion-win64.zip"
  # nasm.us publishes no checksums; this is the hash of the archive when it was pinned.
  $nasmSha256 = "e0ba5157007abc7b1a65118a96657a961ddf55f7e3f632ee035366dfce039ca4"
  $installDir = $env:NASM_PATH
  if (-not $installDir) {
    $installDir = "C:\Nasm"
  }
  if (-Not (Test-Path -Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir | Out-Null
  }

  Write-Host "Downloading NASM $nasmVersion..."
  $zipPath = Join-Path $env:TEMP "nasm-$nasmVersion-win64.zip"
  Invoke-WebRequest -Uri $nasmUrl -OutFile $zipPath
  $actualSha256 = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLower()
  if ($actualSha256 -ne $nasmSha256) {
    throw "NASM archive checksum mismatch: expected $nasmSha256, got $actualSha256"
  }
  Expand-Archive -Path $zipPath -DestinationPath $env:TEMP -Force
  Copy-Item -Path (Join-Path $env:TEMP "nasm-$nasmVersion\nasm.exe") -Destination "$installDir\nasm.exe" -Force

  $envPath = [System.Environment]::GetEnvironmentVariable("Path", [System.EnvironmentVariableTarget]::Machine)
  if (-Not $envPath.Contains($installDir)) {
    [System.Environment]::SetEnvironmentVariable("Path", "$envPath;$installDir", [System.EnvironmentVariableTarget]::Machine)
    Write-Host "NASM installation directory added to system PATH."
  } else {
    Write-Host "NASM installation directory already exists in system PATH."
  }
  # Set ASM_NASM environment variable to the full path of nasm.exe
  [System.Environment]::SetEnvironmentVariable("ASM_NASM", "$installDir\nasm.exe", [System.EnvironmentVariableTarget]::Machine)
  Write-Host "ASM_NASM environment variable set to $installDir\nasm.exe"
  Write-Host "NASM installation completed."
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
Install-Nasm
Install-VisualStudio2022
