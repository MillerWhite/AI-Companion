@echo off
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"
set "HTML=%ROOT%AI_Companion_Emotional_Engine.html"
set "BACKEND_URL=http://127.0.0.1:8787/health"

if not exist "%HTML%" (
  echo Не найден файл AI_Companion_Emotional_Engine.html
  pause
  exit /b 1
)

REM Проверяем, не запущен ли backend уже.
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r=Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 '%BACKEND_URL%'; if($r.StatusCode -eq 200){exit 0}else{exit 1} } catch { exit 1 }" >nul 2>nul
if errorlevel 1 (
  where py >nul 2>nul
  if errorlevel 1 (
    echo Python не найден. Установите Python 3.10+ и включите Add Python to PATH.
    pause
    exit /b 1
  )
  py -c "import sys; raise SystemExit(0 if sys.version_info >= (3,10) else 1)" >nul 2>nul
  if errorlevel 1 (
    echo Требуется Python 3.10 или новее.
    pause
    exit /b 1
  )
  py -c "import fastapi,uvicorn,dotenv" >nul 2>nul
  if errorlevel 1 (
    echo Устанавливаю зависимости backend...
    py -m pip install -r "%ROOT%backend\requirements.txt"
    if errorlevel 1 (
      echo Не удалось установить зависимости backend.
      pause
      exit /b 1
    )
  )
  echo Запускаю облачный backend на http://127.0.0.1:8787 ...
  start "AI Companion Backend" /min "%ROOT%backend\start_backend.bat"
  echo Ожидаю готовности backend...
  set "BACKEND_READY="
  for /L %%i in (1,1,12) do (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r=Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 '%BACKEND_URL%'; if($r.StatusCode -eq 200){exit 0}else{exit 1} } catch { exit 1 }" >nul 2>nul
    if not errorlevel 1 (
      set "BACKEND_READY=1"
      goto :backend_ready
    )
    timeout /t 1 /nobreak >nul
  )
  echo ВНИМАНИЕ: backend не ответил на http://127.0.0.1:8787/health.
  echo Проверьте окно "AI Companion Backend": возможна ошибка Python или занятый порт 8787.
  echo Симулятор откроется, но облачный ИИ не заработает, пока backend не запущен.
)
:backend_ready


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
