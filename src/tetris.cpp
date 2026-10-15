// Tetris
//
// Controls (two buttons):
//   Tap  left   : move left
//   Tap  right  : move right
//   Hold left   : rotate (keeps rotating while held)
//   Hold right  : soft drop (while held)
//   Both        : hard drop

#include "common.h"

namespace tetris {

#define HOLD_MS         220      // press longer than this counts as "hold"
#define ROTATE_REPEAT_MS 350     // repeat rotation while left is held
#define SOFTDROP_MS     35

// ---------------- layout ----------------
static const int COLS = 10, ROWS = 20, CELL = 10;
static const int BOARD_X = 2, BOARD_Y = 39;            // board occupies x 2..101, y 39..238
static const int SIDE_X = 106;                          // right-hand info column
static const int NEXT_Y = 52, NEXT_CELL = 7;

// 4x4 bitmasks per rotation (bit 15 = row 0, col 0)
static const uint16_t SHAPES[7][4] = {
  {0x0F00, 0x2222, 0x00F0, 0x4444}, // I
  {0x8E00, 0x6440, 0x0E20, 0x44C0}, // J
  {0x2E00, 0x4460, 0x0E80, 0xC440}, // L
  {0x6600, 0x6600, 0x6600, 0x6600}, // O
  {0x6C00, 0x4620, 0x06C0, 0x8C40}, // S
  {0x4E00, 0x4640, 0x0E40, 0x4C40}, // T
  {0xC600, 0x2640, 0x0C60, 0x4C80}, // Z
};
static const uint16_t COLORS[7] = {
  TFT_CYAN, 0x435F /* bright blue */, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_MAGENTA, TFT_RED
};

static inline bool maskCell(uint16_t m, int r, int c) { return m & (0x8000 >> (r * 4 + c)); }

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

static uint8_t board[ROWS][COLS];        // 0 = empty, 1..7 = piece type + 1
static int curT, curR, curX, curY, nextT;
static uint32_t score = 0, hiScore = 0, lines = 0;
static int level = 0;
static bool newHigh = false;

static uint8_t bag[7];
static int bagPos = 7;

static bool softDrop = false, dirty = true, armed = false;
static uint32_t lastFall = 0, lastInput = 0;

// ---------------- game logic ----------------
static int drawFromBag() {
  if (bagPos >= 7) {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i > 0; i--) {
      int j = esp_random() % (i + 1);
      uint8_t t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bagPos = 0;
  }
  return bag[bagPos++];
}

static bool fits(int t, int r, int x, int y) {
  uint16_t m = SHAPES[t][r];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      if (maskCell(m, i, j)) {
        int bx = x + j, by = y + i;
        if (bx < 0 || bx >= COLS || by >= ROWS) return false;
        if (by >= 0 && board[by][bx]) return false;
      }
  return true;
}

static uint32_t gravityMs() {
  static const uint16_t t[20] = {800, 717, 633, 550, 467, 383, 300, 217, 133, 100,
                                 83, 83, 83, 67, 67, 67, 50, 50, 50, 33};
  return t[level < 19 ? level : 19];
}

static void gameOver() {
  state = GAMEOVER;
  softDrop = false;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("hi", hiScore);
  }
  lastInput = millis();
  dirty = true;
}

static void spawn() {
  curT = nextT;
  nextT = drawFromBag();
  curR = 0;
  curX = 3;
  curY = 0;
  if (!fits(curT, curR, curX, curY)) gameOver();
}

static void newGame() {
  memset(board, 0, sizeof board);
  score = lines = 0;
  level = 0;
  newHigh = false;
  bagPos = 7;
  nextT = drawFromBag();
  state = PLAY;
  spawn();
  softDrop = false;
  bL.consumed = bR.consumed = true;   // ignore whatever is still held
  lastFall = millis();
  dirty = true;
}

static void tryMove(int dx) {
  if (fits(curT, curR, curX + dx, curY)) { curX += dx; dirty = true; }
}

static void tryRotate() {
  int nr = (curR + 1) & 3;
  static const int kicks[] = {0, -1, 1, -2, 2};
  for (int k : kicks)
    if (fits(curT, nr, curX + k, curY)) { curR = nr; curX += k; dirty = true; return; }
  if (fits(curT, nr, curX, curY - 1)) { curR = nr; curY--; dirty = true; }
}

static int ghostY() {
  int y = curY;
  while (fits(curT, curR, curX, y + 1)) y++;
  return y;
}

static void render();   // forward

static void lockPiece() {
  uint16_t m = SHAPES[curT][curR];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      if (maskCell(m, i, j)) {
        int by = curY + i;
        if (by < 0) { gameOver(); return; }
        board[by][curX + j] = curT + 1;
      }

  int full[4], n = 0;
  for (int y = 0; y < ROWS && n < 4; y++) {
    bool f = true;
    for (int x = 0; x < COLS; x++) if (!board[y][x]) { f = false; break; }
    if (f) full[n++] = y;
  }

  if (n) {
    // short flash of the cleared rows
    render();
    for (int k = 0; k < n; k++)
      spr.fillRect(BOARD_X, BOARD_Y + full[k] * CELL, COLS * CELL, CELL - 1, TFT_WHITE);
    spr.pushSprite(0, 0);
    delay(120);

    for (int k = 0; k < n; k++) {               // ascending order: lower indices stay valid
      for (int y = full[k]; y > 0; y--) memcpy(board[y], board[y - 1], COLS);
      memset(board[0], 0, COLS);
    }
    static const uint16_t pts[5] = {0, 40, 100, 300, 1200};
    score += pts[n] * (level + 1);
    lines += n;
    level = lines / 10;
  }

  spawn();
  lastFall = millis();
  dirty = true;
}

static void stepDown() {
  if (fits(curT, curR, curX, curY + 1)) {
    curY++;
    if (softDrop) score += 1;
    dirty = true;
  } else {
    lockPiece();
  }
}

static void hardDrop() {
  while (fits(curT, curR, curX, curY + 1)) { curY++; score += 2; }
  lockPiece();
}

// ---------------- rendering ----------------
static void drawTitle() {
  char buf[32];
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("TETRIS", 67, 18, 4);

  // little decorative row of pieces
  for (int i = 0; i < 7; i++) drawCell(14 + i * 15, 52, 12, COLORS[i]);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Tap L", "Tap R", "Hold L", "Hold R", "L + R"};
  const char *acts[] = {"left", "right", "rotate", "soft drop", "hard drop"};
  for (int i = 0; i < 5; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 80 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 80 + i * 20, 2);
  }

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
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

  // board
  spr.drawRect(BOARD_X - 1, BOARD_Y - 1, COLS * CELL + 2, ROWS * CELL + 2, TFT_DARKGREY);
  for (int y = 0; y < ROWS; y++)
    for (int x = 0; x < COLS; x++) {
      int px = BOARD_X + x * CELL, py = BOARD_Y + y * CELL;
      if (board[y][x]) drawCell(px, py, CELL, COLORS[board[y][x] - 1]);
      else spr.drawPixel(px + 4, py + 4, 0x2945);   // faint grid dot
    }

  if (state == PLAY) {
    uint16_t m = SHAPES[curT][curR];
    int gy = ghostY();
    uint16_t gcol = tft.alphaBlend(90, COLORS[curT], TFT_BLACK);
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        if (maskCell(m, i, j)) {
          int px = BOARD_X + (curX + j) * CELL;
          if (gy + i >= 0) spr.drawRect(px, BOARD_Y + (gy + i) * CELL, CELL - 1, CELL - 1, gcol);
          if (curY + i >= 0) drawCell(px, BOARD_Y + (curY + i) * CELL, CELL, COLORS[curT]);
        }
  }

  // side column
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("NEXT", SIDE_X, 40, 1);
  uint16_t nm = SHAPES[nextT][0];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      if (maskCell(nm, i, j)) drawCell(SIDE_X + j * NEXT_CELL, NEXT_Y + i * NEXT_CELL, NEXT_CELL, COLORS[nextT]);

  spr.drawString("LEVEL", SIDE_X, 92, 1);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "%d", level);
  spr.drawString(buf, SIDE_X, 102, 2);

  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("LINES", SIDE_X, 126, 1);
  spr.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "%lu", (unsigned long)lines);
  spr.drawString(buf, SIDE_X, 136, 2);

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

  // both buttons -> hard drop
  if ((eL == 1 && bR.stable && !bR.held && !bR.consumed) ||
      (eR == 1 && bL.stable && !bL.held && !bL.consumed)) {
    bL.consumed = bR.consumed = true;
    softDrop = false;
    hardDrop();
    return;
  }

  // holds
  if (bL.stable && !bL.consumed) {
    if (!bL.held && now - bL.t0 >= HOLD_MS) {
      bL.held = true;
      bL.nextRepeat = now + ROTATE_REPEAT_MS;
      tryRotate();
    } else if (bL.held && now >= bL.nextRepeat) {
      bL.nextRepeat = now + ROTATE_REPEAT_MS;
      tryRotate();
    }
  }
  if (bR.stable && !bR.consumed && !bR.held && now - bR.t0 >= HOLD_MS) {
    bR.held = true;
    softDrop = true;
  }

  // taps (on release)
  if (eL == 2 && !bL.consumed && !bL.held) tryMove(-1);
  if (eR == 2) {
    if (!bR.consumed && !bR.held) tryMove(1);
    softDrop = false;
  }
}

// ---------------- entry points ----------------
void drawIcon(int cx, int cy, bool dim, uint32_t t) {
  // an I piece drops into the gap, two rows flash and clear (3x4 cells)
  static const int8_t BASE[4][3] = {{-1, -1, -1}, {5, -1, -1}, {5, 5, -1}, {2, 2, -1}};
  uint32_t p = (t + 1000) % 2400;                  // t = 0: piece just landed
  bool flash = p >= 1200 && p < 1600 && p / 100 % 2 == 0;
  bool cleared = p >= 1600;
  auto cell = [&](int r, int c, uint16_t col) {
    if (flash && r >= 2) col = TFT_WHITE;
    if (dim) col = tft.alphaBlend(80, col, TFT_BLACK);
    drawCell(cx - 12 + c * 8, cy - 16 + r * 8, 8, col);
  };
  if (cleared) { cell(3, 0, COLORS[5]); return; }  // what is left after the clear
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 3; c++)
      if (BASE[r][c] >= 0) cell(r, c, COLORS[BASE[r][c]]);
  int top = p < 800 ? (int)(p / 200) - 2 : 2;      // falls one row per 200 ms
  for (int i = 0; i < 2; i++) cell(top + i, 2, COLORS[0]);
}

void begin() {
  hiScore = prefs.getUInt("hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  handleInput(now);

  if (state == PLAY) {
    uint32_t interval = softDrop ? SOFTDROP_MS : gravityMs();
    if (now - lastFall >= interval) {
      lastFall = now;
      stepDown();
    }
  } else if (idleTooLong(now, lastInput)) {
    goToSleep();
  }

  if (dirty) {
    render();
    dirty = false;
  }
}

}  // namespace tetris
