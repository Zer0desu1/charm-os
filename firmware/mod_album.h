// ============================================================================
// mod_album.h - Photo Album (Foto Albumu)
//   Holds several full-screen RGB565 photos in PSRAM and cycles through them.
//   Phone appends photos one by one (POST /album, 466x466 RGB565 BE).
//   Auto-advances every few seconds; tap = next now. Long-press = home.
// ============================================================================
#pragma once

#include "platform.h"

#define ALBUM_MAX 12
static uint8_t* album_imgs[ALBUM_MAX] = {nullptr};
static int      album_count = 0;
static int      album_idx = 0;
static uint32_t album_lastAt = 0;
static const uint32_t ALBUM_DWELL = 4000;   // ms per photo
static const size_t ALBUM_FRAME = (size_t)LCD_W * LCD_H * 2;

// append one full-screen frame (takes ownership of buf). Returns false if full.
static bool albumAppend(uint8_t* buf) {
  if (album_count >= ALBUM_MAX) return false;
  album_imgs[album_count++] = buf;
  return true;
}
static void albumClear() {
  for (int i = 0; i < album_count; i++) {
    if (album_imgs[i]) heap_caps_free(album_imgs[i]);
    album_imgs[i] = nullptr;
  }
  album_count = 0; album_idx = 0;
}

static void album_enter() { album_idx = 0; album_lastAt = 0; }

static void albumShow(int i) {
  if (i < 0 || i >= album_count || !album_imgs[i]) return;
  gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t*)album_imgs[i], LCD_W, LCD_H);
  // page dots
  for (int d = 0; d < album_count && d < ALBUM_MAX; d++) {
    int x = CXi - (album_count * 7) + d * 14;
    gfx->fillCircle(x, 446, 4, d == i ? C_TEXT : C_BG2);
  }
  present();
}

static void album_tick() {
  uint32_t now = millis();
  if (album_count == 0) {
    gfx->fillScreen(C_BG);
    textCenter("ALBUM BOS", CXi, CYi - 20, 3, C_GOLD);
    textCenter("telefondan foto ekle", CXi, CYi + 30, 2, C_DIM);
    present(); delay(120); return;
  }
  if (g_g.tap) { album_idx = (album_idx + 1) % album_count; album_lastAt = now; albumShow(album_idx); return; }
  if (album_lastAt == 0 || now - album_lastAt >= ALBUM_DWELL) {
    albumShow(album_idx);
    album_idx = (album_idx + 1) % album_count;
    album_lastAt = now;
  }
  delay(30);
}
