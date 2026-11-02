; Compile with package-windows-installer.py using the ZIP's staged payload.
[Setup]
AppId={{2A5AE69D-7194-4F2A-B688-A0C27A690DEF}
AppName=IsaacVideoEditor
AppVersion={#AppVersion}
VersionInfoVersion={#FileVersion}
AppPublisher=Peter Shen
AppPublisherURL=https://github.com/p-shen/IsaacVideoEditor
DefaultDirName={localappdata}\Programs\IsaacVideoEditor
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\IsaacVideoEditor.exe
OutputDir={#OutputDir}
OutputBaseFilename=IsaacVideoEditor-{#PackageVersion}-windows-x86_64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\IsaacVideoEditor"; Filename: "{app}\IsaacVideoEditor.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\IsaacVideoEditor"; Filename: "{app}\IsaacVideoEditor.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\IsaacVideoEditor.exe"; Description: "Launch IsaacVideoEditor"; Flags: nowait postinstall skipifsilent
