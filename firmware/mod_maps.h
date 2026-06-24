// ============================================================================
// mod_maps.h - Module: Harita (Maps / Navigation)
//   Shows a Google map on the device using the PHONE's location. Following the
//   project pattern (the phone does the heavy lifting and pushes pixels), the
//   phone fetches a Google **Static Maps** tile centered on its GPS position and
//   POSTs it as a JPEG to /map; the device decodes it (TJpg_Decoder) into a
//   466x466 RGB565 frame and renders it full-screen with a center location pin
//   and coord/zoom chips.
//
//   - Tap          -> cycle zoom level + ask the phone for a fresh tile.
//   - Long-press   -> home (handled globally).
//   The phone polls GET /map?poll=1 to learn the wanted zoom / refresh flag and
//   keeps the tile updated as the user moves (live map).
// ============================================================================
#pragma once

#include "platform.h"
#include "mag.h"
#include "mpu.h"
#include "heading.h"
#include "deadreckon.h"

static const size_t MAP_FRAME = (size_t)LCD_W * LCD_H * 2;   // 466x466 RGB565
static uint8_t* maps_img = nullptr;        // current map tile (PSRAM, BE RGB565)
static bool     maps_has = false;
static uint32_t maps_at  = 0;              // when the tile arrived
static uint32_t maps_seq = 0;              // bumps on every new frame (mirror)
static double   maps_lat = 0, maps_lon = 0;
static int      maps_zoom = 15;
static volatile bool maps_refresh = false; // phone polls -> push a fresh tile
static bool     maps_needLib = false;      // set by /map when JPEG decoder is missing
static bool     maps_raw     = false;      // mirror mode: draw the frame as-is (no overlay)

static const int MAP_ZOOMS[] = { 13, 15, 17, 19 };
static const int MAP_NZOOM   = sizeof(MAP_ZOOMS) / sizeof(MAP_ZOOMS[0]);
static int maps_zoomIdx = 1;               // -> MAP_ZOOMS[1] == 15

// Yansitma (mirror) sirasinda saat ekranindaki dokunuslar telefona iletilir:
// her tamamlanan vurus (tap/swipe) kuyruga girer, uygulama GET /touch ile
// ceker ve AccessibilityService ile telefon ekranina enjekte eder.
struct MapsTouchEv { int16_t x0, y0, x1, y1; uint16_t ms; };
static MapsTouchEv  maps_tq[6];
static volatile int maps_tqN = 0;
static bool     mapsT_down = false;
static int      mapsT_x0 = 0, mapsT_y0 = 0, mapsT_x1 = 0, mapsT_y1 = 0;
static uint32_t mapsT_at = 0;

// Allocate a fresh PSRAM frame for the JPEG decoder to fill (net.h owns fill).
static uint8_t* mapsAllocFrame() {
  return (uint8_t*) heap_caps_malloc(MAP_FRAME, MALLOC_CAP_SPIRAM);
}

// Hand a finished 466x466 BE-RGB565 tile to the module (takes ownership) and
// return the PREVIOUS frame so the caller can reuse it for the next decode --
// the mirror stream then runs with two recycled buffers instead of a 434KB
// malloc+free per frame.
static uint8_t* mapsSwapImage(uint8_t* buf) {
  if (!buf) return nullptr;
  uint8_t* old = maps_img;
  maps_img = buf; maps_has = true; maps_at = millis(); maps_seq++;
  return old;
}

static void mapsSetLoc(double lat, double lon, int zoom) {
  maps_lat = lat; maps_lon = lon;
  if (zoom > 0) {
    maps_zoom = zoom;
    for (int i = 0; i < MAP_NZOOM; i++) if (MAP_ZOOMS[i] == zoom) maps_zoomIdx = i;
  }
}

static void maps_enter() {}

// device facing direction comes from the shared compass+gyro fusion (heading.h);
// drUpdate() in the main loop already advances it, so read the latest value.
static float mapsFusedHeading() { return motionPresent() ? headingLast() : magHeading(); }

// heading cone + directional dot at the map centre
static void drawHeadingCone(int cx, int cy, float headDeg) {
  float a = headDeg * DEG_TO_RAD;
  int r = 78, half = 22;
  float aL = (headDeg - half) * DEG_TO_RAD, aR = (headDeg + half) * DEG_TO_RAD;
  int tx = cx + (int)(r * sinf(a)),  ty = cy - (int)(r * cosf(a));
  int lx = cx + (int)(r * sinf(aL)), ly = cy - (int)(r * cosf(aL));
  int rx = cx + (int)(r * sinf(aR)), ry = cy - (int)(r * cosf(aR));
  gfx->fillTriangle(cx, cy, lx, ly, tx, ty, C_ACCENT);
  gfx->fillTriangle(cx, cy, rx, ry, tx, ty, C_ACCENT);
  gfx->fillCircle(cx, cy, 9, WHITE);
  gfx->fillCircle(cx, cy, 6, C_DANGER);
}

// Classic teardrop location pin centered on the map (= device/phone position).
static void drawMapPin(int cx, int cy) {
  gfx->fillTriangle(cx - 13, cy - 8, cx + 13, cy - 8, cx, cy + 16, C_DANGER);
  gfx->fillCircle(cx, cy - 16, 15, C_DANGER);
  gfx->fillCircle(cx, cy - 16, 14, C_DANGER);
  gfx->fillCircle(cx, cy - 16, 6, WHITE);
  gfx->fillCircle(cx, cy + 18, 3, rgb(0, 0, 0));   // ground shadow dot
}

// Dark rounded chip so text stays readable over arbitrary map imagery.
static void mapsChip(const char* s, int cx, int cy, uint16_t txt) {
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(1);
  int16_t bx, by; uint16_t bw, bh;
  gfx->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  gfx->setFont(NULL); gfx->setTextSize(1);
  int w = bw + 28, h = 30, x = cx - w / 2, y = cy - h / 2;
  gfx->fillRoundRect(x, y, w, h, h / 2, C_BG);
  gfx->drawRoundRect(x, y, w, h, h / 2, C_BG2);
  textCenter(s, cx, cy, 2, txt);
}

static void maps_tick() {
  // tap: cycle zoom and request a fresh tile from the phone
  // (mirror modunda DEGIL: orada dokunus telefona iletilir)
  if (g_g.tap && !maps_raw) {
    maps_zoomIdx = (maps_zoomIdx + 1) % MAP_NZOOM;
    maps_zoom = MAP_ZOOMS[maps_zoomIdx];
    maps_refresh = true;
    soundTap();
  }

  if (maps_has && maps_img) {
    if (maps_raw) {
      // dokunuslari vurus (stroke) olarak biriktir -> /touch kuyrugu.
      // Hareketsiz uzun basis kuyruga GIRMEZ (o, modulden cikis jesti).
      RawTouch t = touchRead();
      if (t.down) {
        if (!mapsT_down) { mapsT_down = true; mapsT_x0 = t.x; mapsT_y0 = t.y; mapsT_at = millis(); }
        mapsT_x1 = t.x; mapsT_y1 = t.y;
      } else if (mapsT_down) {
        mapsT_down = false;
        uint32_t dur = millis() - mapsT_at;
        int mdx = mapsT_x1 - mapsT_x0, mdy = mapsT_y1 - mapsT_y0;
        bool moved = (mdx * mdx + mdy * mdy) > 400;          // ~20px ustu = kaydirma
        if ((moved || dur < 600) && maps_tqN < (int)(sizeof(maps_tq) / sizeof(maps_tq[0]))) {
          maps_tq[maps_tqN] = { (int16_t)mapsT_x0, (int16_t)mapsT_y0,
                                (int16_t)mapsT_x1, (int16_t)mapsT_y1,
                                (uint16_t)min(dur, (uint32_t)2000) };
          maps_tqN = maps_tqN + 1;
        }
      }

      // screen-mirror: redraw ONLY when a new frame arrived. The old code
      // re-blitted + re-flushed the SAME frame every 20ms (~30ms/flush), which
      // starved netLoop and delayed the next incoming frame -> choppy stream.
      static uint32_t shownSeq = 0;
      if (maps_seq != shownSeq) {
        shownSeq = maps_seq;
        gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t*)maps_img, LCD_W, LCD_H);
        present();
      }
      delay(3);                     // hemen netLoop'a don: siradaki kareyi al
      return;
    }
    gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t*)maps_img, LCD_W, LCD_H);
    float fh = mapsFusedHeading();
    if (fh >= 0) drawHeadingCone(CXi, CYi, fh);   // facing direction (compass+gyro)
    else         drawMapPin(CXi, CYi);            // no sensors -> plain pin

    String coord = String(maps_lat, 4) + ", " + String(maps_lon, 4);
    mapsChip(coord.c_str(), CXi, 34, C_TEXT);

    // rough dead-reckoning route inset (steps x heading) + distance walked
    if (motionPresent() && drCount() >= 2) {
      int ix = 84, iy = 200, ir = 58;
      gfx->fillRoundRect(ix - ir - 6, iy - ir - 6, 2 * (ir + 6), 2 * (ir + 6) + 24, 10, C_BG);
      gfx->drawRoundRect(ix - ir - 6, iy - ir - 6, 2 * (ir + 6), 2 * (ir + 6) + 24, 10, C_BG2);
      drDrawTrail(ix, iy, ir);
      char d[16];
      if (drDistance() >= 1000) snprintf(d, sizeof(d), "%.1f km", drDistance() / 1000);
      else                      snprintf(d, sizeof(d), "%d m", (int)drDistance());
      textCenter(d, ix, iy + ir + 8, 2, C_TEXT);
    }

    String z = String("zoom ") + maps_zoom + (maps_refresh ? "  guncelleniyor..." : "");
    mapsChip(z.c_str(), CXi, 432, maps_refresh ? C_GOLD : C_TEXT);
    present();
    delay(40);
    return;
  }

  // ---- placeholder (no tile yet) ----
  gfx->fillScreen(C_BG);
  uint32_t now = millis();
  // simple map/pin motif
  gfx->drawRoundRect(CXi - 70, CYi - 78, 140, 120, 14, C_BG2);
  for (int i = 1; i < 4; i++) gfx->drawFastHLine(CXi - 70, CYi - 78 + i * 30, 140, C_BG2);
  float bob = sinf(now * 0.004f) * 4;
  drawMapPin(CXi, CYi - 30 + (int)bob);

  textCenter("HARITA", CXi, 70, 3, C_GOLD);
  if (maps_needLib) {                    // firmware compiled without the decoder
    textCenter("TJpg_Decoder", CXi, CYi + 78, 2, C_DANGER);
    textCenter("kutuphanesi gerekli", CXi, CYi + 104, 2, C_DANGER);
    textCenter("kur + yeniden yukle", CXi, CYi + 134, 2, C_DIM);
  } else if (g_myLoc.valid) {
    String coord = String(g_myLoc.lat, 4) + ", " + String(g_myLoc.lon, 4);
    textCenter(coord.c_str(), CXi, CYi + 80, 2, C_TEXT);
    textCenter("telefondan harita", CXi, CYi + 120, 2, C_DIM);
    textCenter("bekleniyor...", CXi, CYi + 146, 2, C_DIM);
    maps_refresh = true;                 // nudge the phone to send a tile
  } else {
    textCenter("konum yok", CXi, CYi + 90, 2, C_DIM);
    textCenter("telefonda konumu ac", CXi, CYi + 130, 2, C_DIM);
  }
  present();
  delay(60);
}
