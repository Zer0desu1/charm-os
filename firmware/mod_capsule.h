// ============================================================================
// mod_capsule.h - Time Capsule (Zaman Kapsulu)
//   Schedule a message + emoji to "open" at a future moment. When the time
//   comes the capsule pops with a sparkle animation and stays until read.
//   Up to 8 capsules stored in NVS as a compact blob.
// ============================================================================
#pragma once

#include "platform.h"

#define CAPSULE_MAX 8
struct Capsule {
  uint32_t openAt;          // unix seconds (already tz-adjusted)
  uint8_t  opened;          // 0/1
  uint8_t  read;            // 0/1
  char     emoji[8];
  char     text[80];
};

static Capsule caps_arr[CAPSULE_MAX];
static int     caps_count = 0;
static int     caps_focused = -1;     // capsule popping right now (-1 none)
static uint32_t caps_popAt = 0;

static void capsLoad() {
  size_t want = sizeof(caps_arr);
  size_t got = g_prefs.getBytes("caps", caps_arr, want);
  caps_count = (int)(got / sizeof(Capsule));
  if (caps_count < 0 || caps_count > CAPSULE_MAX) caps_count = 0;
}
static void capsSave() {
  g_prefs.putBytes("caps", caps_arr, caps_count * sizeof(Capsule));
}

static bool capsAdd(uint32_t openAt, const String& text, const String& emoji) {
  if (caps_count >= CAPSULE_MAX) return false;
  Capsule& c = caps_arr[caps_count++];
  memset(&c, 0, sizeof(c));
  c.openAt = openAt; c.opened = 0; c.read = 0;
  emoji.toCharArray(c.emoji, sizeof(c.emoji));
  text.toCharArray(c.text,  sizeof(c.text));
  capsSave();
  return true;
}

static void capsule_enter() { capsLoad(); }

static void capsCheckDue() {
  if (!timeValid()) return;
  uint32_t now = timeNow();
  for (int i = 0; i < caps_count; i++) {
    if (!caps_arr[i].opened && caps_arr[i].openAt <= now) {
      caps_arr[i].opened = 1; capsSave();
      caps_focused = i; caps_popAt = millis();
    }
  }
  if (caps_focused < 0) {
    // pick newest unread to display
    for (int i = caps_count - 1; i >= 0; i--) {
      if (caps_arr[i].opened && !caps_arr[i].read) { caps_focused = i; break; }
    }
  }
}

static void drawSpark(int x, int y, uint16_t col) {
  gfx->fillCircle(x, y, 2, col);
  gfx->drawPixel(x - 4, y, col); gfx->drawPixel(x + 4, y, col);
  gfx->drawPixel(x, y - 4, col); gfx->drawPixel(x, y + 4, col);
}

static void capsule_tick() {
  capsCheckDue();
  uint32_t now = millis();
  gfx->fillScreen(C_BG);

  if (caps_focused < 0) {
    // no capsule to show -> upcoming list
    textCenter("ZAMAN KAPSULU", CXi, 70, 2, C_GOLD);
    if (caps_count == 0) {
      textCenter("kapsulun yok", CXi, CYi, 2, C_DIM);
      textCenter("telefondan ekle", CXi, CYi + 30, 2, C_DIM);
    } else if (!timeValid()) {
      textCenter("saat senkronu yok", CXi, CYi, 2, C_WARN);
    } else {
      int y = 150, shown = 0;
      uint32_t nowS = timeNow();
      for (int i = 0; i < caps_count && shown < 5; i++) {
        if (caps_arr[i].opened) continue;
        long secs = (long)caps_arr[i].openAt - (long)nowS;
        char b[40];
        if (secs < 0) snprintf(b, sizeof(b), "%s simdi acilacak", caps_arr[i].emoji);
        else if (secs < 3600) snprintf(b, sizeof(b), "%s %ld dk", caps_arr[i].emoji, secs / 60);
        else if (secs < 86400) snprintf(b, sizeof(b), "%s %ld saat", caps_arr[i].emoji, secs / 3600);
        else snprintf(b, sizeof(b), "%s %ld gun", caps_arr[i].emoji, secs / 86400);
        textCenter(b, CXi, y, 2, C_TEXT);
        y += 36; shown++;
      }
    }
    present(); delay(200); return;
  }

  // showing capsule[caps_focused]
  Capsule& c = caps_arr[caps_focused];
  bool fresh = (now - caps_popAt) < 4000;
  float pulse = 0.5f + 0.5f * sinf(now * 0.006f);

  // sparkles around
  for (int i = 0; i < 14; i++) {
    float a = (now * 0.0015f) + i * (TWO_PI / 14);
    int r = 170 + (int)(20 * sinf(now * 0.004f + i));
    int x = CXi + (int)(r * cosf(a));
    int y = CYi + (int)(r * sinf(a));
    drawSpark(x, y, (i % 2) ? C_GOLD : C_ACCENT);
  }
  gfx->fillCircle(CXi, CYi - 30, 70 + (int)(8 * pulse), C_GOLD);
  if (c.emoji[0]) textCenter(c.emoji, CXi, CYi - 30, 5, C_BG);

  textCenter(fresh ? "KAPSUL ACILDI" : "KAPSUL", CXi, CYi + 60, 2, C_GOLD);
  if (c.text[0]) textCenter(c.text, CXi, CYi + 110, 2, C_TEXT);
  textCenter(c.read ? "okundu" : "dokun: oku", CXi, 440, 2, C_DIM);

  if (g_g.tap) {
    c.read = 1; capsSave();
    caps_focused = -1;
  }
  present(); delay(40);
}
