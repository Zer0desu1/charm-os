// ============================================================================
// mod_badge.h - Module 7: Status Badge (Durum Rozeti)
//   Wearable status: MUSAIT / MESGUL / TOPLANTIDA + a name line.
//   Animated colored ring. Set from phone (/badge?state=&text=) or tap to cycle.
// ============================================================================
#pragma once

#include "platform.h"

enum BadgeState { BS_FREE, BS_BUSY, BS_MEET, BS_COUNT };
static const char* BS_TEXT[]  = {"MUSAIT", "MESGUL", "TOPLANTIDA"};
static int badge_state = BS_FREE;

static void badgeSetState(int s) { badge_state = constrain(s, 0, BS_COUNT - 1); setPutInt("bdg_state", badge_state); }
static void badgeSetName(const String& n) { setPutStr("bdg_name", n.substring(0, 16)); }

static void badge_enter() { badge_state = setGetInt("bdg_state", BS_FREE); }

static uint16_t badgeColor(int s) {
  switch (s) { case BS_FREE: return C_OK; case BS_BUSY: return C_DANGER; default: return C_WARN; }
}

static void badge_tick() {
  if (g_g.tap) badgeSetState((badge_state + 1) % BS_COUNT);

  uint16_t col = badgeColor(badge_state);
  gfx->fillScreen(C_BG);
  // animated ring (rotating gap)
  float t = millis() * 0.12f;
  float start = fmodf(t, 360.0f);
  fillRing(CXi, CYi, 188, 208, 0, 360, C_BG2);
  fillRing(CXi, CYi, 188, 208, start, start + 300, col);

  gfx->fillCircle(CXi, CYi, 150, C_BG);
  gfx->drawCircle(CXi, CYi, 150, col);

  String name = setGetStr("bdg_name", "");
  textCenter(BS_TEXT[badge_state], CXi, CYi - 10, badge_state == BS_MEET ? 3 : 4, col);
  if (name.length()) textCenter(name.c_str(), CXi, CYi + 55, 2, C_TEXT);
  textCenter("dokun: degistir", CXi, 430, 2, C_DIM);
  present();
  delay(30);
}
