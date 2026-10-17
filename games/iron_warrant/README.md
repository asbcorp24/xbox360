# Iron Warrant

Original gritty pixel-art action game built with Xbox360 Game Maker.

## Campaign

1. Prison Shower
2. Motel Ambush
3. Cafe Parking Lot
4. House Assault
5. Atlanta Club
6. Final Warehouse

The current playable campaign contains 78 enemies across six long scrolling stages, checkpoints, shooting, melee combat, particles and multi-layer parallax.

## Windows

```bat
play.bat
```

## Xbox 360

```bat
build.bat
```

## Parallax layers

Every chapter uses its own three-layer pixel-art scene:

- far background: speed 0.08
- midground: speed 0.35
- foreground overlay: speed 1.12

The PNG assets live inside this game folder under `assets/backgrounds/`.

## Enemy archetypes

`shiv`, `brute`, `hitman`, `merc`, `bully`, `bouncer`, and `boss` have separate engine archetypes with different speed/health/damage tuning. Distinct final sprite sheets are being wired after the validated campaign build.
