#!/bin/sh
set -e
python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/demos/all_features/game.json" validate
python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/demos/all_features/game.json" build --docker
