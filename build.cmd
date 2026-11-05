@echo off
rem  build.cmd -- one-click build of the Xbox game.
rem
rem  1. copy the game's main folder to  gamedata\main  (see gamedata\README.md)
rem  2. run this file
rem  3. the game appears as  output\mohaa.iso  and as the folder  output\mohaa
rem
rem  Extra options are passed straight through, e.g.:
rem     build.cmd --no-music
rem     build.cmd --gamedata "D:\Games\MOHAA"
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>&1 || set "PY=py -3"

%PY% -m pip install --quiet --disable-pip-version-check -r requirements.txt
%PY% tools\pack_iso.py %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Build FAILED ^(exit %RC%^).
) else (
    echo Done. Check output\ for the disc image and the folder.
)
pause
exit /b %RC%
