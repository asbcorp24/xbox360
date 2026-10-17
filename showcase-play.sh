#!/bin/sh
exec python3 "$(dirname "$0")/tools/xgm.py" --project "$(dirname "$0")/games/neon_district" play
