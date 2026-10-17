@echo off
python "%~dp0tools\xgm.py" --project "%~dp0games\neon_district" validate
if errorlevel 1 exit /b %errorlevel%
python "%~dp0tools\xgm.py" --project "%~dp0games\neon_district" build --docker
