@echo off
REM PumpCurve - build the Windows installer from the staged bundle.
REM
REM Requires Inno Setup 6 (official, free, no-account download from
REM https://jrsoftware.org/isdl.php; in CI: "choco install innosetup -y").
REM If ISCC is not found this script says so and exits without building anything -
REM it never fabricates an installer.
REM
REM Usage: build_installer.cmd [version] [path-to-PumpCurve.vst3]
REM        Set PUMPCURVE_OUTDIR to write the Setup.exe somewhere other than
REM        Builds\Windows\artifacts (CI keeps artifacts outside the checkout).
setlocal enabledelayedexpansion
set HERE=%~dp0
set VERSION=%~1
if "%VERSION%"=="" set VERSION=0.4.1
set BUNDLE=%~2
if "%BUNDLE%"=="" set BUNDLE=%HERE%..\artifacts\PumpCurve.vst3
set BUNDLE=%BUNDLE:"=%

if not exist "%BUNDLE%" (
  echo ERROR: staged bundle not found: %BUNDLE%
  echo        Stage it first:  python tools\stage_windows_vst3.py --stage ^<built SideChain.vst3^> --out-dir ..\artifacts
  exit /b 2
)
if not exist "%BUNDLE%\Contents" (
  echo ERROR: %BUNDLE% has no Contents directory - it is not a VST3 bundle.
  exit /b 2
)

set OUTARG=
if not "%PUMPCURVE_OUTDIR%"=="" set OUTARG=/O"%PUMPCURVE_OUTDIR%"
set OUTDIR=%HERE%..\artifacts
if not "%PUMPCURVE_OUTDIR%"=="" set OUTDIR=%PUMPCURVE_OUTDIR%

set ISCC=
for %%P in (
  "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
  "%ProgramFiles%\Inno Setup 6\ISCC.exe"
  "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
) do (
  if exist %%P set ISCC=%%~P
)
if "%ISCC%"=="" (
  where ISCC.exe >nul 2>&1 && set ISCC=ISCC.exe
)
if "%ISCC%"=="" (
  echo ERROR: Inno Setup 6 ^(ISCC.exe^) not found.
  echo        Install it from https://jrsoftware.org/isdl.php ^(or "choco install innosetup -y"^) and re-run.
  echo        Without it the product can still ship as the ZIP that
  echo        "python tools\stage_windows_vst3.py --package" produces.
  exit /b 3
)

echo == building the installer ==
"%ISCC%" %OUTARG% /DAppVersion=%VERSION% /DStagedBundle="%BUNDLE%" "%HERE%PumpCurve.iss"
if errorlevel 1 (
  echo ERROR: ISCC failed
  exit /b 1
)

set SETUP=%OUTDIR%\PumpCurve-%VERSION%-Windows-x64-Setup.exe
if not exist "%SETUP%" (
  echo ERROR: ISCC reported success but %SETUP% is missing.
  exit /b 1
)

echo == hashing the installer ==
certutil -hashfile "%SETUP%" SHA256
echo == signature state ^(unsigned until a certificate is supplied^) ==
powershell -NoProfile -Command "(Get-AuthenticodeSignature '%SETUP%').Status"
echo Done. The installer still has to pass installer\test_install_uninstall.ps1
echo and the DAW host tests before it is published.
exit /b 0
