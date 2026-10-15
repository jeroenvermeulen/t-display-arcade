// Shooter
//
// Controls (two buttons):
//   Hold left  : fly left
//   Hold right : fly right
// The ship fires on its own. Saucers (10) weave down the screen; bigger
// cruisers (30) take 3 hits and shoot back. Waves get denser and faster over
// time. 3 lives; after a hit you are briefly invulnerable.

#include "common.h"

namespace shooter {

// ---------------- layout ----------------
static const int SHIP_Y = 214;                          // top of the ship (nose)
static const int SHIP_W = 15, SHIP_H = 14;
static const int MAX_SHOTS = 10, MAX_ENEMIES = 12, MAX_BOMBS = 16, MAX_BOOMS = 10, NUM_STARS = 30;
static const int FIRE_TICKS = 14, INVULN_TICKS = 100;
static const int START_LIVES = 3;
static const uint32_t TICK_MS = 16;

static const uint16_t SHIP = 0xCE79, COCKPIT = 0x2B1F, FLAME = TFT_ORANGE;

// ---------------- game state ----------------
enum State { TITLE, PLAY, GAMEOVER };
static State state = TITLE;

struct Shot { bool on; float x, y; };
struct Enemy { bool on; int type, hp, hitFlash, fireIn; float x0, x, y, vy, phase, amp; };
struct Bomb { bool on; float x, y, vx, vy; };
struct Boom { bool on; int x, y, t, size; };
struct Star { float x, y, v; uint16_t col; };

static Shot shots[MAX_SHOTS];
static Enemy enemies[MAX_ENEMIES];
static Bomb bombs[MAX_BOMBS];
static Boom booms[MAX_BOOMS];
static Star stars[NUM_STARS];

static float px;
static int lives, invuln, fireIn, spawnIn;
static uint32_t ticks;
static uint32_t score = 0, hiScore = 0;
static bool newHigh = false, dirty = true, armed = false;
static uint32_t lastTick = 0, lastInput = 0, deadAt = 0;

static int rnd(int n) { return esp_random() % n; }
static int wave() { return ticks / 900; }               // difficulty step every ~15 s

static void gameOver() {
  state = GAMEOVER;
  armed = false;
  newHigh = score > hiScore;
  if (newHigh) {
    hiScore = score;
    prefs.putUInt("shooter_hi", hiScore);
  }
  deadAt = lastInput = millis();
  dirty = true;
}

static void initStars() {
  for (Star &s : stars) {
    s.x = rnd(135);
    s.y = HEADER_H + rnd(240 - HEADER_H);
    s.v = 0.4f + rnd(16) / 10.0f;
    uint8_t b = 80 + (uint8_t)(s.v * 70);
    s.col = tft.color565(b, b, b);
  }
}

static void newGame() {
  memset(shots, 0, sizeof shots);
  memset(enemies, 0, sizeof enemies);
  memset(bombs, 0, sizeof bombs);
  memset(booms, 0, sizeof booms);
  px = 67;
  lives = START_LIVES;
  invuln = fireIn = 0;
  spawnIn = 60;
  ticks = 0;
  score = 0;
  newHigh = false;
  state = PLAY;
  lastTick = millis();
  dirty = true;
}

template <typename T, size_t N> static T *freeSlot(T (&pool)[N]) {
  for (T &t : pool)
    if (!t.on) return &t;
  return nullptr;
}

static void boom(int x, int y, int size) {
  if (Boom *b = freeSlot(booms)) *b = {true, x, y, 0, size};
}

static void spawnEnemy() {
  Enemy *e = freeSlot(enemies);
  if (!e) return;
  int w = wave();
  bool big = rnd(100) < min(45, 10 + w * 6);
  e->on = true;
  e->type = big ? 1 : 0;
  e->hp = big ? 3 : 1;
  e->hitFlash = 0;
  e->x0 = 16 + rnd(135 - 32);
  e->x = e->x0;
  e->y = HEADER_H - 10;
  e->vy = (big ? 0.5f : 0.8f) + w * 0.08f + rnd(30) / 100.0f;
  e->phase = rnd(628) / 100.0f;
  e->amp = big ? 6 : 10 + rnd(10);
  e->fireIn = 40 + rnd(80);
}

static bool overlap(float ax, float ay, int aw, int ah, float bx, float by, int bw, int bh) {
  return fabsf(ax - bx) * 2 < aw + bw && fabsf(ay - by) * 2 < ah + bh;
}

static void loseLife() {
  boom((int)px, SHIP_Y + SHIP_H / 2, 14);
  if (--lives == 0) { gameOver(); return; }
  invuln = INVULN_TICKS;
}

static void tick() {
  ticks++;
  for (Star &s : stars) {
    s.y += s.v;
    if (s.y >= 240) { s.y = HEADER_H; s.x = rnd(135); }
  }
  for (Boom &b : booms)
    if (b.on && ++b.t > 16) b.on = false;
  if (state != PLAY) return;

  // ship
  if (bL.stable && !bR.stable) px -= 3.0f;
  if (bR.stable && !bL.stable) px += 3.0f;
  px = constrain(px, SHIP_W / 2 + 1, 134 - SHIP_W / 2);
  if (invuln) invuln--;

  if (--fireIn <= 0) {
    fireIn = FIRE_TICKS;
    if (Shot *s = freeSlot(shots)) *s = {true, px, (float)SHIP_Y - 4};
  }
  for (Shot &s : shots)
    if (s.on && (s.y -= 6) < HEADER_H) s.on = false;

  // enemies
  if (--spawnIn <= 0) {
    spawnEnemy();
    spawnIn = max(14, 55 - wave() * 5) + rnd(20);
  }
  float shipCy = SHIP_Y + SHIP_H / 2;
  for (Enemy &e : enemies) {
    if (!e.on) continue;
    e.y += e.vy;
    e.phase += 0.05f;
    e.x = e.x0 + sinf(e.phase) * e.amp;
    if (e.hitFlash) e.hitFlash--;
    if (e.y > 250) { e.on = false; continue; }

    if (e.type == 1 && --e.fireIn <= 0 && e.y < SHIP_Y - 40) {   // cruisers aim at you
      e.fireIn = max(50, 110 - wave() * 8) + rnd(40);
      if (Bomb *b = freeSlot(bombs)) {
        float dx = px - e.x, dy = shipCy - e.y, d = sqrtf(dx * dx + dy * dy);
        float sp = 2.0f + wave() * 0.1f;
        *b = {true, e.x, e.y + 6, dx / d * sp, dy / d * sp};
      }
    }

    int ew = e.type ? 20 : 14, eh = e.type ? 12 : 8;
    for (Shot &s : shots) {
      if (!s.on || !overlap(s.x, s.y, 2, 6, e.x, e.y, ew, eh)) continue;
      s.on = false;
      e.hitFlash = 4;
      if (--e.hp == 0) {
        e.on = false;
        score += e.type ? 30 : 10;
        boom((int)e.x, (int)e.y, e.type ? 12 : 8);
        break;
      }
    }
    if (e.on && !invuln && overlap(px, shipCy, SHIP_W - 4, SHIP_H - 4, e.x, e.y, ew, eh)) {
      e.on = false;
      boom((int)e.x, (int)e.y, 10);
      loseLife();
      if (state != PLAY) return;
    }
  }

  for (Bomb &b : bombs) {
    if (!b.on) continue;
    b.x += b.vx;
    b.y += b.vy;
    if (b.y > 240 || b.x < 0 || b.x > 135) { b.on = false; continue; }
    if (!invuln && overlap(px, shipCy, SHIP_W - 6, SHIP_H - 4, b.x, b.y, 3, 3)) {
      b.on = false;
      loseLife();
      if (state != PLAY) return;
    }
  }
}

// ---------------- rendering ----------------
static void drawShip(int x, int y, bool flame) {
  if (flame) {
    int len = 4 + rnd(4);
    spr.fillTriangle(x - 3, y + SHIP_H - 1, x + 3, y + SHIP_H - 1, x, y + SHIP_H - 1 + len, FLAME);
  }
  spr.fillTriangle(x, y, x - 7, y + SHIP_H - 2, x + 7, y + SHIP_H - 2, SHIP);       // hull
  spr.fillTriangle(x - 7, y + SHIP_H - 2, x - 7, y + 6, x - 3, y + SHIP_H - 2, 0x8410);   // wings
  spr.fillTriangle(x + 7, y + SHIP_H - 2, x + 7, y + 6, x + 3, y + SHIP_H - 2, 0x8410);
  spr.fillRect(x - 3, y + SHIP_H - 2, 7, 2, 0x8410);
  spr.fillEllipse(x, y + 7, 2, 3, COCKPIT);
}

static void drawSaucer(int x, int y, uint16_t body) {
  spr.fillCircle(x, y - 2, 3, 0x7FFF);                  // dome
  spr.fillEllipse(x, y, 7, 3, body);
  for (int k = -4; k <= 4; k += 4) spr.drawPixel(x + k, y, TFT_YELLOW);
}

static void drawCruiser(int x, int y, uint16_t body) {
  spr.fillTriangle(x - 10, y - 2, x - 6, y - 6, x - 6, y + 5, body);   // wings
  spr.fillTriangle(x + 10, y - 2, x + 6, y - 6, x + 6, y + 5, body);
  spr.fillRoundRect(x - 6, y - 6, 12, 12, 3, body);
  spr.fillRect(x - 4, y - 2, 3, 3, TFT_YELLOW);         // eyes
  spr.fillRect(x + 2, y - 2, 3, 3, TFT_YELLOW);
  spr.fillRect(x - 2, y + 4, 5, 3, 0x7800);             // gun
}

static void drawGame() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  for (const Star &s : stars) spr.drawPixel((int)s.x, (int)s.y, s.col);

  for (const Enemy &e : enemies) {
    if (!e.on) continue;
    if (e.type == 0) drawSaucer((int)e.x, (int)e.y, e.hitFlash ? TFT_WHITE : TFT_MAGENTA);
    else drawCruiser((int)e.x, (int)e.y, e.hitFlash ? TFT_WHITE : TFT_RED);
  }
  for (const Shot &s : shots)
    if (s.on) spr.fillRect((int)s.x - 1, (int)s.y - 3, 2, 6, TFT_YELLOW);
  for (const Bomb &b : bombs)
    if (b.on) spr.fillCircle((int)b.x, (int)b.y, 2, TFT_ORANGE);

  bool blink = invuln && (invuln / 4) % 2;
  if (state == PLAY && !blink) drawShip((int)px, SHIP_Y, true);

  for (const Boom &b : booms) {
    if (!b.on) continue;
    int r = b.size * b.t / 16 + 2;
    uint16_t col = tft.alphaBlend(255 - b.t * 14, TFT_ORANGE, TFT_RED);
    spr.drawCircle(b.x, b.y, r, col);
    if (b.t < 8) spr.fillCircle(b.x, b.y, r / 2, TFT_YELLOW);
  }

  drawScoreHeader(score, hiScore);
  for (int i = 0; i < lives; i++) {                      // spare ships
    int x = 127 - i * 12, y = 4;
    spr.fillTriangle(x, y, x - 5, y + 9, x + 5, y + 9, SHIP);
  }
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof buf, "Wave %d", wave() + 1);
  spr.drawString(buf, 133, 24, 1);

  if (state == GAMEOVER && millis() - deadAt > 600) drawGameOverBox("GAME OVER", score, hiScore, newHigh);
}

static void drawTitle() {
  char buf[32];
  spr.fillSprite(TFT_BLACK);
  for (const Star &s : stars) spr.drawPixel((int)s.x, (int)s.y, s.col);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("SHOOTER", 67, 14, 4);
  drawSaucer(40, 54, TFT_MAGENTA);
  drawCruiser(95, 56, TFT_RED);
  drawShip(67, 66, true);

  spr.setTextDatum(TL_DATUM);
  const char *keys[] = {"Hold L", "Hold R"};
  const char *acts[] = {"fly left", "fly right"};
  for (int i = 0; i < 2; i++) {
    spr.setTextColor(TFT_YELLOW);
    spr.drawString(keys[i], 8, 98 + i * 20, 2);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(acts[i], 60, 98 + i * 20, 2);
  }
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString("Your ship fires itself", 67, 146, 1);
  spr.drawString("Red ships shoot back", 67, 158, 1);

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
  spr.fillEllipse(x + 4, y - 9, 6, 3, c(TFT_MAGENTA));
  spr.fillRect(x - 1, y - 3, 2, 5, c(TFT_YELLOW));
  spr.fillTriangle(x - 4, y + 3, x - 11, y + 15, x + 3, y + 15, c(SHIP));
}

void begin() {
  initStars();
  hiScore = prefs.getUInt("shooter_hi", 0);
  state = TITLE;
  armed = false;
  lastInput = millis();
  dirty = true;
}

void update(uint32_t now) {
  int eL = updateBtn(bL, now);
  int eR = updateBtn(bR, now);
  if (eL || eR || bL.stable || bR.stable) lastInput = now;

  if (state != PLAY && (state == TITLE || now - deadAt > 600))   // let the explosion play out
    if (startOrEnd(state == GAMEOVER, eL, eR, armed) == END_PLAY) newGame();

  if (state != TITLE) {                                  // stars and explosions keep moving after death
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

}  // namespace shooter
