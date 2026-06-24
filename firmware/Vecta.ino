// ============================================================================
// Vecta - modular platform for Waveshare ESP32-S3 Touch AMOLED 1.32
//   A runtime "launcher" with toggleable modules:
//     1) Canta Susu (Bag Charm)      - decorative image/animation/name
//     2) Cevre Tamagotchi            - mic + touch reactive virtual pet
//     4) Board Game                  - dice / spinner / timer / coin
//     6) Ses Gorsellestirici         - circular audio visualizer
//   Modules are enabled/disabled from the phone app (stored in NVS).
//
//   INPUT: touch (CST820) or BOOT button.
//     Launcher: tap LEFT third = prev, RIGHT third = next, CENTER = open.
//               BOOT short = next, BOOT long = open.
//     In a module: short tap = module action; LONG-PRESS = back to launcher.
//
//   See platform.h for pins to VERIFY (touch I2C), audio.h for mic.
//
//   Libraries: GFX Library for Arduino (Arduino_GFX).
//   Board: ESP32S3 Dev Module | PSRAM: OPI PSRAM | Flash: 8MB
// ============================================================================

#include "platform.h"
#include "mod_config.h"   // slim-build: which optional modules get registered
#include "audio.h"
#include "mag.h"
#include "mpu.h"
#include "heading.h"
#include "deadreckon.h"
#include "cloud.h"
#include "mascot_fw.h"
#include "mod_charm.h"
#include "mod_game.h"
#include "mod_tama.h"
#include "mod_audio.h"
#include "mod_collar.h"
#include "mod_clock.h"
#include "mod_badge.h"
#include "mod_notify.h"
#include "mod_fidget.h"
#include "mod_finder.h"
#include "mod_buddy.h"
#include "mod_compass.h"
#include "mod_draw.h"
#include "mod_knob.h"
#include "mod_lovebox.h"
#include "mod_album.h"
#include "mod_maps.h"
#include "mod_weather.h"
#include "mod_rotate.h"
#include "mod_music.h"
#include "mod_streak.h"
#include "mod_pomo.h"
#include "mod_capsule.h"
#include "mod_camera.h"
#include "mod_rps.h"
#include "mod_reflex.h"
#include "mod_ttt.h"
#include "mod_voicenote.h"
#include "mod_settings.h"
#include "mod_xiaozhi.h"
#include "mod_edgeai.h"    // on-device (edge) mic sound recognition - TinyML iskeleti
#include "mod_openclaw.h"
#include "mod_mirror.h"
#include "mod_lyrics.h"
#include "mod_reader.h"    // Hizli Okuma (RSVP) - telefondan gelen PDF metnini kelime kelime gosterir
#ifdef HAVE_BLE_HID
#  include "ble_ams.h"     // iPhone now-playing (AMS) over the HID link
#  include "ble_nav.h"     // Beeline-style nav data over the HID link
#endif
#include "mod_nav.h"       // Beeline turn-by-turn UI (draws ble_nav data)
#include "net.h"
#include "onboarding.h"    // first-boot setup wizard (gated by NVS "onboarded")

// Optional high-quality launcher icons: drop PNGs in tools/icons/<id>.png, run
// tools/png2mask.py to generate icon_masks.h (alpha masks + iconFor()). When
// present they replace the hand-drawn vector icons automatically.
#if __has_include("icon_masks.h")
#  include "icon_masks.h"
#  define HAVE_ICON_MASKS 1
#endif

static bool launcherDirty = true;

// ---- enabled-module helpers ----
static int enabledCount() {
  int c = 0; for (int i = 0; i < g_moduleCount; i++) if (g_modules[i].enabled) c++; return c;
}
static int enabledNth(int n) {     // module index of the n-th enabled module
  int c = 0;
  for (int i = 0; i < g_moduleCount; i++)
    if (g_modules[i].enabled) { if (c == n) return i; c++; }
  return -1;
}

// Device name at top, battery just below it. NOTE: the old top-right position
// (x=410, y=16) was OUTSIDE the visible circle of the round panel (~280px from
// center) so the battery was never actually visible. Centered now.
static void drawTopBar() {
  String dn = deviceName();
  textCenter(dn.c_str(), CXi, 20, 2, C_DIM);
  int p = batteryPct();
  if (p >= 0) {
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", p);
    drawBatteryIcon(CXi - 36, 38);
    uint16_t col = p < 20 ? C_DANGER : C_DIM;
    textCenter(pb, CXi + 24, 44, 2, col);
  }
}

// ---- crisp vector icons per module (replaces the ASCII glyphs) ----
// Centered at (cx,cy), sized to sit inside the focused ~92px circle.
static void drawModuleIcon(const char* id, int cx, int cy, uint16_t col, uint16_t bg = C_BG2) {
#ifdef HAVE_ICON_MASKS
  if (const IconMask* m = iconFor(id)) { drawIconMask(*m, cx, cy, col, bg); return; }
#endif
  #define IS(s) (strcmp(id, s) == 0)
  auto vline = [&](int x,int y0,int y1,uint16_t c){ gfx->fillRect(x-2,y0,4,y1-y0,c); };

  if (IS("charm")) {                                   // heart
    gfx->fillCircle(cx-15,cy-8,15,col); gfx->fillCircle(cx+15,cy-8,15,col);
    gfx->fillTriangle(cx-28,cy-1,cx+28,cy-1,cx,cy+30,col);
  } else if (IS("lovebox")) {                          // envelope + heart
    gfx->fillRoundRect(cx-34,cy-22,68,44,6,col);
    gfx->fillTriangle(cx-34,cy-20,cx+34,cy-20,cx,cy+6,C_BG);
    gfx->fillCircle(cx-7,cy+4,6,C_DANGER); gfx->fillCircle(cx+7,cy+4,6,C_DANGER);
    gfx->fillTriangle(cx-13,cy+6,cx+13,cy+6,cx,cy+20,C_DANGER);
  } else if (IS("tama")) {                             // pet face
    gfx->fillCircle(cx,cy,34,col);
    gfx->fillCircle(cx-12,cy-6,5,C_BG); gfx->fillCircle(cx+12,cy-6,5,C_BG);
    gfx->drawCircle(cx,cy+6,12,C_BG); gfx->fillRect(cx-12,cy-6,24,12,col);
  } else if (IS("collar")) {                           // paw
    gfx->fillCircle(cx,cy+10,16,col);
    gfx->fillCircle(cx-16,cy-6,7,col); gfx->fillCircle(cx-5,cy-14,7,col);
    gfx->fillCircle(cx+6,cy-14,7,col); gfx->fillCircle(cx+16,cy-6,7,col);
  } else if (IS("game")) {                             // dice
    gfx->fillRoundRect(cx-30,cy-30,60,60,12,col);
    uint16_t p=C_BG; gfx->fillCircle(cx-14,cy-14,5,p); gfx->fillCircle(cx+14,cy-14,5,p);
    gfx->fillCircle(cx,cy,5,p); gfx->fillCircle(cx-14,cy+14,5,p); gfx->fillCircle(cx+14,cy+14,5,p);
  } else if (IS("rps")) {                              // scissors (TKM)
    for (int k=-2;k<=2;k++) {
      gfx->drawLine(cx-18+k,cy+10,cx+14+k,cy-26,col);
      gfx->drawLine(cx+18+k,cy+10,cx-14+k,cy-26,col);
    }
    gfx->drawCircle(cx-18,cy+18,9,col); gfx->drawCircle(cx-18,cy+18,8,col);
    gfx->drawCircle(cx+18,cy+18,9,col); gfx->drawCircle(cx+18,cy+18,8,col);
  } else if (IS("clock")) {                            // clock
    gfx->drawCircle(cx,cy,32,col); gfx->drawCircle(cx,cy,31,col);
    gfx->fillRect(cx-2,cy-20,4,22,col); gfx->fillRect(cx,cy-2,18,4,col);
    gfx->fillCircle(cx,cy,4,col);
  } else if (IS("badge")) {                            // shield + check
    gfx->fillRoundRect(cx-26,cy-30,52,36,8,col);
    gfx->fillTriangle(cx-26,cy-2,cx+26,cy-2,cx,cy+32,col);
    for (int k=0;k<4;k++) {
      gfx->drawLine(cx-12,cy-4+k,cx-3,cy+6+k,C_BG);
      gfx->drawLine(cx-3,cy+6+k,cx+13,cy-12+k,C_BG);
    }
  } else if (IS("notify")) {                           // bell
    gfx->fillCircle(cx,cy-8,20,col);                   // dome
    gfx->fillRect(cx-20,cy-8,40,18,col);               // body
    gfx->fillRoundRect(cx-28,cy+8,56,8,4,col);         // rim
    gfx->fillCircle(cx,cy+22,6,col);                   // clapper
    gfx->fillRect(cx-3,cy-34,6,8,col);                 // handle
  } else if (IS("audio")) {                            // equalizer
    vline(cx-21,cy-6,cy+22,col); vline(cx-7,cy-22,cy+22,col);
    vline(cx+7,cy-14,cy+22,col); vline(cx+21,cy-26,cy+22,col);
  } else if (IS("music")) {                            // double note
    gfx->fillRect(cx-14,cy-22,5,36,col); gfx->fillRect(cx+18,cy-28,5,36,col);
    gfx->fillTriangle(cx-14,cy-22,cx+23,cy-28,cx+23,cy-18,col);  // beam
    gfx->fillTriangle(cx-14,cy-22,cx-14,cy-12,cx+23,cy-18,col);
    gfx->fillCircle(cx-16,cy+14,9,col); gfx->fillCircle(cx+16,cy+8,9,col);
  } else if (IS("fidget")) {                           // breathing rings
    gfx->drawCircle(cx,cy,30,col); gfx->drawCircle(cx,cy,20,col); gfx->fillCircle(cx,cy,8,col);
  } else if (IS("finder")) {                           // location pin
    gfx->fillCircle(cx,cy-8,20,col); gfx->fillTriangle(cx-14,cy+2,cx+14,cy+2,cx,cy+28,col);
    gfx->fillCircle(cx,cy-8,8,C_BG);
  } else if (IS("buddy")) {                            // compass
    gfx->drawCircle(cx,cy,32,col); gfx->drawCircle(cx,cy,31,col);
    gfx->fillTriangle(cx,cy-22,cx-9,cy,cx+9,cy,C_DANGER);
    gfx->fillTriangle(cx,cy+22,cx-9,cy,cx+9,cy,col);
  } else if (IS("compass")) {                          // compass rose + needle
    gfx->drawCircle(cx,cy,32,col); gfx->drawCircle(cx,cy,31,col);
    gfx->fillTriangle(cx,cy-24,cx-8,cy,cx+8,cy,C_DANGER);   // north needle
    gfx->fillTriangle(cx,cy+24,cx-8,cy,cx+8,cy,col);        // south tail
    gfx->fillCircle(cx,cy,4,C_BG);
  } else if (IS("draw")) {                             // pencil
    gfx->fillTriangle(cx-26,cy+26,cx-18,cy+18,cx-26,cy+18,col);
    for(int k=-3;k<=3;k++) gfx->drawLine(cx-22+ k,cy+22,cx+22+k,cy-22,col);
    gfx->fillTriangle(cx+18,cy-26,cx+26,cy-18,cx+26,cy-26,C_GOLD);
  } else if (IS("knob")) {                             // dial
    gfx->fillCircle(cx,cy,30,C_BG2); gfx->drawCircle(cx,cy,30,col); gfx->drawCircle(cx,cy,29,col);
    gfx->fillCircle(cx,cy-18,5,col);
  } else if (IS("maps")) {                             // folded map + route
    gfx->fillRoundRect(cx-32,cy-24,64,48,6,col);
    gfx->fillRect(cx-12,cy-24,4,48,C_BG);              // folds
    gfx->fillRect(cx+8,cy-24,4,48,C_BG);
    gfx->fillCircle(cx-22,cy+12,5,C_BG);               // route start/end
    gfx->fillCircle(cx+22,cy-12,5,C_BG);
  } else if (IS("weather")) {                          // sun behind a cloud
    gfx->fillCircle(cx-14,cy-12,12,C_GOLD);            // sun
    gfx->fillCircle(cx-6,cy+6,14,col); gfx->fillCircle(cx+18,cy+6,14,col);
    gfx->fillCircle(cx+4,cy-6,18,col);                 // cloud
    gfx->fillRoundRect(cx-20,cy+4,46,16,8,col);
  } else if (IS("rotate")) {                           // loop arrows (slideshow)
    gfx->drawCircle(cx,cy,26,col); gfx->drawCircle(cx,cy,25,col);
    gfx->fillTriangle(cx+18,cy-30,cx+31,cy-21,cx+15,cy-13,col);  // top arrowhead
    gfx->fillTriangle(cx-18,cy+30,cx-31,cy+21,cx-15,cy+13,col);  // bottom arrowhead
  } else if (IS("album")) {                            // photo
    gfx->drawRect(cx-32,cy-24,64,48,col); gfx->drawRect(cx-31,cy-23,62,46,col);
    gfx->fillCircle(cx-14,cy-8,6,col);
    gfx->fillTriangle(cx-24,cy+22,cx-2,cy-2,cx+12,cy+22,col);
    gfx->fillTriangle(cx+2,cy+22,cx+16,cy+4,cx+30,cy+22,col);
  } else if (IS("streak")) {                           // flame
    gfx->fillTriangle(cx,cy-30,cx-18,cy+8,cx+18,cy+8,C_DANGER);
    gfx->fillCircle(cx,cy+10,18,C_DANGER); gfx->fillCircle(cx,cy+12,9,C_GOLD);
  } else if (IS("pomo")) {                             // tomato timer
    gfx->fillCircle(cx,cy+6,28,C_DANGER);
    gfx->fillTriangle(cx-10,cy-20,cx+10,cy-20,cx,cy-8,C_OK);
  } else if (IS("capsule")) {                          // hourglass
    gfx->fillTriangle(cx-22,cy-26,cx+22,cy-26,cx,cy,col);
    gfx->fillTriangle(cx-22,cy+26,cx+22,cy+26,cx,cy,col);
    gfx->fillRect(cx-26,cy-30,52,5,col); gfx->fillRect(cx-26,cy+25,52,5,col);
  } else if (IS("camera")) {                           // camera
    gfx->fillRoundRect(cx-32,cy-18,64,40,8,col); gfx->fillRect(cx-14,cy-26,20,10,col);
    gfx->fillCircle(cx,cy+2,13,C_BG); gfx->fillCircle(cx,cy+2,8,col);
  } else if (IS("reflex")) {                           // lightning
    gfx->fillTriangle(cx+6,cy-30,cx-18,cy+6,cx+2,cy+6,C_GOLD);
    gfx->fillTriangle(cx-6,cy+30,cx+18,cy-6,cx-2,cy-6,C_GOLD);
  } else if (IS("ttt")) {                              // tic-tac-toe
    gfx->fillRect(cx-10,cy-30,3,60,col); gfx->fillRect(cx+8,cy-30,3,60,col);
    gfx->fillRect(cx-30,cy-10,60,3,col); gfx->fillRect(cx-30,cy+8,60,3,col);
    gfx->drawCircle(cx-20,cy-20,7,C_DANGER);
    gfx->drawLine(cx+12,cy+12,cx+26,cy+26,C_OK); gfx->drawLine(cx+26,cy+12,cx+12,cy+26,C_OK);
  } else if (IS("voicenote")) {                        // studio microphone
    gfx->fillRoundRect(cx-12,cy-30,24,38,12,col);      // capsule
    fillRing(cx,cy-6,17,21,90,270,col);                // cradle arc
    gfx->fillRect(cx-2,cy+14,4,12,col);                // stand
    gfx->fillRect(cx-14,cy+26,28,4,col);               // base
  } else if (IS("settings")) {                         // gear
    for (int i=0;i<8;i++) {
      float a = i * (TWO_PI/8);
      gfx->fillCircle(cx+(int)(25*cosf(a)),cy+(int)(25*sinf(a)),7,col);
    }
    gfx->fillCircle(cx,cy,23,col);
    gfx->fillCircle(cx,cy,10,C_BG);
  } else if (IS("xiaozhi")) {                          // speech bubble + spark
    gfx->fillRoundRect(cx-30,cy-26,60,42,14,col);
    gfx->fillTriangle(cx-12,cy+14,cx+6,cy+14,cx-14,cy+30,col);
    gfx->fillCircle(cx-12,cy-5,4,C_BG); gfx->fillCircle(cx,cy-5,4,C_BG);
    gfx->fillCircle(cx+12,cy-5,4,C_BG);
  } else if (IS("openclaw")) {                         // crab claw
    gfx->fillCircle(cx,cy+8,20,col);
    gfx->fillCircle(cx-16,cy-14,11,col); gfx->fillCircle(cx+16,cy-14,11,col);
    gfx->fillCircle(cx-16,cy-20,7,C_BG); gfx->fillCircle(cx+16,cy-20,7,C_BG);
    gfx->fillRect(cx-3,cy+24,6,8,col);
  } else if (IS("lyrics")) {                           // music note + text lines
    gfx->fillCircle(cx-18,cy+14,9,col); gfx->fillRect(cx-11,cy-22,5,38,col);
    gfx->fillRect(cx-11,cy-22,20,5,col);
    gfx->fillRect(cx+2,cy-2,26,4,col); gfx->fillRect(cx+2,cy+10,20,4,col);
  } else if (IS("reader")) {                           // open book + pivot dot
    gfx->fillRoundRect(cx-30,cy-22,28,46,4,col);       // left page
    gfx->fillRoundRect(cx+2, cy-22,28,46,4,col);       // right page
    gfx->fillRect(cx-2,cy-22,4,46,C_BG);               // spine
    gfx->drawFastHLine(cx-25,cy-10,18,C_BG); gfx->drawFastHLine(cx+7,cy-10,18,C_BG);
    gfx->drawFastHLine(cx-25,cy,   18,C_BG); gfx->drawFastHLine(cx+7,cy,   18,C_BG);
    gfx->fillCircle(cx,cy+34,5,C_DANGER);              // red pivot point
  } else if (IS("nav")) {                              // navigation chevron
    gfx->fillTriangle(cx,cy-26, cx-22,cy+18, cx+22,cy+18, col);
    gfx->fillTriangle(cx,cy+2,  cx-12,cy+24, cx+12,cy+24, C_BG);
  } else if (IS("mirror")) {                           // phone + outgoing beam
    gfx->drawRoundRect(cx-16,cy-26,32,52,8,col);
    gfx->drawRoundRect(cx-15,cy-25,30,50,7,col);
    gfx->fillCircle(cx,cy+18,3,col);
    for (int r=12;r<=28;r+=8) fillRing(cx+18,cy-18,r,r+3,300,360,col);  // yayin dalgalari
  } else if (IS("assistant")) {                        // AI robot head
    gfx->fillRoundRect(cx-30,cy-22,60,46,10,col);
    gfx->fillCircle(cx-12,cy,6,C_BG); gfx->fillCircle(cx+12,cy,6,C_BG);
    gfx->fillRect(cx-2,cy-36,4,12,col); gfx->fillCircle(cx,cy-38,5,C_GOLD);  // antenna
  } else {                                             // fallback: glyph text
    textCenter(id, cx, cy, 3, col);
  }
  #undef IS
}

// ---- launcher render ----  honeycomb (petek) grid, Apple Watch tarzi:
// saf siyah zemin, sade koyu dairelerde dim ikonlar; SADECE merkezdeki ikon
// tek vurgu rengiyle aydinlanir. Dikey kaydirma + balikgozu: kenara yaklasan
// ikon kuculur.
static Scroller g_lScroll;            // dikey dunya-ofseti (px)
static bool     g_lInit = false;
static const float L_STEP   = 1.0f;   // px parmak = px kaydirma (1:1 his)
static const int   HEX_R0   = 60;     // merkezdeki ikonun yaricapi (px)
static const int   HEX_D    = 132;    // komsu ikon merkezleri arasi mesafe
static const float HEX_ROWH = 114.3f; // D * 0.866 (satir yuksekligi)

// Minimal-dark palette: no rainbow. Idle icons sit on a quiet dark surface with
// a dim glyph; only the FOCUSED (centered) icon lights up with the single accent
// colour + white glyph. Calm, modern smartwatch look.
#define L_FACE_IDLE    C_BG2          // idle icon disk
#define L_FACE_FOCUS   C_ACCENT       // centered icon disk
#define L_GLYPH_IDLE   rgb(150,160,185)
#define L_GLYPH_FOCUS  WHITE

// i. modulun petek dunya konumu. ESKI duzen altigen SPIRALDI: dis halkadaki
// moduller yanlara (x=+-264) dusuyordu ve menu sadece DIKEY kaydigi icin o
// moduller hicbir zaman merkeze gelemiyor, hep kenarda minik kaliyordu
// (Pomodoro, Ikiz, Muzik...). Yeni duzen dikey bir petek SERIDI: satirlar
// 2-3-2-3 ikon (x en fazla +-132), boylece HER modul kaydirinca merkezden
// gecer ve buyur. Ilk satir 2 ikon: acilista (3'lu satir ortalanir) ekran
// tam petek gibi 2-3-2 gorunur, ust satir bos kalmaz.
static void hexCell(int i, float& wx, float& wy) {
  int band = i / 5, rem = i % 5;          // 5 ikon = 2 satir (2 + 3)
  int row; float x;
  if (rem < 2) { row = band * 2;     x = (rem == 0) ? -HEX_D/2.0f : HEX_D/2.0f; } // -D/2, +D/2
  else         { row = band * 2 + 1; x = (rem - 3) * HEX_D; }                     // -D, 0, +D
  wx = x;
  wy = row * HEX_ROWH;
}

// Bu karede cizilen ikonlarin ekran konumlari (tap hit-test icin)
struct HexHit { int x, y, r, mod; };
static HexHit g_lHits[40];
static int    g_lHitN = 0;
static int    g_lCenterMod = -1;       // ekran merkezine en yakin modul

static void drawLauncher() {
  gfx->fillScreen(C_BG);               // saf siyah (#000000) AMOLED zemin
  int n = enabledCount();
  if (n == 0) {
    textCenter("Tum moduller", CXi, CYi - 20, 2, C_DIM);
    textCenter("kapali", CXi, CYi + 10, 2, C_DIM);
    present();
    return;
  }
  if (n > 40) n = 40;

  g_lHitN = 0;
  g_lCenterMod = -1;
  float bestD = 1e9f;

  for (int i = 0; i < n; i++) {
    float wx, wy;
    hexCell(i, wx, wy);
    int sx = CXi + (int)wx;
    int sy = CYi + (int)(wy - g_lScroll.pos);

    // balikgozu: merkezden uzaklastikca kucul. Merkeziyetci duzen: ikonlar
    // ASLA yuvarlak cerceveye tasamaz -- kenara yaklasan ikon once kuculur,
    // sigmayacak kadar kenardaysa hic cizilmez (kirpik yarim daireler yok).
    float d  = sqrtf((float)(sx - CXi) * (sx - CXi) + (float)(sy - CYi) * (sy - CYi));
    float t  = d / 233.0f;
    float sc = 1.0f - 0.50f * t * t;                    // purussuz radyal kuculme
    if (sc < 0.40f) sc = 0.40f;
    int r = (int)(HEX_R0 * sc);
    int rFit = 233 - (int)d - 6;                        // cerceveye sigan max yaricap
    if (rFit < 14) continue;                            // tamamen kenarda -> gizle
    if (r > rFit) r = rFit;                             // kenarda: noktaya buzul

    int mod = enabledNth(i);
    bool focus = d < HEX_D * 0.45f;                      // the centered icon
    gfx->fillCircle(sx, sy, r, focus ? L_FACE_FOCUS : L_FACE_IDLE);
    if (focus) gfx->drawCircle(sx, sy, r, L_GLYPH_FOCUS); // subtle accent ring
    if (r >= 42) {
      drawModuleIcon(g_modules[mod].id, sx, sy, focus ? L_GLYPH_FOCUS : L_GLYPH_IDLE,
                     focus ? L_FACE_FOCUS : L_FACE_IDLE);
    } else {
      gfx->fillCircle(sx, sy, r / 3, L_GLYPH_IDLE);       // kenardaki minik: nokta
    }

    if (g_lHitN < 40) g_lHits[g_lHitN++] = { sx, sy, r, mod };
    if (d < bestD) { bestD = d; g_lCenterMod = mod; }
  }

  drawTopBar();                          // cihaz adi + pil (ikonlarin ustune ince)
  if (g_lCenterMod >= 0)                 // merkezdeki modulun adi, alt kenarda
    textCenter(g_modules[g_lCenterMod].name, CXi, 436, 2, C_TEXT);
  present();
}

static void openModule(int idx) {
  if (idx < 0 || idx >= g_moduleCount) return;
  noteActivity();
  if (!g_rotActive) soundModuleJingle(g_modules[idx].id);  // sessiz gecis (rotasyon)
  g_activeModule = idx;
  g_app = APP_MODULE;
  // Boost to full speed only for CPU-heavy modules; everything else stays cool.
  cpuSetMhz(moduleNeedsHighCpu(g_modules[idx].id) ? CPU_HZ_HIGH : CPU_HZ_LOW);
  if (g_modules[idx].enter) g_modules[idx].enter();
}

static void gotoHome() {
  noteActivity();
  g_app = APP_LAUNCHER;
  g_activeModule = -1;
  g_rotActive = false;                       // leaving a module stops rotation
  cpuSetMhz(CPU_HZ_LOW);                     // back to the cool UI clock
  launcherDirty = true;
}

// ---- launcher input ----  dikey surukle + atalet; ikona dokun = ac
static void launcherInput() {
  int n = enabledCount();
  if (n == 0) return;
  if (!g_lInit) {
    // dikey sinirlar: en ustteki/alttaki petek satiri merkeze gelebilsin
    float wx, wy, lo = 0, hi = 0;
    for (int i = 0; i < n && i < 40; i++) { hexCell(i, wx, wy); if (wy < lo) lo = wy; if (wy > hi) hi = wy; }
    // Acilista ilk 3'lu satiri (row 1) ortala: ust 2'li satir + orta 3'lu +
    // alt 2'li = tam petek "2-3-2". (>=3 modul varsa; degilse basa otur.)
    float start = (n > 2 && hi >= HEX_ROWH) ? HEX_ROWH : lo;
    scrollInit(g_lScroll, start, lo, hi, false, true);  // serbest + elastik kenar
    g_lInit = true;
  }

  if (g_g.down) {
    if (!g_lScroll.active) scrollGrab(g_lScroll);
    if (g_g.dragDY != 0) scrollDrag(g_lScroll, -g_g.dragDY / L_STEP);
  } else if (g_lScroll.active) {
    // parmak kalkti: birakma hiziyla firlat (duz dokunusta 0)
    scrollRelease(g_lScroll, g_g.released ? (-g_g.velY / L_STEP) : 0.0f);
  }

  // BOOT-butonu fallback (dokunmatik yoksa): kisa = bir satir kay, uzun = ac.
  bool isButton = !g_touchOk && g_g.x == CXi && g_g.y == CYi;
  if (g_g.tap && isButton) {
    g_lScroll.pos += HEX_ROWH;
    if (g_lScroll.pos > g_lScroll.hi) g_lScroll.pos = g_lScroll.lo;   // sona gelince basa sar
    g_lScroll.vel = 0; soundTap(); launcherDirty = true; return;
  }
  if (g_g.longPress && isButton && g_lCenterMod >= 0) { openModule(g_lCenterMod); return; }

  // dokunulan dairenin modulunu ac (en yakin ikon, kucuk tolerans payiyla)
  if (g_g.tap) {
    for (int i = 0; i < g_lHitN; i++) {
      int dx = g_g.x - g_lHits[i].x, dy = g_g.y - g_lHits[i].y;
      int rr = g_lHits[i].r + 8;
      if (dx * dx + dy * dy <= rr * rr) { openModule(g_lHits[i].mod); return; }
    }
  }
}

// Auto screen-off is allowed on the launcher and the "ambient" modules (a clock
// face, charm, etc.); active tools (nav/maps/music/games/AI) and the slideshow
// keep the screen on so they're never blanked mid-use.
static bool sleepAllowedNow() {
  if (g_rotActive) return false;
  if (g_app == APP_LAUNCHER) return true;
  if (g_activeModule < 0) return true;
  static const char* const ambient[] = { "clock", "charm", "badge", "collar", "tama" };
  const char* id = g_modules[g_activeModule].id;
  for (const char* a : ambient) if (strcmp(id, a) == 0) return true;
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[Vecta] boot");
  Serial.printf("[Vecta] PSRAM: %u free / %u total\n", ESP.getFreePsram(), ESP.getPsramSize());
  settingsBegin();
  deviceIdInit();

  if (!platformDisplayBegin()) {
    Serial.println("[Vecta] display begin FAILED");
  } else {
    Serial.printf("[Vecta] display OK (canvas=%d)\n", g_haveCanvas);
  }
  displayLoadBrightness();    // NVS'deki kullanici parlakligini uygula
  powerLoadConfig();          // ekran uyku zaman asimi + tilt-to-wake (NVS)
  themeLoad();                // arka plan + vurgu rengi (NVS) - cizimden once

  // Boot animation: pulsing "Vecta" + expanding ring (~1.4s)
  for (int i = 0; i < 28; i++) {
    gfx->fillScreen(C_BG);
    float p = i / 28.0f;
    int r = (int)(p * 230);
    gfx->drawCircle(CXi, CYi, r, C_ACCENT);
    if (r > 30) gfx->drawCircle(CXi, CYi, r - 18, C_BG2);
    // gentle pulse on the wordmark
    int sz = 5 + (int)(2 * sinf(p * PI));
    textCenter("Vecta", CXi, CYi, sz, C_GOLD);
    present();
    delay(40);
  }

  touchBegin();
  magBegin();        // optional QMC5883L on the touch I2C bus
  mpuBegin();        // optional MPU6050 IMU on the same I2C bus (0x68)
  g_autoRotate = setGetBool("autorot", true);   // auto 180 flip when upside-down
  soundLoadTheme();

  // Bring up the ES8311 codec now and SHOW the diagnostics on screen (so we
  // don't depend on Serial, which is silent unless "USB CDC On Boot" is on).
  // Then play a short beep so speaker output can be tested regardless of theme.
  audioBegin();
  gfx->fillScreen(C_BG);
  textCenter("SES TANISI", CXi, 60, 3, C_GOLD);
  gfx->setTextSize(2);
  gfx->setTextColor(g_audioReal ? C_OK : C_WARN);
  gfx->setCursor(30, 150);
  gfx->print(g_audioDiag);
  present();
  // tema muzigi (sessiz temada hoparlor testi icin sade iki ton)
  if (g_soundTheme) soundBootJingle();
  else { audioPlayTone(880, 140); audioPlayTone(1320, 140); }
  delay(4500);

  // Register modules (order defines launcher order). All gated by mod_config.h:
  // a BASE build (CHARM_BASE=1) registers only Settings; the phone installs the
  // rest over OTA. Settings stays unless you explicitly pass -DMOD_SETTINGS=0.
#if MOD_CHARM
  registerModule("charm",  "Canta Susu", "<3",   charm_enter,  charm_tick);
#endif
#if MOD_CLOCK
  registerModule("clock",  "Saat",       "(t)",  clock_enter,  clock_tick);
#endif
#if MOD_TAMA
  registerModule("tama",   "Tamagotchi", ":)",   tama_enter,   tama_tick);
#endif
#if MOD_COLLAR
  registerModule("collar", "Tasma",      "[P]",  collar_enter, collar_tick);
#endif
#if MOD_GAME
  registerModule("game",   "Oyun",       "[#]",  game_enter,   game_tick);
#endif
#if MOD_BADGE
  registerModule("badge",  "Rozet",      "[!]",  badge_enter,  badge_tick);
#endif
#if MOD_NOTIFY
  registerModule("notify", "Bildirim",   "(o)",  notify_enter, notify_tick);
#endif
#if MOD_AUDIO
  registerModule("audio",  "Ses",        "/\\",  av_enter,     audio_tick);
#endif
#if MOD_FIDGET
  registerModule("fidget", "Nefes",      "( )",  fidget_enter, fidget_tick);
#endif
#if MOD_FINDER
  registerModule("finder", "Bulucu",     "(+)",  finder_enter, finder_tick);
#endif
#if MOD_BUDDY
  registerModule("buddy",  "Ikiz",       "->",   buddy_enter,  buddy_tick);
#endif
#if MOD_COMPASS
  registerModule("compass","Pusula",     "(N)",  compass_enter,compass_tick);
#endif
#if MOD_DRAW
  registerModule("draw",   "Cizim",      "(/)",  draw_enter,   draw_tick);
#endif
#if MOD_KNOB
  registerModule("knob",   "Doner Dugme","(O)",  knob_enter,   knob_tick);
#endif
#if MOD_LOVEBOX
  registerModule("lovebox","Lovebox",    "<3",   love_enter,   love_tick);
#endif
#if MOD_ALBUM
  registerModule("album",  "Foto Albumu","[o]",  album_enter,  album_tick);
#endif
#if MOD_MAPS
  registerModule("maps",   "Harita",     "(M)",  maps_enter,   maps_tick);
#endif
#if MOD_WEATHER
  registerModule("weather","Hava Durumu","(W)",  weather_enter,weather_tick);
#endif
#if MOD_MUSIC
  registerModule("music",  "Muzik",      "(M)",  music_enter,  music_tick);
#endif
#if MOD_STREAK
  registerModule("streak", "Streak",     "(*)",  streak_enter, streak_tick);
#endif
#if MOD_POMO
  registerModule("pomo",   "Pomodoro",   "(P)",  pomo_enter,   pomo_tick);
#endif
#if MOD_CAPSULE
  registerModule("capsule","Kapsul",     "[K]",  capsule_enter,capsule_tick);
#endif
#if MOD_CAMERA
  registerModule("camera", "Kamera",     "[O]",  camera_enter, camera_tick);
#endif
#if MOD_RPS
  registerModule("rps",    "TKM",        "TKM",  rps_enter,    rps_tick);
#endif
#if MOD_REFLEX
  registerModule("reflex", "Refleks",    "rfx",  reflex_enter, reflex_tick);
#endif
#if MOD_TTT
  registerModule("ttt",    "XOX",        "xox",  ttt_enter,    ttt_tick);
#endif
#if MOD_ASSISTANT
  registerModule("assistant","Asistan",  "AI",   assistant_enter, assistant_tick);
#endif
#if MOD_VOICENOTE
  registerModule("voicenote","Sesli Not","NOT",  voicenote_enter, voicenote_tick);
#endif
#if MOD_SETTINGS
  registerModule("settings", "Ayarlar",  "(S)",  settings_enter,  settings_tick);
#endif
#if MOD_XIAOZHI
  registerModule("xiaozhi",  "XiaoZhi",  "XZ",   xiaozhi_enter,   xiaozhi_tick);
#endif
  // GECICI: Edge AI boot-loop'a yol aciyordu; panic log'uyla hata bulunana kadar
  // kapali. Tekrar acmak icin yorumu kaldir.
  // registerModule("edgeai",   "Edge AI",  "AI*",  edgeai_enter,    edgeai_tick);
#if MOD_OPENCLAW
  registerModule("openclaw", "OpenClaw", "CL",   openclaw_enter,  openclaw_tick);
#endif
#if MOD_MIRROR
  registerModule("mirror",   "Yansit",   ">]",   mirror_enter,    mirror_tick);
#endif
#if MOD_NAV
  registerModule("nav",      "Navigasyon","^",   nav_enter,       nav_tick);
#endif
#if MOD_LYRICS
  registerModule("lyrics",   "Sozler",   "♪T",   lyrics_enter,    lyrics_tick);
#endif
#if MOD_READER
  registerModule("reader",   "Hizli Okuma","[R]", reader_enter,   reader_tick);
#endif
#if MOD_ROTATE
  registerModule("rotate",   "Otomatik Gecis","[>]", rotate_enter, rotate_tick);
#endif
  rotLoad();                 // restore a saved rotation sequence (NVS)

#ifdef HAVE_BLE_HID
  // BLE'yi acilista baslat: telefon HID + AMS (now playing) her zaman hazir.
  // g_bleOn (Ayarlar > Bluetooth) kapaliysa hic baslatma -> radyo tamamen WiFi'nin.
  if (g_bleOn) {
    bleKb.begin();
    knob_started = true;
    amsBegin();
    navBleBegin(bleKb.server());   // Beeline nav GATT service on the same link
  }
#endif

  netBegin();
  cloudBegin();        // after AP is up, add STA + try home Wi-Fi
  cpuSetMhz(CPU_HZ_LOW);  // run the UI cool; modules boost themselves if needed
  cloudTasksStart();   // background workers for outbound + polling
  // ESP-NOW after Wi-Fi is up; register device-to-device handlers so packets
  // arrive even when their module isn't open yet (we auto-open on receive).
  espnowBegin();
  espnowAddHandler(buddyHandlePacket);
  espnowAddHandler(drawHandlePacket);
  espnowAddHandler(loveHandlePacket);   // device-to-device messages -> notification

  // First boot only: walk through the setup wizard (profile pick + Wi-Fi QR).
  onboardingRun();

  // Connect screen: scan the Wi-Fi QR to join directly. Stays until the user
  // taps (or ~25s), so there is time to scan.
  uint32_t t0 = millis();
  bool dismissed = false;
  while (!dismissed && millis() - t0 < 25000) {
    netLoop();
    gestureUpdate();
    gfx->fillScreen(C_BG);
    textCenter("Vecta", CXi, 30, 3, C_GOLD);
    if (qrAvailable() && g_wifiQR[0]) {
      qrDraw(g_wifiQR, CXi, 240, 8);
      textCenter("Telefon kamerasi ile okut", CXi, 410, 2, C_TEXT);
      textCenter("-> Wi-Fi'a baglan", CXi, 440, 2, C_DIM);
    } else {
      // QR library not installed -> show credentials as text
      textCenter("Wi-Fi", CXi, 150, 2, C_DIM);
      textCenter(AP_SSID, CXi, 195, 2, C_TEXT);
      textCenter("sifre: 12345678", CXi, 235, 2, C_TEXT);
      char ipb[20]; snprintf(ipb, sizeof(ipb), "%s", netIP().toString().c_str());
      textCenter(ipb, CXi, 280, 2, C_DIM);
      textCenter("dokun: gec", CXi, 420, 2, C_DIM);
    }
    // Home-network status (for the Expo app on the same Wi-Fi).
    if (cloud_staJoined) {
      char ipb[40];
      snprintf(ipb, sizeof(ipb), "Ev Wi-Fi: %s", WiFi.localIP().toString().c_str());
      textCenter(ipb, CXi, 70, 2, C_OK);
      textCenter("vecta.local", CXi, 95, 2, C_DIM);
    }
    present();
    if (g_g.tap || g_g.longPress) dismissed = true;
    delay(30);
  }

  gotoHome();
}

void loop() {
  netLoop();
  espnowLoop();              // HELLO beacon + guvenilir paket retry'lari
#ifdef HAVE_BLE_HID
  bleKb.keepAlive();         // bond'lu telefona otomatik yeniden baglan
  amsLoop();                 // iPhone AMS now-playing istemcisi
#endif
  gestureUpdate();
  motionTick();              // always-on IMU motion/activity engine (background)
  drUpdate();                // background dead-reckoning route logger (maps inset)

  // ---- display sleep / wake (battery saver) -------------------------------
  // After g_sleepTimeoutMs of no input the AMOLED is blanked; a touch, the BOOT
  // button, a wrist-raise (IMU) or an incoming push wakes it. Comms (WiFi/BLE/
  // ESP-NOW) keep running while asleep so notifications still arrive.
  {
    bool input  = g_g.down || g_g.tap || g_g.longPress || g_g.released ||
                  g_g.swipeLeft || g_g.swipeRight || g_g.swipeUp || g_g.swipeDown;
    bool netReq = g_wakeReq || g_requestOpen >= 0 || g_requestHome ||
                  g_drawIncoming || g_rotReqStart;
    if (input || netReq) noteActivity();
    g_wakeReq = false;

    if (g_displayAsleep) {
      if (input || netReq || (g_tiltWake && motionRaiseEvent())) {
        displayWake(); launcherDirty = true;
      }
      // If a net request woke us, fall through so it also opens its screen;
      // otherwise consume this frame (don't act on the wake-tap).
      if (g_displayAsleep || !netReq) { delay(20); return; }
    } else if (g_sleepTimeoutMs && sleepAllowedNow() && !g_batCharging) {
      // Stay fully on while charging; otherwise dim a few seconds before off.
      uint32_t idle = millis() - g_lastActivityMs;
      uint32_t pre  = g_sleepTimeoutMs > 6000 ? 4000 : g_sleepTimeoutMs / 3;
      if (idle > g_sleepTimeoutMs)        { displaySleep(); delay(20); return; }
      else if (idle > g_sleepTimeoutMs - pre) { if (!g_displayDimmed) { displayDim(); launcherDirty = true; } }
      else if (g_displayDimmed)           { displayUndim(); launcherDirty = true; }
    } else if (g_displayDimmed) {
      displayUndim(); launcherDirty = true;   // charging / no-sleep module -> undim
    }
  }

  // Auto screen rotation: rotate the whole UI to match how the watch is held
  // (4-way: upright / both landscapes / upside-down).
  if (g_haveCanvas && g_autoRotate && motionPresent()) {
    int want = motionOrient();
    if (want != g_screenRot) { g_screenRot = want; gfx->setRotation(want); launcherDirty = true; }
  }

  // Net-requested navigation
  if (g_requestHome) { g_requestHome = false; gotoHome(); }
  if (g_requestOpen >= 0) { int o = g_requestOpen; g_requestOpen = -1; openModule(o); }

  // Auto-rotation (slideshow): start when requested, then advance through the
  // configured modules on their per-step timers, looping. Long-press (gotoHome)
  // stops it. Manually opening another module also just continues from there.
  if (g_rotReqStart) {
    g_rotReqStart = false;
    if (g_rotN > 0) { g_rotActive = true; g_rotCur = 0; g_rotStepAt = millis(); openModule(g_rot[0].modIdx); }
  }
  if (g_rotActive && g_app == APP_MODULE && g_rotN > 0) {
    if (millis() - g_rotStepAt >= (uint32_t)g_rot[g_rotCur].secs * 1000) {
      g_rotCur = (g_rotCur + 1) % g_rotN;
      g_rotStepAt = millis();
      openModule(g_rot[g_rotCur].modIdx);
    }
  }

  // An incoming drawing auto-opens the Cizim module (if enabled & not already in it)
  if (g_drawIncoming) {
    g_drawIncoming = false;
    int di = findModuleIdx("draw");
    if (di >= 0 && g_modules[di].enabled &&
        !(g_app == APP_MODULE && g_activeModule == di)) {
      openModule(di);
    }
  }

  if (g_app == APP_LAUNCHER) {
    launcherInput();
    bool animating = scrollUpdate(g_lScroll);    // advance inertia/snap physics
    if (launcherDirty || animating) { drawLauncher(); launcherDirty = false; }
    else delay(16);                              // idle: ~no CPU, instant wake on touch
  } else {
    // global gesture: long-press returns to launcher
    if (g_g.longPress) { soundBack(); gotoHome(); return; }
    Module &m = g_modules[g_activeModule];
    if (m.tick) m.tick();
  }
}
