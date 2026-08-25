# Package PandaFarm build artifacts for GitHub Releases + web flasher.
# Usage: .\scripts\package-firmware.ps1 [-Version "0.2.2"]

param(
    [string]$Version = "0.2.2",
    [string]$Env = "pandacupboard-arduino-3x"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root ".pio\build\$Env"
$OutDir = Join-Path $Root "dist\firmware"
$Pio = Join-Path $env:LOCALAPPDATA "Python\pythoncore-3.14-64\Scripts\pio.exe"

Write-Host "Building firmware ($Env)..."
Push-Location $Root
if (Test-Path $Pio) {
    & $Pio run -e $Env
} else {
    python -m platformio run -e $Env
}
if ($LASTEXITCODE -ne 0) { throw "platformio build failed ($LASTEXITCODE)" }
Pop-Location

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem $OutDir -Filter "pandacupboard-*.bin" -ErrorAction SilentlyContinue | Remove-Item -Force

Copy-Item (Join-Path $BuildDir "bootloader.bin") (Join-Path $OutDir "pandafarm-bootloader.bin") -Force
Copy-Item (Join-Path $BuildDir "partitions.bin") (Join-Path $OutDir "pandafarm-partitions.bin") -Force
Copy-Item (Join-Path $BuildDir "firmware.bin") (Join-Path $OutDir "pandafarm-firmware.bin") -Force

$PagesFirmware = Join-Path $Root "docs\flasher\firmware"
New-Item -ItemType Directory -Force -Path $PagesFirmware | Out-Null
Get-ChildItem $PagesFirmware -Filter "pandacupboard-*.bin" -ErrorAction SilentlyContinue | Remove-Item -Force
Copy-Item (Join-Path $OutDir "*") $PagesFirmware -Force
Write-Host "Copied firmware to $PagesFirmware (web flasher same-origin hosting)"

$searchRoots = @(
    (Join-Path $env:USERPROFILE ".platformio\packages"),
    (Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32")
)
$BootApp0 = Get-ChildItem $searchRoots -Recurse -Filter "boot_app0.bin" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'esp32' } |
    Select-Object -First 1
if (-not $BootApp0) {
    $BootApp0 = Get-ChildItem $searchRoots -Recurse -Filter "boot_app0.bin" -ErrorAction SilentlyContinue | Select-Object -First 1
}
if ($BootApp0) {
    Copy-Item $BootApp0.FullName (Join-Path $OutDir "pandafarm-boot_app0.bin") -Force
    Copy-Item $BootApp0.FullName (Join-Path $PagesFirmware "pandafarm-boot_app0.bin") -Force
    Write-Host "Included boot_app0.bin from $($BootApp0.FullName)"
} else {
    Write-Warning "boot_app0.bin not found - web flasher marks it optional; OTA may not work without it."
}

$ZipPath = Join-Path $Root "dist\pandafarm-firmware-v$Version.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $ZipPath

Write-Host ""
Write-Host "Packaged $Env firmware to:"
Write-Host "  $OutDir"
Write-Host "  $ZipPath"
Write-Host ""
Write-Host "Upload these files as GitHub Release assets (tag v$Version):"
Get-ChildItem $OutDir | ForEach-Object { Write-Host "  - $($_.Name)  $($_.Length) bytes" }
