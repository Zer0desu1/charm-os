// ============================================================================
// onboarding.h - First-boot setup wizard (runs once, gated by NVS "onboarded")
//   On a fresh device the 36-module launcher is overwhelming, so the first boot
//   walks the user through:
//     1) Welcome
//     2) Pick a starter PROFILE -> enables a curated module set (others off)
//     3) Finish + Wi-Fi QR (rename / language from the phone app)
//   Re-runnable from the phone via /onboard?reset=1 (clears the flag + reboots).
//   Touch driven, with the BOOT button as a fallback (advances / picks first).
// ============================================================================
#pragma once

#include "platform.h"

// Curated starter profiles. The empty list means "enable everything".
struct OnbProfile {
  const char* name;
  const char* desc;
  const char* ids;     // comma-separated module ids; "" = all modules
};
static const OnbProfile ONB_PROFILES[] = {
  { "Hepsi",   "Tum moduller acik",        "" },
  { "Sade",    "Saat, bildirim, hava",     "clock,notify,weather,music,settings" },
  { "Fitness", "Adim, streak, pomodoro",   "clock,streak,pomo,weather,notify,compass,settings" },
  { "Sosyal",  "Mesaj, ikiz, muzik",       "notify,lovebox,buddy,charm,music,clock,settings" },
};
static const int ONB_PROFILE_N = sizeof(ONB_PROFILES) / sizeof(ONB_PROFILES[0]);

// Apply a profile: empty ids -> enable all; otherwise enable listed, disable rest.
static void onbApplyProfile(const char* ids) {
  if (!ids || !ids[0]) {
    for (int i = 0; i < g_moduleCount; i++) moduleSetEnabled(g_modules[i].id, true);
    return;
  }
  String want = String(",") + ids + ",";
  for (int i = 0; i < g_moduleCount; i++) {
    bool on = want.indexOf(String(",") + g_modules[i].id + ",") >= 0;
    moduleSetEnabled(g_modules[i].id, on);
  }
}

// 3-stage progress dots at the bottom (centered, inside the round panel).
static void onbDots(int stage) {
  const int n = 3, gap = 26, y = 448;
  int x0 = CXi - (n - 1) * gap / 2;
  for (int i = 0; i < n; i++)
    gfx->fillCircle(x0 + i * gap, y, i == stage ? 6 : 4, i == stage ? C_GOLD : C_BG2);
}

static void onbWelcome() {
  gfx->fillScreen(C_BG);
  textCenter("Vecta'ya", CXi, CYi - 70, 4, C_GOLD);
  textCenter("hos geldin", CXi, CYi - 24, 4, C_GOLD);
  textCenter("Kurulum icin dokun", CXi, CYi + 60, 2, C_DIM);
  onbDots(0);
  present();
}

// Profile cards: a vertically CENTERED list with centered text, so nothing is
// clipped by the round display (full-width left-aligned text hit the edges).
static const int ONB_CARD_H = 62, ONB_CARD_GAP = 12;
static const int ONB_CARD_Y0 =
    (LCD_H - (ONB_PROFILE_N * ONB_CARD_H + (ONB_PROFILE_N - 1) * ONB_CARD_GAP)) / 2 + 8;
static void onbProfilesScreen() {
  gfx->fillScreen(C_BG);
  textCenter("Bir profil sec", CXi, 44, 3, C_TEXT);
  const int w = LCD_W - 120, x = (LCD_W - w) / 2;
  for (int i = 0; i < ONB_PROFILE_N; i++) {
    int y = ONB_CARD_Y0 + i * (ONB_CARD_H + ONB_CARD_GAP);
    gfx->fillRoundRect(x, y, w, ONB_CARD_H, 14, C_BG2);
    textCenter(ONB_PROFILES[i].name, CXi, y + 12, 2, C_ACCENT);
    textCenter(ONB_PROFILES[i].desc, CXi, y + 38, 1, C_DIM);
  }
  onbDots(1);
  present();
}

static void onbFinish() {
  gfx->fillScreen(C_BG);
  textCenter("Hazir!", CXi, 44, 4, C_OK);
  if (qrAvailable() && g_wifiQR[0]) {
    qrDraw(g_wifiQR, CXi, 210, 7);
    textCenter("Wi-Fi'a baglanmak icin okut", CXi, 356, 2, C_DIM);
  } else {
    textCenter("Telefon uygulamasini ac", CXi, 200, 2, C_TEXT);
  }
  textCenter("Ad ve dil: uygulamadan", CXi, 392, 2, C_DIM);
  textCenter("dokun: basla", CXi, 420, 2, C_GOLD);
  onbDots(2);
  present();
}

// Runs the wizard if not yet onboarded. Call AFTER modules are registered and
// the display/touch are up. Blocks until finished (no auto-timeout: it's once).
static void onboardingRun() {
  if (setGetBool("onboarded", false)) return;

  int stage = 0;                 // 0 welcome, 1 profiles, 2 finish
  bool redraw = true;
  while (true) {
    gestureUpdate();
    if (redraw) {
      if (stage == 0) onbWelcome();
      else if (stage == 1) onbProfilesScreen();
      else onbFinish();
      redraw = false;
    }
    bool isButton = !g_touchOk && g_g.tap;   // BOOT fallback (no real coords)

    if (stage == 0) {
      if (g_g.tap) { stage = 1; redraw = true; soundTap(); }
    } else if (stage == 1) {
      if (g_g.tap) {
        int pick = -1;
        if (isButton) {
          pick = 0;                            // BOOT picks the first profile
        } else {
          const int w = LCD_W - 120, x = (LCD_W - w) / 2;
          for (int i = 0; i < ONB_PROFILE_N; i++) {
            int y = ONB_CARD_Y0 + i * (ONB_CARD_H + ONB_CARD_GAP);
            if (g_g.x >= x && g_g.x <= x + w && g_g.y >= y && g_g.y <= y + ONB_CARD_H) { pick = i; break; }
          }
        }
        if (pick >= 0) { onbApplyProfile(ONB_PROFILES[pick].ids); soundOk(); stage = 2; redraw = true; }
      }
    } else {  // finish
      if (g_g.tap) { setPutBool("onboarded", true); soundOk(); return; }
    }
    delay(30);
  }
}
