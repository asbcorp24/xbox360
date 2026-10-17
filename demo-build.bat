@echo off
python "%~dp0tools\xgm.py" --project "%~dp0demos\all_features\game.json" validate
if errorlevel 1 exit /b %errorlevel%
python "%~dp0tools\xgm.py" --project "%~dp0demos\all_features\game.json" build --docker
