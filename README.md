# Xbox 360 Pixel Game Prototype

Experimental 2D pixel-art game for **Xbox 360 homebrew** using **libxenon**.

The first prototype is a tiny platform/collect game rendered into a virtual 320x180 canvas and scaled with nearest-neighbor blocks to the Xbox 360 display framebuffer.

## Controls

- Left stick / D-pad: move
- A: jump
- Start: restart

## Goal

Collect all crystals, then reach the exit door.

## Build

Requires a working devkitXenon/libxenon toolchain.

```bash
export DEVKITXENON=/usr/local/xenon
export PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin"
make
```

Expected outputs:

- `xbox360-pixel.elf`
- `xbox360-pixel.elf32`

The `.elf32` image is intended for the usual XeLL/libxenon payload workflow on a compatible homebrew-enabled Xbox 360.

## Notes

The framebuffer presentation helper is derived from the MIT-licensed `xenon-soft3d` project by SaveEditors. See `THIRD_PARTY_LICENSES.md`.

This repository does not contain Microsoft XDK material or commercial game assets.
