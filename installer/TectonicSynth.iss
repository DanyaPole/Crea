#define AppVersion GetEnv("APP_VERSION")

[Setup]
AppId={{D0E6B8B1-1D28-45E1-9E98-8DA2E7C49A47}
AppName=Tectonic Synth
AppVersion={#AppVersion}
AppPublisher=Experimental
DefaultDirName={commoncf}\CLAP
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\build\package
OutputBaseFilename=TectonicSynth-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Tectonic Synth CLAP Plugin

[Files]
Source: "..\build\Release\TectonicSynth.clap"; DestDir: "{commoncf}\CLAP"; Flags: ignoreversion
