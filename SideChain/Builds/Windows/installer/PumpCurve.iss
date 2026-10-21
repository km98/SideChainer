; PumpCurve - Windows x64 VST3 installer recipe (Inno Setup 6)
;
; Build:  ISCC.exe /DAppVersion=0.5.1 /DStagedBundle=<path to PumpCurve.vst3> PumpCurve.iss
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
;
;     The ownership decision is made by cleanup_legacy.ps1, which this recipe
;     ships with "Flags: dontcopy" and extracts at install time. It cannot be made
;     in [Code]: Inno Setup's Pascal Script offers numeric version functions only
;     (GetPackedVersion, ComparePackedVersion, ...), has no StringFileInfo
;     accessor, and rejects a Pointer type - so the version resource cannot be
;     read from Pascal Script at all. The helper uses the same .NET VersionInfo as
;     installer\verify_install.ps1, and exits non-zero without deleting anything
;     when the module or its version information cannot be read.
;   * uninstall removes only the files this installer wrote.
;
; Status: this recipe is compiled and exercised end to end by
; .github/workflows/windows-vst3.yml, which installs it silently, checks the
; installed module hash against the built artifact, checks that a foreign plug-in
; in the same folder survives, then uninstalls and re-checks. The CI run URL must
; be recorded in the release notes before the installer is published.

#define AppName "PumpCurve"
#ifndef AppVersion
  #define AppVersion "0.5.1"
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
; The ownership check runs inside PowerShell, so the installer carries the helper
; and extracts it only when it is actually needed.
Source: "cleanup_legacy.ps1"; Flags: dontcopy

[Code]
const
  LEGACY_BUNDLES = 'SideChain.vst3,SideChainer.vst3';
  CLEANUP_HELPER = 'cleanup_legacy.ps1';

function PowerShellPath(): String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function RunCleanupHelper(BundlePath: String; var Rc: Integer): Boolean;
{ Runs the guarded cleanup. The helper decides ownership from the module's version
  resource and deletes nothing when it cannot read it. }
var
  Params: String;
begin
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
            ExpandConstant('{tmp}\') + CLEANUP_HELPER + '" "' + BundlePath + '"';
  Result := Exec(PowerShellPath(), Params, '', SW_HIDE, ewWaitUntilTerminated, Rc);
end;

procedure HandleLegacyBundles();
var
  Names: TArrayOfString;
  I, Rc: Integer;
  Target: String;
begin
  { StringSplit takes an array of separators, not a single character. }
  Names := StringSplit(LEGACY_BUNDLES, [','], stAll);
  if not FileExists(PowerShellPath()) then begin
    { No PowerShell means no ownership check, so nothing may be deleted. }
    for I := 0 to GetArrayLength(Names) - 1 do begin
      Target := ExpandConstant('{commoncf}\VST3\') + Names[I];
      if DirExists(Target) then
        Log('NOTE: legacy bundle left in place, no ownership check available: ' + Target);
    end;
    exit;
  end;
  ExtractTemporaryFile(CLEANUP_HELPER);
  for I := 0 to GetArrayLength(Names) - 1 do begin
    Target := ExpandConstant('{commoncf}\VST3\') + Names[I];
    if not DirExists(Target) then continue;
    if not RunCleanupHelper(Target, Rc) then
      Log('WARNING: could not run the legacy cleanup for ' + Target)
    else if Rc = 0 then
      Log('removed legacy bundle: ' + Target)
    else if Rc = 3 then
      Log('left in place (not this product): ' + Target)
    else if Rc = 2 then
      Log('left in place (no recognisable module inside): ' + Target)
    else
      Log('WARNING: legacy cleanup reported ' + IntToStr(Rc) + ' for ' + Target);
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
