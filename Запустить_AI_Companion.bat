@echo off
chcp 65001 >nul
set "HTML=%~dp0AI_Companion_Emotional_Engine.html"
if not exist "%HTML%" (
  echo Не найден файл AI_Companion_Emotional_Engine.html
  pause
  exit /b 1
)
where msedge >nul 2>nul
if not errorlevel 1 (
  start "" msedge --new-window "%HTML%"
  exit /b 0
)
where chrome >nul 2>nul
if not errorlevel 1 (
  start "" chrome --new-window "%HTML%"
  exit /b 0
)
if exist "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" (
  start "" "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" --new-window "%HTML%"
  exit /b 0
)
if exist "%ProgramFiles%\Google\Chrome\Application\chrome.exe" (
  start "" "%ProgramFiles%\Google\Chrome\Application\chrome.exe" --new-window "%HTML%"
  exit /b 0
)
start "" "%HTML%"
