; Inno Setup script for the Windows build: one setup.exe with the program and every asset,
; installing per user (no administrator rights) into %LOCALAPPDATA%\Programs\azure_lane_pet,
; with a Start menu entry and an uninstaller. Compiled by package_win.sh, which stages
; dist\azure_lane_pet-win\ (exe + assets) with build_win.sh first and passes /DVersion=....
#ifndef Version
  #define Version "0.0.0"
#endif
#define Src "..\dist\azure_lane_pet-win"

[Setup]
AppId={{E3A6C1D4-2F7B-4B6E-9C1A-5D8F0B2A7C31}
AppName=azure_lane_pet
AppVersion={#Version}
AppPublisher=Chaoyi He
AppPublisherURL=https://github.com/Chaoyi-He1/desk_pet
DefaultDirName={localappdata}\Programs\azure_lane_pet
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=azure_lane_pet-win-setup
SetupIconFile=..\assets\icon.ico
UninstallDisplayIcon={app}\azure_lane_pet.exe
; Frames are LZ4 .bpf and 256-colour PNGs: little left to squeeze, so the fast setting.
Compression=lzma2/fast
SolidCompression=no
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
#if FileExists(CompilerPath + "\Languages\ChineseSimplified.isl")
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式 (desktop shortcut)"; Flags: unchecked

[Files]
Source: "{#Src}\azure_lane_pet.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Src}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: ".DS_Store"

[Icons]
Name: "{autoprograms}\azure_lane_pet"; Filename: "{app}\azure_lane_pet.exe"
Name: "{autodesktop}\azure_lane_pet"; Filename: "{app}\azure_lane_pet.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\azure_lane_pet.exe"; Description: "现在启动 azure_lane_pet (start now)"; Flags: nowait postinstall skipifsilent

[Registry]
; The app's own 开机自动启动 writes this value; uninstalling takes it away again.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "azure_lane_pet"; Flags: uninsdeletevalue dontcreatekey

[UninstallDelete]
; Decoded/scaled frame caches the program may leave next to the assets.
Type: filesandordirs; Name: "{app}\assets"
