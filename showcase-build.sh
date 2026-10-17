#!/bin/sh
set -e
python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/demos/png_showcase/game.json" validate
python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/demos/png_showcase/game.json" build --docker
