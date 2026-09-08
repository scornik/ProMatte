; ProMatte — Inno Setup script
; Builds ProMatte-Setup.exe from the staged plugin tree (build/stage).
; Invoke through installer/build-installer.ps1 which passes StageDir and Version.

#ifndef Version
  #define Version "1.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\build\stage"
#endif

[Setup]
AppId={{7E1C1F52-6C4B-4E1C-9A36-PROMATTE0001}
AppName=ProMatte AI Background Removal for OBS Studio
AppVersion={#Version}
AppVerName=ProMatte {#Version}
AppPublisher=ProMatte
AppPublisherURL=https://github.com/promatte
DefaultDirName={code:GetObsDir}
DefaultGroupName=ProMatte
DisableProgramGroupPage=yes
DisableDirPage=no
DirExistsWarning=no
AppendDefaultDirName=no
UsePreviousAppDir=yes
OutputDir=output
OutputBaseFilename=ProMatte-Setup-{#Version}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
LicenseFile=..\LICENSE
WizardStyle=modern
UninstallDisplayName=ProMatte AI Background Removal (OBS plugin)
SetupLogging=yes
MinVersion=10.0.18362

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Plugin binary + runtime (ONNX Runtime, DirectML)
Source: "{#StageDir}\obs-plugins\64bit\*"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
; Data: effects, locale, model manifest and bundled models
Source: "{#StageDir}\data\obs-plugins\promatte\*"; DestDir: "{app}\data\obs-plugins\promatte"; Flags: ignoreversion recursesubdirs createallsubdirs
; Licences
Source: "..\LICENSE"; DestDir: "{app}\data\obs-plugins\promatte"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}\data\obs-plugins\promatte"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\promatte"

; User settings (scene collections) and downloaded models under %APPDATA% are
; intentionally preserved on uninstall/update.

[Code]
var
  ObsDir: string;

function IsObsRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if Exec('cmd.exe', '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" > nul', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := (ResultCode = 0);
end;

function DetectObsDir(): string;
var
  Dir: string;
begin
  Result := '';
  // 1. OBS Studio's own uninstall entry (Inno based installer)
  if RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Dir) then
    if DirExists(Dir) then begin Result := Dir; exit; end;
  if RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Dir) then
    if DirExists(Dir) then begin Result := Dir; exit; end;
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Dir) then
    if DirExists(Dir) then begin Result := Dir; exit; end;
  // 2. default locations
  Dir := ExpandConstant('{commonpf64}\obs-studio');
  if DirExists(Dir) then begin Result := Dir; exit; end;
  Dir := ExpandConstant('{commonpf}\obs-studio');
  if DirExists(Dir) then begin Result := Dir; exit; end;
end;

function GetObsDir(Param: string): string;
begin
  if ObsDir = '' then
    ObsDir := DetectObsDir();
  if ObsDir = '' then
    ObsDir := ExpandConstant('{commonpf64}\obs-studio');
  Result := ObsDir;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if IsObsRunning() then
  begin
    MsgBox('OBS Studio is running. Please close OBS before installing ProMatte.', mbError, MB_OK);
    Result := False;
    exit;
  end;
  if DetectObsDir() = '' then
    MsgBox('OBS Studio was not detected automatically. Please select the OBS Studio installation folder on the next page (the folder that contains "bin\64bit\obs64.exe").', mbInformation, MB_OK);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    if not FileExists(AddBackslash(WizardDirValue) + 'bin\64bit\obs64.exe') then
    begin
      MsgBox('The selected folder does not contain OBS Studio (bin\64bit\obs64.exe not found). Please choose the OBS Studio installation folder.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if IsObsRunning() then
  begin
    MsgBox('OBS Studio is running. Please close OBS before uninstalling ProMatte.', mbError, MB_OK);
    Result := False;
  end;
end;
