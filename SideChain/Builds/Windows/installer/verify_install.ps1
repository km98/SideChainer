# PumpCurve - verify a Windows installation (VST3)
#
# Checks that what is installed on this machine is exactly the artifact that was
# published, and reports any leftover legacy bundle that could shadow it.
#
# Usage (PowerShell):
#   powershell -ExecutionPolicy Bypass -File installer\verify_install.ps1 `
#            -Sha256 <expected module sha256 from RUN-MANIFEST.json>
#
# Exit code 0 = everything matched, 1 = mismatch or legacy leftovers found.

param(
    [Parameter(Mandatory = $false)]
    [string]$Sha256 = "",
    [string]$CommonVst3 = "$env:CommonProgramFiles\VST3",
    [string]$CommonVst332 = "${env:ProgramFiles(x86)}\Common Files\VST3",
    [string]$ProductName = "PumpCurve"
)

$ErrorActionPreference = "Stop"
$failures = 0

function Report($ok, $message) {
    if ($ok) { Write-Host "  ok:   $message" } else { Write-Host "  FAIL: $message"; $script:failures++ }
}

$bundle = Join-Path $CommonVst3 "$ProductName.vst3"
Write-Host "== PumpCurve Windows VST3 verification =="
Write-Host "  VST3 folder: $CommonVst3"

# 1. The bundle and its module must exist.
Report (Test-Path $bundle) "bundle present: $bundle"
$module = Join-Path $bundle "Contents\x86_64-win\$ProductName.vst3"
Report (Test-Path $module) "module present: $module"

if (Test-Path $module) {
    # 2. The module must be x64 (PE machine type 0x8664), 64-bit and named for the product.
    $bytes = [System.IO.File]::ReadAllBytes($module)
    if ($bytes.Length -lt 0x40) {
        Report $false "module is too small to be a PE file"
    } else {
        $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
        $machine = if ($peOffset -gt 0 -and $peOffset + 4 -lt $bytes.Length) {
            [BitConverter]::ToUInt16($bytes, $peOffset + 4)
        } else { 0 }
        Report ($machine -eq 0x8664) ("module is x64 (PE machine 0x{0:X4})" -f $machine)
    }

    # 3. Hash: compare against the published value when one is supplied.
    $hash = (Get-FileHash -Algorithm SHA256 -Path $module).Hash.ToLower()
    Write-Host "  module sha256: $hash"
    if ($Sha256 -ne "") {
        Report ($hash -eq $Sha256.ToLower()) "installed module matches the published SHA-256"
    } else {
        Write-Host "  note: no -Sha256 supplied; hash shown for manual comparison with RUN-MANIFEST.json"
    }

    # 4. Product identity from the version resource.
    $info = (Get-Item $module).VersionInfo
    Write-Host "  ProductName:  $($info.ProductName)"
    Write-Host "  CompanyName:  $($info.CompanyName)"
    Write-Host "  FileVersion:  $($info.FileVersion)"
    Report (($info.ProductName -eq $ProductName) -or ($info.CompanyName -eq "Music-Prod")) `
        "version resource identifies this product"
}

# 5. Legacy bundles must not be present: two bundles with one identity can shadow
#    each other in a host's plug-in list.
foreach ($legacy in @("SideChain.vst3", "SideChainer.vst3")) {
    $path = Join-Path $CommonVst3 $legacy
    if (Test-Path $path) {
        Report ($false) "legacy bundle still installed: $path (remove it, or re-run the installer)"
    } else {
        Report ($true) "no legacy bundle at $path"
    }
}

# 6. A 32-bit legacy bundle is reported, never treated as a failure: that tree may
#    belong to a 32-bit host the customer still uses.
foreach ($legacy in @("SideChain.vst3", "SideChainer.vst3")) {
    $path = Join-Path $CommonVst332 $legacy
    if (Test-Path $path) {
        Write-Host "  note: 32-bit legacy bundle present and untouched: $path"
    }
}

Write-Host ""
if ($failures -eq 0) { Write-Host "VERIFICATION PASSED" ; exit 0 }
Write-Host "VERIFICATION FAILED ($failures problem(s))"
exit 1
