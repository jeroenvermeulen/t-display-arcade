// Shared hardware, input and drawing helpers for the T-Display games.
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "driver/gpio.h"

// ---------------- configuration ----------------
#define SCREEN_ROTATION 0        // 0 or 2 (both portrait). Use 2 if the picture is upside down.

#if SCREEN_ROTATION == 0
  #define PIN_LEFT  0
  #define PIN_RIGHT 35
#else
  #define PIN_LEFT  35
  #define PIN_RIGHT 0
#endif
// If left/right feel swapped in your orientation, just swap the two pin numbers above.

#define WAKE_PIN        GPIO_NUM_35
#define DEBOUNCE_MS     15
#define IDLE_SLEEP_MS   60000UL

// True after IDLE_SLEEP_MS without input. Signed difference: lastInput may be a
// millis() taken *after* `now` (e.g. stamped at game over during this loop),
// and must then count as "just now", not as an unsigned wrap to ~49 days.
static inline bool idleTooLong(uint32_t now, uint32_t lastInput) {
  return (int32_t)(now - lastInput) > (int32_t)IDLE_SLEEP_MS;
}

extern TFT_eSPI tft;
extern TFT_eSprite spr;
extern Preferences prefs;

// ---------------- buttons ----------------
struct Btn {
  uint8_t pin;
  bool raw = false, stable = false;
  uint32_t rawT = 0, t0 = 0, nextRepeat = 0;
  bool held = false, consumed = false;
  explicit Btn(uint8_t p) : pin(p) {}
};
extern Btn bL, bR;

// returns 0 = nothing, 1 = pressed, 2 = released
int updateBtn(Btn &b, uint32_t now);

void drawCell(int px, int py, int sz, uint16_t col);
// 0 -> 1 -> 0 over `period` ms, for back-and-forth icon animations
float triWave(uint32_t t, uint32_t period);

// ---------------- power (power.cpp) ----------------
static const int NUM_BRIGHTNESS = 5;
void initPower();                     // first thing in setup()
void initBacklight();                 // after tft.init() and prefs.begin()
void setBrightness(int level, bool save);
int brightnessLevel();                // 0 .. NUM_BRIGHTNESS - 1
int batteryMilliVolts();
int batteryPercent(int mv);
bool onUsbPower(int mv);
void goToSleep();                     // deep sleep; the WAKE_PIN button restarts the board

void returnToMenu();

// ---------------- diagnostics (diag.cpp) ----------------
void initDiag(const char *(*itemName)(int));   // after prefs.begin(); item -1 = menu
void dbg(const char *fmt, ...);       // printf to USB serial when USB logging is on
bool usbLogOn();
void setUsbLog(bool on);
void noteActiveItem(int item);        // remembered across a crash reset
bool lastCrash(char *buf, size_t n);  // e.g. "crash in Flappy"; false if none recorded

// ---------------- shared screens ----------------
static const int HEADER_H = 38;  // games keep rows 0..37 for score text

// "Score N" / "Best N" in the header area (cleared first)
void drawScoreHeader(uint32_t score, uint32_t best);
// box over the playfield after a game ends
void drawGameOverBox(const char *title, uint32_t score, uint32_t best, bool newHigh);
// Title screens: any button starts. Game-over screens: L = back to the menu
// (already done on return), R = play again. Acts on the release of a press
// that started on the screen.
enum EndChoice { END_NONE, END_PLAY, END_MENU };
EndChoice startOrEnd(bool gameOver, int eL, int eR, bool &armed);

// ---------------- games ----------------
// Each game: begin() shows its title screen, update() runs one loop iteration,
// drawIcon() draws a menu icon of at most ~24x30 centred on (cx, cy); t is an
// animation clock in ms (0 = still picture), drawing is clipped to the icon area.
#define DECLARE_GAME(ns) \
  namespace ns { \
    void begin(); \
    void update(uint32_t now); \
    void drawIcon(int cx, int cy, bool dim, uint32_t t = 0); \
  }
DECLARE_GAME(tetris)
DECLARE_GAME(snake)
DECLARE_GAME(race)
DECLARE_GAME(flappy)
DECLARE_GAME(tennis)
DECLARE_GAME(bricks)
DECLARE_GAME(stack)
DECLARE_GAME(shooter)
DECLARE_GAME(crossy)

DECLARE_GAME(settings)                // not a game, but uses the same entry points
namespace settings { void menuInfo(char *buf, size_t n); }
