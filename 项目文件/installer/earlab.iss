; ============================================================================
; EarLab per-user installer (Inno Setup 6.3+)
;
; Build via scripts\build-installer.ps1 (recommended), or manually:
;   ISCC.exe installer\earlab.iss
;
; Design notes:
; - Per-user install, no elevation: PrivilegesRequired=lowest and
;   DefaultDirName under {localappdata}\Programs. Do NOT switch to admin:
;   A7 acceptance runs as a standard user and must not raise UAC.
; - The four VC++ 2022 x64 runtime DLLs are staged app-local next to the
;   exe, so the app also starts on a clean machine without the Redist.
; - No [UninstallDelete] for {userappdata}\EarLab: user data survives
;   uninstall by design.
; - Shortcuts point directly at "EarLab Engine.exe" (never cmd.exe).
; ============================================================================

#ifndef MyAppVersion
  #define MyAppVersion "0.6.1"
#endif

#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{B7D50365-01A2-4289-B7E2-5F6F31CFAF6E}
AppName=EarLab
AppVersion={#MyAppVersion}
AppVerName=EarLab {#MyAppVersion}
AppPublisher=EarLab
AppCopyright=Copyright (C) 2026 EarLab
VersionInfoVersion={#MyAppVersion}.0
VersionInfoProductVersion={#MyAppVersion}
VersionInfoProductName=EarLab
VersionInfoDescription=EarLab {#MyAppVersion} Setup
VersionInfoCompany=EarLab
VersionInfoCopyright=Copyright (C) 2026 EarLab
VersionInfoOriginalFileName=EarLab-{#MyAppVersion}-win64-setup.exe
DefaultDirName={localappdata}\Programs\EarLab
DefaultGroupName=EarLab
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
MinVersion=10.0.17763
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=EarLab-{#MyAppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
UninstallDisplayName=EarLab {#MyAppVersion}
UninstallDisplayIcon={app}\EarLab Engine.exe
LicenseFile=LICENSE
InfoBeforeFile=README.txt

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Everything staged by scripts\build-installer.ps1: exe, 4 VC++ runtime DLLs,
; README.txt, LICENSE, THIRD-PARTY-NOTICES.txt, SOURCE-OFFER.txt.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\EarLab"; Filename: "{app}\EarLab Engine.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\EarLab"; Filename: "{app}\EarLab Engine.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\EarLab Engine.exe"; Description: "Launch EarLab"; Flags: nowait postinstall skipifsilent

; Intentionally no [UninstallDelete] for {userappdata}\EarLab.
; Uninstall removes program files, shortcuts and the HKCU Apps entry only.
