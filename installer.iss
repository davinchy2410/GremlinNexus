; Grembling Nexus - Instalador Todo-en-Uno (Inno Setup)
;
; Empaqueta el build desplegado (dist\GremblingNexus\*, ya pasado por
; windeployqt) junto a los 3 drivers de terceros que la app necesita para
; funcionar (vJoy, HidHide, ViGEmBus), instalando cada uno en modo
; silencioso SOLO si no está ya presente en el sistema.
;
; NOTA (desviaciones respecto a la especificación original en
; Claude_Prompt.md, verificadas contra los instaladores reales descargados
; y contra un sistema con los 3 drivers ya instalados):
;   - HidHide y ViGEmBus ya NO se distribuyen como HidHideMSI.msi /
;     ViGEmBus_Setup.exe genéricos: sus releases actuales en GitHub son
;     bootstrappers .exe de Advanced Installer (HidHide_1.5.230_x64.exe /
;     ViGEmBus_1.22.0_x64_x86_arm64.exe). Sus flags silenciosos correctos
;     son "/exenoui /qn /norestart" (el bootstrapper reenvía /qn a msiexec),
;     no "/quiet"/"/q" como asumía la spec original (esos son flags de MSI
;     directo, no del bootstrapper .exe).
;   - La detección de instalación usa las 3 claves de servicio bajo
;     HKLM\SYSTEM\CurrentControlSet\Services\<nombre>, confirmadas contra
;     un sistema real con vjoy/HidHide/ViGEmBus corriendo - la clave que la
;     spec original asumía para ViGEmBus (HKLM\SOFTWARE\Nefarius Software
;     Solutions e.U.\ViGEm Bus Setup) no existe en la práctica; solo HidHide
;     escribe bajo esa rama de Software.
;   - ViGEmBus es un proyecto archivado (releases congeladas en v1.22.0,
;     sin desarrollo futuro) pero sigue siendo la fuente oficial y es el
;     driver que este proyecto ya usa (ver src/output/ViGEmDevice.cpp).

#define MyAppName "Grembling Nexus"
#define MyAppVersion "1.0"
#define MyAppExeName "GremblingNexus.exe"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\Grembling Nexus
DefaultGroupName={#MyAppName}
PrivilegesRequired=admin
OutputBaseFilename=GremblingNexus_Installer
OutputDir=installer_output
SetupIconFile=app.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible

[Types]
Name: "custom"; Description: "Instalación Personalizada"; Flags: iscustom

[Components]
Name: "app"; Description: "Grembling Nexus (Aplicación Principal)"; Types: custom; Flags: fixed
Name: "vjoy"; Description: "Driver vJoy (Joystick Virtual)"; Types: custom
Name: "hidhide"; Description: "Driver HidHide (Ocultamiento de Dispositivos)"; Types: custom
Name: "vigembus"; Description: "Driver ViGEmBus (Emulación de Gamepad)"; Types: custom

[Files]
Source: "dist\GremblingNexus\*"; DestDir: "{app}"; Components: app; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "redist\vJoySetup.exe"; DestDir: "{tmp}"; Components: vjoy; Flags: deleteafterinstall
Source: "redist\HidHide_1.5.230_x64.exe"; DestDir: "{tmp}"; Components: hidhide; Flags: deleteafterinstall
Source: "redist\ViGEmBus_1.22.0_x64_x86_arm64.exe"; DestDir: "{tmp}"; Components: vigembus; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{tmp}\vJoySetup.exe"; Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"; Components: vjoy; Flags: waituntilterminated; StatusMsg: "Instalando driver vJoy..."; AfterInstall: MarkRebootNeeded
Filename: "{tmp}\HidHide_1.5.230_x64.exe"; Parameters: "/exenoui /qn /norestart"; Components: hidhide; Flags: waituntilterminated; StatusMsg: "Instalando driver HidHide..."; AfterInstall: MarkRebootNeeded
Filename: "{tmp}\ViGEmBus_1.22.0_x64_x86_arm64.exe"; Parameters: "/exenoui /qn /norestart"; Components: vigembus; Flags: waituntilterminated; StatusMsg: "Instalando driver ViGEmBus..."; AfterInstall: MarkRebootNeeded
Filename: "{app}\{#MyAppExeName}"; Description: "Ejecutar {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  RequiresReboot: Boolean;
  ComponentsInitialized: Boolean;

// Cada driver se registra como un servicio de Windows bajo esta misma rama
// del registro sin importar el motor del instalador (Advanced Installer,
// Inno Setup o INF directo) - confirmado contra un sistema con los 3
// drivers ya instalados y corriendo, a diferencia de la ruta bajo
// HKLM\SOFTWARE\Nefarius Software Solutions e.U.\... que solo HidHide usa.
// Puras (sin side-effects): usadas desde CurPageChanged (ver más abajo)
// para decidir el check inicial y el texto "(Ya instalado)" de cada
// componente - la instalación real ya no depende de esto, depende de si
// el usuario dejó el componente tildado (ver [Run]'s Components:).
function NeedsVJoy(): Boolean;
begin
  Result := not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\vjoy');
end;

function NeedsHidHide(): Boolean;
begin
  Result := not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\HidHide');
end;

function NeedsViGEm(): Boolean;
begin
  Result := not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\ViGEmBus');
end;

// Llamado por AfterInstall de cada [Run] de driver - a diferencia de
// Needs*, esto solo se dispara si el driver realmente se ejecutó (el
// usuario lo dejó tildado), así que es la señal correcta para saber si
// hace falta reiniciar.
procedure MarkRebootNeeded();
begin
  RequiresReboot := True;
end;

function NeedRestart(): Boolean;
begin
  Result := RequiresReboot;
end;

// InitializeWizard() se ejecuta demasiado temprano: ComponentsList todavía
// no existe como control poblado en ese momento (crashea con "List index
// out of bounds" apenas se toca ItemCaption). CurPageChanged, en cambio,
// se dispara cada vez que el wizard cambia de página - cuando llega a
// wpSelectComponents, la lista ya está construida con sus 4 filas. El
// booleano ComponentsInitialized evita repetir el ajuste si el usuario
// vuelve a esta página con el botón "Atrás". Recorre por el texto del
// Description (Pos(...)) en vez de por índice fijo, porque el orden de
// [Components] podría cambiar sin que este código se entere.
procedure CurPageChanged(CurPageID: Integer);
var
  I: Integer;
begin
  if (CurPageID = wpSelectComponents) and (not ComponentsInitialized) then
  begin
    ComponentsInitialized := True;
    for I := 0 to WizardForm.ComponentsList.Items.Count - 1 do
    begin
      // vJoy
      if Pos('vJoy', WizardForm.ComponentsList.ItemCaption[I]) > 0 then
      begin
        if not NeedsVJoy() then
        begin
          WizardForm.ComponentsList.ItemCaption[I] := WizardForm.ComponentsList.ItemCaption[I] + ' (Ya instalado)';
          WizardForm.ComponentsList.Checked[I] := False; // Desmarcado si ya lo tiene
        end
        else
          WizardForm.ComponentsList.Checked[I] := True;  // Marcado si le falta
      end;

      // HidHide
      if Pos('HidHide', WizardForm.ComponentsList.ItemCaption[I]) > 0 then
      begin
        if not NeedsHidHide() then
        begin
          WizardForm.ComponentsList.ItemCaption[I] := WizardForm.ComponentsList.ItemCaption[I] + ' (Ya instalado)';
          WizardForm.ComponentsList.Checked[I] := False;
        end
        else
          WizardForm.ComponentsList.Checked[I] := True;
      end;

      // ViGEmBus
      if Pos('ViGEmBus', WizardForm.ComponentsList.ItemCaption[I]) > 0 then
      begin
        if not NeedsViGEm() then
        begin
          WizardForm.ComponentsList.ItemCaption[I] := WizardForm.ComponentsList.ItemCaption[I] + ' (Ya instalado)';
          WizardForm.ComponentsList.Checked[I] := False;
        end
        else
          WizardForm.ComponentsList.Checked[I] := True;
      end;
    end;
  end;
end;
