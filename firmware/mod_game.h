// ============================================================================
// mod_game.h - Module 4: Board Game tools
//   Sub-tools: DICE (d6/d20, 1-2), SPINNER (2-8 slots), TIMER (circular),
//   COIN (heads/tails + tally).
//   Tap = action. Long-press handled by launcher (returns home).
//   Tool + config selectable via net (/mode) or on-device: tap top edge
//   cycles the sub-tool.
// ============================================================================
#pragma once

#include "platform.h"
#include "mpu.h"

enum GameTool { GT_DICE, GT_SPINNER, GT_TIMER, GT_COIN, GT_COUNT };
static const char* GT_NAMES[] = {"ZAR", "CARK", "SAYAC", "YAZI-TURA"};

// Swipe gestures change things:
//   vertical (up/down)  -> change the game (tool)
//   horizontal (l/r)    -> change the current tool's variant (die type, slots, timer)
static const int DICE_SIDES[]    = {4, 6, 8, 10, 12, 20};   // d4..d20
static const int DICE_N          = sizeof(DICE_SIDES) / sizeof(DICE_SIDES[0]);
static const int TIMER_PRESETS[] = {10, 30, 60, 180, 300, 600};
static const int TIMER_N         = sizeof(TIMER_PRESETS) / sizeof(TIMER_PRESETS[0]);

static GameTool game_tool = GT_DICE;
static int  game_diceSides = 6;   // 6 or 20
static int  game_diceCount = 1;   // 1 or 2
static int  game_spinSlots = 6;   // 2..8
static int  game_timerSecs = 30;

// dynamic state
static int   dice_vals[2] = {1, 1};
static int   spin_winner = -1;
static float spin_offset = 0;     // wheel rotation (deg)
static bool  timer_running = false;
static bool  timer_done = false;
static uint32_t timer_endMs = 0;
static int   timer_remain = 0;
static int   coin_face = 0;        // 0 = YAZI, 1 = TURA
static int   coin_heads = 0, coin_tails = 0;
static bool  game_needRender = true;

// ----- config setters (called by net) -----
static void gameSetTool(GameTool t) { game_tool = t; game_needRender = true; timer_running = false; timer_done = false; }
static void gameConfig(int sides, int count, int slots, int secs) {
  if (sides > 0) {                          // snap to a supported die (d4..d20)
    game_diceSides = 6;
    for (int i = 0; i < DICE_N; i++) if (DICE_SIDES[i] == sides) game_diceSides = sides;
  }
  if (count > 0) game_diceCount = (count >= 2) ? 2 : 1;
  if (slots > 0) game_spinSlots = constrain(slots, 2, 8);
  if (secs > 0)  game_timerSecs = constrain(secs, 5, 3600);
  game_needRender = true;
}

// ---- swipe-driven selectors ----
// Cycle the die type through DICE_SIDES (dir = +1 right / -1 left).
static void gameCycleDice(int dir) {
  int idx = 1;                              // default position -> d6
  for (int i = 0; i < DICE_N; i++) if (DICE_SIDES[i] == game_diceSides) idx = i;
  idx = (idx + dir + DICE_N) % DICE_N;
  game_diceSides = DICE_SIDES[idx];
  dice_vals[0] = dice_vals[1] = 1;
}
static void gameCycleTimer(int dir) {
  int idx = 1;
  for (int i = 0; i < TIMER_N; i++) if (TIMER_PRESETS[i] == game_timerSecs) idx = i;
  idx = (idx + dir + TIMER_N) % TIMER_N;
  game_timerSecs = TIMER_PRESETS[idx];
  timer_remain = game_timerSecs; timer_running = false; timer_done = false;
}
static void gameCycleSpinner(int dir) {
  game_spinSlots += dir;
  if (game_spinSlots < 2) game_spinSlots = 8;
  if (game_spinSlots > 8) game_spinSlots = 2;
  spin_winner = -1;
}

// ---------------- DICE ----------------
static void drawDie(int cx, int cy, int sz, int val, int sides) {
  int r = sz / 2;
  gfx->fillRoundRect(cx - r, cy - r, sz, sz, sz / 6, C_TEXT);
  gfx->drawRoundRect(cx - r, cy - r, sz, sz, sz / 6, C_DIM);
  if (sides == 6) {
    int p = sz / 4, d = sz / 10;
    uint16_t col = rgb(30, 30, 40);
    auto pip = [&](int gx, int gy) { gfx->fillCircle(cx + gx, cy + gy, d, col); };
    if (val == 1 || val == 3 || val == 5) pip(0, 0);
    if (val >= 2) { pip(-p, -p); pip(p, p); }
    if (val >= 4) { pip(p, -p); pip(-p, p); }
    if (val == 6) { pip(-p, 0); pip(p, 0); }
  } else {
    char b[4]; snprintf(b, sizeof(b), "%d", val);
    textCenter(b, cx, cy, sz / 22, rgb(30, 30, 40));
  }
}

static void renderDice() {
  gfx->fillScreen(C_BG);
  char dl[6]; snprintf(dl, sizeof(dl), "d%d", game_diceSides);
  textCenter(dl, CXi, 40, 2, C_GOLD);
  if (game_diceCount == 1) {
    drawDie(CXi, CYi, 220, dice_vals[0], game_diceSides);
  } else {
    drawDie(CXi - 95, CYi, 150, dice_vals[0], game_diceSides);
    drawDie(CXi + 95, CYi, 150, dice_vals[1], game_diceSides);
    char b[8]; snprintf(b, sizeof(b), "= %d", dice_vals[0] + dice_vals[1]);
    textCenter(b, CXi, CYi + 150, 3, C_GOLD);
  }
  textCenter(motionPresent() ? "salla / dokun: at" : "dokun: at", CXi, 430, 2, C_DIM);
  present();
}

static void rollDice() {
  for (int i = 0; i < 14; i++) {
    dice_vals[0] = 1 + rnd(game_diceSides);
    dice_vals[1] = 1 + rnd(game_diceSides);
    renderDice();
    soundPlay(600 + rnd(500), 14);   // tumbling click (ungated - always plays)
    delay(40 + i * 8);               // decelerate
  }
  soundPlay(1100, 90);               // landed
}

// ---------------- SPINNER ----------------
static const uint16_t SLOT_COLORS[] = {
  rgb(239,68,68), rgb(245,158,11), rgb(34,197,94), rgb(59,130,246),
  rgb(168,85,247), rgb(236,72,153), rgb(20,184,166), rgb(250,204,21)
};

static void renderSpinner(int highlight) {
  gfx->fillScreen(C_BG);
  int R = 205;
  float per = 360.0f / game_spinSlots;
  for (int i = 0; i < game_spinSlots; i++) {
    float a0 = i * per + spin_offset;
    float a1 = a0 + per;
    uint16_t col = SLOT_COLORS[i % 8];
    if (highlight == i) col = C_TEXT;
    fillSector(CXi, CYi, R, a0, a1, col);
    // label
    float mid = (a0 + a1) * 0.5f * DEG_TO_RAD;
    int lx = CXi + (int)(R * 0.7f * sinf(mid));
    int ly = CYi - (int)(R * 0.7f * cosf(mid));
    char b[4]; snprintf(b, sizeof(b), "%d", i + 1);
    textCenter(b, lx, ly, 3, (highlight == i) ? C_BG : C_TEXT);
  }
  gfx->fillCircle(CXi, CYi, 28, C_BG2);
  gfx->drawCircle(CXi, CYi, 28, C_TEXT);
  // pointer at top
  gfx->fillTriangle(CXi - 18, 18, CXi + 18, 18, CXi, 58, C_TEXT);
  gfx->fillTriangle(CXi - 14, 18, CXi + 14, 18, CXi, 52, C_DANGER);
  present();
}

static int spinnerWinner() {
  float off = fmodf(spin_offset, 360.0f);
  if (off < 0) off += 360.0f;
  float per = 360.0f / game_spinSlots;
  float topAngle = fmodf(360.0f - off, 360.0f);
  return ((int)(topAngle / per)) % game_spinSlots;
}

static void doSpinVel(float vel) {
  spin_winner = -1;
  int lastSlot = -1;
  while (vel > 0.6f) {
    spin_offset = fmodf(spin_offset + vel, 360.0f);
    renderSpinner(-1);
    int s = spinnerWinner();           // click as the pointer crosses each slot
    if (s != lastSlot) { soundTap(); lastSlot = s; }
    vel *= 0.965f;
    delay(16);
  }
  spin_winner = spinnerWinner();
  renderSpinner(spin_winner);
  soundCelebrate();
}
static void doSpin() { doSpinVel(22 + rnd(14)); }   // tap: random velocity

// ---------------- TIMER ----------------
static void renderTimer() {
  gfx->fillScreen(C_BG);
  float frac = game_timerSecs > 0 ? (float)timer_remain / game_timerSecs : 0;
  frac = constrain(frac, 0.0f, 1.0f);
  // background ring
  fillRing(CXi, CYi, 165, 205, 0, 360, C_BG2);
  uint16_t col = timer_done ? C_DANGER : (frac < 0.25f ? C_WARN : C_OK);
  if (frac > 0) fillRing(CXi, CYi, 165, 205, 0, 360 * frac, col);
  char b[8]; snprintf(b, sizeof(b), "%d", timer_remain);
  textCenter(b, CXi, CYi - 10, 8, timer_done ? C_DANGER : C_TEXT);
  const char* hint = timer_done ? "dokun: sifirla"
                    : timer_running ? "dokun: durdur" : "dokun: basla";
  textCenter(hint, CXi, CYi + 90, 2, C_DIM);
  present();
}

static void timerTick() {
  if (g_g.tap) {
    if (timer_done) {                       // reset
      timer_done = false; timer_running = false; timer_remain = game_timerSecs;
    } else if (timer_running) {             // pause
      timer_running = false;
    } else {                                // start/resume
      timer_running = true;
      timer_endMs = millis() + (uint32_t)timer_remain * 1000;
      soundOk();
    }
    game_needRender = true;
  }
  if (timer_running) {
    int rem = (int)((long)(timer_endMs - millis()) / 1000) + 1;
    if (rem < 0) rem = 0;
    if (rem != timer_remain) { timer_remain = rem; game_needRender = true; }
    if (timer_remain <= 0) {
      timer_remain = 0; timer_running = false; timer_done = true;
      soundError();                         // alarm when time is up
      // flash
      for (int i = 0; i < 4; i++) { gfx->fillScreen(C_DANGER); present(); delay(120);
                                    gfx->fillScreen(C_BG); present(); delay(120); }
      game_needRender = true;
    }
  }
  if (game_needRender) { renderTimer(); game_needRender = false; }
  else delay(20);
}

// ---------------- COIN ----------------
static void renderCoin(int squashH, int face) {
  gfx->fillScreen(C_BG);
  int rx = 150, ry = max(8, squashH);
  uint16_t col = face == 0 ? C_GOLD : rgb(200, 200, 210);
  gfx->fillEllipse(CXi, CYi, rx, ry, col);
  gfx->drawEllipse(CXi, CYi, rx, ry, C_TEXT);
  if (ry > 60) textCenter(face == 0 ? "YAZI" : "TURA", CXi, CYi, 4, C_BG);
  // tally
  char b[24]; snprintf(b, sizeof(b), "Y:%d  T:%d", coin_heads, coin_tails);
  textCenter(b, CXi, 430, 2, C_DIM);
  present();
}

static void flipCoin() {
  for (int i = 0; i < 16; i++) {
    int sq = (int)(150 * fabsf(cosf(i * 0.6f)));
    int f = (i % 2);
    renderCoin(sq, f);
    if (i % 2 == 0) soundTap();        // flipping ticks
    delay(40 + i * 6);
  }
  coin_face = rnd(2);
  renderCoin(150, coin_face);
  soundOk();                           // landed
  if (coin_face == 0) coin_heads++; else coin_tails++;
  renderCoin(150, coin_face);
}

// ---------------- module API ----------------
static void game_enter() {
  game_needRender = true;
  timer_remain = game_timerSecs;
  timer_running = false; timer_done = false;
}

static void game_tick() {
  // vertical swipe -> change the GAME (dice / spinner / timer / coin)
  if (g_g.swipeUp || g_g.swipeDown) {
    int d = g_g.swipeUp ? 1 : (GT_COUNT - 1);
    game_tool = (GameTool)((game_tool + d) % GT_COUNT);
    soundTap();
    game_enter();
    return;
  }
  // horizontal swipe -> change the current game's TYPE/variant (if it has one)
  if (g_g.swipeLeft || g_g.swipeRight) {
    int dir = g_g.swipeRight ? 1 : -1;
    switch (game_tool) {
      case GT_DICE:    gameCycleDice(dir);    soundTap(); break;
      case GT_SPINNER: gameCycleSpinner(dir); soundTap(); break;
      case GT_TIMER:   gameCycleTimer(dir);   soundTap(); break;
      default: break;                         // coin has no variant
    }
    game_needRender = true;
    return;
  }
  // tap on top 14% (y<65) also cycles tool (kept for discoverability)
  if (g_g.tap && g_g.y < 65) {
    game_tool = (GameTool)((game_tool + 1) % GT_COUNT);
    game_enter();
    return;
  }
  // physical motion: a shake/wrist-flick triggers the active tool's action,
  // with intensity (shake magnitude) feeding the spinner's spin velocity.
  bool shake = motionPresent() && motionShakeEvent();
  switch (game_tool) {
    case GT_DICE:
      if (game_needRender) { renderDice(); game_needRender = false; }
      if (g_g.tap || shake) rollDice();
      break;
    case GT_SPINNER:
      if (game_needRender) { renderSpinner(spin_winner); game_needRender = false; }
      if (shake)       doSpinVel(18 + motionShakeMag() * 14);  // harder flick = faster
      else if (g_g.tap) doSpin();
      break;
    case GT_TIMER:
      timerTick();
      break;
    case GT_COIN:
      if (game_needRender) { renderCoin(150, coin_face); game_needRender = false; }
      if (g_g.tap || shake) flipCoin();
      break;
    default: break;
  }
}
