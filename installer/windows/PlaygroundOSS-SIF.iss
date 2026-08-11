#ifndef StageDir
  #error StageDir must point to the prepared Windows runtime directory
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName "PlaygroundOSS SIF"
#define AppExe "playground-sdl-engine.exe"

[Setup]
AppId={{A9B041B9-9F4C-4B6E-84B9-E00A211DE502}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=PlaygroundOSS contributors
DefaultDirName={autopf}\PlaygroundOSS SIF
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=PlaygroundOSS-SIF-Setup-{#AppVersion}-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\{#AppExe}
VersionInfoVersion={#AppVersion}

[Files]
Source: "{#StageDir}\playground-sdl-engine.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\AppAssets.zip"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\AppAssets.version"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  RemoveUserData: Boolean;

function CommandLineRequestsDataRemoval: Boolean;
begin
  Result := Pos('/REMOVEUSERDATA=1', Uppercase(GetCmdTail)) > 0;
end;

procedure InitializeUninstallProgressForm;
begin
  RemoveUserData := CommandLineRequestsDataRemoval;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and (not UninstallSilent) then
    RemoveUserData :=
      MsgBox(
        'Also remove downloaded assets, settings, and save data from:' + #13#10 +
        ExpandConstant('{localappdata}\PlaygroundOSS-SIF') + '?' + #13#10#13#10 +
        'Choose No to preserve this data for a later reinstall.',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;

  if (CurUninstallStep = usPostUninstall) and RemoveUserData then
    DelTree(ExpandConstant('{localappdata}\PlaygroundOSS-SIF'), True, True, True);
end;
