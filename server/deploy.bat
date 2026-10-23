@echo off
chcp 65001 >nul
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo.
  echo Нужен Node.js ^(бесплатно^): https://nodejs.org  — скачай LTS, установи и запусти deploy.bat снова.
  echo.
  start https://nodejs.org
  pause
  exit /b 1
)
node deploy.mjs
echo.
pause
