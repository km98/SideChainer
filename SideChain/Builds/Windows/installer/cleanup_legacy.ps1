# PumpCurve - remove a previous SideChain/SideChainer bundle, but only our own.
#
# Called by PumpCurve.iss during install: the recipe ships this file with
# "Flags: dontcopy", extracts it with ExtractTemporaryFile, and runs it hidden.
# The ownership decision lives here because Inno Setup's Pascal Script cannot make
# it: its support functions cover numeric version numbers only
# (GetPackedVersion, ComparePackedVersion, ...), it has no StringFileInfo accessor,
# and it rejects a Pointer type - so reading ProductName/CompanyName in [Code] is
# impossible. This script uses the same .NET VersionInfo that
# installer\verify_install.ps1 and test_install_uninstall.ps1 use.
#
# Usage: powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
#            -File cleanup_legacy.ps1 <path to legacy bundle>
#
# Exit codes
#   0  the bundle was ours and has been removed
#   2  no recognisable module inside the bundle: left untouched
#   3  the module is not this product: left untouched
#   1  unexpected failure: left untouched
param([Parameter(Mandatory = $true)][string]$Bundle)

$ErrorActionPreference = 'Stop'

try {
    if (-not (Test-Path -LiteralPath $Bundle)) { exit 2 }

    $leaf = Split-Path -Path $Bundle -Leaf
    $module = Join-Path $Bundle ('Contents\x86_64-win\' + $leaf)
    if (-not (Test-Path -LiteralPath $module)) {
        Write-Host "kept (no module inside): $Bundle"
        exit 2
    }

    $info = (Get-Item -LiteralPath $module).VersionInfo
    $isOurs = ($info.ProductName -eq 'PumpCurve') -or ($info.CompanyName -eq 'Music-Prod')
    if (-not $isOurs) {
        Write-Host ("kept (not this product): {0} [ProductName='{1}', CompanyName='{2}']" -f `
            $Bundle, $info.ProductName, $info.CompanyName)
        exit 3
    }

    Remove-Item -Recurse -Force -LiteralPath $Bundle
    Write-Host "removed legacy bundle: $Bundle"
    exit 0
} catch {
    Write-Host ("cleanup failed for {0}: {1}" -f $Bundle, $_.Exception.Message)
    exit 1
}
