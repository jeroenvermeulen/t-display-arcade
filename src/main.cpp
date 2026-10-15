// Games for LilyGO TTGO T-Display V1.1 (ESP32, ST7789 135x240)
//
// Games: Tetris, Snake, Race, Flappy Bird, Tennis, Bricks (Breakout), Stack,
// Shooter, Crossy (Crossy Road).
// On boot a menu lets you pick a game, open Settings (battery, brightness,
// USB logging, last crash) or put the board to sleep:
//   Tap L : next entry
//   Tap R : start / open
// The last played game is pre-selected. Press the board's RST button (or let it
// go to sleep and wake it) to return to the menu.
//
// High scores and settings are stored in NVS (survive power-off).
// On menu / title / game-over screens the board goes into deep sleep after
// 60 s of inactivity; press the GPIO35 button to wake it.

#include "common.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
Preferences prefs;
Btn bL{PIN_LEFT}, bR{PIN_RIGHT};

int updateBtn(Btn &b, uint32_t now) {
  bool r = digitalRead(b.pin) == LOW;
  if (r != b.raw) { b.raw = r; b.rawT = now; }
  if (r != b.stable && now - b.rawT >= DEBOUNCE_MS) {
    b.stable = r;
    if (r) { b.t0 = now; b.held = false; b.consumed = false; return 1; }
    return 2;
  }
  return 0;
}

void drawCell(int px, int py, int sz, uint16_t col) {
  spr.fillRect(px, py, sz - 1, sz - 1, col);
  uint16_t lt = tft.alphaBlend(120, TFT_WHITE, col);
  uint16_t dk = tft.alphaBlend(110, TFT_BLACK, col);
  spr.drawFastHLine(px, py, sz - 1, lt);
  spr.drawFastVLine(px, py, sz - 1, lt);
  spr.drawFastHLine(px, py + sz - 2, sz - 1, dk);
  spr.drawFastVLine(px + sz - 2, py, sz - 1, dk);
}

void drawScoreHeader(uint32_t score, uint32_t best) {
  char buf[32];
  spr.fillRect(0, 0, 135, HEADER_H, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
  spr.drawString(buf, 2, 24, 1);
}

void drawGameOverBox(const char *title, uint32_t score, uint32_t best, bool newHigh) {
  char buf[32];
  spr.fillRect(6, 92, 123, 84, TFT_BLACK);
  spr.drawRect(6, 92, 123, 84, TFT_RED);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_RED);
  spr.drawString(title, 67, 98, 2);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
  spr.drawString(buf, 67, 116, 2);
  if (newHigh) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString("NEW BEST!", 67, 134, 2);
  } else {
    spr.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
    spr.drawString(buf, 67, 134, 2);
  }
  // button hints: "L menu   R again"
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 16, 154, 2);
  spr.drawString("R", 68, 154, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("menu", 28, 154, 2);
  spr.drawString("again", 80, 154, 2);
}

EndChoice startOrEnd(bool gameOver, int eL, int eR, bool &armed) {
  if (eL == 1 || eR == 1) armed = true;              // need a fresh press...
  if (!armed || (eL != 2 && eR != 2)) return END_NONE;   // ...and its release
  armed = false;
  if (gameOver && eL == 2) {
    returnToMenu();
    return END_MENU;
  }
  return END_PLAY;
}

// ---------------- sleep entry ----------------
#if PIN_RIGHT == 35
  #define WAKE_BTN "R"
#else
  #define WAKE_BTN "L"
#endif

void drawMoon(int x, int y, int r, uint16_t col, uint16_t bg) {
  spr.fillCircle(x, y, r, col);
  spr.fillCircle(x + r / 2, y - r / 3, r - 1, bg);    // bite out a crescent
}

void sleepIcon(int x, int y, bool dim) {
  drawMoon(x - 2, y, 11, dim ? tft.alphaBlend(80, TFT_YELLOW, TFT_BLACK) : TFT_YELLOW, TFT_BLACK);
  uint16_t star = dim ? TFT_DARKGREY : TFT_WHITE;
  spr.fillCircle(x + 9, y + 6, 1, star);
  spr.drawPixel(x + 5, y - 10, star);
}

void sleepInfo(char *buf, size_t n) { snprintf(buf, n, WAKE_BTN " wakes it up"); }

void sleepNow() {
  spr.fillSprite(TFT_BLACK);
  drawMoon(67, 80, 24, TFT_YELLOW, TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Good night", 67, 130, 4);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Press " WAKE_BTN " to wake", 67, 164, 2);
  spr.pushSprite(0, 0);
  delay(1500);
  goToSleep();
}

// ---------------- menu ----------------
struct MenuItem {
  const char *name;              // font 4, must stay under ~90 px to clear the icon
  const char *hiKey;             // NVS key of a game's high score; nullptr if not a game
  void (*begin)();
  void (*update)(uint32_t now);  // nullptr: begin() never returns
  void (*drawIcon)(int cx, int cy, bool dim);
  void (*info)(char *buf, size_t n);   // subtitle when there is no high score
  const char *action;            // footer verb for R
};
#define GAME(name, key, ns) {name, key, ns::begin, ns::update, ns::drawIcon, nullptr, "play"}
static const MenuItem ITEMS[] = {
  GAME("Tetris",  "hi",         tetris),
  GAME("Snake",   "snake_hi",   snake),
  GAME("Race",    "race_hi",    race),
  GAME("Flappy",  "flappy_hi",  flappy),
  GAME("Tennis",  "tennis_hi",  tennis),
  GAME("Bricks",  "bricks_hi",  bricks),
  GAME("Stack",   "stack_hi",   stack),
  GAME("Shooter", "shooter_hi", shooter),
  GAME("Crossy",  "crossy_hi",  crossy),
  {"Settings", nullptr, settings::begin, settings::update, settings::drawIcon, settings::menuInfo, "open"},
  {"Sleep",    nullptr, sleepNow,        nullptr,          sleepIcon,          sleepInfo,          "sleep"},
};
static const int NUM_ITEMS = sizeof ITEMS / sizeof ITEMS[0];

int selected = 0;
int active = -1;                   // -1 = in menu
bool menuDirty = true, pressedL = false, pressedR = false;
uint32_t menuLastInput = 0, menuLastAnim = 0;

// The game list scrolls inside a viewport between the header and the footer.
static const int MENU_LIST_Y = 26, MENU_LIST_H = 186;       // screen rows 26..211
static const int BOX_H = 56, BOX_PITCH = 62;      // 3 boxes fit in the viewport
static const int MAX_SCROLL = max(0, NUM_ITEMS * BOX_PITCH - 6 + 4 - MENU_LIST_H);
int scroll = 0;                                    // current list offset, px

// smallest change to the current scroll that shows the selected box completely
int scrollTarget() {
  int top = selected * BOX_PITCH, bottom = top + BOX_H + 4;
  int t = scroll;
  if (bottom > t + MENU_LIST_H) t = bottom - MENU_LIST_H;
  if (top < t) t = top;
  return constrain(t, 0, MAX_SCROLL);
}

void renderMenu() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("MENU", 67, 6, 2);

  // boxes, clipped to the list area (viewport coordinates start at its top)
  spr.setViewport(0, MENU_LIST_Y, 135, MENU_LIST_H);
  spr.setTextDatum(TL_DATUM);
  for (int g = 0; g < NUM_ITEMS; g++) {
    bool sel = g == selected;
    int y = 2 + g * BOX_PITCH - scroll;
    if (y + BOX_H < 0 || y >= MENU_LIST_H) continue;
    uint16_t frame = sel ? TFT_YELLOW : TFT_DARKGREY;
    spr.drawRoundRect(6, y, 123, BOX_H, 6, frame);
    if (sel) spr.drawRoundRect(7, y + 1, 121, BOX_H - 2, 5, frame);

    spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
    spr.drawString(ITEMS[g].name, 12, y + 6, 4);
    spr.setTextColor(sel ? TFT_LIGHTGREY : TFT_DARKGREY);
    if (ITEMS[g].hiKey) snprintf(buf, sizeof buf, "Best %lu", (unsigned long)prefs.getUInt(ITEMS[g].hiKey, 0));
    else ITEMS[g].info(buf, sizeof buf);
    spr.drawString(buf, 12, y + 34, 2);
    ITEMS[g].drawIcon(114, y + 28, !sel);
  }
  spr.resetViewport();

  // arrows when there is more list above / below
  if (scroll > 0) spr.fillTriangle(124, 15, 132, 15, 128, 9, TFT_LIGHTGREY);
  if (scroll < MAX_SCROLL) spr.fillTriangle(124, 222, 132, 222, 128, 228, TFT_LIGHTGREY);

  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 10, 218, 2);
  spr.drawString("R", 74, 218, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 22, 218, 2);
  spr.drawString(ITEMS[selected].action, 86, 218, 2);
  spr.pushSprite(0, 0);
}

const char *itemName(int i) { return i >= 0 && i < NUM_ITEMS ? ITEMS[i].name : "menu"; }

void startItem(int g) {
  active = g;
  noteActiveItem(g);
  dbg("start %s\n", ITEMS[g].name);
  if (ITEMS[g].hiKey) prefs.putUChar("game", g);   // remember games only
  ITEMS[g].begin();
}

void returnToMenu() {
  active = -1;
  noteActiveItem(-1);
  dbg("back to menu\n");
  pressedL = pressedR = false;
  menuLastInput = millis();
  menuDirty = true;
}

void updateMenu(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) menuLastInput = now;

  // act on release, but only for presses that started in the menu
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  if (eL == 2 && pressedL) {
    pressedL = false;
    selected = (selected + 1) % NUM_ITEMS;
    menuDirty = true;
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    startItem(selected);
    return;
  }

  // glide towards the selected box, ~60 fps
  int target = scrollTarget();
  if (scroll != target && now - menuLastAnim >= 16) {
    menuLastAnim = now;
    int d = (target - scroll) / 3;
    scroll += d ? d : (target > scroll ? 1 : -1);
    menuDirty = true;
  }

  if (idleTooLong(now, menuLastInput)) goToSleep();
  if (menuDirty) {
    renderMenu();
    menuDirty = false;
  }
}

// ---------------- main ----------------
void setup() {
  initPower();

  pinMode(0, INPUT_PULLUP);
  pinMode(35, INPUT);              // input-only pin, pulled up on the board

  // a button still held from the wake-up press must not count as a press
  bL.raw = bL.stable = digitalRead(bL.pin) == LOW;
  bR.raw = bR.stable = digitalRead(bR.pin) == LOW;

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);

  spr.setColorDepth(16);
  if (!spr.createSprite(tft.width(), tft.height())) {
    spr.setColorDepth(8);          // fallback if heap is tight
    spr.createSprite(tft.width(), tft.height());
  }

  prefs.begin("tetris", false);
  initDiag(itemName);
  initBacklight();
  selected = prefs.getUChar("game", 0) % NUM_ITEMS;
  if (!ITEMS[selected].hiKey) selected = 0;
  scroll = scrollTarget();         // start with the remembered game in view, no animation

  menuLastInput = millis();
}

void loop() {
  uint32_t now = millis();
  if (active >= 0) ITEMS[active].update(now);
  else updateMenu(now);

  static uint32_t lastLog = 0;
  if (usbLogOn() && now - lastLog >= 5000) {
    lastLog = now;
    dbg("t=%lus %s heap=%u minheap=%u stackfree=%u\n", (unsigned long)(now / 1000), itemName(active),
        ESP.getFreeHeap(), ESP.getMinFreeHeap(), (unsigned)uxTaskGetStackHighWaterMark(NULL));
  }
  delay(2);
}
