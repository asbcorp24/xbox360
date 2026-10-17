#!/bin/sh
exec python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/demos/png_showcase/game.json" play
