#ifndef RepositoryRoot
  #error RepositoryRoot must be supplied by build_windows_bridge_installer.ps1
#endif
#define BridgeExe RepositoryRoot + "\build\windows_bridge_exe\dist\SmallDesktopDisplayBridge.exe"
#define BridgeVersion GetStringFileInfo(BridgeExe, "ProductVersion")

[Setup]
AppId={{78E40D91-05D2-4EE3-A121-D119FF79CB7F}
AppName=MiniDisplay Bridge
AppVersion={#BridgeVersion}
AppPublisher=SmallDesktopDisplay community
AppPublisherURL=https://github.com/ismethr/SmallDesktopDisplay_PIO
DefaultDirName={localappdata}\Programs\MiniDisplay Bridge
DefaultGroupName=MiniDisplay Bridge
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#RepositoryRoot}\build\packages
OutputBaseFilename=MiniDisplayBridge-{#BridgeVersion}-windows-x64-setup
SetupIconFile={#RepositoryRoot}\build\windows_bridge_installer\MiniDisplayBridge.ico
UninstallDisplayIcon={app}\SmallDesktopDisplayBridge.exe
LicenseFile={#RepositoryRoot}\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=no
RestartApplications=no
UninstallLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut / 创建桌面快捷方式"; Flags: unchecked
Name: "autostart"; Description: "Start at Windows sign-in / 登录 Windows 后自动运行"; Flags: unchecked

[Files]
Source: "{#BridgeExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepositoryRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepositoryRoot}\tools\desktop_display_bridge\WINDOWS_QUICKSTART.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepositoryRoot}\tools\desktop_display_bridge\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepositoryRoot}\build\windows_sensors\sensors\*"; DestDir: "{app}\sensors"; Excludes: "*.pdb,*.config.backup,LibreHardwareMonitor.config,PawnIO_setup.exe"; Flags: recursesubdirs createallsubdirs
Source: "{#RepositoryRoot}\build\windows_bridge_installer\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\MiniDisplay Bridge"; Filename: "{app}\SmallDesktopDisplayBridge.exe"
Name: "{group}\使用说明"; Filename: "{app}\WINDOWS_QUICKSTART.md"
Name: "{group}\Uninstall MiniDisplay Bridge"; Filename: "{uninstallexe}"
Name: "{autodesktop}\MiniDisplay Bridge"; Filename: "{app}\SmallDesktopDisplayBridge.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "MiniDisplayBridge"; ValueData: """{app}\SmallDesktopDisplayBridge.exe"" --background"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\SmallDesktopDisplayBridge.exe"; Description: "Open MiniDisplay Bridge / 打开连接面板"; Flags: nowait postinstall skipifsilent

[Code]
function StopBridge(): Boolean;
var
  ExitCode, Attempt: Integer;
  Executable: String;
begin
  Result := True;
  if not CheckForMutexes('Local\SmallDesktopDisplayBridge-9E5B3921') then Exit;
  Executable := ExpandConstant('{app}\SmallDesktopDisplayBridge.exe');
  if FileExists(Executable) then
    Exec(Executable, '--stop', '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  for Attempt := 1 to 60 do begin
    if not CheckForMutexes('Local\SmallDesktopDisplayBridge-9E5B3921') then Exit;
    Sleep(250);
  end;
  Result := False;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not StopBridge() then
    Result := 'Please exit MiniDisplay Bridge from its tray menu, then retry. / 请先退出正在运行的桥接程序，再重试。';
end;

function InitializeUninstall(): Boolean;
begin
  Result := StopBridge();
  if not Result then
    MsgBox('Please exit MiniDisplay Bridge, then retry. / 请先退出桥接程序。', mbError, MB_OK);
end;
