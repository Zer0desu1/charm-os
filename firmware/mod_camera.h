// ============================================================================
// mod_camera.h - Bluetooth Camera Shutter Remote
//   Sends "Volume Up" over BLE HID. iOS/Android camera apps treat volume-up
//   as the shutter button, so the device works as a remote tripod trigger.
//   Reuses the same "Vecta" BLE keyboard as the knob; pair once from settings.
// ============================================================================
#pragma once

#include "platform.h"
// HAVE_BLE_HID and the shared bleKb / knob_started come from mod_knob.h,
// which is included before this file in Vecta.ino (single translation unit).

// ---- shot preview -----------------------------------------------------------
// After the shutter fires, the phone app pushes the just-taken photo to
// POST /photo?cam=1 (net.h). The decoded 466x466 frame lands here and is shown
// full-screen in this module until the user taps (or 10 s passes).
static uint8_t* cam_preview   = nullptr;   // BE RGB565 frame (PSRAM), owned here
static uint32_t cam_previewAt = 0;

static void cameraSetPreview(uint8_t* frame) {   // takes ownership of frame
  if (cam_preview) heap_caps_free(cam_preview);
  cam_preview = frame;
  cam_previewAt = millis();
}

// Draw the preview if present. Returns true when it consumed this tick.
static bool cameraPreviewTick() {
  if (!cam_preview) return false;
  if (g_g.tap || millis() - cam_previewAt > 10000) {   // tap = back to shutter
    heap_caps_free(cam_preview); cam_preview = nullptr;
    soundTap();
    return false;
  }
  gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t*)cam_preview, LCD_W, LCD_H);
  textCenter("dokun: kapat", CXi, 446, 2, C_TEXT);
  present();
  delay(30);
  return true;
}

#ifdef HAVE_BLE_HID

enum CamMode { CAM_FOTO = 0, CAM_SERI = 1, CAM_COUNT = 2 };
static const char* CAM_NAMES[CAM_COUNT] = { "FOTO", "SERI" };
static int      cam_mode = 0;
static uint32_t cam_flashUntil = 0;

static void camera_enter() {
  if (!knob_started) {
    bleKb.begin();
    knob_started = true;
  }
}

static void camera_tick() {
  if (cameraPreviewTick()) return;         // çekilen foto ekranda — dokunana kadar

  bleKb.keepAlive();                       // auto-reconnect to a bonded phone
  bool connected = bleKb.isConnected();
  uint32_t now = millis();

  // swipe left/right -> change capture mode
  if (g_g.swipeLeft)  { cam_mode = (cam_mode + 1) % CAM_COUNT;             soundTap(); }
  if (g_g.swipeRight) { cam_mode = (cam_mode + CAM_COUNT - 1) % CAM_COUNT; soundTap(); }

  // tap -> shutter. Flash/beep ALWAYS fire (proves the tap registered); the BLE
  // key only sends when connected (so we can tell tap vs BLE problems apart).
  if (g_g.tap) {
    cam_flashUntil = now + 250;
    soundOk();
    if (connected) {
      if (cam_mode == CAM_SERI) {
        for (int i = 0; i < 3; i++) { bleKb.write(KEY_MEDIA_VOLUME_UP); delay(180); }  // burst
      } else {
        bleKb.write(KEY_MEDIA_VOLUME_UP);                                              // single shot
      }
    }
  }

  bool flash = now < cam_flashUntil;
  gfx->fillScreen(C_BG);

  // mode tabs across the top
  for (int i = 0; i < CAM_COUNT; i++) {
    bool act = (i == cam_mode);
    textCenter(CAM_NAMES[i], CXi + (i - 1) * 120, 44, act ? 3 : 2, act ? C_GOLD : C_DIM);
  }
  textCenter("< kaydir: mod >", CXi, 84, 2, C_DIM);

  // shutter
  uint16_t ring = connected ? C_ACCENT : C_DIM;
  for (int r = 122; r >= 106; r -= 2) gfx->drawCircle(CXi, CYi, r, ring);
  gfx->fillCircle(CXi, CYi, 96, flash ? C_BG : C_TEXT);
  gfx->fillCircle(CXi, CYi, 84, flash ? C_TEXT : C_DANGER);
  textCenter(cam_mode == CAM_SERI ? "x3" : "CEK", CXi, CYi, 3, flash ? C_DANGER : C_BG);

  const char* hint = cam_mode == CAM_SERI ? "dokun: 3 cekim" : "dokun: foto cek";
  textCenter(connected ? hint : "BLE bekliyor - Vecta ile eslestir", CXi, 420, 2,
             connected ? C_OK : C_WARN);
  textCenter("eslesme: Bluetooth -> Vecta", CXi, 446, 2, C_DIM);
  present();
  delay(20);
}

#else
static void camera_enter() {}
static void camera_tick() {
  if (cameraPreviewTick()) return;
  gfx->fillScreen(C_BG);
  textCenter("KAMERA DEKLANSORU", CXi, 150, 3, C_GOLD);
  textCenter("BLE kutuphanesi gerekli", CXi, 220, 2, C_DIM);
  textCenter("ESP32 BLE Keyboard kur", CXi, 250, 2, C_TEXT);
  present(); delay(200);
}
#endif
