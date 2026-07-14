; Gremlin Nexus - Instalador (Inno Setup)
;
; Empaqueta UNICAMENTE el build desplegado (dist\GremlinNexus\*, ya pasado
; por windeployqt). Los 3 drivers de terceros que la app necesita para
; funcionar (vJoy, HidHide, ViGEmBus) YA NO se bundlean aca a proposito:
;   - Un instalador que descarga/ejecuta drivers de terceros en modo
;     silencioso dispara mucho mas facil los avisos de "no seguro" de
;     SmartScreen/antivirus que uno que solo copia los archivos de la app.
;   - El instalador de vJoy embebido fallo en la practica en la PC de un
;     usuario (instalacion silenciosa incompleta/rota), algo dificil de
;     diagnosticar a distancia sin sus logs.
; En su lugar, el README documenta los links oficiales de descarga de los
; 3 drivers para que el usuario los instale el mismo, por separado.

#define MyAppName "Gremlin Nexus"
#define MyAppVersion "1.0"
#define MyAppExeName "GremlinNexus.exe"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\Gremlin Nexus
DefaultGroupName={#MyAppName}
PrivilegesRequired=admin
OutputBaseFilename=GremlinNexus_Installer
OutputDir=installer_output
SetupIconFile=app.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
; Excludes "Logs\*" - runtime log files from local test runs, not part of the
; shipped app; they'd otherwise get bundled verbatim since Source uses a
; recursive wildcard.
Source: "dist\GremlinNexus\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "Logs\*"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Ejecutar {#MyAppName}"; Flags: nowait postinstall skipifsilent
