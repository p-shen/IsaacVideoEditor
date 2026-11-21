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

[Code]
var
  CurrentPackageFiles: TArrayOfString;

function FileAttributes(const FileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function SafePackageFile(const AppDir, Name: String): Boolean;
var
  Remaining, Part, Current: String;
  Separator, I: Integer;
  Attributes: Cardinal;
begin
  Result := False;
  if (Name = '') or (Pos('\', Name) > 0) then Exit;
  Current := AppDir;
  if (FileAttributes(Current) and $400) <> 0 then Exit;
  Remaining := Name;
  while Remaining <> '' do begin
    Separator := Pos('/', Remaining);
    if Separator = 0 then begin
      Part := Remaining;
      Remaining := '';
    end else begin
      Part := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
      if Remaining = '' then Exit;
    end;
    if (Part = '') or (Part = '.') or (Part = '..') then Exit;
    if (Part[Length(Part)] = '.') or (Part[Length(Part)] = ' ') then Exit;
    for I := 1 to Length(Part) do
      if (Ord(Part[I]) < 32) or (Pos(Part[I], ':*?"<>|') > 0) then Exit;
    Current := Current + '\' + Part;
    Attributes := FileAttributes(Current);
    if (Attributes = $FFFFFFFF) or ((Attributes and $400) <> 0) then Exit;
    if (Remaining <> '') and ((Attributes and $10) = 0) then Exit;
  end;
  Result := (Attributes and $10) = 0;
end;

function InitializeUninstall: Boolean;
begin
  { Read before Inno removes the manifest recorded in its original file log. }
  if SafePackageFile(ExpandConstant('{app}'), 'isaac-package-files.txt') then
    LoadStringsFromFile(ExpandConstant('{app}\isaac-package-files.txt'), CurrentPackageFiles);
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  I: Integer;
  AppDir, FileName, Directory: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  AppDir := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
  for I := 0 to GetArrayLength(CurrentPackageFiles) - 1 do begin
    if SafePackageFile(AppDir, CurrentPackageFiles[I]) then begin
      FileName := CurrentPackageFiles[I];
      StringChangeEx(FileName, '/', '\', True);
      FileName := AppDir + '\' + FileName;
      if DeleteFile(FileName) then begin
        Directory := ExtractFileDir(FileName);
        while CompareText(Directory, AppDir) <> 0 do begin
          if not RemoveDir(Directory) then Break;
          Directory := ExtractFileDir(Directory);
        end;
      end else
        Log('Could not remove updated package file: ' + FileName);
    end;
  end;
end;
