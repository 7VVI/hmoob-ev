; Inno Setup script for Hmmob EV.
; Build with: ISCC.exe /DMyAppVersion=0.1.2 scripts\installer.iss
; Expects dist\Hmmob-EV-<version>-win64 to exist (scripts\package.ps1).

#ifndef MyAppVersion
  #define MyAppVersion "0.1.2"
#endif

#define MyAppName "Hmmob EV"
#define MyAppPublisher "Hmmob"
#define MyAppExeName "hmmob_ev_app.exe"
#define MyAppSource "..\dist\Hmmob-EV-" + MyAppVersion + "-win64"

[Setup]
AppId={{8E3C2D61-5C1B-4E2A-9B7F-3A8C5D914E20}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Hmmob EV
DefaultGroupName=Hmmob EV
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Hmmob-EV-{#MyAppVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\logo.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "{#MyAppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
