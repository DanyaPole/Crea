# Tectonic Synth v5 — Automatic CLAP Installer for Windows (Bitwig Studio / Reaper / DAW)
$ErrorActionPreference = "Stop"
$TargetDir = "$env:CommonProgramFiles\CLAP"
if (!(Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
}
Copy-Item -Path "$PSScriptRoot\TectonicSynthV5.clap" -Destination "$TargetDir\TectonicSynthV5.clap" -Force
Write-Host "Successfully installed TectonicSynthV5.clap to $TargetDir" -ForegroundColor Green
