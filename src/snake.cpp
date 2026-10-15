// Snake
//
// Controls (two buttons), relative to the snake's heading:
//   Tap left  : turn left
//   Tap right : turn right
// Up to two turns are buffered, so a quick double tap makes a U-turn.

#include "common.h"

namespace snake {

// ---------------- layout ----------------
static const int COLS = 14, ROWS = 22, CELL = 9;
static const int BOARD_X = 4, BOARD_Y = 40;            // board occupies x 4..129, y 40..237
static const int MAXLEN = COLS * ROWS;

static const int DX[4] = {0, 1, 0, -1};                // up, right, down, left
static const int DY[4] = {-1, 0, 1, 0};

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t occ[ROWS][COLS];          // 1 = snake body
static uint16_t body[MAXLEN];            // ring buffer of y * COLS + x, tail first
static int tail = 0, len = 0, dir = 0;
static int turns[2], nTurns = 0;
static int foodX = 0, foodY = 0;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastStep = 0, lastInput = 0;

static inline uint16_t bodyAt(int i) { return body[(tail + i) % MAXLEN]; }

static uint32_t stepMs() {
  int ms = 170 - (int)(len - 3) * 3;      // speeds up as the snake grows
  return ms < 70 ? 70 : ms;
}

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("snake_hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static bool placeFood() {
  int free = MAXLEN - len;
  if (free <= 0) return false;
  int k = esp_random() % free;
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++)
      if (!occ[y][x] && k-- == 0) { foodX = x; foodY = y; return true; }
  return false;
}

static void newGame() {
  memset(occ, 0, sizeof occ);
  tail = 0;
  len = 0;
  dir = 0;                               // heading up
  for (int i = 0; i < 3; i++) {
    int x = COLS / 2, y = ROWS / 2 + 2 - i;
    body[len++] = y * COLS + x;
    occ[y][x] = 1;
  }
  nTurns = 0;
  score = 0;
  newHigh = false;
  placeFood();
  state = PLAY;
  lastStep = millis();
  dirty = true;
}

static void step() {
  if (nTurns) {
    dir = (dir + turns[0]) & 3;
    turns[0] = turns[1];
    nTurns--;
  }

  uint16_t hp = bodyAt(len - 1);
  int nx = hp % COLS + DX[dir], ny = hp / COLS + DY[dir];
  if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) { gameOver(); return; }

  bool eat = nx == foodX && ny == foodY;
  uint16_t tp = body[tail];
  bool intoTail = !eat && tp == ny * COLS + nx;      // tail moves away this step
  if (occ[ny][nx] && !intoTail) { gameOver(); return; }

  if (!eat) {
    occ[tp / COLS][tp % COLS] = 0;
    tail = (tail + 1) % MAXLEN;
    len--;
  }
  body[(tail + len) % MAXLEN] = ny * COLS + nx;
  len++;
  occ[ny][nx] = 1;

  if (eat) {
    score += 10;
    if (!placeFood()) gameOver();        // board full: you win
  }
  dirty = true;
}

// ---------------- rendering ----------------
static void drawTitle() {
  char buf[32];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("SNAKE", 67, 18, 4);

  for (int i = 0; i < 6; i++)
    drawCell(15 + i * 12, 54, 12, tft.alphaBlend(120 + i * 27, TFT_GREEN, TFT_BLACK));
  spr.fillCircle(107, 59, 4, TFT_RED);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R"};
  const char *acts[] = {"turn left", "turn right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 84 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 84 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Turns are relative to", 67, 136, 1);
  spr.drawString("the snake's heading", 67, 148, 1);

  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 67, 190, 2);
  spr.setTextColor(TFT_GREEN);
  spr.drawString("Press a button", 67, 214, 2);
}

static void drawGame() {
  char buf[32];

  // header
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  spr.drawString(buf, 2, 2, 2);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)hiScore);
  spr.drawString(buf, 2, 24, 1);
  spr.setTextDatum(TR_DATUM);
  snprintf(buf, sizeof buf, "Length %d", len);
  spr.drawString(buf, 132, 24, 1);

  // board
  spr.drawRect(BOARD_X - 1, BOARD_Y - 1, COLS * CELL + 2, ROWS * CELL + 2, TFT_DARKGREY);
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++)
      if (!occ[y][x]) spr.drawPixel(BOARD_X + x * CELL + 4, BOARD_Y + y * CELL + 4, 0x2945);

  if (state != TITLE) {
    spr.fillCircle(BOARD_X + foodX * CELL + 4, BOARD_Y + foodY * CELL + 4, 3, TFT_RED);

    // body fades towards the tail, head is brightest
    for (int i = 0; i < len; i++) {
      uint16_t p = bodyAt(i);
      uint16_t col = i == len - 1 ? TFT_GREENYELLOW
                                  : tft.alphaBlend(110 + 120 * i / len, TFT_GREEN, TFT_BLACK);
      drawCell(BOARD_X + (p % COLS) * CELL, BOARD_Y + (p / COLS) * CELL, CELL, col);
    }
  }

  if (state == GAMEOVER) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void render() {
  spr.fillSprite(TFT_BLACK);
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
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();
    return;
  }

  // turn on press for responsiveness
  if (eL == 1 && nTurns < 2) turns[nTurns++] = 3;      // -1 mod 4
  if (eR == 1 && nTurns < 2) turns[nTurns++] = 1;
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim) {
  // snake bending round a corner towards the food
  static const int8_t PATH[5][2] = {{0, 2}, {1, 2}, {2, 2}, {2, 1}, {2, 0}};
  for (int i = 0; i < 5; i++) {
    uint16_t col = i == 4 ? TFT_GREENYELLOW : tft.alphaBlend(110 + i * 30, TFT_GREEN, TFT_BLACK);
    if (dim) col = tft.alphaBlend(80, col, TFT_BLACK);
    drawCell(cx - 12 + PATH[i][0] * 8, cy - 12 + PATH[i][1] * 8, 8, col);
  }
  spr.fillCircle(cx - 8, cy - 8, 3, dim ? tft.alphaBlend(80, TFT_RED, TFT_BLACK) : TFT_RED);
}

void begin() {
  hiScore = prefs.getUInt("snake_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  handleInput(now);

  if (state == PLAY) {
    if (now - lastStep >= stepMs()) {
      lastStep = now;
      step();
    }
  } else if (idleTooLong(now, lastInput)) {
    goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace snake
