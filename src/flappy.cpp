// Flappy Bird
//
// Controls: tap either button to flap.
// Fly through the gaps between the pipes; each pipe passed scores a point.
// It starts easy (wide gaps at similar heights, slower pipes) and gets harder
// until about 30 points: gaps narrow, pipes speed up, heights vary more.

#include "common.h"

namespace flappy {

// ---------------- layout ----------------
static const int GROUND_Y = 220;                       // sky: y 0..219, ground below
static const int BIRD_X = 36, BIRD_R = 6;
static const int PIPE_W = 22, CAP_H = 8;
static const int NUM_PIPES = 3;
static const float SPACING = 80;                       // horizontal distance between pipes
static const uint32_t TICK_MS = 16;

static const float GRAVITY = 0.22f, FLAP_V = -3.6f, MAX_FALL = 5.0f;

static const uint16_t SKY = 0x867D, PIPE = 0x3E66, PIPE_DK = 0x1B42, PIPE_LT = 0x8FEF;
static const uint16_t GRASS = 0x5E0A, DIRT = 0xDEAA, DIRT_DK = 0xBD47, DARK = 0x18E3;

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

struct Pipe { float x; int gapY, gap; bool scored; };
static Pipe pipes[NUM_PIPES];
static float cloudX[3] = {20, 75, 130};
static float birdY, vy, groundOff;
static bool started = false;                            // bird hovers until the first flap
static uint32_t frame = 0;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0, deadAt = 0;

static TFT_eSprite birdSpr(&tft);                       // scratch sprite for the tilted bird

static int rnd(int n) { return esp_random() % n; }

static int lastGapY = 110;                              // centre of the most recent gap

static float pipeSpeed() { return min(2.2f, 1.3f + score * 0.03f); }

static void resetPipe(Pipe &p, float x) {
  p.x = x;
  p.gap = max(50, 80 - (int)score);
  int lo = p.gap / 2 + 14, hi = GROUND_Y - p.gap / 2 - 14;   // keep gaps off the very top/bottom
  // early gaps stay near the previous one; later they can be anywhere
  int reach = 30 + (int)score * 4;
  lo = max(lo, lastGapY - reach);
  hi = min(hi, lastGapY + reach);
  p.gapY = lo + rnd(hi - lo + 1);
  lastGapY = p.gapY;
  p.scored = false;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("flappy_hi", hiScore);
  }
  deadAt = lastInput = millis();
  dirty = true;
}

static void newGame() {
  score = 0;
  lastGapY = 110;
  for (int i = 0; i < NUM_PIPES; i++) resetPipe(pipes[i], 160 + i * SPACING);
  birdY = 100;
  vy = 0;
  started = false;
  newHigh = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static bool hitsPipe(const Pipe &p) {
  // slightly forgiving square hitbox around the bird
  int r = BIRD_R - 1;
  if (BIRD_X + r < p.x || BIRD_X - r > p.x + PIPE_W) return false;
  return birdY - r < p.gapY - p.gap / 2 || birdY + r > p.gapY + p.gap / 2;
}

static void tick() {
  frame++;
  for (float &c : cloudX) {
    c -= 0.3f;
    if (c < -25) c += 185;
  }
  if (!started) {                                       // gentle bobbing while waiting
    birdY = 100 + 4 * sinf(frame * 0.08f);
    return;
  }

  float sp = pipeSpeed();
  groundOff += sp;

  vy = min(vy + GRAVITY, MAX_FALL);
  birdY += vy;
  if (birdY < BIRD_R) { birdY = BIRD_R; vy = 0; }       // can't fly over the pipes
  if (birdY + BIRD_R >= GROUND_Y) { birdY = GROUND_Y - BIRD_R; gameOver(); return; }

  for (Pipe &p : pipes) {
    p.x -= sp;
    if (p.x < -PIPE_W - 4) resetPipe(p, p.x + NUM_PIPES * SPACING);
    if (!p.scored && p.x + PIPE_W < BIRD_X - BIRD_R) { p.scored = true; score++; }
    if (hitsPipe(p)) { gameOver(); return; }
  }
}

// ---------------- rendering ----------------
static void drawBirdShape(TFT_eSprite &s, int cx, int cy, int wing, bool dim = false) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  s.fillEllipse(cx, cy, 7, 6, c(TFT_YELLOW));
  s.drawEllipse(cx, cy, 7, 6, c(TFT_ORANGE));
  s.fillEllipse(cx - 3, cy + 1 + wing, 4, 2, c(TFT_WHITE));       // wing
  s.fillCircle(cx + 3, cy - 2, 2, c(TFT_WHITE));                   // eye
  s.drawPixel(cx + 4, cy - 2, DARK);
  s.fillTriangle(cx + 5, cy, cx + 9, cy + 1, cx + 5, cy + 3, c(TFT_ORANGE));   // beak
}

// bird centred on (cx, cy), tilted clockwise by angle degrees
static void drawBird(int cx, int cy, float angle, int wing) {
  if (!birdSpr.created()) { drawBirdShape(spr, cx, cy, wing); return; }
  birdSpr.fillSprite(TFT_BLACK);                        // black = transparent
  drawBirdShape(birdSpr, birdSpr.width() / 2, birdSpr.height() / 2, wing);
  spr.setPivot(cx, cy);
  birdSpr.pushRotated(&spr, (int)roundf(angle), TFT_BLACK);
}

static void drawPipe(const Pipe &p) {
  int x = (int)p.x;
  int top = p.gapY - p.gap / 2, bot = p.gapY + p.gap / 2;
  // shafts
  spr.fillRect(x, 0, PIPE_W, top, PIPE);
  spr.fillRect(x, bot, PIPE_W, GROUND_Y - bot, PIPE);
  spr.fillRect(x + 3, 0, 3, top, PIPE_LT);
  spr.fillRect(x + 3, bot, 3, GROUND_Y - bot, PIPE_LT);
  spr.drawFastVLine(x, 0, top, PIPE_DK);
  spr.drawFastVLine(x + PIPE_W - 1, 0, top, PIPE_DK);
  spr.drawFastVLine(x, bot, GROUND_Y - bot, PIPE_DK);
  spr.drawFastVLine(x + PIPE_W - 1, bot, GROUND_Y - bot, PIPE_DK);
  // caps at the gap
  spr.fillRect(x - 2, top - CAP_H, PIPE_W + 4, CAP_H, PIPE);
  spr.drawRect(x - 2, top - CAP_H, PIPE_W + 4, CAP_H, PIPE_DK);
  spr.fillRect(x - 2, bot, PIPE_W + 4, CAP_H, PIPE);
  spr.drawRect(x - 2, bot, PIPE_W + 4, CAP_H, PIPE_DK);
}

static void drawBackground() {
  spr.fillSprite(SKY);
  for (int i = 0; i < 3; i++) {
    int x = (int)cloudX[i], y = 40 + i * 45;
    spr.fillCircle(x, y, 8, TFT_WHITE);
    spr.fillCircle(x + 9, y - 4, 10, TFT_WHITE);
    spr.fillCircle(x + 19, y, 8, TFT_WHITE);
    spr.fillRect(x, y, 19, 8, TFT_WHITE);
  }
}

static void drawGround() {
  spr.fillRect(0, GROUND_Y, 135, 240 - GROUND_Y, DIRT);
  spr.fillRect(0, GROUND_Y, 135, 4, GRASS);
  spr.drawFastHLine(0, GROUND_Y, 135, PIPE_DK);
  int off = (int)groundOff % 12;
  for (int x = -off; x < 135; x += 12)                  // scrolling diagonal stripes
    for (int k = 0; k < 6; k++) spr.drawFastHLine(x + k, GROUND_Y + 6 + k * 2, 5, DIRT_DK);
}

static void drawOutlined(const char *txt, int x, int y, int font, uint16_t col) {
  spr.setTextColor(DARK);
  spr.drawString(txt, x + 1, y + 1, font);
  spr.setTextColor(col);
  spr.drawString(txt, x, y, font);
}

static void drawGame() {
  char buf[32];
  drawBackground();
  for (const Pipe &p : pipes) drawPipe(p);
  drawGround();

  float angle = started ? constrain(vy * 8, -25.0f, 70.0f) : 0;
  int wing = state == PLAY && (!started || vy < 0) ? (int)(frame / 4 % 3) - 1 : 0;
  drawBird(BIRD_X, (int)birdY, angle, wing);

  spr.setTextDatum(TC_DATUM);
  snprintf(buf, sizeof buf, "%lu", (unsigned long)score);
  drawOutlined(buf, 67, 8, 4, TFT_WHITE);

  if (state == PLAY && !started) {
    drawOutlined("Get ready!", 67, 60, 2, TFT_YELLOW);
    drawOutlined("Tap to flap", 67, 130, 2, TFT_WHITE);
  }

  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  drawBackground();
  drawGround();
  spr.setTextDatum(TC_DATUM);
  drawOutlined("FLAPPY", 67, 14, 4, TFT_YELLOW);
  drawOutlined("BIRD", 67, 40, 4, TFT_YELLOW);
  drawBird(67, 84, -15, -1);

  spr.fillRoundRect(8, 104, 119, 108, 6, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  spr.drawString("Tap L/R", 16, 112, 2);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("flap", 74, 112, 2);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Fly through the gaps", 67, 138, 1);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 160, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 186, 2);
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

  if (state == PLAY) {
    if (eL == 1 || eR == 1) { vy = FLAP_V; started = true; }
    return;
  }
  // after a crash, ignore button mashing for a moment so you see the result
  if (state == GAMEOVER && now - deadAt < 600) return;
  if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim) {
  drawBirdShape(spr, cx, cy, -1, dim);
}

void begin() {
  if (!birdSpr.created()) {
    birdSpr.setColorDepth(spr.getColorDepth());         // pushRotated needs matching depths
    birdSpr.createSprite(22, 22);                        // room for the bird at any tilt
  }
  hiScore = prefs.getUInt("flappy_hi", 0);
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
  }
  if ((state != PLAY || !started) && idleTooLong(now, lastInput)) goToSleep();

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace flappy
