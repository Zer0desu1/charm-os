// ============================================================================
// mod_collar.h - Module 3: Smart Pet Collar (Akilli Tasma)
//   * ID card: pet name + owner phone (set from phone, stored in NVS)
//   * Lost mode: flashing "KAYIP" + big phone number (+ QR if qrcode lib present)
//   * "Find me": phone triggers a bright attention flash (collarFindMe()).
//   No GPS - this is an ID tag + attention beacon, honest about that.
//   Tap = toggle lost mode. Long-press = home.
//
//   Optional QR: install the "QRCode" Arduino library (by Richard Moore) to get
//   a scannable tel: QR in lost mode. Without it, the phone number is shown big.
// ============================================================================
#pragma once

#include "platform.h"
#include "mpu.h"
// HAVE_QRCODE comes from platform.h when USE_QRCODE is enabled there.

static bool     collar_lost = false;
static uint32_t collar_findUntil = 0;   // bright flash until this millis
static int      collar_page = 0;        // 0 = ID card, 1 = activity/health

static void collarSetInfo(const String& name, const String& phone, const String& owner) {
  if (name.length())  setPutStr("col_name", name.substring(0, 16));
  if (phone.length()) setPutStr("col_phone", phone.substring(0, 20));
  if (owner.length()) setPutStr("col_owner", owner.substring(0, 16));
}
static void collarSetLost(bool lost) { collar_lost = lost; }
static void collarFindMe(uint32_t ms = 4000) { collar_findUntil = millis() + ms; }

static void collar_enter() {}

#ifdef HAVE_QRCODE
static void collarDrawQR(const char* text, int cx, int cy, int scale) {
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(4)];
  qrcode_initText(&qr, buf, 4, ECC_LOW, text);
  int n = qr.size;
  int total = n * scale;
  int x0 = cx - total / 2, y0 = cy - total / 2;
  gfx->fillRect(x0 - 6, y0 - 6, total + 12, total + 12, C_TEXT);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (qrcode_getModule(&qr, x, y))
        gfx->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, BLACK);
}
#endif

// --- ID-card art (matches the pet-collar design) ---------------------------
// Small gold cat silhouette, side profile facing right, tail curling up.
static void drawPetIcon(int cx, int cy, uint16_t col) {
  gfx->fillRoundRect(cx - 22, cy - 7, 40, 15, 7, col);   // body
  gfx->fillRoundRect(cx + 6,  cy - 16, 14, 14, 6, col);  // neck/chest
  gfx->fillCircle(cx + 20, cy - 18, 9, col);             // head
  gfx->fillTriangle(cx + 13, cy - 25, cx + 18, cy - 15, cx + 10, cy - 16, col); // ear
  gfx->fillTriangle(cx + 24, cy - 27, cx + 29, cy - 16, cx + 21, cy - 17, col); // ear
  gfx->fillRect(cx - 18, cy + 6, 5, 16, col);            // legs
  gfx->fillRect(cx - 6,  cy + 6, 5, 16, col);
  gfx->fillRect(cx + 6,  cy + 6, 5, 16, col);
  gfx->fillRect(cx + 13, cy + 6, 5, 16, col);
  for (int i = 0; i < 12; i++) {                         // tail, curls up
    float a = i / 11.0f;
    int tx = cx - 24 - (int)(sinf(a * 3.14159f) * 8);
    int ty = cy + 4 - (int)(a * 28);
    gfx->fillCircle(tx, ty, 4, col);
  }
}

// Classic telephone handset (~20px), top-left at (x,y).
static void drawPhoneGlyph(int x, int y, uint16_t col) {
  gfx->fillRoundRect(x,     y,     9, 7, 3, col);        // earpiece cup
  gfx->fillRoundRect(x + 9, y + 9, 9, 7, 3, col);        // mouthpiece cup
  for (int t = -2; t <= 2; t++)                          // thick handle bar
    gfx->drawLine(x + 4, y + 4 + t, x + 13, y + 13 + t, col);
}

// Phone icon + number drawn as one centered line.
static void drawPhoneLine(const String& phone, int cy, uint16_t col) {
  gfx->setFont(&FreeSansBold9pt7b);
  gfx->setTextSize(1);                                   // == textCenter size 2
  int16_t bx, by; uint16_t bw, bh;
  gfx->getTextBounds(phone.c_str(), 0, 0, &bx, &by, &bw, &bh);
  gfx->setFont(NULL); gfx->setTextSize(1);
  const int iconW = 20, gap = 12;
  int total = iconW + gap + bw;
  int x = CXi - total / 2;
  drawPhoneGlyph(x, cy - 10, col);
  textCenter(phone.c_str(), x + iconW + gap + bw / 2, cy, 2, col);
}

// --- activity / health dashboard (page 1), powered by the IMU motion engine ---
static void collarDrawActivity(const String& name) {
  gfx->fillScreen(C_BG);
  textCenter(name.c_str(), CXi, 40, 3, C_GOLD);

  if (!motionPresent()) {
    textCenter("AKTIVITE", CXi, CYi - 30, 3, C_DIM);
    textCenter("IMU yok", CXi, CYi + 20, 2, C_DANGER);
    textCenter("MPU6050 -> 0x68", CXi, CYi + 54, 2, C_DIM);
    textCenter("kaydir: kimlik", CXi, 440, 2, C_DIM);
    present(); delay(120); return;
  }

  MotionAct act = motionActivity();
  uint16_t col = (act == ACT_RUN) ? C_DANGER
               : (act == ACT_SCRATCH) ? C_WARN
               : (act == ACT_WALK) ? C_OK : C_DIM;

  // big current-activity word in a ring
  gfx->drawCircle(CXi, 150, 70, C_BG2);
  fillRing(CXi, 150, 64, 70, 0, (act == ACT_REST) ? 30 : 360, col);
  textCenter(motionActivityStr(), CXi, 150, 2, col);

  // steps today
  char sb[24]; snprintf(sb, sizeof(sb), "%lu adim", (unsigned long)motionStepsToday());
  textCenter(sb, CXi, 250, 3, C_TEXT);
  textCenter("bugun", CXi, 282, 2, C_DIM);

  // scratch health line
  int pct = motionScratchPct();
  if (motionScratchBase() < 1) {
    textCenter("Kasinma: olcum suruyor", CXi, 340, 2, C_DIM);
  } else if (pct >= 30) {
    char hb[40];
    snprintf(hb, sizeof(hb), "%%%d daha fazla kasindi", pct);
    textCenter("! SAGLIK UYARISI", CXi, 330, 2, C_DANGER);
    textCenter(hb, CXi, 360, 2, C_WARN);
    textCenter("(Alerji / Pire riski?)", CXi, 388, 2, C_DIM);
  } else {
    textCenter("Kasinma: normal", CXi, 345, 2, C_OK);
  }

  textCenter("kaydir: kimlik", CXi, 440, 2, C_DIM);
  present();
  delay(80);
}

static void collar_tick() {
  // swipe toggles between the ID card and the activity/health page
  if (g_g.swipeLeft || g_g.swipeRight) collar_page ^= 1;

  String name  = setGetStr("col_name", "PISI");
  String phone = setGetStr("col_phone", "0500 000 0000");
  String owner = setGetStr("col_owner", "");

  // activity page (lost/find still override below take precedence over this)
  if (collar_page == 1 && !collar_lost && millis() >= collar_findUntil) {
    collarDrawActivity(name);
    return;
  }

  if (g_g.tap) collar_lost = !collar_lost;

  // "find me" bright flash overrides everything
  if (millis() < collar_findUntil) {
    bool on = ((millis() / 200) % 2) == 0;
    gfx->fillScreen(on ? C_TEXT : C_ACCENT);
    textCenter(name.c_str(), CXi, CYi, 5, on ? C_BG : C_TEXT);
    present();
    delay(20);
    return;
  }

  if (collar_lost) {
    bool on = ((millis() / 400) % 2) == 0;
    gfx->fillScreen(on ? C_DANGER : C_BG);
    textCenter("KAYIP", CXi, 70, 4, on ? C_TEXT : C_DANGER);
    textCenter(name.c_str(), CXi, 130, 3, C_TEXT);
#ifdef HAVE_QRCODE
    {
      String tel = String("tel:") + phone;
      collarDrawQR(tel.c_str(), CXi, 255, 5);
      textCenter("tara veya ara", CXi, 360, 2, C_DIM);
    }
#else
    textCenter("ARAYIN", CXi, 230, 2, C_DIM);
    textCenter(phone.c_str(), CXi, 275, 3, C_TEXT);
#endif
    if (owner.length()) textCenter(owner.c_str(), CXi, 410, 2, C_DIM);
    present();
    delay(30);
    return;
  }

  // normal ID card (matches the collar design: black bg, gold pet icon,
  // big white name, gold "SAHIBI: owner", gold phone line)
  gfx->fillScreen(BLACK);
  drawPetIcon(CXi, 150, C_GOLD);
  textCenter(name.c_str(), CXi, 215, 5, C_TEXT);
  if (owner.length()) {
    String line = String("SAHIBI: ") + owner;
    textCenter(line.c_str(), CXi, 278, 2, C_GOLD);
  }
  drawPhoneLine(phone, owner.length() ? 322 : 300, C_GOLD);
  present();
  delay(40);
}
