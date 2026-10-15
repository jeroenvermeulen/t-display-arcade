// Settings screen: battery level (read only), display brightness, USB logging
// and the last recorded crash.
//
//   Tap L : next row
//   Tap R : change brightness / toggle USB log / back to the menu

#include "common.h"

namespace settings {

enum Row { ROW_BRIGHTNESS, ROW_USBLOG, ROW_BACK, NUM_ROWS };

static int row = ROW_BRIGHTNESS;
static int mv = 0;                                      // last battery reading
static bool dirty = true, pressedL = false, pressedR = false;
static uint32_t lastRead = 0, lastInput = 0;

static void drawBattery(int x, int y, int w, int h, int pct, uint16_t col) {
  spr.drawRoundRect(x, y, w, h, 3, TFT_LIGHTGREY);
  spr.fillRect(x + w, y + h / 3, 3, h / 3, TFT_LIGHTGREY);   // terminal
  int fill = (w - 4) * pct / 100;
  if (fill > 0) spr.fillRect(x + 2, y + 2, fill, h - 4, col);
}

static void drawBolt(int x, int y, uint16_t col) {
  spr.fillTriangle(x + 4, y, x, y + 9, x + 5, y + 9, col);
  spr.fillTriangle(x + 3, y + 7, x + 8, y + 7, x + 2, y + 16, col);
}

static void drawRowBox(int y, int h, bool sel) {
  uint16_t frame = sel ? TFT_YELLOW : TFT_DARKGREY;
  spr.drawRoundRect(6, y, 123, h, 6, frame);
  if (sel) spr.drawRoundRect(7, y + 1, 121, h - 2, 5, frame);
}

static void render() {
  char buf[40];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("SETTINGS", 67, 6, 4);

  // battery (read only)
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Battery", 12, 36, 2);
  if (onUsbPower(mv)) {
    drawBattery(12, 56, 46, 24, 100, 0x2D6A);
    drawBolt(31, 60, TFT_YELLOW);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("USB", 72, 56, 4);
    spr.setTextColor(TFT_LIGHTGREY);
    spr.drawString("power", 72, 82, 2);
  } else {
    int pct = batteryPercent(mv);
    uint16_t col = pct > 40 ? TFT_GREEN : pct > 15 ? TFT_YELLOW : TFT_RED;
    drawBattery(12, 56, 46, 24, pct, col);
    spr.setTextColor(col);
    snprintf(buf, sizeof buf, "%d%%", pct);
    spr.drawString(buf, 72, 56, 4);
    spr.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof buf, "%d.%02d V", mv / 1000, mv % 1000 / 10);
    spr.drawString(buf, 72, 82, 2);
  }

  // brightness
  bool sel = row == ROW_BRIGHTNESS;
  drawRowBox(102, 46, sel);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("Brightness", 14, 106, 2);
  int lvl = brightnessLevel();
  for (int i = 0; i < NUM_BRIGHTNESS; i++) {
    int x = 15 + i * 22, h = 6 + i * 2, y = 142 - h;
    if (i <= lvl) spr.fillRect(x, y, 18, h, sel ? TFT_YELLOW : TFT_DARKGREY);
    else spr.drawRect(x, y, 18, h, TFT_DARKGREY);
  }

  // USB log
  sel = row == ROW_USBLOG;
  drawRowBox(152, 26, sel);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("USB log", 14, 157, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(!sel ? TFT_DARKGREY : usbLogOn() ? TFT_GREEN : TFT_LIGHTGREY);
  spr.drawString(usbLogOn() ? "On" : "Off", 121, 157, 2);

  // back
  sel = row == ROW_BACK;
  drawRowBox(182, 24, sel);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
  spr.drawString("Back to menu", 67, 186, 2);

  // last crash, if any (small, so it doesn't crowd the screen)
  if (lastCrash(buf, sizeof buf)) {
    char line[48];
    snprintf(line, sizeof line, "! %s", buf);   // e.g. "! watchdog in Flappy"
    spr.setTextColor(TFT_ORANGE);
    spr.drawString(line, 67, 209, 1);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("L", 10, 222, 2);
  spr.drawString("R", 70, 222, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("next", 22, 222, 2);
  spr.drawString(row == ROW_BRIGHTNESS ? "change" : row == ROW_USBLOG ? "toggle" : "back", 82, 222, 2);
  spr.pushSprite(0, 0);
}

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim) {
  uint16_t col = dim ? tft.alphaBlend(80, TFT_LIGHTGREY, TFT_BLACK) : TFT_LIGHTGREY;
  for (int i = 0; i < 8; i++) {                         // gear teeth
    float a = i * PI / 4;
    spr.fillCircle(x + (int)roundf(cosf(a) * 8), y + (int)roundf(sinf(a) * 8), 2, col);
  }
  spr.fillCircle(x, y, 7, col);
  spr.fillCircle(x, y, 3, TFT_BLACK);
}

void menuInfo(char *buf, size_t n) {
  // the menu redraws every frame while scrolling; don't measure that often
  static int v = 0;
  static uint32_t readAt = 0;
  if (!readAt || millis() - readAt > 5000) { v = batteryMilliVolts(); readAt = millis(); }
  if (onUsbPower(v)) snprintf(buf, n, "USB power");
  else snprintf(buf, n, "Battery %d%%", batteryPercent(v));
}

void begin() {
  row = ROW_BRIGHTNESS;
  pressedL = pressedR = false;
  mv = batteryMilliVolts();
  lastRead = lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  // act on release, only for presses that started on this screen
  if (eL == 1) pressedL = true;
  if (eR == 1) pressedR = true;
  if (eL == 2 && pressedL) {
    pressedL = false;
    row = (row + 1) % NUM_ROWS;
    dirty = true;
  }
  if (eR == 2 && pressedR) {
    pressedR = false;
    if (row == ROW_BACK) { returnToMenu(); return; }
    if (row == ROW_USBLOG) setUsbLog(!usbLogOn());
    else setBrightness((brightnessLevel() + 1) % NUM_BRIGHTNESS, true);   // wraps to the dimmest
    dirty = true;
  }

  if (now - lastRead >= 1000) {                          // live battery reading
    lastRead = now;
    mv = batteryMilliVolts();
    dirty = true;
  }
  if (idleTooLong(now, lastInput)) goToSleep();

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace settings
