# Neon District

Self-contained Xbox360 Game Maker demo.

Everything for this game lives in this folder:

- `game.json` — game logic/data
- `assets/player/` — player sprite sheet
- `assets/enemies/` — enemy sprite sheets
- `assets/npc/` — NPC sprites
- `assets/backgrounds/` — PNG backgrounds/parallax/foreground
- `assets/ui/` — UI artwork
- `play.bat` — Windows Simulator
- `build.bat` — Xbox 360 Docker build

## Windows

```bat
play.bat
```

## Xbox 360 build

```bat
build.bat
```

The engine resolves every relative asset path from this folder's `game.json`.
