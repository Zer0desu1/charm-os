// ============================================================================
// mod_notify.h - Module 8: Notifications (Bildirimler) - smartwatch style
//   The phone forwards its notifications (calls / messages / app alerts) just
//   like a real smartwatch:  /notify?type=&app=&title=&body=&color=
//     - type=call  -> full-screen INCOMING CALL ui (answer / decline buttons,
//                     pulsing rings, ringtone). Tap green = answer, red = reject.
//     - type=msg/mail/alarm/info -> a notification CARD (contact avatar + sender
//                     + body) slides in; the newest is shown first.
//   A scrollable FEED keeps the last few notifications: swipe to move between
//   them, tap to dismiss the current one. Backward compatible with the old
//   /notify?count=&text=&color= call.
//
//   The UI is designed for the 466x466 ROUND AMOLED: everything is centered and
//   kept inside the circle, with a contact-avatar look, a position ring, a tiny
//   status clock and soft intro animations.
// ============================================================================
#pragma once

#include <ctype.h>
#include "platform.h"

// ---- notification kinds -----------------------------------------------------
enum NotifKind { NK_INFO = 0, NK_MESSAGE = 1, NK_CALL = 2, NK_MAIL = 3, NK_ALARM = 4 };

struct Notif {
  uint8_t  kind;
  char     app[20];     // source app  ("WhatsApp", "Telefon", ...)
  char     title[28];   // sender / headline
  char     body[72];    // message body
  uint16_t color;       // accent (0 -> kind default)
  uint32_t at;          // millis() received
  bool     read;
};

#define NOTIF_MAX 8
static Notif    notif_buf[NOTIF_MAX];
static int      notif_n     = 0;        // how many slots used (newest at index 0)
static int      notif_cur   = 0;        // feed cursor (which one is shown)
static uint32_t notif_freshUntil = 0;   // unread highlight window
static uint32_t notif_introAt    = 0;   // (legacy) intro-anim start
static int      notify_count = 0;       // legacy: kept in sync with unread count

// momentum feed scrolling (cards slide with inertia + bounce at the ends)
static Scroller feedScroll;
static bool     feedInit = false;
static const float CARD_SLIDE = 430.0f; // px one card shifts off-screen
static const float FEED_STEP  = 300.0f; // finger px == one card

// ---- incoming call state ----------------------------------------------------
// 0=none  1=ringing  2=answered  3=rejected/ended (brief confirmation, then off)
static int      call_state   = 0;
static char     call_who[28] = "";
static char     call_app[20] = "";
static uint16_t call_color   = 0;
static uint32_t call_since    = 0;
static uint32_t call_endShow  = 0;      // how long to show the end/answer banner
static int      g_callAction  = 0;      // last action for the phone to poll: 1=answered 2=rejected

static uint16_t notifKindColor(uint8_t k) {
  switch (k) {
    case NK_CALL:    return rgb(52, 199, 120);   // soft green
    case NK_MESSAGE: return rgb(90, 130, 246);   // soft blue
    case NK_MAIL:    return rgb(245, 170, 66);   // amber
    case NK_ALARM:   return rgb(244, 110, 80);   // coral
    default:         return C_ACCENT;
  }
}

static int notifUnread() {
  int u = 0;
  for (int i = 0; i < notif_n; i++) if (!notif_buf[i].read) u++;
  return u;
}

// ---- public API -------------------------------------------------------------
// Push a notification into the feed (newest first). Calls are handled apart.
static void notifyPush(uint8_t kind, const String& app, const String& title,
                       const String& body, uint16_t color) {
  if (kind == NK_CALL) {
    call_state = 1;                          // start ringing
    call_color = color ? color : notifKindColor(NK_CALL);
    call_since = millis();
    String who = title.length() ? title : (body.length() ? body : String("Bilinmeyen"));
    who.toCharArray(call_who, sizeof(call_who));
    (app.length() ? app : String("Telefon")).toCharArray(call_app, sizeof(call_app));
    soundOk();
    return;
  }
  // shift the ring buffer down, insert new at front
  for (int i = min(notif_n, NOTIF_MAX - 1); i > 0; i--) notif_buf[i] = notif_buf[i - 1];
  Notif &nf = notif_buf[0];
  nf.kind  = kind;
  nf.color = color;
  nf.at    = millis();
  nf.read  = false;
  (app.length()  ? app  : String("Bildirim")).toCharArray(nf.app,  sizeof(nf.app));
  title.toCharArray(nf.title, sizeof(nf.title));
  body.toCharArray(nf.body,  sizeof(nf.body));
  if (notif_n < NOTIF_MAX) notif_n++;
  notif_cur = 0;                              // jump feed to the newest
  notif_freshUntil = millis() + 5000;
  notif_introAt    = millis();
  feedInit = false;                          // re-sync the scroller to the newest
  notify_count = notifUnread();
  soundTap();
}

static void notifyClearAll() {
  notif_n = 0; notif_cur = 0; notify_count = 0;
  call_state = 0;
}

// Backward-compatible shim for old callers (/notify?count=&text=&color=, and
// the ESP-NOW lovebox bridge). count==0 with empty text clears the feed.
static void notifySet(int count, uint32_t color565, const String& text) {
  if (count <= 0 && !text.length()) { notifyClearAll(); return; }
  notifyPush(NK_MESSAGE, "Bildirim", "", text, (uint16_t)color565);
}

static void notify_enter() { notif_cur = 0; notif_introAt = millis(); feedInit = false; }

// ----------------------------------------------------------------------------
// small drawing helpers (round-screen aware)
// ----------------------------------------------------------------------------

// Centered word-wrap (built-in font has no wrapping). Returns lines drawn.
static int drawWrapped(const char* s, int cx, int yTop, uint8_t size, uint16_t color,
                       int maxChars, int lineH, int maxLines) {
  if (!s || !s[0]) return 0;
  char line[64]; int li = 0, lines = 0, y = yTop;
  const char* w = s;
  auto flush = [&]() {
    if (li == 0) return;
    line[li] = 0; textCenter(line, cx, y, size, color);
    y += lineH; lines++; li = 0;
  };
  while (*w && lines < maxLines) {
    const char* sp = w; while (*sp && *sp != ' ') sp++;
    int wl = sp - w;
    if (li > 0 && li + 1 + wl > maxChars) { flush(); if (lines >= maxLines) break; }
    if (li > 0 && li < (int)sizeof(line) - 1) line[li++] = ' ';
    for (int k = 0; k < wl && li < (int)sizeof(line) - 1; k++) line[li++] = w[k];
    w = (*sp == ' ') ? sp + 1 : sp;
  }
  flush();
  return lines;
}

// First alphabetic char of a name, uppercased (0 if none).
static char nameInitial(const char* s) {
  for (const char* p = s; p && *p; p++)
    if (isalpha((unsigned char)*p)) return (char)toupper((unsigned char)*p);
  return 0;
}

// Smooth thick rounded handset arc (smile = receiver, frown = decline).
static void drawHandset(int cx, int cy, int s, uint16_t col, bool decline) {
  int R = s, thick = max(2, s / 3);
  float a0 = decline ? 198 : 18, a1 = decline ? 342 : 162;
  int steps = 12, ex0 = cx, ey0 = cy, ex1 = cx, ey1 = cy;
  for (int i = 0; i <= steps; i++) {
    float a = (a0 + (a1 - a0) * i / steps) * DEG_TO_RAD;
    int x = cx + (int)(R * cosf(a)), y = cy + (int)(R * sinf(a));
    gfx->fillCircle(x, y, thick, col);
    if (i == 0) { ex0 = x; ey0 = y; } if (i == steps) { ex1 = x; ey1 = y; }
  }
  gfx->fillCircle(ex0, ey0, thick + 2, col);   // ear / mouth pieces
  gfx->fillCircle(ex1, ey1, thick + 2, col);
}

// Glyph for a notification kind, drawn in `fg` with `bg` for cut-outs.
static void drawKindGlyph(uint8_t kind, int cx, int cy, int s, uint16_t fg, uint16_t bg) {
  switch (kind) {
    case NK_CALL:
      drawHandset(cx, cy, (int)(s * 0.78f), fg, false);
      break;
    case NK_MESSAGE: {                                   // speech bubble
      gfx->fillRoundRect(cx - s, cy - s + 2, 2 * s, (3 * s) / 2, s / 2, fg);
      gfx->fillTriangle(cx - s/2, cy + s/2, cx + 2, cy + s/2, cx - s + 2, cy + s, fg);
      for (int i = -1; i <= 1; i++) gfx->fillCircle(cx + i * (s/2), cy - s/4 + 2, max(1, s/8), bg);
    } break;
    case NK_MAIL: {                                      // envelope
      gfx->fillRoundRect(cx - s, cy - s/2 - 2, 2 * s, s + 6, 3, fg);
      gfx->drawLine(cx - s + 2, cy - s/2, cx, cy + s/4, bg);
      gfx->drawLine(cx + s - 2, cy - s/2, cx, cy + s/4, bg);
    } break;
    case NK_ALARM: {                                     // alarm clock
      gfx->fillCircle(cx, cy + 1, s, fg);
      gfx->fillCircle(cx, cy + 1, s - 3, bg);
      gfx->fillRect(cx - 1, cy - s/2, 2, s/2 + 1, fg);   // hands
      gfx->fillRect(cx, cy, s/2, 2, fg);
      gfx->fillCircle(cx - s + 2, cy - s + 2, 3, fg);    // bells
      gfx->fillCircle(cx + s - 2, cy - s + 2, 3, fg);
    } break;
    default: {                                           // bell
      gfx->fillRoundRect(cx - s + 2, cy - s, 2 * s - 4, (3 * s) / 2, s, fg);
      gfx->fillRect(cx - s, cy + s/2, 2 * s, 3, fg);
      gfx->fillCircle(cx, cy + s, 3, fg);
      gfx->fillRect(cx - 2, cy - s - 3, 4, 4, fg);
    } break;
  }
}

// Contact-style avatar: colored disc with the sender's initial, or the kind
// glyph when there is no name.
static void drawAvatar(int cx, int cy, int r, uint16_t col, uint8_t kind, const char* title) {
  gfx->fillCircle(cx, cy, r, col);
  char ini = (kind == NK_CALL || kind == NK_MESSAGE) ? nameInitial(title) : 0;
  if (ini) {
    char b[2] = { ini, 0 };
    textCenter(b, cx, cy, (uint8_t)max(3, r / 9 + 1), C_BG);
  } else {
    drawKindGlyph(kind, cx, cy, r / 2, C_BG, col);
  }
}

static const char* notifKindLabel(uint8_t k) {
  switch (k) {
    case NK_CALL:    return "Arama";
    case NK_MESSAGE: return "Mesaj";
    case NK_MAIL:    return "E-posta";
    case NK_ALARM:   return "Alarm";
    default:         return "Bildirim";
  }
}

static String notifAgo(uint32_t at) {
  uint32_t s = (millis() - at) / 1000;
  if (s < 5)    return "simdi";
  if (s < 60)   return String(s) + " sn once";
  if (s < 3600) return String(s / 60) + " dk once";
  return String(s / 3600) + " sa once";
}

// Tiny status clock at the very top (only when the phone has synced time).
static void drawStatusClock() {
  if (!timeValid()) return;
  char t[8]; snprintf(t, sizeof(t), "%02d:%02d", timeHour(), timeMin());
  textCenter(t, CXi, 30, 2, C_DIM);
}

// Rounded "app chip": small colored pill with the app/source name.
static void drawAppChip(const char* label, int cx, int cy, uint16_t col) {
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(1);
  int16_t bx, by; uint16_t bw, bh;
  gfx->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  gfx->setFont(NULL); gfx->setTextSize(1);
  int padX = 16, dot = 6, w = bw + padX * 2 + dot + 6, h = 30;
  int x = cx - w / 2, y = cy - h / 2;
  gfx->fillRoundRect(x, y, w, h, h / 2, C_BG2);
  gfx->fillCircle(x + padX, cy, dot, col);
  textCenter(label, cx + (dot + 6) / 2, cy, 2, C_TEXT);
}

// ----------------------------------------------------------------------------
// incoming call screen
// ----------------------------------------------------------------------------
static void drawCallScreen() {
  uint32_t now = millis();
  uint16_t col = call_color ? call_color : notifKindColor(NK_CALL);
  gfx->fillScreen(C_BG);
  drawStatusClock();

  if (call_state == 1) {                      // RINGING
    // soft expanding ripples behind the avatar
    for (int i = 0; i < 3; i++) {
      float ph = fmodf(now * 0.0014f + i * 0.34f, 1.0f);
      gfx->drawCircle(CXi, 180, 70 + (int)(ph * 110), C_BG2);
    }
    float beat = 0.5f + 0.5f * sinf(now * 0.006f);
    drawAvatar(CXi, 180, 60 + (int)(4 * beat), col, NK_CALL, call_who);

    textCenter("Gelen arama", CXi, 84, 2, C_DIM);
    textCenter(call_who[0] ? call_who : "Bilinmeyen", CXi, 290, 3, C_TEXT);
    if (call_app[0]) textCenter(call_app, CXi, 328, 2, C_DIM);

    // answer (green, left) + reject (red, right)
    bool blink = (now / 500) % 2;
    if (blink) gfx->fillCircle(158, 388, 49, C_BG2);     // pulsing halo on answer
    gfx->fillCircle(158, 388, 44, C_OK);
    gfx->fillCircle(308, 388, 44, C_DANGER);
    drawHandset(158, 388, 16, C_BG, false);
    drawHandset(308, 388, 16, C_BG, true);
    textCenter("cevapla", 158, 440, 2, C_OK);
    textCenter("reddet",  308, 440, 2, C_DANGER);
    if (g_soundTheme && (now / 700) % 2 == 0) soundPlay(880, 60);   // ring pulse
  } else if (call_state == 2) {               // ANSWERED
    drawAvatar(CXi, 175, 58, C_OK, NK_CALL, call_who);
    textCenter(call_who[0] ? call_who : "Gorusme", CXi, 280, 3, C_TEXT);
    uint32_t secs = (now - call_since) / 1000;
    char d[8]; snprintf(d, sizeof(d), "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    textCenter(d, CXi, 322, 2, C_OK);
    textCenter("ses telefonda", CXi, 356, 2, C_DIM);
    gfx->fillCircle(CXi, 416, 38, C_DANGER);             // hang-up button
    drawHandset(CXi, 416, 14, C_BG, true);
  } else if (call_state == 3) {               // ENDED / REJECTED confirmation
    gfx->fillCircle(CXi, 190, 58, C_BG2);
    drawKindGlyph(NK_CALL, CXi, 190, 30, C_DIM, C_BG2);
    textCenter("Arama bitti", CXi, 300, 3, C_DIM);
  }
  present();
}

static void callInput() {
  uint32_t now = millis();
  if (call_state == 1) {                      // ringing: hit-test the buttons
    if (g_g.tap) {
      int dxA = g_g.x - 158, dyA = g_g.y - 388;
      int dxR = g_g.x - 308, dyR = g_g.y - 388;
      if (dxA*dxA + dyA*dyA <= 62*62) {       // answer
        call_state = 2; g_callAction = 1; call_since = now; soundOk();
      } else if (dxR*dxR + dyR*dyR <= 62*62) {// reject
        call_state = 3; g_callAction = 2; call_endShow = now + 1200; soundError();
        notifyPush(NK_INFO, call_app, call_who, "Cevapsiz arama", C_DANGER);
      }
    }
  } else if (call_state == 2) {               // in call: tap hangs up
    if (g_g.tap) { call_state = 3; g_callAction = 2; call_endShow = now + 1200; soundTap(); }
  }
  if (call_state == 3 && now > call_endShow) call_state = 0;  // auto-dismiss banner
}

// ----------------------------------------------------------------------------
// notification feed
// ----------------------------------------------------------------------------
// Draw one notification card shifted horizontally by dx (for the slide/bounce).
static void drawNotifCard(int i, int dx) {
  if (i < 0 || i >= notif_n) return;
  uint32_t now = millis();
  Notif &nf = notif_buf[i];
  uint16_t col = nf.color ? nf.color : notifKindColor(nf.kind);
  int cx = CXi + dx;

  drawAppChip(nf.app[0] ? nf.app : notifKindLabel(nf.kind), cx, 72, col);
  textCenter(notifAgo(nf.at).c_str(), cx, 104, 2, C_DIM);

  int cardX = 56 + dx, cardY = 138, cardW = 354, cardH = 218;
  gfx->fillRoundRect(cardX, cardY, cardW, cardH, 22, C_BG2);
  gfx->drawRoundRect(cardX, cardY, cardW, cardH, 22, col);
  if (i == 0 && now < notif_freshUntil)
    gfx->drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 21, col);

  drawAvatar(cx, cardY + 6, 30, col, nf.kind, nf.title);
  if (!nf.read) gfx->fillCircle(cx + 26, cardY - 14, 6, C_DANGER);

  int y = cardY + 78;
  if (nf.title[0]) textCenter(nf.title, cx, y, 3, C_TEXT);
  else             textCenter(notifKindLabel(nf.kind), cx, y, 3, col);
  y += 44;
  if (nf.body[0]) drawWrapped(nf.body, cx, y, 2, C_DIM, 24, 30, 3);
}

static void drawFeed() {
  gfx->fillScreen(C_BG);
  drawStatusClock();

  if (notif_n == 0) {
    gfx->fillCircle(CXi, CYi - 34, 50, C_BG2);
    drawKindGlyph(NK_INFO, CXi, CYi - 34, 24, C_DIM, C_BG2);
    textCenter("Bildirim yok", CXi, CYi + 48, 3, C_TEXT);
    textCenter("telefon bildirimleri", CXi, CYi + 90, 2, C_DIM);
    textCenter("burada gorunur", CXi, CYi + 116, 2, C_DIM);
    present();
    return;
  }

  float pos = feedScroll.pos;
  notif_cur = constrain((int)lroundf(pos), 0, notif_n - 1);
  uint16_t curCol = notif_buf[notif_cur].color ? notif_buf[notif_cur].color
                                               : notifKindColor(notif_buf[notif_cur].kind);

  // continuous outer position ring (tracks the scroll, bounces with it)
  if (notif_n > 1) {
    float p = constrain(pos, 0.0f, (float)(notif_n - 1));
    fillRing(CXi, CYi, 221, 226, 0, 360.0f * (p + 1) / notif_n, curCol);
  }

  // draw the visible cards sliding horizontally with the scroll position
  int base = (int)floorf(pos);
  for (int i = base - 1; i <= base + 2; i++) {
    if (i < 0 || i >= notif_n) continue;
    int dx = (int)((i - pos) * CARD_SLIDE);
    if (dx <= -LCD_W || dx >= LCD_W) continue;
    drawNotifCard(i, dx);
  }

  // static chrome (does not slide)
  if (notif_n > 1) {
    int gap = 18, x0 = CXi - (notif_n - 1) * gap / 2;
    for (int i = 0; i < notif_n; i++)
      gfx->fillCircle(x0 + i * gap, 392, i == notif_cur ? 5 : 3, i == notif_cur ? curCol : C_BG2);
  }
  textCenter(notif_n > 1 ? "kaydir: gez  dokun: kapat" : "dokun: kapat", CXi, 430, 2, C_DIM);
  present();
}

static void feedInput() {
  if (notif_n == 0) { feedInit = false; return; }
  if (!feedInit) { scrollInit(feedScroll, constrain(notif_cur, 0, notif_n - 1), 0, notif_n - 1, true, true); feedInit = true; }
  scrollSetBounds(feedScroll, 0, notif_n - 1);

  if (g_g.down) {
    if (!feedScroll.active) scrollGrab(feedScroll);
    if (g_g.dragDX != 0) scrollDrag(feedScroll, -g_g.dragDX / FEED_STEP);
  } else if (feedScroll.active) {
    scrollRelease(feedScroll, g_g.released ? (-g_g.velX / FEED_STEP) : 0.0f);
    if (g_g.released) soundTap();
  }

  notif_cur = constrain((int)lroundf(feedScroll.pos), 0, notif_n - 1);

  // mark read once we've settled on a card
  if (!feedScroll.active && fabsf(feedScroll.pos - notif_cur) < 0.12f &&
      !notif_buf[notif_cur].read) {
    notif_buf[notif_cur].read = true; notify_count = notifUnread();
  }

  if (g_g.tap) {                              // dismiss current card
    for (int i = notif_cur; i < notif_n - 1; i++) notif_buf[i] = notif_buf[i + 1];
    notif_n--;
    notify_count = notifUnread();
    if (notif_n == 0) { feedInit = false; soundTap(); return; }
    notif_cur = constrain(notif_cur, 0, notif_n - 1);
    scrollInit(feedScroll, notif_cur, 0, notif_n - 1, true, true);   // re-center after removal
    soundTap();
  }
}

// ---- module tick ------------------------------------------------------------
static void notify_tick() {
  if (call_state != 0) {                      // a call always takes over the screen
    callInput();
    if (call_state != 0) { drawCallScreen(); delay(24); return; }
  }
  feedInput();
  scrollUpdate(feedScroll);     // advance inertia / edge-bounce each frame
  drawFeed();
  delay(16);                    // ~60fps for a smooth glide
}
