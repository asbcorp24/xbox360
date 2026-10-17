@echo off
setlocal
cd /d "%~dp0..\.."
python tools\xgm.py --project "games\neon_district" validate
if errorlevel 1 exit /b %errorlevel%
python tools\xgm.py --project "games\neon_district" build --docker
