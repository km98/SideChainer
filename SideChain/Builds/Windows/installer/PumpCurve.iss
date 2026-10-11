; PumpCurve - Windows x64 VST3 installer recipe (Inno Setup 6)
;
; Build:  ISCC.exe /DAppVersion=0.4.1 /DStagedBundle=<path to PumpCurve.vst3> PumpCurve.iss
;         (installer\build_installer.cmd does exactly that and hashes the result)
;
; What it does
;   * installs PumpCurve.vst3 into the shared VST3 folder ({commoncf}\VST3) and
;     nothing else; the uninstaller lives outside that folder.
;   * removes a previously installed SideChain.vst3 / SideChainer.vst3 ONLY when
;     the file really belongs to this product, checked through its version
;     resource (ProductName "PumpCurve" or CompanyName "Music-Prod"). When the
;     information cannot be read, the bundle is left alone and logged - an
;     unrelated plug-in is never deleted. A 32-bit legacy bundle in the 32-bit
;     shared folder is reported but never touched.
;   * uninstall removes only the files this installer wrote.
;
; Status: this recipe is compiled and exercised end to end by
; .github/workflows/windows-vst3.yml, which installs it silently, checks the
; installed module hash against the built artifact, checks that a foreign plug-in
; in the same folder survives, then uninstalls and re-checks. The CI run URL must
; be recorded in the release notes before the installer is published.

#define AppName "PumpCurve"
#ifndef AppVersion
  #define AppVersion "0.4.1"
#endif
#ifndef StagedBundle
  #define StagedBundle "..\..\artifacts\PumpCurve.vst3"
#endif

[Setup]
AppId={{8A1C9B4E-3D2F-4C77-9E31-1F5B7C0A2D64}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Music-Prod
AppPublisherURL=https://music-prod.com
AppSupportURL=https://music-prod.com/support
DefaultDirName={commoncf}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
; Keep the uninstaller out of the folder hosts scan for plug-ins.
UninstallFilesDir={commoncf}\{#AppName}
OutputDir=..\..\artifacts
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupLogging=yes
; The plug-in goes into the shared VST3 folder, so administration rights are
; required. That folder itself is never removed.
Uninstallable=yes
UninstallDisplayName={#AppName} {#AppVersion} (VST3)

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#StagedBundle}\Contents\*"; DestDir: "{commoncf}\VST3\{#AppName}.vst3\Contents"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
const
  LEGACY_BUNDLES = 'SideChain.vst3,SideChainer.vst3';

function GetFileVersionString(FileName: String; const Key: String): String;
{ Reads a string from the file's VERSIONINFO resource. Returns '' when absent. }
var
  Dummy, Len: DWORD;
  Buf: array of Byte;
  P: Pointer;
  PSize: UINT;
  Value: String;
begin
  Result := '';
  Len := GetFileVersionInfoSize(FileName, Dummy);
  if Len = 0 then exit;
  SetLength(Buf, Len);
  if not GetFileVersionInfo(FileName, 0, Len, Buf) then exit;
  if not VerQueryValue(Buf, '\StringFileInfo\' + Key, P, PSize) then exit;
  if PSize = 0 then exit;
  Value := String(PChar(P));
  if Length(Value) > 0 then Result := Value;
end;

function IsOurs(BundlePath: String): Boolean;
{ True only when the installed module really is this product. Unknown => False, so
  an unreadable or foreign bundle is never removed. }
var
  Module: String;
  Product, Company: String;
begin
  Result := False;
  Module := BundlePath + '\Contents\x86_64-win\' + ExtractFileName(BundlePath) + '.vst3';
  if not FileExists(Module) then exit;
  Product := GetFileVersionString(Module, '040904B0\ProductName');
  Company := GetFileVersionString(Module, '040904B0\CompanyName');
  if (Product = '{#AppName}') or (Company = 'Music-Prod') then Result := True;
end;

procedure HandleLegacyBundles();
var
  Names: TArrayOfString;
  I: Integer;
  Target: String;
begin
  Names := StringSplit(LEGACY_BUNDLES, ',', stALL);
  for I := 0 to GetArrayLength(Names) - 1 do begin
    Target := ExpandConstant('{commoncf}\VST3\') + Names[I];
    if not DirExists(Target) then continue;
    if IsOurs(Target) then begin
      if DelTree(Target, True, True, True) then
        Log('removed legacy bundle: ' + Target)
      else
        Log('WARNING: could not remove legacy bundle: ' + Target);
    end else
      Log('left in place (not this product): ' + Target);
  end;
  { Both folders share the legacy names, but the 32-bit tree may belong to a
    32-bit host the customer still uses: report it, never delete it. }
  for I := 0 to GetArrayLength(Names) - 1 do begin
    Target := ExpandConstant('{commoncf32}\VST3\') + Names[I];
    if DirExists(Target) then
      Log('NOTE: a 32-bit legacy bundle exists and was left untouched: ' + Target);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    HandleLegacyBundles();
end;
