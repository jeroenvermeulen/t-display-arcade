# T-Display Arcade

> **🤖 This project is 100% AI-generated.** Every line of code, configuration and
> documentation was written by AI (Claude, using Claude Code). A human chose the
> features, played the games on the real board and reported problems, but did not
> write or edit any of the code. Read and use it with that in mind.

Nine small arcade games for the **LilyGO TTGO T-Display V1.1**, an ESP32 board with a
1.14" colour screen (135 × 240, used upright) and two buttons. Pick a game from a
scrolling menu; high scores and settings are remembered when the power is off.

## Games

The two buttons are called **L** and **R**: L is the button on GPIO0 (also the BOOT
button) and R the one on GPIO35, which also wakes the board from sleep.

| Game | Controls | Goal |
|---|---|---|
| **Tetris** | Tap L/R: move · Hold L: rotate · Hold R: soft drop · L+R: hard drop | Clear lines; speeds up every 10 lines |
| **Snake** | Tap L/R: turn left/right (relative to the snake) | Eat food, avoid walls and yourself |
| **Race** | Hold L/R: steer | Dodge traffic on a winding road; grass slows you down |
| **Flappy** | Tap L or R: flap | Fly through the gaps; easy start, harder after ~30 points |
| **Tennis** | Hold L/R: move | Beat the computer; hit with the racket edge for sharp angles. It wins at 5 points |
| **Bricks** | Hold L/R: move paddle | Breakout: clear the wall, 3 lives, faster each level |
| **Stack** | Tap L or R: drop block | Stack as high as you can; overhangs are cut off, 3 perfect drops in a row widen the block |
| **Shooter** | Hold L/R: fly (fires by itself) | Shoot saucers and cruisers in growing waves; 3 lives |
| **Crossy** | Tap L or R: hop forward · Hold L/R: hop sideways · L + R: hop back | Cross roads and rivers; keep moving or the eagle gets you |

**Starting and ending a game:** on a game's title screen, press any button to start.
When the game ends, press **L** to go back to the menu or **R** to play again.

## Menu, settings and sleep

- **Menu**: tap **L** to move to the next entry, **R** to open it. The last game you
  played is selected when the board starts.
- **Settings** (in the menu, after the games):
  - *Battery*: charge level and voltage. Shows "USB power" while plugged in (the board
    can't measure the battery then).
  - *Brightness*: 5 levels; tap R to step up (wraps round to the dimmest). Dimmer
    lasts longer on battery.
  - *USB log*: sends debug messages to a connected computer (see below).
  - If the board ever crashed or was reset during a game, an orange line at the bottom
    says so, e.g. "! crash in Flappy".
- **Sleep** (last menu entry): puts the board into deep sleep, using as little battery as
  it can. The board also falls asleep by itself after 60 seconds without a button press
  (not during play). **Press R to wake it**; it starts again at the menu.

## Building and flashing

You need [PlatformIO](https://platformio.org/) — the VS Code extension or the `pio`
command line. Everything else (ESP32 toolchain, TFT_eSPI library) is downloaded on the
first build.

```sh
pio run              # build
pio run -t upload    # build and flash the board over USB
pio device monitor   # watch the board's serial output (Ctrl+C to quit)
```

In VS Code, **Terminal → Run Task…** has ready-made tasks: build, upload (with an
automatic retry), upload + monitor, serial monitor, show the last crash record,
reset high scores & settings, clean build and a size report.

The display settings live in `platformio.ini` as build flags, so the TFT_eSPI library
doesn't need editing. If the picture is upside down, set `SCREEN_ROTATION` to `2` in
`src/common.h` (this also swaps which physical button is L and R).

## Troubleshooting

- **Upload fails** ("No serial data received", "serial noise"): just run it again —
  this board's USB chip often fails the first attempt. If it keeps failing, press a
  button or RST to wake the board, or hold the L (BOOT) button while the upload starts.
- **Screen goes black**: the board is asleep. Press R.
- **Seeing what the board is doing**: turn on Settings → USB log, then run
  `pio device monitor`. Opening the monitor restarts the board, and sleep drops the USB
  connection, so reopen it after waking. Crash reports always appear and are decoded
  to file and line numbers.

## Project layout

```
platformio.ini        board, libraries, display pins, upload/monitor settings
src/common.h          pins, shared declarations
src/main.cpp          menu, shared drawing helpers, setup/loop
src/power.cpp         brightness, battery, deep sleep
src/diag.cpp          USB log, crash record
src/settings.cpp      settings screen
src/<game>.cpp        one file per game
AGENTS.md             notes for AI coding agents (also useful for humans adding a game)
```

Want to add a game? See "Adding a game" in [AGENTS.md](AGENTS.md); it takes one new
file and two one-line registrations.

## Hardware
### Module
[TTGO T-Display v1.1](https://lilygo.cc/products/t-display)
- ESP32 Xtensa dual-core LX6 microprocessor
- 1.14 inch ST7789V IPS LCD. 
  - SPI interface
  - 135 x 240 pixels, 260 PPI
  - Full Color
- 4M or 16M Flash
- CH9102 Serial
- 2 Pin 1.25mm JST battery terminal

### Battery
3.7V Lipo Battery, I am using a 102540

### Case
[Case for 3D Printing](https://www.thingiverse.com/thing:4943960)