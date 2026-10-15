# Agent instructions

Arcade games for the LilyGO TTGO T-Display V1.1 (ESP32, ST7789 135×240 portrait,
two buttons), built with PlatformIO + Arduino core 2.0.x + TFT_eSPI. Human docs: README.md.

## Build, flash, verify

- Build: `pio run`. Must finish with no errors and no new warnings (the TFT_eSPI
  `TOUCH_CS pin not defined` warning is expected).
- Flash: `pio run -t upload || pio run -t upload`. The board's USB chip often fails
  the first attempt ("No serial data received", "serial noise"); retry once. If the
  port is gone (`ls /dev/cu.usbserial-*` empty) the board is unplugged; say so.
  The port name can change between `usbserial-54FC0117661` and `usbserial-54350247581`.
- Keep `upload_speed = 460800`; 921600 corrupts uploads on this board.
- You cannot see the screen. After a visual or gameplay change, say what to check on
  the device. For game balance, simulate the relevant logic in a throwaway Python script
  rather than guessing (that is how the CPU miss rates in `tennis.cpp` `pickAimErr()` were tuned).
- Serial: `pio device monitor` (115200, exception decoder enabled). Extra logging only
  appears when Settings > USB log is on; use `dbg(...)`, never raw `Serial.print`.
- Saved crash record without the screen: VS Code task "Show last crash record"
  (reads NVS at 0x9000, length 0x5000).

## Code layout

| File | Contents |
|---|---|
| `src/common.h` | Pins, shared declarations, `DECLARE_GAME`, `idleTooLong()`, `HEADER_H` |
| `src/main.cpp` | Globals (`tft`, `spr`, `prefs`, buttons), shared drawing helpers, scrolling menu (`ITEMS[]`), Sleep entry, `setup`/`loop` |
| `src/power.cpp` | Backlight PWM, battery voltage, `goToSleep()` |
| `src/diag.cpp` | USB log (`dbg`), crash record across resets |
| `src/settings.cpp` | Settings screen (uses the game entry points) |
| `src/<game>.cpp` | One game per file, each in its own namespace |

Everything draws into one full-screen sprite `spr` and pushes it with `spr.pushSprite(0, 0)`.

## Adding a game

1. New `src/<name>.cpp` with `namespace <name> { ... }` exposing `begin()` (show title
   screen), `update(uint32_t now)` (one loop iteration) and `drawIcon(cx, cy, dim)`
   (≤ 24×30 px, darker when `dim`). Copy the structure of `bricks.cpp` or `stack.cpp`.
2. `DECLARE_GAME(<name>)` in `common.h`, and a `GAME("Name", "<name>_hi", <name>)` line in
   `ITEMS[]` in `main.cpp` (before Settings).
3. Use the shared pieces: states `TITLE / PLAY / GAMEOVER`; `drawScoreHeader()` for
   rows 0..`HEADER_H`-1; `drawGameOverBox()`; `startOrEnd(state == GAMEOVER, eL, eR, armed)`
   for title/game-over input (it already handles "L = menu"); fixed 16 ms ticks with the
   "don't catch up after a stall" guard; `idleTooLong(now, lastInput)` + `goToSleep()`
   when not playing; a high score in NVS under `<name>_hi`.
4. Title screen: name in font 4, controls as yellow key / white action pairs in font 2,
   "Best N", "Press a button" in green. Update the controls tables in README.md.

## Pitfalls (each of these has bitten this project)

- **C++11** (`-std=gnu++11`): a struct with default member initializers is not an
  aggregate, so `Btn b{pin}` needs an explicit constructor.
- **Macro name clashes**: FreeRTOS defines `LIST_H`; Arduino defines `B0`..`B11111111`,
  `PI`, `min`/`max`. Prefix new constants (`MENU_LIST_H`) or keep them in a namespace.
- **millis() comparisons**: never `now - lastX > LIMIT` when `lastX` may have been set
  from `millis()` after `now` was taken (unsigned wrap → instant sleep). Use
  `idleTooLong()` or a signed `(int32_t)(a - b)` comparison.
- **Text must fit 135 px.** Font 1 is 6 px/char: ≤ 22 chars per line. Font 2 ≈ 7–8 px/char.
  Font 4 is wide: menu names must stay ≤ ~90 px (mixed case; "BREAKOUT" doesn't fit, hence
  "Bricks"). Exact font 4 widths: `widtbl_f32` in `.pio/libdeps/*/TFT_eSPI/Fonts/Font32rle.c`.
- **Rotated sprites** (`pushRotated`, used for race cars and the flappy bird): source and
  destination colour depth must match; black (`TFT_BLACK`) is the transparent key, so
  use `0x18E3`-ish dark grey for "black" details. TFT_eSPI's `getRotatedBounds` has a
  harmless clipping bug (`min_x = 0` instead of `*min_x = 0`); don't "fix" files in `.pio/`.
- **Buttons**: `updateBtn()` returns 1 = pressed, 2 = released. Menus and title screens
  act on *release* of a press that *started on that screen* (`armed` / `pressedL`), so a
  button held over from the previous screen never triggers anything. At boot the
  button state is pre-seeded so the wake-up press doesn't count.
- **Deep sleep** wakes only on GPIO35 (ext1, `ALL_LOW`); waking is a full reboot into the menu.
  `goToSleep()` must detach the backlight PWM and hold `TFT_BL` and `ADC_EN` (GPIO14) low.
- **NVS**: namespace is `"tetris"` for historical reasons and Tetris's score key is
  `"hi"`; keep both, or existing high scores are lost. Other keys: `<game>_hi`, `game`
  (last played menu index), `bright`, `usblog`, `lastcrash`.
- **Battery**: GPIO34 reads half the battery voltage, only while GPIO14 (`ADC_EN`) is high.
  On USB it reads the ~5 V supply (`onUsbPower()` ≥ 4.35 V), not the battery.

## Style

Match the existing code: 2-space indent, `static` file-local state inside the game's
namespace, short comments that say *why*, section banners
(`// ---------------- rendering ----------------`). Each game file starts with a
comment listing its controls and rules.
