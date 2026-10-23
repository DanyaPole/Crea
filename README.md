# Tectonic Synth

Tectonic Synth is a CLAP instrument plugin.

The plugin's embedded Dear ImGui interface provides controls for the five-engine continuum, X/Y morph pad, quantum field, filter, envelope, space, and output. CMake fetches Dear ImGui v1.90.1 during configuration, so the first build requires network access.

## Downloads

Download the latest Windows installer from [GitHub Releases](https://github.com/DanyaPole/Crea/releases/latest/download/TectonicSynth-Setup.exe). The installer places `TectonicSynth.clap` in the system CLAP plugin directory (`Common Files\CLAP`). Restart your DAW and rescan CLAP plugins if it is already open.

The macOS package is available on the [Releases page](https://github.com/DanyaPole/Crea/releases).

## Build

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Create a version tag such as `v5.0.0` and push it to publish Windows and macOS downloads as a GitHub Release.
