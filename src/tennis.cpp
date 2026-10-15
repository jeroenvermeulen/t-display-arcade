// Tennis
//
// You (bottom) against the CPU (top), seen from above.
//   Hold left  : move left
//   Hold right : move right
// Where the ball hits your racket sets its angle; it speeds up during a rally.
// Angled shots are harder for the CPU to read, so hitting with the racket
// edges wins points. Every point you win makes the CPU a bit sharper.
// The CPU needs 5 points to win.

#include "common.h"

namespace tennis {

// ---------------- layout ----------------
static const int WALL_L = 2, WALL_R = 133;              // ball bounces off the screen sides
static const int COURT_X = 10, COURT_Y = 42, COURT_W = 115, COURT_H = 194;
static const int NET_Y = COURT_Y + COURT_H / 2;
static const int PAD_W = 26, PAD_H = 4;
static const int CPU_Y = 46, PLAYER_Y = 228;            // top edge of each racket
static const int BALL_R = 3;
static const int CPU_WINS = 5;
static const uint32_t TICK_MS = 16, SERVE_MS = 800;

static const uint16_t SURROUND = 0x2C45, COURT = 0x3A6E, BALL = 0xCFE0, SHADOW = 0x1923;

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static float bx, by, vx, vy, ballSpeed;
static float px, cx;                                    // racket centres
static float aimErr;                                    // CPU's misjudgement of the ball
static int you, cpu;
static bool serving;
static uint32_t serveAt;
static uint32_t hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0;

static int rnd(int n) { return esp_random() % n; }

// How far off the CPU will aim after you hit: grows with the sideways speed of
// your shot, shrinks as you score. Its racket reaches 16 px either side, so
// errors beyond that are misses (~25-30% of shots at the start, ~15-20% at 4 points).
static void pickAimErr() {
  int m = max(3, (int)(2 + fabsf(vx) * 9) - you);
  aimErr = rnd(2 * m + 1) - m;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = (uint32_t)you > hiScore;
  if (newHigh) {
    hiScore = you;
    prefs.putUInt("tennis_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void newPoint() {
  serving = true;
  serveAt = millis() + SERVE_MS;
  bx = 67;
  by = NET_Y;
  vx = vy = 0;
}

static void serve() {
  serving = false;
  ballSpeed = 2.4f;
  float ang = (rnd(61) - 30) * DEG_TO_RAD;
  int dir = (you + cpu) % 2 ? -1 : 1;                   // alternate who receives
  vx = ballSpeed * sinf(ang);
  vy = dir * ballSpeed * cosf(ang);
  pickAimErr();
}

static void newGame() {
  you = cpu = 0;
  px = cx = 67;
  newHigh = false;
  state = PLAY;
  newPoint();
  lastTick = millis();
  dirty = true;
}

// x where the ball will cross row y, folding in the side-wall bounces
static float predictX(float y) {
  if (vy == 0) return bx;
  float t = (y - by) / vy;
  float lo = WALL_L + BALL_R, w = WALL_R - BALL_R - lo;
  float m = fmodf(bx + vx * t - lo, 2 * w);
  if (m < 0) m += 2 * w;
  if (m > w) m = 2 * w - m;
  return lo + m;
}

static void bounce(float padX, int dirY) {
  float off = constrain((bx - padX) / (PAD_W / 2 + BALL_R), -1.0f, 1.0f);
  float ang = off * 55 * DEG_TO_RAD;
  ballSpeed = min(5.5f, ballSpeed + 0.12f);
  vx = ballSpeed * sinf(ang);
  vy = dirY * ballSpeed * cosf(ang);
}

static void tick(uint32_t now) {
  // player
  if (bL.stable && !bR.stable) px -= 3.2f;
  if (bR.stable && !bL.stable) px += 3.2f;
  px = constrain(px, PAD_W / 2 + WALL_L, WALL_R - PAD_W / 2);

  // CPU reacts once the ball crosses the net, then heads for where it thinks
  // the ball will arrive; it gets faster as you score
  float target = vy < 0 && by < NET_Y ? predictX(CPU_Y + PAD_H) + aimErr : 67;
  float cpuSpeed = min(3.0f, 1.5f + you * 0.1f);
  cx += constrain(target - cx, -cpuSpeed, cpuSpeed);
  cx = constrain(cx, PAD_W / 2 + WALL_L, WALL_R - PAD_W / 2);

  if (serving) {
    if ((int32_t)(now - serveAt) >= 0) serve();
    return;
  }

  float oldY = by;
  bx += vx;
  by += vy;
  if (bx < WALL_L + BALL_R) { bx = WALL_L + BALL_R; vx = fabsf(vx); }
  if (bx > WALL_R - BALL_R) { bx = WALL_R - BALL_R; vx = -fabsf(vx); }

  // rackets: only when the ball crosses the racket's front edge this tick
  if (vy > 0 && oldY + BALL_R <= PLAYER_Y && by + BALL_R >= PLAYER_Y &&
      fabsf(bx - px) <= PAD_W / 2 + BALL_R) {
    by = PLAYER_Y - BALL_R;
    bounce(px, -1);
    pickAimErr();
  }
  if (vy < 0 && oldY - BALL_R >= CPU_Y + PAD_H && by - BALL_R <= CPU_Y + PAD_H &&
      fabsf(bx - cx) <= PAD_W / 2 + BALL_R) {
    by = CPU_Y + PAD_H + BALL_R;
    bounce(cx, 1);
  }

  if (by > 240 + BALL_R) {
    cpu++;
    if (cpu >= CPU_WINS) { gameOver(); return; }
    newPoint();
  } else if (by < HEADER_H - BALL_R) {
    you++;
    newPoint();
  }
}

// ---------------- rendering ----------------
static void drawRacket(int x, int y, uint16_t col) {
  spr.fillRoundRect(x - PAD_W / 2, y, PAD_W, PAD_H, 2, col);
  spr.drawFastHLine(x - PAD_W / 2 + 2, y, PAD_W - 4, tft.alphaBlend(140, TFT_WHITE, col));
}

static void drawCourt() {
  spr.fillRect(0, HEADER_H, 135, 240 - HEADER_H, SURROUND);
  spr.fillRect(COURT_X, COURT_Y, COURT_W, COURT_H, COURT);
  spr.drawRect(COURT_X, COURT_Y, COURT_W, COURT_H, TFT_WHITE);
  // singles side lines, service lines and centre line
  spr.drawFastVLine(COURT_X + 12, COURT_Y, COURT_H, TFT_WHITE);
  spr.drawFastVLine(COURT_X + COURT_W - 13, COURT_Y, COURT_H, TFT_WHITE);
  int sv1 = NET_Y - 52, sv2 = NET_Y + 52;
  spr.drawFastHLine(COURT_X + 12, sv1, COURT_W - 24, TFT_WHITE);
  spr.drawFastHLine(COURT_X + 12, sv2, COURT_W - 24, TFT_WHITE);
  spr.drawFastVLine(67, sv1, sv2 - sv1, TFT_WHITE);
  // net
  for (int x = COURT_X - 4; x < COURT_X + COURT_W + 4; x += 4) spr.drawFastHLine(x, NET_Y, 2, TFT_LIGHTGREY);
  spr.fillRect(COURT_X - 5, NET_Y - 1, 3, 3, TFT_WHITE);
  spr.fillRect(COURT_X + COURT_W + 2, NET_Y - 1, 3, 3, TFT_WHITE);
}

static void drawGame() {
  char buf[32];
  drawCourt();
  drawRacket((int)cx, CPU_Y, TFT_RED);
  drawRacket((int)px, PLAYER_Y, TFT_YELLOW);

  bool showBall = !serving || (millis() / 150) % 2;      // blink while waiting to serve
  if (state == PLAY && showBall) {
    spr.fillCircle((int)bx + 2, (int)by + 2, BALL_R, SHADOW);
    spr.fillCircle((int)bx, (int)by, BALL_R, BALL);
  }

  drawScoreHeader(you, hiScore);
  spr.fillRect(0, 0, 135, 22, TFT_BLACK);               // replace "Score" with the match score
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_YELLOW);
  snprintf(buf, sizeof buf, "You %d", you);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_RED);
  snprintf(buf, sizeof buf, "Me %d/%d", cpu, CPU_WINS);
  spr.drawString(buf, 133, 2, 2);

  if (state == GAMEOVER) drawGameOverBox("MATCH TO ME!", you, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(BALL);
  spr.drawString("TENNIS", 67, 18, 4);
  drawIcon(67, 64, false);

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
  spr.drawString("Edges: sharper angles", 67, 146, 1);
  spr.drawString("I win at 5 points", 67, 158, 1);

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
  // rally: the ball bounces between the paddles, which chase it sideways
  auto c = [dim](uint16_t col) { return dim ? tft.alphaBlend(80, col, TFT_BLACK) : col; };
  auto ballX = [x](uint32_t t) { return x - 7 + (int)roundf(14 * triWave(t + 1200, 2600)); };
  int redX = constrain(ballX(t + 2600 - 250) - x, -5, 5);   // lagging behind the ball
  int yelX = constrain(ballX(t + 2600 - 150) - x, -5, 5);
  spr.fillRoundRect(x + redX - 7, y - 12, 14, 4, 2, c(TFT_RED));
  spr.fillRoundRect(x + yelX - 7, y + 8, 14, 4, 2, c(TFT_YELLOW));
  for (int i = -12; i <= 12; i += 4) spr.drawFastHLine(x + i, y, 2, c(TFT_LIGHTGREY));
  spr.fillCircle(ballX(t), y - 5 + (int)roundf(10 * triWave(t + 300, 1200)), 3, c(BALL));
}

void begin() {
  hiScore = prefs.getUInt("tennis_hi", 0);
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

}  // namespace tennis
