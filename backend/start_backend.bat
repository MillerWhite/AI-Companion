@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0.."
echo AI Companion backend
echo Адрес: http://127.0.0.1:8787
echo Для остановки закройте это окно.
py -m uvicorn backend.main:app --host 127.0.0.1 --port 8787
pause
