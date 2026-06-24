// ============================================================================
// mod_knob.h - BLE Media Knob (Doner Dugme)
//   The round screen becomes a rotary dial:
//     * rotate finger around the center -> volume up / down
//     * quick tap (no rotation)         -> play / pause
//   The device pairs to a PC/phone as a Bluetooth HID media controller.
//   Still long-press (no movement) exits to launcher (handled by main loop).
//
//   REQUIRES the "ESP32 BLE Keyboard" library (by T-vK). If it is not
//   installed, this module compiles to a friendly "library needed" screen.
//   Library: https://github.com/T-vK/ESP32-BLE-Keyboard  (Library Manager:
//   search "ESP32 BLE Keyboard", or add the ZIP).
// ============================================================================
#pragma once

#include "platform.h"

// BLE media-knob / camera-shutter (HID consumer control). Uses our own minimal
// HID built on the ESP32 core's BLE library (vecta_blehid.h) - NO external
// library needed, compiles on core 3.x. Set to 0 to drop BLE entirely.
// NOTE: BLE is large - select Tools -> Partition Scheme -> "Huge APP".
#define VECTA_USE_BLE 1

#if VECTA_USE_BLE
#  include "vecta_blehid.h"
#  define HAVE_BLE_HID 1
#endif

#ifdef HAVE_BLE_HID
static VectaBleHid bleKb("Vecta", "Vecta", 100);
static bool knob_started = false;

static float knob_lastAngle = 0;
static bool  knob_pressing = false;
static float knob_accum = 0;       // accumulated degrees toward a volume step
static float knob_moved = 0;       // total finger displacement this press (px)
static int   knob_downX = 0, knob_downY = 0;
static uint32_t knob_pressStart = 0;
static float knob_marker = 0;      // visual marker angle (deg)
static const float STEP_DEG = 18;  // degrees per volume step

static bool     knob_playing = true;       // local guess of play state (for the glyph)
static int      knob_volDir = 0;           // +1/-1 while turning (for the ses hint)
static uint32_t knob_volAt = 0;

// Which control was just tapped (brief highlight + reliable, instant action).
enum { KACT_NONE = 0, KACT_PLAY, KACT_PREV, KACT_NEXT };
static int      knob_act = KACT_NONE;
static uint32_t knob_actAt = 0;
static const int KNOB_BTN_DX = 148;        // prev/next buttons flank the center
static const int KNOB_BTN_R  = 33;

static float angDiff(float a, float b) {
  float d = a - b;
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}

static void knob_enter() {
  if (!knob_started) {
    bleKb.begin();          // advertises as "Vecta"; pairs automatically
    knob_started = true;
  }
  knob_pressing = false; knob_accum = 0;
}

static void knob_tick() {
  bleKb.keepAlive();                       // auto-reconnect to a bonded phone

  RawTouch t = touchRead();
  uint32_t now = millis();
  bool connected = bleKb.isConnected();

  const int pvx = CXi - KNOB_BTN_DX, pvy = CYi;   // prev button center
  const int ntx = CXi + KNOB_BTN_DX, nty = CYi;   // next button center

  if (t.down) {
    float dx = t.x - CXi, dy = t.y - CYi;
    float r = sqrtf(dx * dx + dy * dy);
    if (!knob_pressing) {
      knob_pressing = true;
      knob_downX = t.x; knob_downY = t.y; knob_pressStart = now;
      knob_moved = 0; knob_accum = 0;
      knob_lastAngle = atan2f(dy, dx) / DEG_TO_RAD;
    } else {
      int mdx = t.x - knob_downX, mdy = t.y - knob_downY;
      knob_moved = sqrtf((float)(mdx * mdx + mdy * mdy));   // displacement from press
      float ang = atan2f(dy, dx) / DEG_TO_RAD;
      float d = angDiff(ang, knob_lastAngle);
      knob_lastAngle = ang;
      if (r > 70) {                                   // turning out on the rim -> volume
        knob_accum += d; knob_marker += d;
        while (knob_accum >= STEP_DEG) {
          if (connected) bleKb.write(KEY_MEDIA_VOLUME_UP);
          knob_accum -= STEP_DEG; knob_volDir = 1; knob_volAt = now;
        }
        while (knob_accum <= -STEP_DEG) {
          if (connected) bleKb.write(KEY_MEDIA_VOLUME_DOWN);
          knob_accum += STEP_DEG; knob_volDir = -1; knob_volAt = now;
        }
      }
    }
  } else if (knob_pressing) {
    knob_pressing = false;
    // A TAP (little movement) acts instantly - no multi-tap delay. The control
    // is chosen by WHERE the finger went down (reliable hit-test), so the big
    // center button always responds and prev/next never get confused.
    if (knob_moved < 20 && now - knob_pressStart < 600) {
      float pr = hypotf(knob_downX - pvx, knob_downY - pvy);
      float nr = hypotf(knob_downX - ntx, knob_downY - nty);
      float cr = hypotf(knob_downX - CXi, knob_downY - CYi);
      knob_act = KACT_NONE;
      if (pr < KNOB_BTN_R + 16)      { if (connected) bleKb.write(KEY_MEDIA_PREVIOUS_TRACK); knob_act = KACT_PREV; }
      else if (nr < KNOB_BTN_R + 16) { if (connected) bleKb.write(KEY_MEDIA_NEXT_TRACK);     knob_act = KACT_NEXT; }
      else if (cr < 100)             { if (connected) bleKb.write(KEY_MEDIA_PLAY_PAUSE); knob_playing = !knob_playing; knob_act = KACT_PLAY; }
      if (knob_act != KACT_NONE) { knob_actAt = now; soundTap(); }
    }
  }

  // ---- render dial ----
  uint16_t accent = connected ? C_ACCENT : C_DIM;
  gfx->fillScreen(C_BG);

  // graduated outer ring (volume track) with tick marks
  fillRing(CXi, CYi, 196, 210, 0, 360, C_BG2);
  for (int i = 0; i < 48; i++) {
    float a = i * (TWO_PI / 48);
    int r1 = (i % 6 == 0) ? 182 : 190;
    gfx->drawLine(CXi + (int)(196 * cosf(a)), CYi + (int)(196 * sinf(a)),
                  CXi + (int)(r1 * cosf(a)),  CYi + (int)(r1 * sinf(a)), C_DIM);
  }
  // marker bead rides the rim as you turn
  float ma = knob_marker * DEG_TO_RAD;
  int mx = CXi + (int)(203 * cosf(ma)), my = CYi + (int)(203 * sinf(ma));
  gfx->fillCircle(mx, my, 15, accent);
  gfx->fillCircle(mx, my, 7, C_BG);

  // prev / next buttons (flank the center)
  bool pf = (knob_act == KACT_PREV) && (now - knob_actAt < 220);
  bool nf = (knob_act == KACT_NEXT) && (now - knob_actAt < 220);
  gfx->fillCircle(pvx, pvy, KNOB_BTN_R, pf ? accent : C_BG2);
  gfx->drawCircle(pvx, pvy, KNOB_BTN_R, accent);
  gfx->fillCircle(ntx, nty, KNOB_BTN_R, nf ? accent : C_BG2);
  gfx->drawCircle(ntx, nty, KNOB_BTN_R, accent);
  uint16_t pic = pf ? C_BG : C_TEXT, nic = nf ? C_BG : C_TEXT;
  gfx->fillRect(pvx - 15, pvy - 11, 3, 22, pic);                                  // |<<
  gfx->fillTriangle(pvx - 1, pvy - 11, pvx - 1, pvy + 11, pvx - 11, pvy, pic);
  gfx->fillTriangle(pvx + 12, pvy - 11, pvx + 12, pvy + 11, pvx + 2, pvy, pic);
  gfx->fillTriangle(ntx - 12, nty - 11, ntx - 12, nty + 11, ntx - 2, nty, nic);   // >>|
  gfx->fillTriangle(ntx + 1, nty - 11, ntx + 1, nty + 11, ntx + 11, nty, nic);
  gfx->fillRect(ntx + 12, nty - 11, 3, 22, nic);

  // center play/pause button
  bool cf = (knob_act == KACT_PLAY) && (now - knob_actAt < 220);
  gfx->fillCircle(CXi, CYi, 96, C_BG2);
  gfx->drawCircle(CXi, CYi, 96, accent);
  gfx->fillCircle(CXi, CYi, 78, cf ? C_TEXT : accent);
  uint16_t glyph = cf ? accent : C_BG;
  if (knob_playing) {                                  // pause = two bars
    gfx->fillRoundRect(CXi - 24, CYi - 30, 16, 60, 4, glyph);
    gfx->fillRoundRect(CXi + 8,  CYi - 30, 16, 60, 4, glyph);
  } else {                                             // paused -> play triangle
    gfx->fillTriangle(CXi - 18, CYi - 32, CXi - 18, CYi + 32, CXi + 32, CYi, glyph);
  }

  // volume hint while turning (+/-)
  if (now - knob_volAt < 600 && knob_volDir != 0) {
    textCenter(knob_volDir > 0 ? "SES +" : "SES -", CXi, CYi - 132, 2, accent);
  }

  textCenter(connected ? (knob_playing ? "Caliyor" : "Duraklatildi") : "BLE bekliyor",
             CXi, 384, 2, connected ? C_OK : C_WARN);
  textCenter("cevir: ses   orta: oynat/dur   yan: gec", CXi, 432, 2, C_DIM);
  present();
  delay(12);
}

#else  // ---- library not installed ----
static void knob_enter() {}
static void knob_tick() {
  gfx->fillScreen(C_BG);
  textCenter("DONER DUGME", CXi, 150, 3, C_GOLD);
  textCenter("BLE kutuphanesi gerekli:", CXi, 220, 2, C_DIM);
  textCenter("\"ESP32 BLE Keyboard\"", CXi, 255, 2, C_TEXT);
  textCenter("(T-vK) kur ve tekrar derle", CXi, 290, 2, C_DIM);
  present();
  delay(200);
}
#endif
