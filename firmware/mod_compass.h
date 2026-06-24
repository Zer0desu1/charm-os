// ============================================================================
// mod_compass.h - Pusula (standalone magnetic compass)
//   Pure on-device module: needs ONLY the QMC5883L (mag.h) wired to the touch
//   I2C bus (SDA/SCL, 0x0D). No phone / GPS / Wi-Fi.
//
//   Display: a north-up rotating compass rose (N always points to magnetic
//   north), a fixed top index showing where the device faces, and the heading
//   in degrees + 8-wind cardinal in the centre.
//
//   Tap        = start / stop a calibration spin (turn the watch a full circle;
//                min/max midpoint per axis becomes the hard-iron offset, saved
//                to NVS via magSetOffsets()).
//   Long-press = home (handled globally).
//
//   Include AFTER mag.h.
// ============================================================================
#pragma once

#include "platform.h"
#include "mag.h"
#include "heading.h"   // compass+gyro fusion (stabilises heading when MPU present)

// mounting offset: degrees to add so 0 = the direction the TOP of the watch
// faces. magHeading() reports the sensor's +X axis; tweak this if the rose is
// rotated 90/180 vs. reality on your build.
#define COMPASS_MOUNT_DEG 0.0f

static float    compass_disp   = 0;       // smoothed displayed heading
static bool     compass_cal    = false;   // calibration in progress
static uint32_t compass_calEnd = 0;       // millis when the spin ends
static int16_t  compass_minX, compass_maxX, compass_minY, compass_maxY;

static void compass_enter() {
  compass_cal = false;
  float h = magHeading();
  compass_disp = (h >= 0) ? h : 0;
}

// shortest-path angular lerp toward target (degrees)
static float compassLerp(float cur, float target, float k) {
  float d = target - cur;
  while (d > 180)  d -= 360;
  while (d < -180) d += 360;
  cur += d * k;
  if (cur < 0) cur += 360; else if (cur >= 360) cur -= 360;
  return cur;
}

static const char* compassCardinal(float deg) {
  static const char* w[8] = { "K", "KD", "D", "GD", "G", "GB", "B", "KB" };
  int i = (int)((deg + 22.5f) / 45.0f) & 7;   // K=North, D=East, G=South, B=West
  return w[i];
}

// point at screen angle a (0 = up, clockwise +) and radius r
static void compassPt(float aDeg, int r, int& x, int& y) {
  float a = aDeg * DEG_TO_RAD;
  x = CXi + (int)(r * sinf(a));
  y = CYi - (int)(r * cosf(a));
}

static void compass_tick() {
  // ---- no sensor: diagnostic screen (tap = re-scan/re-probe the bus) ----
  if (!magPresent()) {
    if (g_g.tap) magBegin();                 // re-probe after fixing wiring
    else magScanBus();                       // live refresh of bus scan
    gfx->fillScreen(C_BG);
    textCenter("PUSULA", CXi, 80, 3, C_GOLD);
    textCenter("sensor bulunamadi", CXi, 140, 2, C_DANGER);
    textCenter("I2C taramasi:", CXi, 200, 2, C_DIM);
    textCenter(magScanStr().c_str(), CXi, 232, 2, C_TEXT);
    textCenter("beklenen: 0D veya 1E", CXi, 286, 2, C_DIM);
    textCenter("kablo -> dokunmatik", CXi, 320, 2, C_DIM);
    textCenter("SDA/SCL hatti", CXi, 350, 2, C_DIM);
    textCenter("dokun: tekrar tara", CXi, 410, 2, C_ACCENT);
    present();
    delay(250);
    return;
  }

  // ---- input: tap toggles calibration ----
  if (g_g.tap) {
    if (!compass_cal) {
      compass_cal = true;
      compass_calEnd = millis() + 9000;     // ~9 s to spin a full turn
      compass_minX = compass_minY = 32767;
      compass_maxX = compass_maxY = -32768;
    } else {
      compass_cal = false;                  // tap again = cancel
    }
  }

  // ---- calibration spin ----
  if (compass_cal) {
    int16_t rx, ry;
    if (magRawXY(rx, ry)) {
      if (rx < compass_minX) compass_minX = rx;
      if (rx > compass_maxX) compass_maxX = rx;
      if (ry < compass_minY) compass_minY = ry;
      if (ry > compass_maxY) compass_maxY = ry;
    }
    uint32_t now = millis();
    if ((int32_t)(compass_calEnd - now) <= 0) {
      // finish: hard-iron offset = midpoint of swept min/max
      int16_t ox = (int16_t)(((int32_t)compass_minX + compass_maxX) / 2);
      int16_t oy = (int16_t)(((int32_t)compass_minY + compass_maxY) / 2);
      magSetOffsets(ox, oy);
      compass_cal = false;
    } else {
      gfx->fillScreen(C_BG);
      float frac = 1.0f - (float)(compass_calEnd - now) / 9000.0f;
      fillRing(CXi, CYi, 188, 200, 0, (int)(frac * 360), C_ACCENT);
      gfx->drawCircle(CXi, CYi, 200, C_BG2);
      textCenter("KALIBRASYON", CXi, CYi - 50, 3, C_GOLD);
      textCenter("cihazi yavasca", CXi, CYi + 6, 2, C_TEXT);
      textCenter("tam tur cevir", CXi, CYi + 36, 2, C_TEXT);
      char s[8]; snprintf(s, sizeof(s), "%d", 1 + (int)((compass_calEnd - now) / 1000));
      textCenter(s, CXi, CYi + 90, 4, C_ACCENT);
      present();
      delay(30);
      return;
    }
  }

  // ---- normal compass ----
  // gyro-fused heading when an IMU is present (much steadier), else raw compass
  float h = mpuPresent() ? headingFused() : magHeading();
  if (h >= 0) {
    h += COMPASS_MOUNT_DEG;
    if (h >= 360) h -= 360; else if (h < 0) h += 360;
    compass_disp = compassLerp(compass_disp, h, 0.25f);
  }
  float head = compass_disp;        // direction the device faces (deg)

  gfx->fillScreen(C_BG);

  // bezel
  gfx->drawCircle(CXi, CYi, 210, C_BG2);
  gfx->drawCircle(CXi, CYi, 209, C_BG2);

  // rotating tick ring: N is at screen angle (0 - head)
  for (int deg = 0; deg < 360; deg += 15) {
    float sa = deg - head;          // world bearing -> screen angle
    bool major = (deg % 45) == 0;
    int x1, y1, x2, y2;
    compassPt(sa, 200, x1, y1);
    compassPt(sa, major ? 178 : 190, x2, y2);
    gfx->drawLine(x1, y1, x2, y2, major ? C_TEXT : C_DIM);
  }

  // cardinal letters around the rose
  const char* card[4] = { "N", "E", "S", "W" };
  for (int i = 0; i < 4; i++) {
    float sa = i * 90 - head;
    int lx, ly;
    compassPt(sa, 155, lx, ly);
    textCenter(card[i], lx, ly - 8, 3, (i == 0) ? C_DANGER : C_TEXT);
  }

  // North-pointing needle (red half to north, white tail to south)
  float na = -head;                 // screen angle of magnetic north
  int ntx, nty, ntailx, ntaily, lwx, lwy, rwx, rwy;
  compassPt(na, 120, ntx, nty);          // north tip
  compassPt(na + 180, 90, ntailx, ntaily); // south tail
  compassPt(na - 160, 26, lwx, lwy);
  compassPt(na + 160, 26, rwx, rwy);
  gfx->fillTriangle(ntx, nty, lwx, lwy, CXi, CYi, C_DANGER);
  gfx->fillTriangle(ntx, nty, rwx, rwy, CXi, CYi, C_DANGER);
  gfx->fillTriangle(ntailx, ntaily, lwx, lwy, CXi, CYi, rgb(210, 215, 230));
  gfx->fillTriangle(ntailx, ntaily, rwx, rwy, CXi, CYi, rgb(210, 215, 230));
  gfx->fillCircle(CXi, CYi, 10, C_BG2);

  // fixed top index = the direction the device faces
  gfx->fillTriangle(CXi - 12, 18, CXi + 12, 18, CXi, 44, C_GOLD);

  // centre readout
  char db[8]; snprintf(db, sizeof(db), "%d", (int)(head + 0.5f) % 360);
  textCenter(db, CXi - 14, CYi + 80, 4, C_TEXT);
  gfx->fillCircle(CXi + 30, CYi + 70, 4, C_TEXT);      // degree mark
  gfx->fillCircle(CXi + 30, CYi + 70, 2, C_BG);
  textCenter(compassCardinal(head), CXi, CYi + 130, 3, C_ACCENT);

  textCenter("dokun: kalibre", CXi, 446, 2, C_DIM);

  present();
  delay(33);
}
