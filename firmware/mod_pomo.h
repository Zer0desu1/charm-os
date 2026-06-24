// ============================================================================
// mod_pomo.h - Pomodoro (25 work / 5 break) with session counter.
//   Tap = start/pause. Long-press = home (handled by launcher). When a phase
//   ends the device flashes and auto-advances; daily completed sessions are
//   stored in NVS.
// ============================================================================
#pragma once

#include "platform.h"

static int  pomo_workMin  = 25;
static int  pomo_breakMin = 5;
static bool pomo_inBreak  = false;
static bool pomo_running  = false;
static uint32_t pomo_endMs = 0;
static int  pomo_remain  = 25 * 60;
static int  pomo_doneToday = 0;
static int  pomo_dayKey    = 0;

static void pomoLoadDay() {
  pomo_doneToday = setGetInt("pm_n", 0);
  // reset the daily session count when a new day rolls over. timeDay() (epoch
  // day number) lives in mod_streak.h, included before this one.
  uint32_t today = timeDay();
  int stored = setGetInt("pm_day", 0);
  if (today != 0 && (int)today != stored) {
    pomo_doneToday = 0;
    setPutInt("pm_n", 0);
    setPutInt("pm_day", (int)today);
  }
}
static void pomoSaveDay() {
  setPutInt("pm_n", pomo_doneToday);
  uint32_t today = timeDay();
  if (today != 0) setPutInt("pm_day", (int)today);
}

static void pomo_enter() {
  pomoLoadDay();
  pomo_inBreak = false; pomo_running = false;
  pomo_remain = pomo_workMin * 60;
}

static void pomoRender() {
  gfx->fillScreen(C_BG);
  textCenter(pomo_inBreak ? "MOLA" : "ODAK", CXi, 50, 2,
             pomo_inBreak ? C_OK : C_ACCENT);

  int total = (pomo_inBreak ? pomo_breakMin : pomo_workMin) * 60;
  float frac = total > 0 ? (float)pomo_remain / total : 0;
  frac = constrain(frac, 0.0f, 1.0f);
  fillRing(CXi, CYi, 165, 205, 0, 360, C_BG2);
  uint16_t col = pomo_inBreak ? C_OK : (frac < 0.25f ? C_WARN : C_ACCENT);
  if (frac > 0) fillRing(CXi, CYi, 165, 205, 0, 360 * frac, col);

  int m = pomo_remain / 60, s = pomo_remain % 60;
  char b[8]; snprintf(b, sizeof(b), "%02d:%02d", m, s);
  textCenter(b, CXi, CYi, 7, C_TEXT);

  const char* hint = pomo_running ? "dokun: duraklat" : "dokun: basla";
  textCenter(hint, CXi, CYi + 90, 2, C_DIM);

  char dn[24]; snprintf(dn, sizeof(dn), "bugun: %d oturum", pomo_doneToday);
  textCenter(dn, CXi, 430, 2, C_DIM);
  present();
}

static void pomo_tick() {
  if (g_g.tap) {
    if (pomo_running) { pomo_running = false; }
    else { pomo_running = true; pomo_endMs = millis() + (uint32_t)pomo_remain * 1000; }
  }
  if (pomo_running) {
    int rem = (int)((long)(pomo_endMs - millis()) / 1000) + 1;
    if (rem < 0) rem = 0;
    pomo_remain = rem;
    if (pomo_remain <= 0) {
      pomo_running = false; pomo_remain = 0;
      // flash + advance phase
      for (int i = 0; i < 4; i++) {
        gfx->fillScreen(pomo_inBreak ? C_OK : C_GOLD); present(); delay(120);
        gfx->fillScreen(C_BG); present(); delay(120);
      }
      if (!pomo_inBreak) { pomo_doneToday++; pomoSaveDay(); }
      pomo_inBreak = !pomo_inBreak;
      pomo_remain = (pomo_inBreak ? pomo_breakMin : pomo_workMin) * 60;
    }
  }
  pomoRender();
  delay(80);
}
