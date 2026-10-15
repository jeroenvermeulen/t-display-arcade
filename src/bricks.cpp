// Bricks (Breakout)
//
// Controls (two buttons):
//   Hold left  : move paddle left
//   Hold right : move paddle right
// Where the ball hits the paddle sets its angle. Higher rows are worth more.
// 3 lives; clearing the wall starts the next, faster level with more rows.

#include "common.h"

namespace bricks {

// ---------------- layout ----------------
static const int COLS = 9, MAX_ROWS = 8;
static const int BRICK_W = 14, BRICK_H = 7, PITCH_X = 15, PITCH_Y = 9;
static const int WALL_X = 1, WALL_Y = 52;               // top-left of the brick wall
static const int PAD_W = 28, PAD_H = 5, PAD_Y = 226;
static const int BALL_R = 2;
static const int START_LIVES = 3;
static const uint32_t TICK_MS = 16, SERVE_MS = 900;

static const uint16_t ROW_COLORS[MAX_ROWS] = {
  TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, 0x435F, TFT_MAGENTA, TFT_WHITE
};

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static bool alive[MAX_ROWS][COLS];
static int rows, left, level, lives;
static float bx, by, vx, vy, ballSpeed, padX;
static bool stuck;                                      // ball resting on the paddle before launch
static uint32_t launchAt;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("bricks_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void serve() {
  stuck = true;
  launchAt = millis() + SERVE_MS;
}

static void buildWall() {
  rows = min(MAX_ROWS, 5 + level);
  left = rows * COLS;
  for (int r = 0; r < MAX_ROWS; r++)
    for (int c = 0; c < COLS; c++) alive[r][c] = r < rows;
}

static void newGame() {
  score = 0;
  level = 1;
  lives = START_LIVES;
  padX = 67;
  newHigh = false;
  buildWall();
  serve();
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

static void launch() {
  stuck = false;
  ballSpeed = min(3.6f, 2.2f + level * 0.2f);
  float ang = (rnd(41) - 20) * DEG_TO_RAD;
  vx = ballSpeed * sinf(ang);
  vy = -ballSpeed * cosf(ang);
}

// removes the first brick the ball overlaps; true if one was hit
static bool hitBrick() {
  int c0 = max(0, (int)((bx - BALL_R - WALL_X) / PITCH_X));
  int c1 = min(COLS - 1, (int)((bx + BALL_R - WALL_X) / PITCH_X));
  int r0 = max(0, (int)floorf((by - BALL_R - WALL_Y) / PITCH_Y));
  int r1 = min(rows - 1, (int)floorf((by + BALL_R - WALL_Y) / PITCH_Y));
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++) {
      if (!alive[r][c]) continue;
      int x = WALL_X + c * PITCH_X, y = WALL_Y + r * PITCH_Y;
      if (bx + BALL_R < x || bx - BALL_R >= x + BRICK_W) continue;
      if (by + BALL_R < y || by - BALL_R >= y + BRICK_H) continue;
      alive[r][c] = false;
      left--;
      score += 10 * (rows - r);
      ballSpeed = min(4.5f, ballSpeed + 0.02f);
      return true;
    }
  return false;
}

static void setSpeed() {
  float s = sqrtf(vx * vx + vy * vy);
  vx *= ballSpeed / s;
  vy *= ballSpeed / s;
}

static void tick(uint32_t now) {
  if (bL.stable && !bR.stable) padX -= 3.4f;
  if (bR.stable && !bL.stable) padX += 3.4f;
  padX = constrain(padX, PAD_W / 2, 135 - PAD_W / 2);

  if (stuck) {
    bx = padX;
    by = PAD_Y - BALL_R - 1;
    if ((int32_t)(now - launchAt) >= 0) launch();
    return;
  }

  // small sub-steps so the ball can't skip through a brick
  int steps = (int)ceilf(ballSpeed / 2);
  for (int i = 0; i < steps; i++) {
    float oldY = by;
    bx += vx / steps;                                   // x first: side hits flip vx
    if (bx < BALL_R) { bx = BALL_R; vx = fabsf(vx); }
    if (bx > 135 - BALL_R) { bx = 135 - BALL_R; vx = -fabsf(vx); }
    if (hitBrick()) { bx -= vx / steps; vx = -vx; setSpeed(); }

    by += vy / steps;                                   // then y: top/bottom hits flip vy
    if (by < HEADER_H + BALL_R) { by = HEADER_H + BALL_R; vy = fabsf(vy); }
    if (hitBrick()) { by -= vy / steps; vy = -vy; setSpeed(); }

    if (vy > 0 && oldY + BALL_R <= PAD_Y && by + BALL_R >= PAD_Y &&
        fabsf(bx - padX) <= PAD_W / 2 + BALL_R) {
      float off = constrain((bx - padX) / (PAD_W / 2 + BALL_R), -1.0f, 1.0f);
      float ang = off * 60 * DEG_TO_RAD;
      vx = ballSpeed * sinf(ang);
      vy = -ballSpeed * cosf(ang);
      by = PAD_Y - BALL_R;
    }

    if (left == 0) {                                    // wall cleared
      level++;
      buildWall();
      serve();
      return;
    }
    if (by > 240 + BALL_R) {
      if (--lives == 0) gameOver();
      else serve();
      return;
    }
  }
}

// ---------------- rendering ----------------
static void drawBrick(int x, int y, int w, int h, uint16_t col) {
  spr.fillRect(x, y, w, h, col);
  spr.drawFastHLine(x, y, w, tft.alphaBlend(130, TFT_WHITE, col));
  spr.drawFastHLine(x, y + h - 1, w, tft.alphaBlend(110, TFT_BLACK, col));
}

static void drawGame() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.drawFastHLine(0, HEADER_H, 135, TFT_DARKGREY);

  for (int r = 0; r < rows; r++)
    for (int c = 0; c < COLS; c++)
      if (alive[r][c]) drawBrick(WALL_X + c * PITCH_X, WALL_Y + r * PITCH_Y, BRICK_W, BRICK_H, ROW_COLORS[r]);

  spr.fillRoundRect((int)padX - PAD_W / 2, PAD_Y, PAD_W, PAD_H, 2, TFT_LIGHTGREY);
  spr.drawFastHLine((int)padX - PAD_W / 2 + 2, PAD_Y, PAD_W - 4, TFT_WHITE);
  if (state == PLAY) spr.fillCircle((int)bx, (int)by, BALL_R, TFT_WHITE);

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) spr.fillCircle(128 - i * 9, 9, 3, TFT_WHITE);   // spare balls
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Level %d", level);
  spr.drawString(buf, 133, 24, 1);

  if (state == PLAY && stuck && level > 1 && left == rows * COLS) {   // fresh wall

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_YELLOW);
    snprintf(buf, sizeof buf, "LEVEL %d", level);
    spr.drawString(buf, 67, 150, 4);
  }
  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_ORANGE);
  spr.drawString("BRICKS", 67, 18, 4);
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < COLS; c++) drawBrick(WALL_X + c * PITCH_X, 50 + r * PITCH_Y, BRICK_W, BRICK_H, ROW_COLORS[r]);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"move left", "move right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 96 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 96 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Paddle edges = angles", 67, 146, 1);
  spr.drawString("Top rows score more", 67, 158, 1);

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
void drawIcon(int x, int y, bool dim) {
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++) drawBrick(x - 12 + k * 8, y - 12 + r * 5, 7, 4, c(ROW_COLORS[r]));
  spr.fillCircle(x + 3, y + 4, 2, c(TFT_WHITE));
  spr.fillRect(x - 7, y + 10, 14, 3, c(TFT_LIGHTGREY));
}

void begin() {
  hiScore = prefs.getUInt("bricks_hi", 0);
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
    if (now - lastTick > 200) lastTick = now - TICK_MS;   // don't try to catch up after a stall
    while (state == PLAY && now - lastTick >= TICK_MS) {
      lastTick += TICK_MS;
      tick(now);
      dirty = true;
    }
  } else {
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    if (idleTooLong(now, lastInput)) goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace bricks
