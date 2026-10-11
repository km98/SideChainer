# PumpCurve - automated install / upgrade-cleanup / uninstall test (Windows).
#
# This is the test that turns the installer recipe into a verified installer. It
# runs on a throwaway Windows machine (the CI runner) and checks the three things
# a customer would otherwise discover:
#
#   1. install        - the Setup.exe installs PumpCurve.vst3, and the installed
#                       module is byte-identical to the built artifact
#   2. upgrade        - a previously installed SideChain.vst3 that really is our
#                       own product is removed, so one identity exists once
#   3. preservation   - another vendor's plug-in in the same folder is untouched
#   4. uninstall      - everything this installer wrote is gone again, and the
#                       other vendor's plug-in is still there
#
# Usage (elevated PowerShell):
#   powershell -ExecutionPolicy Bypass -File installer\test_install_uninstall.ps1 `
#       -Setup <PumpCurve-0.4.1-Windows-x64-Setup.exe> `
#       [-ExpectedModuleSha256 <sha256>] [-StagedBundle <PumpCurve.vst3>]
#
# Exit code 0 = PASS, 1 = FAIL, 2 = usage/setup problem.

param(
    [Parameter(Mandatory = $true)][string]$Setup,
    [string]$ExpectedModuleSha256 = "",
    [string]$StagedBundle = "",
    [string]$Vst3Dir = "$env:CommonProgramFiles\VST3",
    [string]$LogDir = "",
    [string]$ProductName = "PumpCurve"
)

$ErrorActionPreference = "Stop"
$script:failures = 0
$script:passes = 0

function Ok($message)   { Write-Host "  ok:   $message"; $script:passes++ }
function Bad($message)  { Write-Host "  FAIL: $message"; $script:failures++ }
function Check($condition, $message) {
    if ($condition) { Ok $message } else { Bad $message }
}
function RunSilent([string]$exe, [string[]]$extra) {
    $args = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-") + $extra
    Write-Host "  $ exe $($args -join ' ')"
    $p = Start-Process -FilePath $exe -ArgumentList $args -Wait -PassThru
    return $p.ExitCode
}
function Sha256Of([string]$path) { (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLower() }

if (-not (Test-Path $Setup)) {
    Write-Host "ERROR: no installer at $Setup"
    exit 2
}
if (-not $LogDir) { $LogDir = Join-Path $env:RUNNER_TEMP "pumpcurve-installer-logs" }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$setupHash = Sha256Of $Setup
Write-Host "== PumpCurve installer install/uninstall test =="
Write-Host "  installer:      $Setup"
Write-Host "  installer sha256: $setupHash"
Write-Host "  signature:      $((Get-AuthenticodeSignature $Setup).Status)"
Write-Host "  VST3 folder:    $Vst3Dir"
Write-Host ""

$bundle    = Join-Path $Vst3Dir "$ProductName.vst3"
$module    = Join-Path $bundle "Contents\x86_64-win\$ProductName.vst3"
$legacy    = Join-Path $Vst3Dir "SideChain.vst3"
$legacyMod = Join-Path $legacy "Contents\x86_64-win\SideChain.vst3"
$decoyDir  = Join-Path $Vst3Dir "NotOurPlugin.vst3"
$decoyMod  = Join-Path $decoyDir "Contents\x86_64-win\NotOurPlugin.vst3"
$uninstaller = Join-Path "$env:CommonProgramFiles\$ProductName" "unins000.exe"

# 0. Nothing of ours may be installed before we start.
Write-Host "-- preconditions"
Check (-not (Test-Path $bundle)) "no PumpCurve.vst3 is installed yet"
if (Test-Path $bundle) { Remove-Item -Recurse -Force $bundle }

# 1. Fixtures: a foreign plug-in that must survive, and a legacy bundle that really
#    is ours and must be cleaned up during the install.
Write-Host "-- fixtures"
New-Item -ItemType Directory -Force -Path (Split-Path $decoyMod) | Out-Null
[System.IO.File]::WriteAllBytes($decoyMod, [byte[]](1..200))
$decoyHash = Sha256Of $decoyMod
New-Item -ItemType Directory -Force -Path (Split-Path $legacyMod) | Out-Null
if ($StagedBundle -and (Test-Path (Join-Path $StagedBundle "Contents\x86_64-win\$ProductName.vst3"))) {
    Copy-Item (Join-Path $StagedBundle "Contents\x86_64-win\$ProductName.vst3") $legacyMod -Force
} elseif (Test-Path $Setup) {
    # Fall back to whatever module the previous install left, if any.
    if (Test-Path $module) { Copy-Item $module $legacyMod -Force } else {
        [System.IO.File]::WriteAllBytes($legacyMod, [byte[]](7..90))
    }
}
Write-Host "  planted legacy bundle (legacy name, our product identity): $legacy"

# 2. Install silently.
Write-Host "-- install"
$rc = RunSilent $Setup @("/LOG=$(Join-Path $LogDir 'install.log')")
Check ($rc -eq 0) "installer exit code 0 (got $rc)"
Check (Test-Path $bundle) "PumpCurve.vst3 installed at $bundle"
Check (Test-Path $module) "module installed at $module"
if (Test-Path $module) {
    $installedHash = Sha256Of $module
    Write-Host "  installed module sha256: $installedHash"
    if ($ExpectedModuleSha256) {
        Check ($installedHash -eq $ExpectedModuleSha256.ToLower()) `
            "installed module is byte-identical to the built artifact"
    } else {
        Write-Host "  note: no -ExpectedModuleSha256 supplied; hash recorded only"
    }
    $info = (Get-Item $module).VersionInfo
    Check (($info.ProductName -eq $ProductName) -or ($info.CompanyName -eq "Music-Prod")) `
        "version resource identifies the product (ProductName '$($info.ProductName)')"
}

# 3. Upgrade cleanup and preservation.
Write-Host "-- upgrade cleanup and preservation"
Check (-not (Test-Path $legacy)) "legacy SideChain.vst3 was removed (only ours)"
Check (Test-Path $decoyMod) "foreign plug-in survived the install"
if (Test-Path $decoyMod) {
    Check ((Sha256Of $decoyMod) -eq $decoyHash) "foreign plug-in is byte-identical after install"
}

# 4. Uninstall silently.
Write-Host "-- uninstall"
if (-not (Test-Path $uninstaller)) {
    Bad "uninstaller not found at $uninstaller"
} else {
    $rc = RunSilent $uninstaller @("/LOG=$(Join-Path $LogDir 'uninstall.log')")
    Check ($rc -eq 0) "uninstaller exit code 0 (got $rc)"
    Check (-not (Test-Path $bundle)) "PumpCurve.vst3 removed"
    Check (-not (Test-Path $uninstaller)) "uninstaller removed itself"
    Check (Test-Path $decoyMod) "foreign plug-in survived the uninstall"
    if (Test-Path $decoyMod) {
        Check ((Sha256Of $decoyMod) -eq $decoyHash) "foreign plug-in is byte-identical after uninstall"
    }
}

# 5. Clean the fixture up so later steps see a pristine folder.
Remove-Item -Recurse -Force $decoyDir -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "INSTALLER TEST: $script:passes passed, $script:failures failed"
Write-Host "logs: $LogDir"
if ($script:failures -eq 0) { Write-Host "RESULT: PASS"; exit 0 }
Write-Host "RESULT: FAIL"; exit 1
