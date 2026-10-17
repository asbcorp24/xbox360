@echo off
cd /d "%~dp0..\.."
python tools\xgm.py --project "games\iron_warrant" validate
if errorlevel 1 exit /b %errorlevel%
python tools\xgm.py --project "games\iron_warrant" build --docker
