@echo off
setlocal
cd /d "%~dp0..\.."
python tools\xgm.py --project "games\neon_district" play
