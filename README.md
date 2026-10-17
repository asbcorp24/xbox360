# Xbox360 Game Maker

Console-first mini game maker for **Xbox 360 homebrew** on **libxenon**.

The project is data-driven: edit `project/game.json` from the CLI, keep normal PNG/WAV assets, generate C data, then build an Xbox 360 `.elf/.elf32`.

## Current features

- console project editor (`xgm`)
- multiple level rooms/screens
- solid platforms
- collectible crystals
- player spawn and exit editing
- real PNG sprite-sheet source assets
- player animation: idle / walk / jump
- horizontal sprite flipping
- WAV sound-effect support
- built-in tone fallback when WAV files are not configured
- automatic PNG/WAV -> C generation
- Xbox 360 controller input
- pixel-perfect 320x180 virtual rendering
- GitHub Actions builds

## Console usage

Windows CMD / PowerShell:

```bat
xgm.bat info
xgm.bat list
xgm.bat validate
xgm.bat generate
xgm.bat build --docker
```

Linux / MSYS2:

```bash
./xgm info
./xgm list
./xgm validate
./xgm generate
./xgm build --docker
```

Direct Python also works:

```bash
python tools/xgm.py info
```

## Editing from the console

Add a room:

```bash
./xgm add-room "Factory"
```

Add a platform to room 1:

```bash
./xgm add-platform 1 40 120 80 8
```

Add a crystal:

```bash
./xgm add-crystal 1 75 108
```

Move player start:

```bash
./xgm set-player 0 14 148
```

Move the exit:

```bash
./xgm set-exit 2 301 139 --w 11 --h 21
```

Then:

```bash
./xgm validate
./xgm generate
./xgm build --docker
```

Build outputs:

```text
xbox360-pixel.elf
xbox360-pixel.elf32
```

## PNG sprites

The demo uses:

```text
assets/player.png
```

It is a horizontal sprite sheet with four 8x12 frames:

```text
0 = idle
1 = walk A
2 = walk B
3 = jump
```

Replace it with your own non-interlaced 8-bit RGB/RGBA PNG and update `player_frame_size` in `project/game.json`.

## Sound

The runtime has jump and collect sounds immediately through libxenon's `xenon_tone` fallback.

For custom sound effects, configure ordinary WAV files in `project/game.json`:

```json
"sounds": {
  "jump": "assets/jump.wav",
  "collect": "assets/collect.wav"
}
```

WAV requirements:

```text
48000 Hz
16-bit signed PCM
stereo
uncompressed
```

The CLI embeds them as PCM arrays and the Xbox 360 runtime submits them through `xenon_sound`.

## Multiple screens

The included demo has three rooms. Walking past the right edge changes to the next room; walking past the left edge returns to the previous one. Collected crystals stay collected between rooms.

The final exit is on room 2 and opens after all crystals are collected.



## Tilemaps, enemies and game entities

Set a solid tile in room 0:

```bash
./xgm set-tile 0 5 8 1
```

Set a decorative non-solid tile:

```bash
./xgm set-tile 0 10 6 2 --decor
```

Tile coordinates are grid coordinates. The default tile size is 16 pixels and is configured by `tile_size` in `project/game.json`.

Add a horizontally patrolling enemy:

```bash
./xgm add-enemy 0 190 151 --left 180 --right 230 --speed 0.45
```

Add an NPC:

```bash
./xgm add-npc 0 102 148 "Collect every crystal!"
```

Stand near an NPC and press **B** to interact.

Add a trigger:

```bash
./xgm add-trigger 1 180 120 24 40 --action tone --once
```

Trigger actions currently supported:

```text
tone
checkpoint
teleport
```

Teleport trigger example:

```bash
./xgm add-trigger 1 180 120 24 40 --action teleport --target-room 2 --target-x 20 --target-y 145
```

Add a door. Doors are activated with **B**:

```bash
./xgm add-door 1 294 140 2 18 145
```

Door that only opens after all crystals are collected:

```bash
./xgm add-door 1 294 140 2 18 145 --requires-all-crystals
```

Add an automatic teleporter:

```bash
./xgm add-teleport 2 15 148 0 280 145
```

The demo project includes examples of every new entity.

### Gameplay behavior

- solid tiles participate in player collision
- decorative tiles render without collision
- enemies patrol between `left` and `right`
- touching an enemy respawns the player at the latest checkpoint
- checkpoint triggers change the respawn point
- NPC interaction uses **B**
- doors use **B**
- teleports activate automatically
- one-shot triggers remember that they already fired
- collected crystals remain collected when changing rooms



## Scripting layer

Xbox360 Game Maker now has a small data-driven scripting VM. Scripts are stored in `project/game.json`, compiled by `xgm generate`, and executed by the Xbox 360 runtime.

### Flags and variables

Create or change a flag:

```bash
./xgm set-flag quest_started 1
```

Create or change a numeric variable:

```bash
./xgm set-var score 0
```

### Events

Supported events:

```text
on_enter
on_interact
on_collect
```

`room=-1` means any room. `target=-1` means any matching entity.

Example: show a dialog when room 1 is entered:

```bash
./xgm add-script on_enter --room 1 --once \
  --do "dialog:Welcome to room 1"
```

Example: NPC 0 in room 0 starts a quest and increments a variable:

```bash
./xgm add-script on_interact --room 0 --target 0 \
  --do set_flag:quest_started:1 \
  --do add_var:talks:1 \
  --do "dialog:Go to the roof"
```

Example: every collected crystal increases score:

```bash
./xgm add-script on_collect \
  --do add_var:score:1
```

### Conditions

A script can require a flag:

```bash
./xgm add-script on_interact --room 0 --target 0 \
  --if-flag quest_started=1 \
  --do "dialog:The quest is already active"
```

Or a variable:

```bash
./xgm add-script on_enter --room 2 \
  --if-var score=6 \
  --do "dialog:You collected everything"
```

### Script actions

Supported actions:

```text
set_flag:name:value
set_var:name:value
add_var:name:value
open_door:room:local_index
spawn_enemy:room:local_index
change_room:room:x:y
dialog:text
```

Open a script-locked door:

```bash
./xgm add-door 1 294 140 2 18 145 --locked

./xgm add-script on_collect --room 1 --target 1 --once \
  --do open_door:1:0 \
  --do "dialog:The door is now open"
```

Spawn an initially inactive enemy:

```bash
./xgm add-enemy 2 180 151 --left 170 --right 230 --inactive

./xgm add-script on_enter --room 2 --once \
  --do spawn_enemy:2:0
```

Change room from a script:

```bash
./xgm add-script on_interact --room 0 --target 0 \
  --if-flag quest_started=1 \
  --do change_room:2:20:145
```

The scripting layer is compiled into static C tables. The Xbox 360 build does not parse JSON at runtime.



## Inventory, health, combat and branching dialogue

Player RPG stats:

```bash
./xgm set-player-stats --health 5 --damage 1
```

Create inventory items:

```bash
./xgm add-item roof_key key
./xgm add-item medkit consumable --power 2
./xgm add-item rusty_sword weapon --power 2 --count 1
```

Place a pickup in a room:

```bash
./xgm add-pickup 1 roof_key 110 82
./xgm add-pickup 0 medkit 210 148
```

Doors can require a key:

```bash
./xgm add-door 1 294 140 2 18 145 --locked --key roof_key
```

Enemy types:

```text
walker  - normal patrol
charger - accelerates toward the player when nearby
tank    - slower but intended for more HP / damage
```

Examples:

```bash
./xgm add-enemy 0 190 151 --left 180 --right 230 \
  --type walker --hp 2 --damage 1

./xgm add-enemy 1 160 84 --left 145 --right 200 \
  --type charger --speed 0.7 --hp 3 --damage 1

./xgm add-enemy 2 205 151 --left 190 --right 250 \
  --type tank --hp 5 --damage 2
```

Combat controls:

```text
X = attack
A = jump
B = interact / confirm dialogue answer
D-pad / left stick = move and select dialogue answers
```

Weapon damage is taken from the strongest weapon currently present in the inventory. If there is no weapon, `player.damage` is used.

### Branching dialogue

Create dialogue nodes:

```bash
./xgm add-dialog guide_start "Do you want the roof key?"
./xgm add-dialog guide_help "Find the key upstairs. Take this medkit."
./xgm add-dialog guide_no "Then be careful. The tank hits hard."
```

Add choices:

```bash
./xgm add-choice guide_start "Yes, help me." \
  --next guide_help \
  --set-flag accepted_help=1

./xgm add-choice guide_start "No, I can manage." \
  --next guide_no

./xgm add-choice guide_help "Thanks." \
  --give-item medkit:1
```

Attach a dialogue to an NPC when creating it:

```bash
./xgm add-npc 0 102 148 "Guide" --dialogue guide_start
```

The dialogue UI is rendered directly by the Xbox 360 runtime. Choices can branch to other nodes, set flags, or give inventory items.

## Project data

Main project file:

```text
project/game.json
```

Generated files:

```text
source/generated_game.h
source/generated_assets.h
```

Do not edit generated files manually; run:

```bash
./xgm generate
```

## Controls

- Left stick / D-pad: move
- A: jump
- B: interact with NPCs / doors / confirm dialogue
- X: attack
- Y: use first available consumable
- D-pad / left stick: move and select dialogue answers
- Start: restart
- collect all crystals and reach the exit

## Toolchain

Editing/generation only needs Python 3 standard library.

For Xbox 360 builds use Docker:

```bash
./xgm build --docker
```

or a local devkitXenon/libxenon toolchain.

This repository contains no Microsoft XDK material or commercial game assets.
