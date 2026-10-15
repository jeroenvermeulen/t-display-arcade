// Crossy (Crossy Road)
//
// Controls (two buttons):
//   Tap L or R : hop forward
//   Hold L     : hop left  (repeats while held)
//   Hold R     : hop right (repeats while held)
// Cross roads without getting hit, ride the logs over rivers (the water and
// the screen edge are deadly), and walk around the trees. The view slowly
// scrolls forward on its own; fall off the bottom and you're too slow.
// Score is the furthest row reached.

#include "common.h"

namespace crossy {

// ---------------- layout ----------------
static const int CELL = 15, COLS = 9;                   // 9 x 15 px = 135
static const int VIS_ROWS = (240 - HEADER_H) / CELL + 2;
static const int RING = 32;                              // generated lanes kept (more than visible)
static const int MAX_OBJ = 4;
static const float WRAP_W = 195;                         // cars/logs wrap over x -60 .. 135
static const int HOLD_MS = 200, SIDE_REPEAT_MS = 220, HOP_TICKS = 6;
static const uint32_t TICK_MS = 16;

static const uint16_t GRASS1 = 0x5E68, GRASS2 = 0x5627, ROAD = 0x39E7, LINE = 0x8410;
static const uint16_t WATER = 0x1C9F, WATER_LT = 0x4DDF, LOG = 0x8A43, LOG_DK = 0x5980;
static const uint16_t TRUNK = 0x8A22, LEAF = 0x1C83, LEAF_LT = 0x2DA6;
static const uint16_t CAR_COLORS[] = {TFT_RED, 0x435F, TFT_YELLOW, TFT_ORANGE, TFT_MAGENTA, TFT_CYAN};

// ---------------- world ----------------
enum LaneType : uint8_t { GRASS, ROAD_LANE, RIVER };
struct Obj { float x; int16_t len; uint16_t col; };
struct Lane {
  LaneType type;
  uint8_t nObj;
  uint16_t trees;                                        // bit c set = tree in column c (grass)
  float speed;                                           // px per tick, sign = direction
  Obj obj[MAX_OBJ];
};
static Lane lanes[RING];
static int genUpTo;                                      // highest generated world row
static LaneType runType;
static int runLeft, safeCol;

static int rnd(int n) { return esp_random() % n; }
static Lane &lane(int r) { return lanes[r & (RING - 1)]; }

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
enum Death { CAR, WATER_DEATH, EAGLE };
static State state = TITLE;
static Death death;

static int row, maxRow;                                  // world row of the chicken
static float px;                                         // chicken left edge (float: logs carry it)
static int facing;                                       // 0 up, -1 left, 1 right
static int hopT;                                         // ticks left in the hop animation
static float fromX, fromRow;                             // where the current hop started
static float cam;                                        // world row shown at the bottom of the screen
static bool moved;                                       // view only starts creeping after the first hop
static uint32_t ticks;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0, deadAt = 0;

static void generate(int r) {
  Lane &l = lane(r);
  l.nObj = 0;
  l.trees = 0;
  l.speed = 0;
  // e: easing-in over the first ~60 rows; d: slow long-term difficulty
  float e = constrain((r - 5) / 60.0f, 0.0f, 1.0f);
  float d = min(1.0f, r / 150.0f);

  if (r < 5) {                                           // safe start meadow
    l.type = GRASS;
    if (r == 0) l.trees = 0x1FF & ~0x038;                // hedge behind the start
    else l.trees = 0x101;                                // trees at the edges
    return;
  }

  if (runLeft <= 0) {
    // more grass to rest on at first, rivers only from row 15
    int grassPct = 50 - (int)(15 * e), riverPct = r < 15 ? 0 : 10 + (int)(15 * e);
    int pick = rnd(100);
    LaneType next = pick < grassPct ? GRASS : pick < 100 - riverPct ? ROAD_LANE : RIVER;
    if (next == runType && next == GRASS) next = ROAD_LANE;   // no long boring meadows
    runType = next;
    // single lanes at first, wider roads and rivers later
    int extra = (int)(e * 2) + (int)(d * 2);
    runLeft = runType == GRASS ? 1 + rnd(2) : 1 + rnd(1 + extra);
  }
  runLeft--;
  l.type = runType;

  if (l.type == GRASS) {
    // trees, but one column (wandering slowly) always stays free so there is a way through
    safeCol = constrain(safeCol + rnd(3) - 1, 1, COLS - 2);
    for (int c = 0; c < COLS; c++)
      if (c != safeCol && rnd(100) < 15 + (int)(d * 15)) l.trees |= 1 << c;
    return;
  }

  int dir = rnd(2) ? 1 : -1;
  if (r > 0 && lane(r - 1).type == l.type && lane(r - 1).speed != 0)
    dir = lane(r - 1).speed > 0 ? -1 : 1;                // neighbouring lanes go opposite ways
  if (l.type == ROAD_LANE) {
    l.speed = dir * min(2.2f, 0.4f + rnd(40) / 100.0f + e * 0.4f + d * 0.8f);
    l.nObj = 2 + (rnd(100) < (int)(e * 50) ? 1 : 0);   // a third car only later
    uint16_t col = CAR_COLORS[rnd(6)];
    for (int i = 0; i < l.nObj; i++) {
      int16_t len = rnd(100) < 10 + (int)(e * 15) ? 34 : 20;   // trucks and cars
      l.obj[i] = {-60 + i * WRAP_W / l.nObj + rnd(10), len, rnd(3) ? col : CAR_COLORS[rnd(6)]};
    }
  } else {
    l.speed = dir * (0.3f + rnd(30) / 100.0f + e * 0.2f + d * 0.4f);
    l.nObj = rnd(100) < (int)(e * 50) ? 2 : 3;          // first rivers: 3 long logs, nearly a bridge
    for (int i = 0; i < l.nObj; i++) {
      int16_t len = l.nObj == 2 ? 44 + rnd(17) : 48 - (int)(e * 14) + rnd(9);
      l.obj[i] = {-60 + i * WRAP_W / l.nObj + rnd(10), len, LOG};
    }
  }
}

static void ensureGenerated(int upTo) {
  while (genUpTo < upTo) generate(++genUpTo);
}

static int colOf(float x) { return constrain((int)roundf(x / CELL), 0, COLS - 1); }
static bool treeAt(int r, int c) { return lane(r).type == GRASS && (lane(r).trees >> c & 1); }

// log the chicken stands on in a river row, or nullptr
static const Obj *logUnder(int r, float x) {
  const Lane &l = lane(r);
  float cx = x + CELL / 2.0f;
  for (int i = 0; i < l.nObj; i++)
    if (cx >= l.obj[i].x + 2 && cx <= l.obj[i].x + l.obj[i].len - 2) return &l.obj[i];
  return nullptr;
}

static void gameOver(Death why) {
  state = GAMEOVER;
  death = why;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("crossy_hi", hiScore);
  }
  deadAt = lastInput = millis();
  dirty = true;
}

static void newGame() {
  genUpTo = -1;
  runType = GRASS;
  runLeft = 0;
  safeCol = COLS / 2;
  ensureGenerated(VIS_ROWS + 2);
  row = maxRow = 1;
  px = (COLS / 2) * CELL;
  facing = 0;
  hopT = 0;
  cam = 0;
  moved = false;
  ticks = 0;
  score = 0;
  newHigh = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void hop(int dCol, int dRow) {
  if (state != PLAY) return;
  facing = dRow ? 0 : dCol;
  int nr = row + dRow;
  float nx = px + dCol * CELL;
  ensureGenerated(nr + 1);

  // on solid ground the chicken stays in the grid; logs leave it anywhere
  if (lane(nr).type != RIVER) {
    nx = colOf(nx) * CELL;
    if (px + dCol * CELL < -2 || px + dCol * CELL > (COLS - 1) * CELL + 2) return;   // screen edge
    if (treeAt(nr, colOf(nx))) return;
  }
  fromX = px;
  fromRow = row;
  hopT = HOP_TICKS;
  px = nx;
  row = nr;
  moved = true;
  if (row > maxRow) { maxRow = row; score = maxRow - 1; }

  if (lane(row).type == RIVER && !logUnder(row, px)) gameOver(WATER_DEATH);
}

static void tick() {
  ticks++;
  // move cars and logs on and near the screen
  for (int r = max(0, (int)cam - 1); r <= (int)cam + VIS_ROWS + 1 && r <= genUpTo; r++) {
    Lane &l = lane(r);
    for (int i = 0; i < l.nObj; i++) {
      Obj &o = l.obj[i];
      o.x += l.speed;
      if (l.speed > 0 && o.x > 135) o.x -= WRAP_W;
      if (l.speed < 0 && o.x < -60) o.x += WRAP_W;
    }
  }
  if (hopT) hopT--;
  if (state != PLAY) return;

  const Lane &l = lane(row);
  if (l.type == RIVER) {
    if (!logUnder(row, px)) { gameOver(WATER_DEATH); return; }
    px += l.speed;                                        // ride the log
    fromX += l.speed;
    if (px < -6 || px > 135 - CELL + 6) { gameOver(WATER_DEATH); return; }
  } else if (l.type == ROAD_LANE) {
    for (int i = 0; i < l.nObj; i++)
      if (px + 3 < l.obj[i].x + l.obj[i].len && px + CELL - 3 > l.obj[i].x) { gameOver(CAR); return; }
  }

  // the view creeps forward and follows the chicken
  if (moved) cam += min(0.012f, 0.002f + score * 0.00008f);   // gentle at first
  if (row - 4 > cam) cam += (row - 4 - cam) * 0.15f;
  ensureGenerated((int)cam + VIS_ROWS + 2);
  if (row < cam - 0.6f) gameOver(EAGLE);
}

// ---------------- rendering ----------------
static int rowY(float r) { return 240 - (int)roundf((r - cam + 1) * CELL); }

static void drawTree(int x, int y) {
  spr.fillRect(x + 6, y + 9, 3, 5, TRUNK);
  spr.fillCircle(x + 7, y + 6, 6, LEAF);
  spr.fillCircle(x + 5, y + 4, 2, LEAF_LT);
}

static void drawCar(const Obj &o, int y, bool right) {
  int x = (int)o.x, len = o.len;
  for (int k = 0; k < 2; k++) {                          // wheels
    int wx = x + 3 + k * (len - 9);
    spr.fillRect(wx, y + 1, 4, 2, TFT_BLACK);
    spr.fillRect(wx, y + 12, 4, 2, TFT_BLACK);
  }
  spr.fillRoundRect(x, y + 2, len, 11, 3, o.col);
  int wx = right ? x + len - 8 : x + 3;                  // windscreen at the front
  spr.fillRect(wx, y + 4, 5, 7, 0x2104);
  if (len > 25) spr.fillRect(right ? x + 2 : x + 12, y + 3, len - 14, 9, tft.alphaBlend(160, TFT_WHITE, o.col));   // cargo box
}

static void drawLog(const Obj &o, int y) {
  int x = (int)o.x;
  spr.fillRoundRect(x, y + 2, o.len, 11, 4, LOG);
  spr.drawFastHLine(x + 3, y + 5, o.len - 6, LOG_DK);
  spr.drawFastHLine(x + 3, y + 9, o.len - 6, LOG_DK);
  spr.fillCircle(x + 3, y + 7, 2, 0xB4A8);
}

static void drawChicken(int x, int y, int lift, bool flat) {
  if (flat) {                                            // run over
    spr.fillRoundRect(x, y + 6, CELL, 5, 2, TFT_WHITE);
    spr.fillRect(x + 6, y + 5, 3, 2, TFT_RED);
    return;
  }
  y -= lift;
  spr.fillRect(x + 4, y + 13, 2, 2, TFT_ORANGE);          // feet
  spr.fillRect(x + 9, y + 13, 2, 2, TFT_ORANGE);
  spr.fillRoundRect(x + 2, y + 2, 11, 11, 3, TFT_WHITE); // body
  spr.fillRect(x + 6, y, 3, 3, TFT_RED);                 // comb
  if (facing == 0) {
    spr.fillRect(x + 6, y + 3, 3, 2, TFT_ORANGE);        // beak
    spr.drawPixel(x + 4, y + 4, TFT_BLACK);
    spr.drawPixel(x + 10, y + 4, TFT_BLACK);
  } else {
    int fx = facing > 0 ? x + 12 : x;
    spr.fillRect(fx, y + 6, 3, 2, TFT_ORANGE);
    spr.drawPixel(facing > 0 ? x + 10 : x + 4, y + 4, TFT_BLACK);
  }
}

static void drawLane(int r, int y) {
  const Lane &l = lane(r);
  if (l.type == GRASS) {
    spr.fillRect(0, y, 135, CELL, r % 2 ? GRASS1 : GRASS2);
    for (int c = 0; c < COLS; c++)
      if (l.trees >> c & 1) drawTree(c * CELL, y);
  } else if (l.type == ROAD_LANE) {
    spr.fillRect(0, y, 135, CELL, ROAD);
    if (r + 1 <= genUpTo && lane(r + 1).type == ROAD_LANE)
      for (int x = 2; x < 135; x += 16) spr.drawFastHLine(x, y, 8, LINE);   // lane markings
    for (int i = 0; i < l.nObj; i++) drawCar(l.obj[i], y, l.speed > 0);
  } else {
    spr.fillRect(0, y, 135, CELL, WATER);
    int off = (int)(ticks * fabsf(l.speed) * 0.5f) % 30;
    for (int x = -30 + off; x < 135; x += 30) spr.drawFastHLine(x, y + 4 + r % 3 * 3, 6, WATER_LT);
    for (int i = 0; i < l.nObj; i++) drawLog(l.obj[i], y);
  }
}

static void drawGame() {
  int base = (int)floorf(cam);
  for (int r = base - 1; r <= base + VIS_ROWS && r <= genUpTo; r++) {
    if (r < 0) continue;
    int y = rowY(r);
    if (y >= 240 || y + CELL <= HEADER_H) continue;
    drawLane(r, y);
  }
  if (base - 1 < 0) spr.fillRect(0, rowY(-1), 135, 240 - rowY(-1), GRASS2);

  // chicken, mid-hop between rows with a little jump
  float p = 1 - (float)hopT / HOP_TICKS;
  float dx = fromX + (px - fromX) * p, dr = fromRow + (row - fromRow) * p;
  int lift = hopT ? (int)(sinf(p * PI) * 5) : 0;
  int y = rowY(dr);
  if (state == GAMEOVER && death == WATER_DEATH) {
    int t = min(20, (int)(millis() - deadAt) / 30);
    spr.drawCircle((int)px + 7, rowY(row) + 7, 3 + t / 2, TFT_WHITE);
    spr.drawCircle((int)px + 7, rowY(row) + 7, 1 + t / 4, WATER_LT);
  } else if (state == GAMEOVER && death == EAGLE) {
    drawChicken((int)dx, y, 0, false);
    int ey = y - 30 + min(30, (int)(millis() - deadAt) / 15);   // eagle swoops down
    spr.fillTriangle((int)dx - 10, ey, (int)dx + 7, ey + 8, (int)dx + 24, ey, TFT_DARKGREY);
    spr.fillTriangle((int)dx + 3, ey + 4, (int)dx + 11, ey + 4, (int)dx + 7, ey + 12, 0x6A20);
  } else {
    drawChicken((int)dx, y, lift, state == GAMEOVER && death == CAR);
  }

  drawScoreHeader(score, hiScore);
  if (state == GAMEOVER && millis() - deadAt > 700) {
    const char *why = death == CAR ? "SPLAT!" : death == WATER_DEATH ? "SPLASH!" : "TOO SLOW!";
    drawGameOverBox(why, score, hiScore, newHigh);
  }
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("CROSSY", 67, 14, 4);
  spr.fillRect(0, 46, 135, CELL, ROAD);
  Obj car = {12, 20, TFT_RED}, truck = {84, 34, 0x435F};
  drawCar(car, 46, true);
  drawCar(truck, 46, true);
  spr.fillRect(0, 61, 135, CELL, GRASS1);
  facing = 0;
  drawChicken(60, 61, 0, false);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L/R", "Hold L", "Hold R"};
  const char *acts[] = {"hop forward", "hop left", "hop right"};
  for (int i = 0; i < 3; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 6, 86 + i * 18, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 62, 86 + i * 18, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Ride logs, dodge cars", 67, 148, 1);
  spr.drawString("and keep moving!", 67, 160, 1);

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

// ---------------- input ----------------
static void handleInput(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state != PLAY) {
    if ((state == TITLE || now - deadAt > 700) && startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    return;
  }

  // hold: sideways hops, repeating while held
  Btn *bs[2] = {&bL, &bR};
  for (int k = 0; k < 2; k++) {
    Btn &b = *bs[k];
    if (!b.stable || b.consumed) continue;
    if ((!b.held && now - b.t0 >= HOLD_MS) || (b.held && now >= b.nextRepeat)) {
      b.held = true;
      b.nextRepeat = now + SIDE_REPEAT_MS;
      hop(k == 0 ? -1 : 1, 0);
    }
  }
  // tap (released before it became a hold): hop forward
  if ((eL == 2 && !bL.held && !bL.consumed) || (eR == 2 && !bR.held && !bR.consumed)) hop(0, 1);
}

// ---------------- entry points ----------------
void drawIcon(int x, int y, bool dim, uint32_t t) {
  // the chicken hops up across two lanes of traffic, just missing the cars, then
  // the view scrolls on to the next road (verge / lane A / lane B, 8 px rows)
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  static const uint32_t PERIOD = 2400;            // cars move 120 px = 3 gaps: seamless loop
  uint32_t p = t % PERIOD;

  // car's left edge relative to x, -26..13; 0.05 px/ms, one car every 40 px
  auto carX = [p](int e0, int dir) {
    int v = e0 + 26 + dir * (int)(p / 20);
    return ((v % 40) + 40) % 40 - 26;
  };
  // timed so a car passes right before the chicken enters lane B and right
  // after it leaves lane A
  int carA = carX(-78, 1), carB = carX(20, -1);

  float s = p < 1900 ? 0 : (p - 1900) / 500.0f;   // scroll on in the last 500 ms
  int off = (int)roundf(24 * s * s * (3 - 2 * s));
  for (int r = -1; r <= 1; r++) {
    int top = y - 16 + off + r * 24;
    spr.fillRect(x - 12, top + 8, 25, 16, c(ROAD));
    for (int k = -12; k <= 12; k += 6) spr.drawFastHLine(x + k, top + 16, 3, c(LINE));
    int cars[2] = {carA, carB};
    for (int l = 0; l < 2; l++) {
      int cx = x + cars[l], cy = top + 9 + l * 8;
      spr.fillRoundRect(cx, cy, 10, 6, 2, c(l ? 0x435F : TFT_RED));
      spr.fillRect(l ? cx + 1 : cx + 7, cy + 1, 2, 4, c(0x18E3));   // windscreen, facing its way
    }
  }

  // chicken: hops of 8 px at fixed times, then rides the scroll down
  static const uint16_t HOP[3] = {680, 950, 1180};
  int cy = y + 12;
  for (int h = 0; h < 3; h++)
    if (p >= HOP[h]) cy -= p >= HOP[h] + 120u ? 8 : (int)((p - HOP[h]) / 15);
  cy += off;
  spr.fillRoundRect(x - 3, cy - 3, 7, 7, 2, c(TFT_WHITE));
  spr.fillRect(x - 1, cy - 4, 3, 2, c(TFT_RED));     // comb
  spr.fillRect(x - 1, cy - 1, 3, 1, c(TFT_ORANGE));  // beak
}

void begin() {
  hiScore = prefs.getUInt("crossy_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  handleInput(now);

  if (state != TITLE) {                                  // traffic keeps moving after a crash
    if (now - lastTick > 200) lastTick = now - TICK_MS;   // don't try to catch up after a stall
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

}  // namespace crossy
