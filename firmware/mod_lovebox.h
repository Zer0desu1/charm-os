// ============================================================================
// mod_lovebox.h - Lovebox (Mesaj Kutusu)
//   Phone pushes a short message + emoji (/love?text=&emoji=). The device
//   shows a heart-burst animation and the message; a heart icon stays until
//   the receiver taps to read it (the classic "spinning heart = unread").
//
//   Local push (sender's phone connects to THIS device's AP). For remote
//   (different networks) a small cloud/MQTT relay would be needed - noted.
// ============================================================================
#pragma once

#include "platform.h"
#include "espnow.h"

static char love_msg[80]   = "";
static char love_emoji[12] = "";
static bool love_unread    = false;
static uint32_t love_at    = 0;

static void loveSet(const String& text, const String& emoji) {
  text.toCharArray(love_msg, sizeof(love_msg));
  emoji.toCharArray(love_emoji, sizeof(love_emoji));
  love_unread = true;
  love_at = millis();
}

// Inbound cloud dispatch (called from cloudPollTask)
static void cloudHandleLove(const String& text, const String& emoji) {
  loveSet(text, emoji);
}

// ---- device-to-device messaging (ESP-NOW) --------------------------------
// When a message is sent to this Vecta (from the phone), it is also broadcast
// so a nearby paired Vecta receives it and shows a notification + the message.
#define LOVE_MAGIC 0x5A
struct LovePkt { uint8_t magic; char text[80]; char emoji[12]; } __attribute__((packed));

static void loveBroadcast(const String& text, const String& emoji) {
  LovePkt p = {};
  p.magic = LOVE_MAGIC;
  text.toCharArray(p.text, sizeof(p.text));
  emoji.toCharArray(p.emoji, sizeof(p.emoji));
  // ACK'li unicast: eslesmis Vecta'lara kayipsiz ulasir (peser yoksa broadcast)
  espnowSendReliable((uint8_t*)&p, sizeof(p));
}

// On receive: store the message (lovebox heart) AND raise a notification orb,
// then auto-open the Notification module so the other person notices it.
static void loveHandlePacket(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(LovePkt)) return;
  const LovePkt* p = (const LovePkt*)data;
  if (p->magic != LOVE_MAGIC) return;
  loveSet(String(p->text), String(p->emoji));
  soundNotifyChirp();
  notifyPush(NK_MESSAGE, "Lovebox", "Esin", String(p->text), C_DANGER);
  int idx = findModuleIdx("notify");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
}

static void love_enter() {}

static void drawHeart(int cx, int cy, int s, uint16_t col) {
  gfx->fillCircle(cx - s/2, cy - s/3, s/2, col);
  gfx->fillCircle(cx + s/2, cy - s/3, s/2, col);
  gfx->fillTriangle(cx - s, cy - s/6, cx + s, cy - s/6, cx, cy + s, col);
}

static void love_tick() {
  uint32_t now = millis();
  if (g_g.tap && love_unread) love_unread = false;   // mark read

  gfx->fillScreen(C_BG);

  if (love_unread) {
    // floating hearts rising
    for (int i = 0; i < 7; i++) {
      uint32_t ph = (now / 12 + i * 60) % 360;
      int hx = 60 + (i * 53) % 346 + (int)(20 * sinf((now*0.003f)+i));
      int hy = 420 - (int)((ph) * 1.1f);
      int sz = 10 + (i % 3) * 5;
      drawHeart(hx, hy, sz, (i % 2) ? C_DANGER : rgb(255,120,150));
    }
    // pulsing center heart
    float pulse = 0.5f + 0.5f * sinf(now * 0.006f);
    drawHeart(CXi, CYi - 30, 50 + (int)(14 * pulse), C_DANGER);
    if (love_emoji[0]) textCenter(love_emoji, CXi, CYi - 30, 4, C_TEXT);
    textCenter("YENI MESAJ", CXi, CYi + 70, 2, C_GOLD);
    if (love_msg[0]) textCenter(love_msg, CXi, CYi + 110, 2, C_TEXT);
    textCenter("dokun: oku", CXi, 440, 2, C_DIM);
  } else if (love_msg[0]) {
    // read state: show message calmly
    drawHeart(CXi, 120, 34, rgb(255,120,150));
    if (love_emoji[0]) textCenter(love_emoji, CXi, 200, 5, C_TEXT);
    textCenter(love_msg, CXi, 280, 3, C_TEXT);
    textCenter("okundu", CXi, 440, 2, C_DIM);
  } else {
    drawHeart(CXi, CYi - 20, 44, rgb(255,120,150));
    textCenter("mesaj bekleniyor", CXi, CYi + 60, 2, C_DIM);
  }
  present();
  delay(24);
}
