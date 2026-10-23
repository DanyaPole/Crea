# Tectonic Synth

Tectonic Synth is a CLAP instrument plugin.

## Downloads

Download the latest Windows installer from [GitHub Releases](https://github.com/DanyaPole/Crea/releases/latest/download/TectonicSynth-Setup.exe). The installer places `TectonicSynth.clap` in the system CLAP plugin directory (`Common Files\CLAP`). Restart your DAW and rescan CLAP plugins if it is already open.

Other platform packages (macOS and Linux) are available on the [Releases page](https://github.com/DanyaPole/Crea/releases).

## Build

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Create a version tag such as `v4.0.0` and push it to publish Windows, macOS, and Linux downloads as a GitHub Release.

