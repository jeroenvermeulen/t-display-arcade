// Stack
//
// Controls: tap either button to drop the sliding block.
// Whatever hangs over the edge of the tower is cut off, so the blocks get
// narrower. Drop it (almost) exactly on top for a PERFECT; three perfects in
// a row make the block grow back a little. The blocks slide faster as the
// tower grows.

#include "common.h"

namespace stack {

// ---------------- layout ----------------
static const int BH = 10;                               // block height
static const int BASE_Y = 236;                          // bottom of the base block (at camera 0)
static const int START_W = 70;
static const int RING = 32;                             // remembered levels (more than fit on screen)
static const int KEEP_ROWS = 12;                        // camera keeps the top this many rows above the base line
static const int PERFECT_PX = 2;
static const int MAX_FALLING = 4;
static const uint32_t TICK_MS = 16;

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

struct Level { int16_t x, w; uint16_t col; };
static Level levels[RING];
static int height;                                      // placed blocks, including the base
static float mx, mv;                                    // moving block x and velocity
static int mw;                                          // moving block width
static int combo, perfectFlash;
static float cam, camTarget;

struct Falling { bool on; float x, y, vy; int w; uint16_t col; };
static Falling falling[MAX_FALLING];

static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0, deadAt = 0;

static Level &lv(int k) { return levels[k % RING]; }

// rainbow colour that shifts as the tower grows
static uint16_t hueColor(int h) {
  h %= 360;
  int region = h / 60, f = (h % 60) * 255 / 60;
  const uint8_t v = 235, p = 50;
  uint8_t q = v - (v - p) * f / 255, t = p + (v - p) * f / 255;
  switch (region) {
    case 0:  return tft.color565(v, t, p);
    case 1:  return tft.color565(q, v, p);
    case 2:  return tft.color565(p, v, t);
    case 3:  return tft.color565(p, q, v);
    case 4:  return tft.color565(t, p, v);
    default: return tft.color565(v, p, q);
  }
}

static int levelTopY(int k) { return BASE_Y - (k + 1) * BH + (int)cam; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("stack_hi", hiScore);
  }
  deadAt = lastInput = millis();
  dirty = true;
}

static void nextBlock() {
  float speed = min(4.0f, 1.4f + height * 0.06f);
  if (height % 2) { mx = 0; mv = speed; }                // alternate the side it comes from
  else { mx = 135 - mw; mv = -speed; }
}

static void newGame() {
  height = 1;
  lv(0) = {(int16_t)((135 - START_W) / 2), (int16_t)START_W, hueColor(0)};
  mw = START_W;
  combo = perfectFlash = 0;
  cam = camTarget = 0;
  memset(falling, 0, sizeof falling);
  score = 0;
  newHigh = false;
  nextBlock();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void dropPiece(float x, int w, uint16_t col) {
  for (Falling &f : falling)
    if (!f.on) { f = {true, x, (float)levelTopY(height), 0, w, col}; return; }
}

static void drop() {
  const Level &top = lv(height - 1);
  uint16_t col = hueColor(height * 12);
  int x = (int)roundf(mx);
  int l = max(x, (int)top.x), r = min(x + mw, top.x + top.w);

  if (r - l <= 0) {                                      // missed the tower completely
    dropPiece(x, mw, col);
    gameOver();
    return;
  }

  Level placed;
  if (abs(x - top.x) <= PERFECT_PX) {                    // perfect: snap onto the tower
    placed = {top.x, top.w, col};
    perfectFlash = 30;
    if (++combo >= 3 && placed.w < START_W) {            // reward streaks with some width back
      int grow = min(4, START_W - placed.w);
      placed.w += grow;
      placed.x = constrain(placed.x - grow / 2, 0, 135 - placed.w);
    }
  } else {
    combo = 0;
    placed = {(int16_t)l, (int16_t)(r - l), col};
    if (x < l) dropPiece(x, l - x, col);                 // overhang on the left
    if (x + mw > r) dropPiece(r, x + mw - r, col);       // overhang on the right
  }

  lv(height) = placed;
  height++;
  score = height - 1;
  mw = placed.w;
  camTarget = max(0, (height - KEEP_ROWS) * BH);
  nextBlock();
}

static void tick() {
  if (state == PLAY) {
    mx += mv;
    if (mx < 0) { mx = 0; mv = fabsf(mv); }
    if (mx > 135 - mw) { mx = 135 - mw; mv = -fabsf(mv); }
  }
  if (perfectFlash) perfectFlash--;
  cam += (camTarget - cam) * 0.15f;
  for (Falling &f : falling)
    if (f.on) {
      f.vy += 0.35f;
      f.y += f.vy;
      if (f.y > 240) f.on = false;
    }
}

// ---------------- rendering ----------------
static void drawBlock(int x, int y, int w, uint16_t col) {
  spr.fillRect(x, y, w, BH - 1, col);
  spr.drawFastHLine(x, y, w, tft.alphaBlend(140, TFT_WHITE, col));
  spr.drawFastHLine(x, y + BH - 2, w, tft.alphaBlend(120, TFT_BLACK, col));
}

static void drawBackground() {
  // gentle vertical gradient that darkens as you climb
  uint16_t topCol = tft.alphaBlend(min(255, 40 + height * 4), 0x0006, 0x3A6F);
  for (int y = HEADER_H; y < 240; y += 8)
    spr.fillRect(0, y, 135, 8, tft.alphaBlend((y - HEADER_H) * 255 / (240 - HEADER_H), 0x18C3, topCol));
}

static void drawGame() {
  char buf[32];
  drawBackground();

  int first = max(0, height - 26);
  for (int k = first; k < height; k++) {
    int y = levelTopY(k);
    if (y >= 240 || y + BH < HEADER_H) continue;
    drawBlock(lv(k).x, y, lv(k).w, lv(k).col);
  }
  if (state == PLAY) drawBlock((int)roundf(mx), levelTopY(height), mw, hueColor(height * 12));
  for (const Falling &f : falling)
    if (f.on) drawBlock((int)f.x, (int)f.y, f.w, f.col);

  if (perfectFlash && state == PLAY) {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(combo >= 3 ? "PERFECT +" : "PERFECT!", 67, levelTopY(height) - 30, 2);
  }

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER && millis() - deadAt > 500) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(hueColor(30));
  spr.drawString("STACK", 67, 18, 4);
  static const int8_t W[5] = {60, 52, 52, 44, 40};
  static const int8_t X[5] = {38, 42, 40, 46, 50};
  for (int i = 0; i < 5; i++) drawBlock(X[i], 90 - i * BH, W[i], hueColor(i * 24));

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Tap L/R", 8, 106, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("drop block", 64, 106, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Overhangs are cut off", 67, 134, 1);
  spr.drawString("3 perfects = wider", 67, 146, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
}

static void render() {
  if (state == TITLE) drawTitle();
  else drawGame();
  spr.pushSprite(0, 0);
}

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  // the top block swings back and forth, then stops on the tower
  static const int8_t W[4] = {24, 20, 18, 14};
  static const int8_t X[4] = {-12, -10, -8, -6};
  uint32_t p = (t + 1000) % 1600;                  // t = 0: block just placed
  for (int i = 0; i < 4; i++) {
    uint16_t col = hueColor(i * 30);
    if (i == 3 && p >= 1000 && p < 1150) col = TFT_WHITE;   // flash on landing
    if (dim) col = tft.alphaBlend(80, col, TFT_BLACK);
    int bx = X[i];
    if (i == 3 && p < 1000) bx += (int)roundf(sinf(p * 3 * PI / 1000) * 10);
    spr.fillRect(x + bx, y + 8 - i * 6, W[i], 5, col);
  }
}

void begin() {
  hiScore = prefs.getUInt("stack_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state == PLAY) {
    if (eL == 1 || eR == 1) { drop(); dirty = true; }
  } else if (state == TITLE || now - deadAt > 600) {     // ignore mashing right after a miss
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
  }

  if (state != TITLE) {                                  // animate play and the falling pieces
    if (now - lastTick > 200) lastTick = now - TICK_MS;
    while (now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick();
      dirty = true;
    }
  }
  if (state != PLAY && idleTooLong(now, lastInput)) goToSleep();

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace stack
