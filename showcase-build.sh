#!/bin/sh
set -e
python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/games/neon_district" validate
python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/games/neon_district" build --docker
