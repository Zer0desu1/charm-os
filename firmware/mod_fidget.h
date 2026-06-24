// ============================================================================
// mod_fidget.h - Module 9: Breathing & Fidget (Nefes & Fidget)
//   Breathing guide: circle expands ("Nefes Al"), holds, contracts ("Ver").
//   Tap anywhere spawns a satisfying ripple (fidget). Pure on-device.
// ============================================================================
#pragma once

#include "platform.h"

// breathing cycle: 4s in, 2s hold, 4s out, 1s hold = 11s
static const uint32_t BR_IN = 4000, BR_HOLD1 = 2000, BR_OUT = 4000, BR_HOLD2 = 1000;
static const uint32_t BR_TOTAL = BR_IN + BR_HOLD1 + BR_OUT + BR_HOLD2;

struct Ripple { int x, y; uint32_t born; bool active; };
#define N_RIPPLE 6
static Ripple fidget_r[N_RIPPLE];
static uint32_t fidget_start = 0;

static void fidget_enter() {
  fidget_start = millis();
  for (int i = 0; i < N_RIPPLE; i++) fidget_r[i].active = false;
}

static void spawnRipple(int x, int y) {
  for (int i = 0; i < N_RIPPLE; i++) {
    if (!fidget_r[i].active) { fidget_r[i] = {x, y, millis(), true}; return; }
  }
  fidget_r[0] = {x, y, millis(), true};  // overwrite oldest-ish
}

static void fidget_tick() {
  if (g_g.tap) spawnRipple(g_g.x, g_g.y);

  uint32_t e = (millis() - fidget_start) % BR_TOTAL;
  float scale; const char* label; uint16_t col;
  if (e < BR_IN) {
    scale = (float)e / BR_IN;            // 0->1 grow
    label = "Nefes Al"; col = C_OK;
  } else if (e < BR_IN + BR_HOLD1) {
    scale = 1.0f; label = "Tut"; col = C_GOLD;
  } else if (e < BR_IN + BR_HOLD1 + BR_OUT) {
    scale = 1.0f - (float)(e - BR_IN - BR_HOLD1) / BR_OUT;  // 1->0 shrink
    label = "Ver"; col = C_ACCENT;
  } else {
    scale = 0.0f; label = "Tut"; col = C_DIM;
  }
  // ease
  scale = scale * scale * (3 - 2 * scale);
  int r = 40 + (int)(160 * scale);

  gfx->fillScreen(C_BG);
  // guide rings
  gfx->drawCircle(CXi, CYi, 200, C_BG2);
  gfx->fillCircle(CXi, CYi, r, col);
  gfx->fillCircle(CXi, CYi, max(0, r - 14), C_BG);
  gfx->fillCircle(CXi, CYi, max(0, r - 14), col);

  // ripples
  for (int i = 0; i < N_RIPPLE; i++) {
    if (!fidget_r[i].active) continue;
    uint32_t age = millis() - fidget_r[i].born;
    if (age > 900) { fidget_r[i].active = false; continue; }
    int rr = (int)(age * 0.18f);
    gfx->drawCircle(fidget_r[i].x, fidget_r[i].y, rr, C_GOLD);
    gfx->drawCircle(fidget_r[i].x, fidget_r[i].y, rr + 1, C_WARN);
  }

  textCenter(label, CXi, CYi, 3, C_BG);
  textCenter("dokun: fidget", CXi, 435, 2, C_DIM);
  present();
  delay(24);
}
