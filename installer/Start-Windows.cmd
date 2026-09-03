@echo off
cd /d "%~dp0"
py -3 -m venv .venv
if errorlevel 1 goto failed
.venv\Scripts\python.exe -m pip install -r requirements.txt
if errorlevel 1 goto failed
.venv\Scripts\python.exe upgrade.py %*
if errorlevel 1 goto failed
pause
exit /b 0
:failed
echo Stopped. Read the error above and the guide. Python 3.11 is recommended.
pause
exit /b 1
