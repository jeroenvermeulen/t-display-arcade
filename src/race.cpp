// Race
//
// Controls (two buttons):
//   Hold left  : steer left
//   Hold right : steer right
// Dodge the slower traffic and follow the bends; the grass slows you down.
// The cruising speed keeps rising; score is distance.

#include "common.h"

namespace race {

// ---------------- layout ----------------
static const int TOP = 38;                             // road area: y 38..239
static const int LANE_W = 28, HALF_W = LANE_W * 3 / 2; // 3 lanes, 84 px of tarmac
static const int KERB = 4;
static const float MID_X = 67.5f;                      // screen centre
static const float MAX_BEND = 20;                      // road centre stays within MID_X +- this
static const float SEG = 160;                          // distance between bend control points
static const int NUM_CTRL = 8;                         // ring buffer, covers far more than the screen
static const float START_D = 400;                      // world distance at the start of a race
static const int CAR_W = 18, CAR_H = 30;
static const int PLAYER_Y = 202;                       // top of the player's car
static const int MAX_CARS = 8;
static const uint32_t TICK_MS = 16;

static const uint16_t GRASS = 0x02E0, GRASS2 = 0x0360, ROAD = 0x4208, DARK = 0x18E3;
static const uint16_t TRAFFIC[] = {0x435F, TFT_YELLOW, TFT_ORANGE, TFT_MAGENTA, TFT_WHITE, TFT_CYAN};
static const int NUM_TRAFFIC = sizeof TRAFFIC / sizeof TRAFFIC[0];


// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

struct Car { bool on; float y; int lane; uint16_t col; };
static Car cars[MAX_CARS];
static float px, speed, cruise, travel, sinceSpawn, nextGap;
static float pAngle;                                   // player heading, degrees (clockwise)
static float ctrl[NUM_CTRL];                           // road centre offsets at k * SEG
static int ctrlUpTo;                                    // highest generated control point
static bool offRoad;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

// ---------------- road shape ----------------
// The road centre is a cosine-smoothed curve through random control points
// spaced SEG apart in world distance; generated lazily as the race goes on.
static float ctrlAt(int k) {
  while (ctrlUpTo < k) {
    int n = ++ctrlUpTo;
    float prev = ctrl[(n + NUM_CTRL - 1) % NUM_CTRL];
    float v;
    if (n * SEG < START_D + 300) v = 0;                         // straight start
    else if (rnd(100) < 30) v = prev;                           // straight stretch
    else v = rnd(2 * (int)MAX_BEND + 1) - MAX_BEND;             // bend
    ctrl[n % NUM_CTRL] = v;
  }
  return ctrl[k % NUM_CTRL];
}

static float centreAtD(float d) {
  int k = (int)(d / SEG);
  float t = (d - k * SEG) / SEG;
  float s = (1 - cosf(t * PI)) / 2;
  float a = ctrlAt(k), b = ctrlAt(k + 1);
  return MID_X + a + (b - a) * s;
}

// world distance shown at screen row y (further ahead = higher up)
static inline float dAtY(float y) { return travel + PLAYER_Y - y; }
static inline float centreAtY(float y) { return centreAtD(dAtY(y)); }

static float carX(const Car &c) {
  return centreAtY(c.y + CAR_H / 2) + (c.lane - 1) * LANE_W;
}

// direction of the road at screen row y, degrees clockwise from straight up
static float roadAngleAtY(float y) {
  float d = dAtY(y);
  float slope = (centreAtD(d + 4) - centreAtD(d - 4)) / 8;
  return atanf(slope) * RAD_TO_DEG;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("race_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newGame() {
  memset(cars, 0, sizeof cars);
  memset(ctrl, 0, sizeof ctrl);
  ctrlUpTo = 0;
  travel = START_D;
  px = centreAtY(PLAYER_Y + CAR_H / 2);
  speed = cruise = 2.5f;
  pAngle = 0;
  offRoad = false;
  sinceSpawn = 0;
  nextGap = 60;
  score = 0;
  newHigh = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void spawnCar(int lane) {
  for (Car &c : cars)
    if (!c.on) {
      c.on = true;
      c.lane = lane;
      c.y = TOP - CAR_H;
      c.col = TRAFFIC[rnd(NUM_TRAFFIC)];
      return;
    }
}

static void spawnRow() {
  int lane = rnd(3);
  spawnCar(lane);
  // at higher speed, sometimes block two lanes (one always stays free)
  if (speed > 3.5f && rnd(100) < 30) spawnCar((lane + 1 + rnd(2)) % 3);
  // vertical gap must leave time to switch lanes, more so when going fast
  int minGap = CAR_H + 40 + (int)(speed * 10);
  nextGap = minGap + rnd(60);
}

static bool hits(const Car &c) {
  // Horizontally the full car width counts: lanes are 33 px apart, so straddling
  // the line between two lanes (16.5 px from each) must hit both. Vertically a
  // little slack feels fairer.
  return fabsf(carX(c) - px) < CAR_W - 1 && fabsf(c.y - PLAYER_Y) < CAR_H - 4;
}

static void tick() {
  float steer = 2.2f + speed * 0.25f;
  float oldPx = px;
  if (bL.stable && !bR.stable) px -= steer;
  if (bR.stable && !bL.stable) px += steer;
  px = constrain(px, CAR_W / 2, 135 - CAR_W / 2);

  // point the car where it is actually going, turning smoothly
  float target = constrain(atan2f(px - oldPx, speed) * RAD_TO_DEG, -20.0f, 20.0f);
  pAngle += constrain(target - pAngle, -3.0f, 3.0f);

  // grass (wheels past the kerb) slows you down; back on the road you
  // quickly recover to the cruising speed, which keeps rising
  offRoad = fabsf(px - centreAtY(PLAYER_Y + CAR_H / 2)) > HALF_W + KERB - CAR_W / 2;
  if (cruise < 7.0f) cruise += 0.0008f;
  if (offRoad) speed = max(2.0f, speed - 0.03f);
  else speed = min(cruise, speed + 0.01f);

  travel += speed;
  score = (uint32_t)((travel - START_D) / 10);

  float rel = speed * 0.5f;                            // traffic is slower than us
  sinceSpawn += rel;
  if (sinceSpawn >= nextGap) {
    sinceSpawn = 0;
    spawnRow();
  }

  for (Car &c : cars) {
    if (!c.on) continue;
    c.y += rel;
    if (c.y > 240) c.on = false;
    else if (hits(c)) { gameOver(); return; }
  }
}

// ---------------- rendering ----------------
static TFT_eSprite carSpr(&tft);                        // scratch sprite for rotated cars

static void drawCar(TFT_eSprite &s, int cx, int y, uint16_t col) {
  for (int k = -1; k <= 1; k += 2) {                    // wheels
    s.fillRect(cx + k * 8 - 1, y + 5, 3, 7, DARK);
    s.fillRect(cx + k * 8 - 1, y + 20, 3, 7, DARK);
  }
  s.fillRoundRect(cx - 7, y, 14, CAR_H, 4, col);
  s.fillRect(cx - 5, y + 8, 10, 5, DARK);               // windshield
  s.fillRect(cx - 4, y + 14, 8, 7, tft.alphaBlend(150, TFT_WHITE, col));   // roof
  s.fillRect(cx - 5, y + 23, 10, 3, DARK);              // rear window
}

static void drawCar(int cx, int y, uint16_t col) { drawCar(spr, cx, y, col); }

// car centred on (cx, y + CAR_H / 2), rotated clockwise by angle degrees
static void drawCar(int cx, int y, uint16_t col, float angle) {
  int a = (int)roundf(angle);
  if (a == 0 || !carSpr.created()) { drawCar(cx, y, col); return; }
  carSpr.fillSprite(TFT_BLACK);                          // black = transparent
  drawCar(carSpr, carSpr.width() / 2, 1, col);
  spr.setPivot(cx, y + CAR_H / 2);
  carSpr.pushRotated(&spr, a, TFT_BLACK);
}

static void drawScene() {
  // road row by row, each row with its own centre so the bends show
  for (int y = TOP; y < 240; y++) {
    float d = dAtY(y);
    int id = (int)d;
    int l = (int)roundf(centreAtD(d)) - HALF_W, r = l + 2 * HALF_W;
    spr.drawFastHLine(0, y, 135, id % 40 < 20 ? GRASS2 : GRASS);
    spr.drawFastHLine(l, y, r - l, ROAD);
    uint16_t kerb = id % 20 < 10 ? TFT_RED : TFT_WHITE;
    spr.drawFastHLine(l - KERB, y, KERB, kerb);
    spr.drawFastHLine(r, y, KERB, kerb);
    if (id % 30 < 14)
      for (int k = 1; k < 3; k++) spr.drawFastHLine(l + LANE_W * k - 1, y, 2, TFT_LIGHTGREY);
  }

  for (const Car &c : cars)
    if (c.on) drawCar((int)roundf(carX(c)), (int)c.y, c.col, roadAngleAtY(c.y + CAR_H / 2));
  int shake = offRoad && state == PLAY ? rnd(3) - 1 : 0;   // bumpy on the grass
  drawCar((int)px + shake, PLAYER_Y, TFT_RED, pAngle);

  if (state == GAMEOVER) {                              // crash burst
    static const int8_t B[5][3] = {{0, 0, 9}, {-6, -5, 6}, {7, -3, 5}, {-4, 8, 5}, {5, 7, 6}};
    for (auto &b : B) spr.fillCircle((int)px + b[0], PLAYER_Y + b[1], b[2], TFT_ORANGE);
    spr.fillCircle((int)px, PLAYER_Y, 5, TFT_YELLOW);
  }

  // header drawn last so cars entering from the top never overlap it
  char buf[32];
  spr.fillRect(0, 0, 135, TOP, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 2, 24, 1);
  spr.setTextDatum(TR_DATUM);
  snprintf(buf, sizeof buf, "%d km/h", (int)(speed * 30));
  spr.drawString(buf, 132, 24, 1);

  if (state == GAMEOVER) drawGameOverBox("CRASH!", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("RACE", 67, 18, 4);

  drawCar(37, 50, TRAFFIC[0]);
  drawCar(67, 50, TFT_RED);
  drawCar(97, 50, TRAFFIC[1]);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"steer left", "steer right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Dodge the traffic.", 67, 146, 1);
  spr.drawString("Grass slows you down.", 67, 158, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
}

static void render() {
  spr.fillSprite(TFT_BLACK);
  if (state == TITLE) drawTitle();
  else drawScene();
  spr.pushSprite(0, 0);
}

// ---------------- input ----------------
static void handleInput(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state != PLAY) {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
  }
  // steering reads the held state directly in tick()
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim) {
  drawCar(cx, cy - CAR_H / 2, dim ? tft.alphaBlend(80, TFT_RED, TFT_BLACK) : TFT_RED);
}

void begin() {
  if (!carSpr.created()) {
    carSpr.setColorDepth(spr.getColorDepth());           // pushRotated needs matching depths
    carSpr.createSprite(20, CAR_H + 2);                  // pivot defaults to the centre
  }
  hiScore = prefs.getUInt("race_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  handleInput(now);

  if (state == PLAY) {
    if (now - lastTick > 200) lastTick = now - TICK_MS;   // don't try to catch up after a stall
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick();
      dirty = true;
    }
  } else if (idleTooLong(now, lastInput)) {
    goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace race
