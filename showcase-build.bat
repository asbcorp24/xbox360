@echo off
python "%~dp0tools\xgm.py" --project "%~dp0demos\png_showcase\game.json" validate
if errorlevel 1 exit /b %errorlevel%
python "%~dp0tools\xgm.py" --project "%~dp0demos\png_showcase\game.json" build --docker
