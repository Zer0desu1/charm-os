// ============================================================================
// mod_charm.h - Module 1: "Canta Susu" (Bag Charm)
//   Decorative always-on display. Shows a phone-pushed image/animation
//   (RGB565 frames), or falls back to a name tag + clock + sparkle when none.
//   Animation upload handled in net.h via charmStoreAnimation().
// ============================================================================
#pragma once

#include "platform.h"

// ---- pushed animation storage (PSRAM) ----
static uint8_t* charm_buf = nullptr;
static size_t   charm_bytes = 0;
static uint16_t charm_frames = 0;
static uint16_t charm_frameMs = 100;
static uint16_t charm_w = 0, charm_h = 0;
static SemaphoreHandle_t charm_lock = nullptr;
static bool charm_has = false;

static uint32_t charm_lastFrameAt = 0;
static uint16_t charm_curFrame = 0;

// Called from net.h after a full upload is staged.
static bool charmStoreAnimation(uint8_t* data, size_t bytes, uint16_t frames,
                                uint16_t frameMs, uint16_t w, uint16_t h) {
  if (!charm_lock) charm_lock = xSemaphoreCreateMutex();
  xSemaphoreTake(charm_lock, portMAX_DELAY);
  if (charm_buf) heap_caps_free(charm_buf);
  charm_buf = data; charm_bytes = bytes;
  charm_frames = frames; charm_frameMs = frameMs;
  charm_w = w; charm_h = h;
  charm_curFrame = 0; charm_lastFrameAt = 0;
  charm_has = true;
  xSemaphoreGive(charm_lock);
  return true;
}

static void charm_enter() {
  charm_curFrame = 0; charm_lastFrameAt = 0;
}

static void charmDrawFallback() {
  gfx->fillScreen(C_BG);
  // sparkle ring
  uint32_t t = millis();
  for (int i = 0; i < 12; i++) {
    float a = (t * 0.0015f) + i * (TWO_PI / 12);
    int r = 180 + (int)(20 * sinf(t * 0.003f + i));
    int x = CXi + (int)(r * cosf(a));
    int y = CYi + (int)(r * sinf(a));
    gfx->fillCircle(x, y, 3, (i % 2) ? C_GOLD : C_ACCENT);
  }
  String name = setGetStr("charm_name", "CHARM");
  textCenter(name.c_str(), CXi, CYi - 10, 5, C_TEXT);
  textCenter("bag charm", CXi, CYi + 50, 2, C_DIM);
}

// Scaled full-screen buffer (nearest-neighbor upscale of a small frame).
static uint8_t* charm_scaled = nullptr;

static void charm_tick() {
  if (charm_has && charm_buf) {
    xSemaphoreTake(charm_lock, portMAX_DELAY);
    uint16_t fc = charm_frames;
    uint16_t fm = charm_frameMs;
    uint16_t w = charm_w, h = charm_h;
    uint32_t now = millis();
    if (charm_lastFrameAt == 0 || (fc > 1 && now - charm_lastFrameAt >= (fm ? fm : 100))) {
      size_t frameBytes = (size_t)w * h * 2;
      uint8_t* fr = charm_buf + (size_t)charm_curFrame * frameBytes;
      if (w == LCD_W && h == LCD_H) {
        gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t*)fr, w, h);   // already full size
      } else {
        // Upscale (nearest-neighbor) to fill the whole 466x466 screen. Frames
        // are square so this keeps aspect. Byte-copy preserves big-endian order.
        if (!charm_scaled) charm_scaled = (uint8_t*) heap_caps_malloc((size_t)LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
        if (charm_scaled) {
          for (int y = 0; y < LCD_H; y++) {
            int sy = (int)((uint32_t)y * h / LCD_H);
            const uint8_t* srow = fr + (size_t)sy * w * 2;
            uint8_t* drow = charm_scaled + (size_t)y * LCD_W * 2;
            for (int x = 0; x < LCD_W; x++) {
              int sx = (int)((uint32_t)x * w / LCD_W);
              drow[x*2]   = srow[sx*2];
              drow[x*2+1] = srow[sx*2+1];
            }
          }
          gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t*)charm_scaled, LCD_W, LCD_H);
        } else {
          int ox = (LCD_W - w) / 2, oy = (LCD_H - h) / 2;
          gfx->fillScreen(BLACK);
          gfx->draw16bitBeRGBBitmap(ox, oy, (uint16_t*)fr, w, h);
        }
      }
      present();
      charm_curFrame = (charm_curFrame + 1) % (fc ? fc : 1);
      charm_lastFrameAt = now;
    }
    xSemaphoreGive(charm_lock);
    delay(fc > 1 ? 5 : 50);     // don't busy-spin between frames (battery)
  } else {
    charmDrawFallback();
    present();
    delay(30);
  }
}
