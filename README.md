# RetroGun

A pixel-art top-down space shooter for **MS-DOS 6.2+**, designed to run on
period-accurate hardware and fit comfortably on a single 1.44 MB floppy disk.

---

## Gameplay

You pilot a lone fighter ship through five increasingly difficult waves of alien
invaders.  Survive all five waves to win; lose all three lives and it's game over.

| Wave | Description |
|------|-------------|
| 1–2  | Scout swarms – fast, fragile ships |
| 3–4  | Mixed squads – Scouts and Fighters |
| 5    | All enemy types including tough Bombers |

### Controls

| Key         | Action       |
|-------------|--------------|
| Arrow keys  | Move ship    |
| Space       | Fire         |
| ESC         | Quit         |

### Scoring

| Enemy   | Points |
|---------|--------|
| Scout   | 10     |
| Fighter | 25     |
| Bomber  | 50     |

A bonus life is awarded at the end of every second wave (up to 5 lives maximum).

---

## Technical Details

| Property        | Value |
|-----------------|-------|
| Platform        | MS-DOS 6.2+ |
| CPU requirement | Intel 386 or better |
| Graphics        | VGA – Mode 13h (320 × 200, 256 colours) |
| Compiler        | DJGPP 2.x (GCC for DOS) |
| DOS extender    | CWSDPMI (32-bit protected mode) |
| Typical EXE size | < 80 KB – fits easily on a 1.44 MB floppy |

The game uses:

* **Mode 13h** VGA – 320 × 200 with 256 colours
* **Double-buffered rendering** – a 64 KB back-buffer is blitted to video RAM
  each frame to eliminate flicker
* **Interrupt-driven keyboard** – IRQ 1 handler for smooth multi-key input
* **Parallax star field** – three-layer scrolling starfield background
* **Particle explosions** – colour-coded debris for each enemy type
* **Embedded 8 × 8 pixel font** – no external font file required
* **Pixel-art sprites** – player ship (13 × 11) and three enemy types (11 × 11)
  encoded directly in source as byte arrays

---

## Building

### Option A – Cross-compile on Linux (recommended for development)

1. Install the DJGPP cross-compiler:

   ```bash
   # Build from source (https://github.com/andrewwutw/build-djgpp)
   # or use a pre-built package for your distribution.
   # Example for Debian/Ubuntu (if the package is available):
   sudo apt-get install gcc-djgpp
   ```

2. Build:

   ```bash
   make
   ```

   This produces `RETROGN.EXE`.

3. Copy `RETROGN.EXE` and `CWSDPMI.EXE` (see below) to your DOS system or
   DOSBox directory and run.

### Option B – Build on DOS / DOSBox with DJGPP

1. Install DJGPP 2.x inside DOS or DOSBox.
2. Copy this repository to the DOS filesystem.
3. Run `BUILD.BAT`:

   ```
   BUILD
   ```

### Running the game

```
RETROGN.EXE
```

`CWSDPMI.EXE` must be in the same directory or on `PATH`.  It is the free
32-bit DOS extender used by DJGPP programs.  Download it from:
<https://sandmann.de/cwsdpmi/>

### DOSBox quick-start

```dosbox
[autoexec]
MOUNT C /path/to/RetroGun
C:
RETROGN.EXE
```

---

## Floppy disk distribution

The distribution fits entirely on a **1.44 MB (3.5″)** floppy:

```
RETROGN.EXE   ~70 KB
CWSDPMI.EXE   ~16 KB
README.TXT     ~3 KB
              ──────
Total         ~89 KB   (6 % of floppy capacity)
```

To create a bootable floppy image with DOSBox:

```
imgmake floppy.img -t fd_1440
imgmount a floppy.img
copy RETROGN.EXE a:\
copy CWSDPMI.EXE a:\
```

---

## Project structure

```
RetroGun/
├── src/
│   └── RETROGN.C   Single-file C source (~1100 lines)
├── Makefile         Cross-compile (Linux → DOS) and native DOS build
├── BUILD.BAT        DOS batch build script
└── README.md        This file
```

---

## License

Released under the MIT License.  See source file header for details.
