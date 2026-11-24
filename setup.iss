; Inno Setup — справжній інсталятор для Windows
; Збірка: Inno Setup 6 (https://jrsoftware.org/isinfo.php)
; 1. Скомпілюй sokil.exe:  gcc -O2 -std=c99 -o sokil.exe sokil.c
; 2. Відкрий цей файл у Inno Setup → Compile

[Setup]
AppName=Сокіл (Sokil)
AppVersion=2.19
AppPublisher=Sokil
PrivilegesRequired=admin
DefaultDirName={autopf}\Sokil
DefaultGroupName=Сокіл
DisableProgramGroupPage=yes
OutputBaseFilename=Sokil-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
Source: "sokil.exe"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}"; Check: NeedsAddPath('{app}')

[UninstallDelete]
Name: "{app}"; Type: filesandordirs

[Code]
function NeedsAddPath(Param: String): Boolean;
var
  OrigPath: String;
begin
  if not RegQueryStringValue(HKLM, 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment', 'Path', OrigPath) then
  begin
    Result := True;
    Exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;