@echo off
rem Build AimForge.exe on Windows (needs Git for Windows, Python, `pip install ziglang`) and copy it next to the game data.
chcp 65001 >nul
cd /d "%~dp0"
set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not exist "%BASH%" (echo Git Bash not found: %BASH% & exit /b 1)
"%BASH%" -lc "cd \"$(cygpath -u '%~dp0')\" && sh build_windows.sh" || exit /b 1
tasklist /FI "IMAGENAME eq AimForge.exe" | find /I "AimForge.exe" >nul && (echo AimForge is running - close it to replace AimForge.exe & exit /b 0)
copy /Y build\win\AimForge.exe AimForge.exe >nul && echo Copied build\win\AimForge.exe to .\AimForge.exe
