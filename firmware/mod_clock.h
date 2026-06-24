// ============================================================================
// mod_clock.h - Module: Clock (Saat) with a WATCH-FACE GALLERY
//   Several selectable faces on the round display. Time is synced from the
//   phone (/time endpoint) since there is no RTC/internet in AP mode.
//     Tap            = next face (cycles)
//     /face?set=N    = pick a face from the phone (face "store")
//   Faces: 0 Analog, 1 Dijital, 2 Sade (minimal), 3 Buyuk, 4 Fit (steps+pil).
//   Long-press = back to launcher (handled globally).
// ============================================================================
#pragma once

#include "platform.h"
#include "mpu.h"      // Fit face: today's step count

// --- face registry (exposed to net.h /face) ---
#define CLK_FACES 5
static const char* const CLK_FACE_NAMES[CLK_FACES] = {
  "Analog", "Dijital", "Sade", "Buyuk", "Fit"
};

static int clock_face = 0;

static int  clockFace()           { return clock_face; }
static void clockSetFace(int f) {
  clock_face = ((f % CLK_FACES) + CLK_FACES) % CLK_FACES;   // wrap negatives too
  setPutInt("clk_face", clock_face);
}

static void clock_enter() {
  // migrate the old "clk_style" (0 analog / 1 digital) on first run
  clock_face = setGetInt("clk_face", setGetInt("clk_style", 0));
  if (clock_face < 0 || clock_face >= CLK_FACES) clock_face = 0;
}

static void drawHand(float angleDeg, int len, int w, uint16_t col) {
  float a = angleDeg * DEG_TO_RAD;
  int x = CXi + (int)(len * sinf(a));
  int y = CYi - (int)(len * cosf(a));
  gfx->drawLine(CXi, CYi, x, y, col);
  // thickness
  gfx->drawLine(CXi + 1, CYi, x + 1, y, col);
  gfx->drawLine(CXi, CYi + 1, x, y + 1, col);
  if (w > 2) { gfx->drawLine(CXi - 1, CYi, x - 1, y, col); gfx->drawLine(CXi, CYi - 1, x, y - 1, col); }
}

// ---- 0: Analog ----
static void drawAnalog() {
  gfx->fillScreen(C_BG);
  gfx->drawCircle(CXi, CYi, 210, C_BG2);
  for (int i = 0; i < 12; i++) {
    float a = i * 30 * DEG_TO_RAD;
    int r1 = 196, r2 = (i % 3 == 0) ? 170 : 184;
    uint16_t col = (i % 3 == 0) ? C_TEXT : C_DIM;
    gfx->drawLine(CXi + (int)(r1 * sinf(a)), CYi - (int)(r1 * cosf(a)),
                  CXi + (int)(r2 * sinf(a)), CYi - (int)(r2 * cosf(a)), col);
  }
  int h = timeHour(), m = timeMin(), s = timeSec();
  float hAng = (h % 12) * 30 + m * 0.5f;
  float mAng = m * 6 + s * 0.1f;
  float sAng = s * 6;
  drawHand(hAng, 110, 4, C_TEXT);
  drawHand(mAng, 165, 3, C_TEXT);
  drawHand(sAng, 180, 1, C_DANGER);
  gfx->fillCircle(CXi, CYi, 8, C_GOLD);
  present();
}

// Subtle top complication: tiny battery icon (so a face still shows charge at a
// glance, like a real watch). Skipped on Fit (it has the battery ring already).
static void clockBattery() {
  if (batteryPct() >= 0) drawBatteryIcon(CXi - 13, 56);
}

// ---- 1: Dijital ----
static void drawDigital() {
  gfx->fillScreen(C_BG);
  clockBattery();
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", timeHour(), timeMin());
  textCenter(b, CXi, CYi - 10, 9, C_TEXT);
  char sb[4]; snprintf(sb, sizeof(sb), "%02d", timeSec());
  textCenter(sb, CXi, CYi + 90, 3, C_ACCENT);
  present();
}

// ---- 2: Sade (minimal) ----  no seconds, thin dim ticks, calm
static void drawMinimal() {
  gfx->fillScreen(C_BG);
  clockBattery();
  for (int i = 0; i < 12; i++) {                 // four cardinal ticks only
    if (i % 3) continue;
    float a = i * 30 * DEG_TO_RAD;
    gfx->fillCircle(CXi + (int)(196 * sinf(a)), CYi - (int)(196 * cosf(a)), 3, C_DIM);
  }
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", timeHour(), timeMin());
  textCenter(b, CXi, CYi, 7, C_TEXT);
  present();
}

// ---- 3: Buyuk ----  hours over minutes, oversized
static void drawBig() {
  gfx->fillScreen(C_BG);
  clockBattery();
  char hb[4], mb[4];
  snprintf(hb, sizeof(hb), "%02d", timeHour());
  snprintf(mb, sizeof(mb), "%02d", timeMin());
  textCenter(hb, CXi, CYi - 70, 11, C_TEXT);
  textCenter(mb, CXi, CYi + 78, 11, C_ACCENT);
  present();
}

// ---- 4: Fit ----  time + battery ring (outer) + step-goal ring (inner)
#define CLK_STEP_GOAL 8000
static void drawFit() {
  gfx->fillScreen(C_BG);
  // outer ring = battery
  int p = batteryPct(); if (p < 0) p = 0;
  gfx->drawCircle(CXi, CYi, 210, C_BG2);
  fillRing(CXi, CYi, 200, 210, 0, 360.0f * p / 100.0f,
           p < 20 ? C_DANGER : (p < 40 ? C_WARN : C_OK));
  // inner ring = progress toward the daily step goal
  uint32_t steps = motionPresent() ? motionStepsToday() : 0;
  float gp = (float)steps / CLK_STEP_GOAL; if (gp > 1.0f) gp = 1.0f;
  gfx->drawCircle(CXi, CYi, 186, C_BG2);
  fillRing(CXi, CYi, 178, 186, 0, 360.0f * gp, gp >= 1.0f ? C_OK : C_GOLD);
  // time
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", timeHour(), timeMin());
  textCenter(b, CXi, CYi - 40, 7, C_TEXT);
  // steps today
  char sb[24];
  if (motionPresent()) {
    snprintf(sb, sizeof(sb), "%lu adim", (unsigned long)steps);
    textCenter(sb, CXi, CYi + 50, 3, C_GOLD);
  } else {
    textCenter("IMU yok", CXi, CYi + 50, 2, C_DIM);
  }
  char pb[8]; snprintf(pb, sizeof(pb), "%%%d", p);
  textCenter(pb, CXi, CYi + 110, 2, C_DIM);
  present();
}

static void clock_tick() {
  if (g_g.tap) { clockSetFace(clock_face + 1); soundTap(); }
  if (!timeValid()) {
    gfx->fillScreen(C_BG);
    textCenter("SAAT", CXi, CYi - 40, 4, C_GOLD);
    textCenter("Telefondan", CXi, CYi + 20, 2, C_DIM);
    textCenter("senkronize et", CXi, CYi + 50, 2, C_DIM);
    present();
    delay(200);
    return;
  }
  switch (clock_face) {
    case 1:  drawDigital(); break;
    case 2:  drawMinimal(); break;
    case 3:  drawBig();     break;
    case 4:  drawFit();     break;
    default: drawAnalog();  break;
  }
  delay(200);
}
