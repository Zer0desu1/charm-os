// ============================================================================
// platform.h - CharmOS core: display(canvas), touch+button, gesture,
//               settings(NVS), module registry, drawing helpers.
// Single translation unit (Arduino): include this ONCE from CharmOS.ino.
// ============================================================================
#pragma once

#include <Arduino_GFX_Library.h>
#include "FreeSansBold9pt7b.h"   // clean, readable UI font (bundled in the sketch)
#include <Wire.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_mac.h>      // ESP32 core 3.x: esp_read_mac / ESP_MAC_WIFI_STA

// Color constants (newer Arduino_GFX no longer exports these)
#ifndef BLACK
#define BLACK 0x0000
#endif
#ifndef WHITE
#define WHITE 0xFFFF
#endif

// ----------------------------------------------------------------------------
// PINS  (Waveshare ESP32-S3 Touch AMOLED 1.32)
//   Verified against Waveshare's official demo user_config.h:
//   https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.32
//   (Example/Arduino/04_BATT_PWR_Test/user_config.h + vbat_bsp.cpp)
//   The BOOT button (GPIO0) is always usable as a fallback input.
// ----------------------------------------------------------------------------
#define LCD_SCK   11      // LCD_PCLK_PIN
#define LCD_CS    10      // LCD_CS_PIN
#define LCD_SDIO0 12      // LCD_DATA0_PIN
#define LCD_SDIO1 13      // LCD_DATA1_PIN
#define LCD_SDIO2 14      // LCD_DATA2_PIN
#define LCD_SDIO3 15      // LCD_DATA3_PIN
#define LCD_RST   8       // LCD_RST_PIN (direct GPIO, not via I/O expander)
#define LCD_TE    9       // tearing-effect (unused by Arduino_GFX here)

// System power enable: must be driven HIGH at boot to power the peripheral
// rail (display, etc.). In the Waveshare demo this is batt_dev.Set_VbatPowerON().
#define SYS_POWER_IO 18

#define LCD_W 466
#define LCD_H 466
#define CXi   233      // center x (int)
#define CYi   233      // center y (int)

// --- Touch (CST820) I2C ---  (verified against Waveshare user_config.h)
#define TOUCH_SDA  47     // ESP32_SDA_NUM
#define TOUCH_SCL  48     // ESP32_SCL_NUM
#define TOUCH_RST  7      // TOUCH_RST_PIN
#define TOUCH_INT  6      // TOUCH_INT_PIN (-1 to poll instead of interrupt)
#define CST820_ADDR 0x15

// --- Button ---
#define BTN_BOOT   0      // active LOW

// ----------------------------------------------------------------------------
// Display + offscreen canvas (flicker-free)
// ----------------------------------------------------------------------------
static Arduino_DataBus *g_bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
// Concrete CO5300 pointer so setBrightness() (DCS 0x51) is reachable.
// Concrete CO5300 pointer so setBrightness() (DCS 0x51) is reachable.
static Arduino_CO5300 *g_panel = new Arduino_CO5300(
    g_bus, LCD_RST, 0, LCD_W, LCD_H, 6, 0, 6, 0);    // 9 args (GFX 1.5+)

static Arduino_Canvas *g_canvas = nullptr;   // PSRAM offscreen
static Arduino_GFX *gfx = nullptr;           // active drawing target
static bool g_haveCanvas = false;

// Global screen rotation (0 = normal, 2 = 180 upside-down). Driven by the IMU
// in the main loop (auto-rotate); touchRead() remaps coordinates to match.
static int  g_screenRot  = 0;
static bool g_autoRotate = true;             // toggle (loaded from NVS in setup)

static inline void present() {
  if (g_haveCanvas) g_canvas->flush();
}

// ---------------------------------------------------------------------------
// Recolorable, anti-aliased icon masks. An IconMask is an 8-bit alpha bitmap
// (0=transparent, 255=solid); it's blended in ANY colour over a known bg, so it
// keeps the launcher's idle/focus recolour AND gives smooth edges (unlike the
// hand-drawn vector icons / 1-bit GFX fonts). Generate icon_masks.h with
// tools/png2mask.py from PNGs. Data lives in flash (PROGMEM); no RAM cost.
// ---------------------------------------------------------------------------
struct IconMask { uint8_t w, h; const uint8_t* a; };

// blend bg->fg by t (0..255) in RGB565
static inline uint16_t lerp565(uint16_t bg, uint16_t fg, uint8_t t) {
  int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  int fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  int r = br + ((fr - br) * t) / 255;
  int g = bgc + ((fgc - bgc) * t) / 255;
  int b = bb + ((fb - bb) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// draw mask centred at (cx,cy): fg = icon colour, bg = the disk colour behind it
static void drawIconMask(const IconMask& m, int cx, int cy, uint16_t fg, uint16_t bg) {
  int x0 = cx - m.w / 2, y0 = cy - m.h / 2;
  for (int y = 0; y < m.h; y++) {
    const uint8_t* row = m.a + (size_t)y * m.w;
    for (int x = 0; x < m.w; x++) {
      uint8_t al = row[x];
      if (!al) continue;                          // skip transparent
      gfx->drawPixel(x0 + x, y0 + y, al == 255 ? fg : lerp565(bg, fg, al));
    }
  }
}

static bool platformDisplayBegin() {
  // Power the peripheral rail (display, codec, etc.) BEFORE touching the panel.
  // Without this the AMOLED stays dark even though QSPI initializes fine.
  pinMode(SYS_POWER_IO, OUTPUT);
  digitalWrite(SYS_POWER_IO, HIGH);
  delay(50);

  // Initialize the panel (and its QSPI bus) exactly once.
  if (!g_panel->begin()) return false;
  gfx = g_panel;
  g_haveCanvas = false;

  // Try to add a PSRAM-backed offscreen canvas for flicker-free drawing.
  // GFX_SKIP_OUTPUT_BEGIN avoids re-initializing the already-initialized bus
  // (a second spi_bus_initialize() aborts with ESP_ERR_INVALID_STATE).
  g_canvas = new Arduino_Canvas(LCD_W, LCD_H, g_panel);
  if (g_canvas && g_canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    gfx = g_canvas;
    g_haveCanvas = true;
  } else {
    // Framebuffer alloc failed (no PSRAM?) — draw directly, with some flicker.
    delete g_canvas;
    g_canvas = nullptr;
  }

  gfx->fillScreen(BLACK);
  present();
  return true;
}

// ----------------------------------------------------------------------------
// Color helpers (RGB565)
// ----------------------------------------------------------------------------
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// Themeable colors are RUNTIME variables (changed from Ayarlar > Renkler and
// persisted to NVS). The macros below keep every existing `C_BG`/`C_BG2`/
// `C_ACCENT` use-site working unchanged - they now just read a variable.
// The remaining colors (text/dim/ok/warn/danger/gold) stay fixed so status
// cues keep their meaning regardless of the chosen theme.
static uint16_t g_cBg     = rgb(8, 12, 24);    // background
static uint16_t g_cBg2    = rgb(18, 24, 48);   // cards / idle item faces
static uint16_t g_cAccent = rgb(99, 102, 241); // focus / highlight (item color)
#define C_BG       g_cBg
#define C_BG2      g_cBg2
#define C_TEXT     rgb(240, 244, 250)
#define C_DIM      rgb(130, 145, 170)
#define C_ACCENT   g_cAccent
#define C_OK       rgb(34, 197, 94)
#define C_WARN     rgb(245, 158, 11)
#define C_DANGER   rgb(239, 68, 68)
#define C_GOLD     rgb(255, 196, 64)

// ---- Theme presets (background pairs + accent swatches) --------------------
struct BgTheme  { const char* name; uint8_t bg[3]; uint8_t bg2[3]; };
struct AccTheme { const char* name; uint8_t c[3]; };

static const BgTheme  BG_THEMES[] = {
  { "Gece",     {  8, 12, 24}, { 18, 24, 48} },   // 0 default
  { "Siyah",    {  0,  0,  0}, { 24, 24, 26} },   // pure-black AMOLED
  { "Komur",    { 18, 18, 20}, { 38, 38, 44} },
  { "Lacivert", {  6, 14, 40}, { 16, 28, 64} },
  { "Bordo",    { 28,  8, 16}, { 56, 18, 32} },
  { "Orman",    {  6, 20, 12}, { 14, 40, 26} },
};
static const AccTheme ACC_THEMES[] = {
  { "Indigo",  { 99, 102, 241} },   // 0 default
  { "Mavi",    { 56, 140, 255} },
  { "Yesil",   { 34, 197,  94} },
  { "Turuncu", {245, 158,  11} },
  { "Pembe",   {236,  72, 153} },
  { "Mor",     {168,  85, 247} },
  { "Kirmizi", {239,  68,  68} },
  { "Altin",   {255, 196,  64} },
};
static const int BG_THEME_N  = sizeof(BG_THEMES)  / sizeof(BG_THEMES[0]);
static const int ACC_THEME_N = sizeof(ACC_THEMES) / sizeof(ACC_THEMES[0]);

static int g_bgTheme  = 0;
static int g_accTheme = 0;

static void themeApply() {
  const BgTheme&  b = BG_THEMES[constrain(g_bgTheme, 0, BG_THEME_N - 1)];
  const AccTheme& a = ACC_THEMES[constrain(g_accTheme, 0, ACC_THEME_N - 1)];
  g_cBg     = rgb(b.bg[0],  b.bg[1],  b.bg[2]);
  g_cBg2    = rgb(b.bg2[0], b.bg2[1], b.bg2[2]);
  g_cAccent = rgb(a.c[0],   a.c[1],   a.c[2]);
}

// ----------------------------------------------------------------------------
// Drawing helpers
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Turkish / UTF-8 text. The GFX fonts are ASCII-only, so instead of folding TR
// letters away (the old trAscii path) we draw the ASCII BASE glyph and COMPOSE
// the accent on top (dieresis ¨, cedilla ¸, breve ˘, dot-above). Accents add no
// width, so layout/centering still measures the folded base. cp->base+accent:
//   acc: 0 none, 1 dieresis (o/u/O/U), 2 cedilla (c/s/C/S), 3 breve (g/G),
//        4 dot-above (I)
// ----------------------------------------------------------------------------
static int trUtf8(const char* s, uint32_t& cp) {            // decode 1 codepoint
  uint8_t c = (uint8_t)s[0];
  if (c < 0x80) { cp = c; return 1; }
  if ((c & 0xE0) == 0xC0 && s[1]) { cp = ((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F); return 2; }
  if ((c & 0xF0) == 0xE0 && s[1] && s[2]) { cp = ((c & 0x0F) << 12) | (((uint8_t)s[1] & 0x3F) << 6) | ((uint8_t)s[2] & 0x3F); return 3; }
  if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) { cp = ((c & 0x07) << 18) | (((uint8_t)s[1] & 0x3F) << 12) | (((uint8_t)s[2] & 0x3F) << 6) | ((uint8_t)s[3] & 0x3F); return 4; }
  cp = '?'; return 1;
}
static char trCp(uint32_t cp, uint8_t& acc) {               // -> ASCII base + accent
  acc = 0;
  if (cp < 0x80) return (char)cp;
  switch (cp) {
    case 0x00E7: acc = 2; return 'c'; case 0x00C7: acc = 2; return 'C';   // ç Ç
    case 0x00F6: acc = 1; return 'o'; case 0x00D6: acc = 1; return 'O';   // ö Ö
    case 0x00FC: acc = 1; return 'u'; case 0x00DC: acc = 1; return 'U';   // ü Ü
    case 0x011F: acc = 3; return 'g'; case 0x011E: acc = 3; return 'G';   // ğ Ğ
    case 0x0131:          return 'i'; case 0x0130: acc = 4; return 'I';   // ı İ
    case 0x015F: acc = 2; return 's'; case 0x015E: acc = 2; return 'S';   // ş Ş
    default: return '?';
  }
}
static int trFold(const char* s, char* out, int cap) {      // UTF-8 -> ASCII base
  int n = 0;
  for (const char* p = s; *p && n < cap - 1; ) {
    uint32_t cp; p += trUtf8(p, cp); uint8_t a; out[n++] = trCp(cp, a);
  }
  out[n] = 0; return n;
}
// Draw one accent centered at cx; aboveY = top of the above-accent zone,
// belowY = top of the cedilla zone, s = pixel scale (~ text size).
static void drawAccentMark(int cx, int aboveY, int belowY, int s, uint8_t acc, uint16_t col) {
  if (s < 1) s = 1;
  int d = s + 1;                                   // dot thickness
  switch (acc) {
    case 1:  gfx->fillRect(cx - 2 * s, aboveY, d, d, col);                 // dieresis: two dots
             gfx->fillRect(cx + s,     aboveY, d, d, col); break;
    case 2:  gfx->fillRect(cx - s / 2, belowY, d, 2 * s, col);             // cedilla: hook below
             gfx->fillRect(cx - 2 * s, belowY + 2 * s, 2 * s + d, d, col); break;
    case 3:  gfx->fillRect(cx - 2 * s, aboveY + s, 4 * s, d, col);         // breve: arc above
             gfx->fillRect(cx - 2 * s, aboveY, d, s, col);
             gfx->fillRect(cx + 2 * s - d, aboveY, d, s, col); break;
    case 4:  gfx->fillRect(cx - d / 2, aboveY, d, d, col); break;          // dot above (I)
  }
}
// Draw a UTF-8 string with FreeSansBold + composed accents. (x, originY) is the
// GFX print origin (baseline). Font is set here and the classic font restored.
static void gfxDrawAccented(const char* utf8, int x, int originY, uint8_t ts, uint16_t col) {
  char base[220]; trFold(utf8, base, sizeof(base));
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(ts); gfx->setTextColor(col);
  gfx->setCursor(x, originY); gfx->print(base);
  const int aboveY = originY - 12 * ts;            // above the caps
  const int belowY = originY + 2;                  // just below the baseline
  int idx = 0;
  for (const char* p = utf8; *p; ) {
    uint32_t cp; p += trUtf8(p, cp); uint8_t acc; trCp(cp, acc);
    if (acc) {
      char tmp[220]; int16_t bx, by; uint16_t w0 = 0, w1 = 0, hh;
      strncpy(tmp, base, idx); tmp[idx] = 0;             gfx->getTextBounds(tmp, 0, 0, &bx, &by, &w0, &hh);
      strncpy(tmp, base, idx + 1); tmp[idx + 1] = 0;     gfx->getTextBounds(tmp, 0, 0, &bx, &by, &w1, &hh);
      drawAccentMark(x + (int)(w0 + w1) / 2, aboveY, belowY, ts, acc, col);
    }
    idx++;
  }
  gfx->setFont(NULL); gfx->setTextSize(1);
}
static int gfxWidthAccented(const char* utf8, uint8_t ts) {
  char base[220]; trFold(utf8, base, sizeof(base));
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(ts);
  int16_t bx, by; uint16_t bw = 0, bh; gfx->getTextBounds(base, 0, 0, &bx, &by, &bw, &bh);
  gfx->setFont(NULL); gfx->setTextSize(1);
  return bw;
}

// Draw centered text of given `size` at (cx, cy). Proportional FreeSansBold with
// composed Turkish accents (see above). Width/centering measured on the folded
// base; accents are overlaid.
static void textCenter(const char* s, int cx, int cy, uint8_t size, uint16_t color) {
  uint8_t ts = size >= 2 ? (uint8_t)(size - 1) : 1;
  char base[220]; trFold(s, base, sizeof(base));
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(ts);
  int16_t bx = 0, by = 0; uint16_t bw = 0, bh = 0;
  gfx->getTextBounds(base, 0, 0, &bx, &by, &bw, &bh);
  int startX  = cx - bw / 2 - bx;
  int originY = cy - bh / 2 - by;
  gfx->setFont(NULL);
  gfxDrawAccented(s, startX, originY, ts, color);   // re-sets font, draws + accents
}

// Filled circular sector (pie slice) from angle a0..a1 (degrees, 0=top, CW).
static void fillSector(int cx, int cy, int r, float a0, float a1, uint16_t color) {
  const float step = 3.0f; // degrees
  for (float a = a0; a < a1; a += step) {
    float b = min(a + step, a1);
    float ar = a * DEG_TO_RAD, br = b * DEG_TO_RAD;
    int x0 = cx + (int)(r * sinf(ar));
    int y0 = cy - (int)(r * cosf(ar));
    int x1 = cx + (int)(r * sinf(br));
    int y1 = cy - (int)(r * cosf(br));
    gfx->fillTriangle(cx, cy, x0, y0, x1, y1, color);
  }
}

// Thick arc ring from a0..a1 (degrees, 0=top, CW), radii rInner..rOuter.
static void fillRing(int cx, int cy, int rInner, int rOuter, float a0, float a1, uint16_t color) {
  const float step = 2.5f;
  for (float a = a0; a < a1; a += step) {
    float b = min(a + step, a1);
    float ar = a * DEG_TO_RAD, br = b * DEG_TO_RAD;
    int xo0 = cx + (int)(rOuter * sinf(ar));
    int yo0 = cy - (int)(rOuter * cosf(ar));
    int xo1 = cx + (int)(rOuter * sinf(br));
    int yo1 = cy - (int)(rOuter * cosf(br));
    int xi0 = cx + (int)(rInner * sinf(ar));
    int yi0 = cy - (int)(rInner * cosf(ar));
    int xi1 = cx + (int)(rInner * sinf(br));
    int yi1 = cy - (int)(rInner * cosf(br));
    gfx->fillTriangle(xo0, yo0, xo1, yo1, xi0, yi0, color);
    gfx->fillTriangle(xo1, yo1, xi1, yi1, xi0, yi0, color);
  }
}

// ----------------------------------------------------------------------------
// QR code (optional - install "QRCode" Arduino library by Richard Moore).
// Used to show a Wi-Fi join QR ("scan to connect") + the pet collar tag.
// ----------------------------------------------------------------------------
// To enable Wi-Fi join QR + Tasma QR, install the "QRCode" Arduino library
// by Richard Moore, then change this 0 to 1 and recompile. Default off so the
// build works without the library (and avoids clashing with ESP-IDF's own
// esp_qrcode.h in newer cores).
#define USE_QRCODE 0
#if USE_QRCODE
#  include <qrcode.h>
#  define HAVE_QRCODE 1
#endif

static char g_wifiQR[72] = "";   // "WIFI:T:WPA;S:..;P:..;;" filled in netBegin()

#ifdef HAVE_QRCODE
static bool qrAvailable() { return true; }
static void qrDraw(const char* text, int cx, int cy, int scale) {
  QRCode qr;
  static uint8_t qrbuf[qrcode_getBufferSize(5)];
  if (qrcode_initText(&qr, qrbuf, 5, ECC_LOW, text) != 0) return;
  int total = qr.size * scale;
  int x0 = cx - total / 2, y0 = cy - total / 2;
  gfx->fillRect(x0 - 10, y0 - 10, total + 20, total + 20, WHITE);  // quiet zone
  for (int y = 0; y < qr.size; y++)
    for (int x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        gfx->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, BLACK);
}
#else
static bool qrAvailable() { return false; }
static void qrDraw(const char*, int, int, int) {}
#endif

// ----------------------------------------------------------------------------
// Battery monitor (best-effort; pin needs verification against Waveshare schematic)
// ----------------------------------------------------------------------------
#define BAT_ADC_PIN 4         // VERIFY: GPIO connected to battery voltage divider
static int      g_batPct  = -1;
static int      g_batPrev = -1;
static uint32_t g_batRead = 0;
static bool     g_batCharging = false;
static uint32_t g_batChgFalseAt = 0;

static int batteryPct() {
  uint32_t now = millis();
  if (g_batPct >= 0 && now - g_batRead < 6000) return g_batPct;
  int adc = analogRead(BAT_ADC_PIN);
  if (adc <= 30 || adc >= 4080) { g_batRead = now; return g_batPct; } // invalid -> keep last
  float v = (adc / 4095.0f) * 3.3f * 2.0f;             // assumes 2:1 divider
  float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  // Charging heuristic: if voltage trended up since last reading -> assume charging.
  if (g_batPct >= 0) {
    if ((int)pct > g_batPct) { g_batCharging = true; g_batChgFalseAt = 0; }
    else if ((int)pct < g_batPct) {
      // start a 30s "stop charging" window to debounce spikes
      if (g_batChgFalseAt == 0) g_batChgFalseAt = now;
      if (now - g_batChgFalseAt > 30000) g_batCharging = false;
    }
  }
  g_batPrev = g_batPct;
  g_batPct = (int)pct; g_batRead = now;
  return g_batPct;
}

static void drawBatteryIcon(int x, int y) {
  int p = batteryPct();
  if (p < 0) return;
  gfx->drawRect(x, y, 24, 12, C_TEXT);
  gfx->fillRect(x + 24, y + 3, 2, 6, C_TEXT);
  uint16_t col = p < 20 ? C_DANGER : (p < 40 ? C_WARN : C_OK);
  int fill = (p * 22) / 100;
  if (g_batCharging) {
    // animated rising fill while charging
    int t = (millis() / 250) % 22;
    int w = (fill + t) % 22 + 1;
    gfx->fillRect(x + 1, y + 1, w, 10, C_OK);
    // lightning bolt overlay
    int bx = x + 10, by = y + 1;
    gfx->fillTriangle(bx, by + 6, bx + 4, by + 6, bx + 2, by, C_GOLD);
    gfx->fillTriangle(bx, by + 6, bx + 4, by + 10, bx + 2, by + 6, C_GOLD);
  } else if (fill > 0) {
    gfx->fillRect(x + 1, y + 1, fill, 10, col);
  }
}

// ----------------------------------------------------------------------------
// Settings (NVS / Preferences)
// ----------------------------------------------------------------------------
static Preferences g_prefs;
static void settingsBegin() { g_prefs.begin("charmos", false); }

static bool  setGetBool(const char* k, bool d)        { return g_prefs.getBool(k, d); }
static void  setPutBool(const char* k, bool v)        { g_prefs.putBool(k, v); }
static int   setGetInt(const char* k, int d)          { return g_prefs.getInt(k, d); }
static void  setPutInt(const char* k, int v)          { g_prefs.putInt(k, v); }
static String setGetStr(const char* k, const char* d) { return g_prefs.getString(k, d); }
static void  setPutStr(const char* k, const String& v){ g_prefs.putString(k, v); }

// ---- Theme (color) persistence -------------------------------------------
static void themeLoad() {
  g_bgTheme  = constrain(setGetInt("th_bg",  0), 0, BG_THEME_N  - 1);
  g_accTheme = constrain(setGetInt("th_acc", 0), 0, ACC_THEME_N - 1);
  themeApply();
}
static void themeSetBg(int idx) {
  g_bgTheme = constrain(idx, 0, BG_THEME_N - 1);
  setPutInt("th_bg", g_bgTheme); themeApply();
}
static void themeSetAcc(int idx) {
  g_accTheme = constrain(idx, 0, ACC_THEME_N - 1);
  setPutInt("th_acc", g_accTheme); themeApply();
}

// ----------------------------------------------------------------------------
// Display brightness (CO5300 DCS 0x51), persisted to NVS. 10..100 percent.
// ----------------------------------------------------------------------------
static int g_brightPct = 80;
static void displayApplyBrightness() {
  int v = constrain(g_brightPct, 10, 100);
  g_panel->setBrightness((uint8_t)(v * 255 / 100));
}
static void displayLoadBrightness() {
  g_brightPct = setGetInt("bright", 80);
  displayApplyBrightness();
}
static void displaySetBrightness(int pct) {
  g_brightPct = constrain(pct, 10, 100);
  setPutInt("bright", g_brightPct);
  displayApplyBrightness();
}

// ----------------------------------------------------------------------------
// Display sleep + tilt/touch wake (battery saver)
//   On an AMOLED, setting brightness to 0 turns the emitters off, so blanking
//   the panel after a period of no input gives a near-zero-power "screen off"
//   without a deep-sleep reboot (BLE/WiFi/notifications keep running). A touch,
//   the BOOT button, an incoming push (g_wakeReq) or a wrist-raise (IMU, see
//   motionRaiseEvent in mpu.h) wakes it. Timeout 0 = never sleep.
// ----------------------------------------------------------------------------
static bool          g_displayAsleep   = false;
static bool          g_displayDimmed   = false;   // pre-sleep dim stage
static uint32_t      g_lastActivityMs  = 0;
static uint32_t      g_sleepTimeoutMs  = 25000;   // loaded from NVS ("sleep_s")
static bool          g_tiltWake        = true;    // loaded from NVS ("tiltwake")
static volatile bool g_wakeReq         = false;   // net / notifications force wake

// Merge Now-Playing (Muzik) + Sozler into one view: show synced lyrics when
// available, otherwise the now-playing banner. Auto-opens on playback.
static bool          g_mlMerge         = true;    // loaded from NVS ("ml_merge")

// Bluetooth (BLE HID + AMS + nav) at boot. The ESP32-S3 shares ONE 2.4 GHz radio
// between BLE and Wi-Fi; with BLE active the Wi-Fi STA can drop. Turn this OFF to
// give the whole radio to Wi-Fi (rock-solid app/lyrics/camera) at the cost of the
// BLE features (knob, camera shutter, iPhone now-playing). Applied at boot.
static bool          g_bleOn           = true;    // loaded from NVS ("ble_on")

static void powerLoadConfig() {
  g_sleepTimeoutMs = (uint32_t)setGetInt("sleep_s", 25) * 1000UL;
  g_tiltWake       = setGetBool("tiltwake", true);
  g_mlMerge        = setGetBool("ml_merge", true);
  g_bleOn          = setGetBool("ble_on", true);
  g_lastActivityMs = millis();
}
static int  powerSleepSecs()          { return (int)(g_sleepTimeoutMs / 1000UL); }
static void powerSetSleepSecs(int s) {
  s = constrain(s, 0, 600);                 // 0 = never, max 10 min
  g_sleepTimeoutMs = (uint32_t)s * 1000UL;
  setPutInt("sleep_s", s);
}
static void powerSetTiltWake(bool on) { g_tiltWake = on; setPutBool("tiltwake", on); }
static void setMlMerge(bool on)       { g_mlMerge = on;  setPutBool("ml_merge", on); }
static void setBleOn(bool on)         { g_bleOn = on;    setPutBool("ble_on", on); }

static inline void noteActivity() { g_lastActivityMs = millis(); }

// Pre-sleep dim: drop to a fraction of the user's brightness (NOT persisted) so
// the screen visibly fades a few seconds before it fully blanks, like a real
// watch. displayUndim()/displayWake() restore the user setting.
static void displayDim() {
  if (g_displayDimmed || g_displayAsleep) return;
  g_displayDimmed = true;
  int v = constrain(g_brightPct, 10, 100);
  g_panel->setBrightness((uint8_t)max(8, v * 255 / 100 / 5));   // ~20%, floor 8
}
static void displayUndim() {
  if (!g_displayDimmed) return;
  g_displayDimmed = false;
  displayApplyBrightness();
}
static void displaySleep() {
  if (g_displayAsleep) return;
  g_displayAsleep = true;
  g_displayDimmed = false;
  g_panel->setBrightness(0);        // blank the OLED (true-black pixels = off)
}
static void displayWake() {
  noteActivity();
  if (!g_displayAsleep && !g_displayDimmed) return;
  g_displayAsleep = false;
  g_displayDimmed = false;
  displayApplyBrightness();         // restore the user's brightness
}

// ----------------------------------------------------------------------------
// Power / thermal management
//   The S3 runs hot when pinned at 240 MHz with WiFi at full TX. We keep the UI
//   at a cooler 160 MHz and only boost to 240 MHz for CPU-heavy modules (audio
//   DSP, Opus voice/AI, JPEG mirror/camera). WiFi modem-sleep + lower TX power
//   cut the radio's contribution (see cloudBegin()).
// ----------------------------------------------------------------------------
static const uint32_t CPU_HZ_LOW  = 160;   // arayuz / bosta: serin
static const uint32_t CPU_HZ_HIGH = 240;   // ses / yapay zeka / akis: tam hiz
static uint32_t g_cpuHz = 0;
static void cpuSetMhz(uint32_t mhz) {
  if (g_cpuHz == mhz) return;
  setCpuFrequencyMhz(mhz);
  g_cpuHz = mhz;
}
// Modules that genuinely need full speed (real-time DSP / codec / JPEG over WiFi).
static bool moduleNeedsHighCpu(const char* id) {
  static const char* const heavy[] = {
    "audio", "assistant", "voicenote", "xiaozhi", "openclaw", "mirror", "camera",
  };
  for (size_t i = 0; i < sizeof(heavy) / sizeof(heavy[0]); i++)
    if (strcmp(id, heavy[i]) == 0) return true;
  return false;
}

// ----------------------------------------------------------------------------
// Device identity (4 hex chars from MAC)
// ----------------------------------------------------------------------------
static char g_devId[5] = "0000";
static void deviceIdInit() {
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(g_devId, sizeof(g_devId), "%02X%02X", mac[4], mac[5]);
}

// ----------------------------------------------------------------------------
// Turkish text -> readable ASCII. The built-in GFX font is ASCII-only, so
// UTF-8 Turkish letters render as garbage. Map them to their ASCII base
// (s,c,g,i,o,u) so phone-sent names/messages stay readable. Other non-ASCII
// (e.g. emoji) collapses to '?'.
// ----------------------------------------------------------------------------
static String trAscii(const String& in) {
  String out; out.reserve(in.length());
  int n = (int)in.length();
  for (int i = 0; i < n; i++) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) { out += (char)c; continue; }     // plain ASCII
    uint8_t c2 = (i + 1 < n) ? (uint8_t)in[i + 1] : 0;
    char r = 0;
    if (c == 0xC3) {            // Latin-1 supplement block
      if      (c2 == 0xA7) r = 'c'; else if (c2 == 0x87) r = 'C';   // ç Ç
      else if (c2 == 0xB6) r = 'o'; else if (c2 == 0x96) r = 'O';   // ö Ö
      else if (c2 == 0xBC) r = 'u'; else if (c2 == 0x9C) r = 'U';   // ü Ü
    } else if (c == 0xC4) {     // Latin Extended-A
      if      (c2 == 0x9F) r = 'g'; else if (c2 == 0x9E) r = 'G';   // ğ Ğ
      else if (c2 == 0xB1) r = 'i'; else if (c2 == 0xB0) r = 'I';   // ı İ
    } else if (c == 0xC5) {
      if      (c2 == 0x9F) r = 's'; else if (c2 == 0x9E) r = 'S';   // ş Ş
    }
    if (r) { out += r; i++; }                        // consumed 2 bytes
    else { out += '?'; if (c >= 0xC0) { while (i + 1 < n && ((uint8_t)in[i+1] & 0xC0) == 0x80) i++; } }
  }
  return out;
}

// ----------------------------------------------------------------------------
// Device friendly name (NVS, falls back to MAC-derived ID)
// ----------------------------------------------------------------------------
static String deviceName() {
  String n = setGetStr("dev_name", "");
  if (!n.length()) n = String("Vecta-") + g_devId;
  return n;
}
static void deviceSetName(const String& n) {
  String s = n; if (s.length() > 16) s = s.substring(0, 16);
  setPutStr("dev_name", s);
}

// ----------------------------------------------------------------------------
// Sound feedback (scaffold). The Waveshare board has ES8311 + speaker; real
// audio init is omitted because exact I2S pins are board-specific. The API is
// a no-op by default so visuals/logic work. Enable real audio later by filling
// in soundPlay() with an I2S DAC tone.
// ----------------------------------------------------------------------------
enum SoundTheme { ST_SILENT = 0, ST_CLASSIC = 1, ST_SOFT = 2, ST_RETRO = 3 };
static int g_soundTheme = 0;
static void soundLoadTheme() { g_soundTheme = setGetInt("snd_th", 0); }
static void soundSetTheme(int t) {
  g_soundTheme = constrain(t, 0, 3);
  setPutInt("snd_th", g_soundTheme);
}
// Defined in audio.h (included after this file). Plays a tone on the speaker
// when the ES8311 codec is present; otherwise a no-op.
void audioPlayTone(int freq, int ms);

// /screen son istek zamani (net.h damgalar). Telefon yansitmasi (mirror)
// izlerken saat SESSIZ kalir: tonlar blokladigi icin kare akisini da bozuyordu.
static volatile uint32_t g_mirrorPollAt = 0;
static inline bool soundMuted() {
  return g_mirrorPollAt && (millis() - g_mirrorPollAt < 2500);
}
static inline void soundPlay(int freq, int ms) {
  if (soundMuted()) return;
  audioPlayTone(freq, ms);
}

// Short melody: "freq,ms" pairs, freq 0 = rest. Callers keep totals < ~400ms
// so the (blocking) playback never feels like UI lag.
static void soundSeq(const int* fm, int pairs) {
  if (!g_soundTheme || soundMuted()) return;
  for (int i = 0; i < pairs; i++) {
    int f = fm[i * 2], ms = fm[i * 2 + 1];
    if (f) audioPlayTone(f, ms); else delay(ms);
  }
}
#define SOUND_SEQ(...) do { static const int _s[] = {__VA_ARGS__}; \
                            soundSeq(_s, (int)(sizeof(_s)/sizeof(_s[0])/2)); } while (0)

// Theme-aware UI sfx. SOFT = low gentle chirps, RETRO = 8-bit squeaks,
// CLASSIC = clean mid beeps.
static void soundTap() {
  if (!g_soundTheme) return;
  switch (g_soundTheme) {
    case ST_SOFT:   soundPlay(520, 14);  break;
    case ST_RETRO:  soundPlay(1568, 16); break;
    default:        soundPlay(880, 16);  break;
  }
}
static void soundBack() {
  if (!g_soundTheme) return;
  SOUND_SEQ(700,30, 480,40);
}
static void soundOk() {
  if (!g_soundTheme) return;
  switch (g_soundTheme) {
    case ST_SOFT:   SOUND_SEQ(659,40, 988,70);           break;
    case ST_RETRO:  SOUND_SEQ(1047,28, 1319,28, 1568,50); break;
    default:        SOUND_SEQ(880,36, 1175,70);          break;
  }
}
static void soundError() {
  if (!g_soundTheme) return;
  SOUND_SEQ(330,70, 220,120);
}
static void soundCelebrate() {
  if (!g_soundTheme) return;
  SOUND_SEQ(784,50, 988,50, 1175,50, 1568,110);
}
static void soundNotifyChirp() {
  if (!g_soundTheme) return;
  SOUND_SEQ(1319,45, 0,35, 1319,60);
}
// Boot theme music: a short ascending signature, flavoured per theme.
static void soundBootJingle() {
  if (!g_soundTheme) return;
  switch (g_soundTheme) {
    case ST_SOFT:   SOUND_SEQ(392,80, 523,80, 659,80, 784,160);          break;
    case ST_RETRO:  SOUND_SEQ(523,55, 659,55, 784,55, 1047,55, 1319,140); break;
    default:        SOUND_SEQ(523,70, 659,70, 880,70, 1175,150);         break;
  }
}
// Per-module signature jingles so opening an app has its own character.
static void soundModuleJingle(const char* id) {
  if (!g_soundTheme || !id) return;
  if      (!strcmp(id, "tama"))     SOUND_SEQ(988,40, 1319,40, 1760,70);  // cute rise
  else if (!strcmp(id, "lovebox"))  SOUND_SEQ(659,60, 784,60, 988,110);   // sweet arpeggio
  else if (!strcmp(id, "charm"))    SOUND_SEQ(784,50, 1047,90);
  else if (!strcmp(id, "game") ||
           !strcmp(id, "rps") ||
           !strcmp(id, "ttt"))      SOUND_SEQ(440,35, 554,35, 440,50);    // playful
  else if (!strcmp(id, "music") ||
           !strcmp(id, "knob"))     SOUND_SEQ(523,45, 0,25, 784,80);
  else if (!strcmp(id, "notify"))   soundNotifyChirp();
  else if (!strcmp(id, "fidget"))   SOUND_SEQ(392,90, 330,130);           // calm fall
  else if (!strcmp(id, "settings")) SOUND_SEQ(660,25, 880,40);
  else if (!strcmp(id, "xiaozhi") ||
           !strcmp(id, "openclaw") ||
           !strcmp(id, "assistant")) SOUND_SEQ(880,35, 1109,35, 1319,60);  // AI acilisi
  else                              soundOk();
}

// ----------------------------------------------------------------------------
// Random
// ----------------------------------------------------------------------------
static inline uint32_t rnd(uint32_t n) { return n ? (esp_random() % n) : 0; }

// ----------------------------------------------------------------------------
// Touch (CST820) + Button + Gesture detection
// ----------------------------------------------------------------------------
struct RawTouch { bool down; int x; int y; };

static bool g_touchOk = false;

static void touchBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  if (TOUCH_RST >= 0) {
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);  delay(10);
    digitalWrite(TOUCH_RST, HIGH); delay(50);
  }
  // probe
  Wire.beginTransmission(CST820_ADDR);
  g_touchOk = (Wire.endTransmission() == 0);
  pinMode(BTN_BOOT, INPUT_PULLUP);
}

static RawTouch touchRead() {
  RawTouch t = {false, 0, 0};
  if (g_touchOk) {
    Wire.beginTransmission(CST820_ADDR);
    Wire.write(0x01);
    if (Wire.endTransmission(false) == 0) {
      Wire.requestFrom((int)CST820_ADDR, 6);
      if (Wire.available() >= 6) {
        uint8_t b[6];
        for (int i = 0; i < 6; i++) b[i] = Wire.read();
        uint8_t fingers = b[1];
        if (fingers > 0) {
          int x = ((b[2] & 0x0F) << 8) | b[3];
          int y = ((b[4] & 0x0F) << 8) | b[5];
          t.down = true; t.x = x; t.y = y;
        }
      }
    }
  }
  // Button fallback: emulate a touch at center while pressed
  if (!t.down && digitalRead(BTN_BOOT) == LOW) {
    t.down = true; t.x = CXi; t.y = CYi;
  }
  // remap touch to match the rotated screen so taps land where drawn (square panel)
  if (t.down && g_screenRot) {
    int rx = t.x, ry = t.y;
    switch (g_screenRot) {
      case 1: t.x = ry;                t.y = (LCD_W - 1) - rx; break;
      case 2: t.x = (LCD_W - 1) - rx;  t.y = (LCD_H - 1) - ry; break;
      case 3: t.x = (LCD_H - 1) - ry;  t.y = rx;               break;
    }
  }
  return t;
}

// Gesture events (global, valid for one loop iteration)
struct Gesture {
  bool tap;        // short press released
  bool longPress;  // held > threshold
  bool swipeLeft;  // horizontal swipe (finger moved left)
  bool swipeRight; // horizontal swipe (finger moved right)
  bool swipeUp;    // vertical swipe (finger moved up)
  bool swipeDown;  // vertical swipe (finger moved down)
  int x, y;        // position of the event
  // --- continuous drag + fling (for momentum scrolling) ---
  bool  down;      // finger is currently on the glass
  bool  released;  // finger lifted THIS frame (after any drag, not a tap)
  int   dragDX, dragDY;   // per-frame finger delta while down (0 otherwise)
  float velX, velY;       // smoothed fling velocity (px/frame) valid on `released`
};
static Gesture g_g = {false, false, false, false, false, false, 0, 0, false, false, 0, 0, 0, 0};

static bool   _gPrevDown = false;
static uint32_t _gDownAt = 0;
static int    _gDownX = 0, _gDownY = 0;
static int    _gLastX = 0, _gLastY = 0;
static bool   _gLongFired = false;
static bool   _gDragged = false;          // finger moved a lot -> not a tap/hold
static float  _gVelX = 0, _gVelY = 0;     // low-pass velocity estimate
static const uint32_t LONG_MS = 800;
static const int DRAG_PX = 30;            // movement beyond this = drag
static const int SWIPE_PX = 55;           // horizontal travel that counts as a swipe

static void gestureUpdate() {
  g_g.tap = false; g_g.longPress = false; g_g.swipeLeft = false; g_g.swipeRight = false;
  g_g.swipeUp = false; g_g.swipeDown = false;
  g_g.released = false; g_g.dragDX = 0; g_g.dragDY = 0;
  RawTouch t = touchRead();
  uint32_t now = millis();
  if (t.down && !_gPrevDown) {
    _gPrevDown = true; _gDownAt = now; _gDownX = t.x; _gDownY = t.y;
    _gLastX = t.x; _gLastY = t.y;
    _gLongFired = false; _gDragged = false;
    _gVelX = 0; _gVelY = 0;
    g_g.down = true;
  } else if (t.down && _gPrevDown) {
    int fdx = t.x - _gLastX, fdy = t.y - _gLastY;     // per-frame delta
    g_g.dragDX = fdx; g_g.dragDY = fdy; g_g.down = true;
    // exponential moving average of instantaneous velocity (smooths jitter)
    _gVelX = 0.6f * _gVelX + 0.4f * fdx;
    _gVelY = 0.6f * _gVelY + 0.4f * fdy;
    _gLastX = t.x; _gLastY = t.y;
    int dx = t.x - _gDownX, dy = t.y - _gDownY;
    if (dx * dx + dy * dy > DRAG_PX * DRAG_PX) _gDragged = true;
    // long-press only fires for a STILL hold (a drag must not exit module)
    if (!_gLongFired && !_gDragged && now - _gDownAt > LONG_MS) {
      g_g.longPress = true; g_g.x = _gDownX; g_g.y = _gDownY;
      _gLongFired = true;
      Serial.printf("[gesture] Long press at x=%d, y=%d\n", g_g.x, g_g.y);
    }
  } else if (!t.down && _gPrevDown) {
    _gPrevDown = false; g_g.down = false;
    int dx = _gLastX - _gDownX, dy = _gLastY - _gDownY;
    if (_gDragged) {                                   // a real drag ended -> fling
      g_g.released = true; g_g.velX = _gVelX; g_g.velY = _gVelY;
      g_g.x = _gLastX; g_g.y = _gLastY;
    }
    // A clear horizontal drag = swipe. Anything else that isn't a long-press
    // counts as a tap (so a slightly-moved finger still triggers the shutter).
    if (!_gLongFired && abs(dx) > SWIPE_PX && abs(dx) > abs(dy) + 10) {
      if (dx < 0) g_g.swipeLeft = true; else g_g.swipeRight = true;
      g_g.x = _gLastX; g_g.y = _gLastY;
      Serial.printf("[gesture] Swipe %s at x=%d, y=%d\n", dx < 0 ? "LEFT" : "RIGHT", g_g.x, g_g.y);
    } else if (!_gLongFired && abs(dy) > SWIPE_PX && abs(dy) > abs(dx) + 10) {
      if (dy < 0) g_g.swipeUp = true; else g_g.swipeDown = true;
      g_g.x = _gLastX; g_g.y = _gLastY;
      Serial.printf("[gesture] Swipe %s at x=%d, y=%d\n", dy < 0 ? "UP" : "DOWN", g_g.x, g_g.y);
    } else if (!_gLongFired && !_gDragged) {
      g_g.tap = true; g_g.x = _gDownX; g_g.y = _gDownY;
      Serial.printf("[gesture] Tap at x=%d, y=%d\n", g_g.x, g_g.y);
    }
  } else {
    g_g.down = false;
  }
}

// ----------------------------------------------------------------------------
// Momentum scrolling engine (AMOLED-friendly: pure float math, no allocations).
//   A `Scroller` holds a 1-D position with inertia (friction decay) and a spring
//   at the bounds for an elastic overshoot/bounce. Drag past the edge rubber-
//   bands; releasing flings with decaying velocity; a list with [lo,hi] bounds
//   bounces, while a snap-to-grid carousel eases to the nearest item with a
//   subtle overshoot. Call scrollUpdate() every frame; it returns true while
//   still animating so the caller knows a redraw is needed (idle = no work).
// ----------------------------------------------------------------------------
static inline float easeOutCubic(float t) { t = constrain(t, 0.0f, 1.0f); float u = 1 - t; return 1 - u * u * u; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

struct Scroller {
  float pos;     // current offset (caller-defined units: px, or index)
  float vel;     // velocity per frame
  float lo, hi;  // bounds (used when `bounded`)
  bool  snap;    // true -> settle to nearest integer (carousel); false -> free list
  bool  bounded; // true -> clamp to [lo,hi] with bounce; false -> infinite (wrap)
  bool  active;  // finger currently controlling it
};

static const float SCROLL_FRICTION = 0.90f;   // inertia decay / frame
static const float SCROLL_SPRING   = 0.22f;   // edge / snap pull strength
static const float SCROLL_RUBBER   = 0.45f;   // drag resistance past an edge
static const float SCROLL_MINVEL   = 0.30f;   // below this, inertia stops

static inline void scrollInit(Scroller& s, float pos, float lo, float hi, bool snap, bool bounded) {
  s.pos = pos; s.vel = 0; s.lo = lo; s.hi = hi; s.snap = snap; s.bounded = bounded; s.active = false;
}
static inline void scrollSetBounds(Scroller& s, float lo, float hi) { s.lo = lo; s.hi = hi; }
static inline void scrollGrab(Scroller& s)  { s.active = true;  s.vel = 0; }
static inline void scrollDrag(Scroller& s, float delta) {
  float np = s.pos + delta;
  // rubber-band: movement past a bound only counts partially
  if (s.bounded && (np < s.lo || np > s.hi)) s.pos += delta * SCROLL_RUBBER;
  else s.pos = np;
}
static inline void scrollRelease(Scroller& s, float vel) { s.active = false; s.vel = vel; }

// Advance one frame. Returns true while it still needs to animate.
static inline bool scrollUpdate(Scroller& s) {
  if (s.active) return true;                 // finger owns the position
  bool moving = true;

  if (s.snap && fabsf(s.vel) < 1.2f) {
    // settle to the nearest item with a damped spring (subtle overshoot). The
    // velocity is integrated into pos HERE so it is never killed by an inertia
    // dead-zone -> the carousel always lands centered on an item.
    float target = roundf(s.pos);
    if (s.bounded) target = constrain(target, s.lo, s.hi);
    float d = target - s.pos;
    s.vel = s.vel * 0.55f + d * 0.34f;
    s.pos += s.vel;
    if (fabsf(d) < 0.01f && fabsf(s.vel) < 0.02f) { s.pos = target; s.vel = 0; moving = false; }
  } else {
    // free inertia (also: a big fling glides here until it slows into the snap zone)
    s.pos += s.vel;
    s.vel *= SCROLL_FRICTION;
    if (fabsf(s.vel) < SCROLL_MINVEL) { s.vel = 0; if (!s.snap) moving = false; }
  }

  if (s.bounded) {                            // elastic edges (bounce back in)
    if (s.pos < s.lo)      { s.pos += (s.lo - s.pos) * SCROLL_SPRING; s.vel *= 0.5f; moving = true; }
    else if (s.pos > s.hi) { s.pos += (s.hi - s.pos) * SCROLL_SPRING; s.vel *= 0.5f; moving = true; }
  }
  return moving;
}

// ----------------------------------------------------------------------------
// Device geo-location (phone pushes GPS via /gps)
// ----------------------------------------------------------------------------
struct GeoLoc { bool valid; double lat; double lon; uint32_t at; };
static GeoLoc g_myLoc = {false, 0, 0, 0};     // this device's location (from phone)
static void setMyLoc(double lat, double lon) { g_myLoc = {true, lat, lon, millis()}; }

// great-circle bearing (deg, 0=N, CW) from a -> b
static float geoBearing(double lat1, double lon1, double lat2, double lon2) {
  double r = DEG_TO_RAD;
  double dLon = (lon2 - lon1) * r;
  double y = sin(dLon) * cos(lat2 * r);
  double x = cos(lat1 * r) * sin(lat2 * r) - sin(lat1 * r) * cos(lat2 * r) * cos(dLon);
  double b = atan2(y, x) / r;
  if (b < 0) b += 360.0;
  return (float)b;
}
// haversine distance in meters
static float geoDistance(double lat1, double lon1, double lat2, double lon2) {
  double r = DEG_TO_RAD, R = 6371000.0;
  double dLat = (lat2 - lat1) * r, dLon = (lon2 - lon1) * r;
  double a = sin(dLat/2)*sin(dLat/2) + cos(lat1*r)*cos(lat2*r)*sin(dLon/2)*sin(dLon/2);
  return (float)(R * 2 * atan2(sqrt(a), sqrt(1-a)));
}

// ----------------------------------------------------------------------------
// Time base (no RTC/NTP in AP mode -> phone syncs epoch via /time)
// ----------------------------------------------------------------------------
static uint32_t g_timeEpoch = 0;    // unix seconds at last sync (local, tz applied)
static uint32_t g_timeAtMs = 0;     // millis() at last sync
static void timeSet(uint32_t epochLocal) { g_timeEpoch = epochLocal; g_timeAtMs = millis(); }
static bool timeValid() { return g_timeEpoch != 0; }
static uint32_t timeNow() {
  if (!g_timeEpoch) return 0;
  return g_timeEpoch + (millis() - g_timeAtMs) / 1000;
}
static int timeHour() { return (timeNow() / 3600) % 24; }
static int timeMin()  { return (timeNow() / 60) % 60; }
static int timeSec()  { return timeNow() % 60; }

// ----------------------------------------------------------------------------
// Module registry
// ----------------------------------------------------------------------------
typedef void (*VoidFn)();
struct Module {
  const char* id;     // stable key for NVS
  const char* name;   // display name
  const char* glyph;  // short label/emoji-ish
  VoidFn enter;        // called when module opened
  VoidFn tick;         // called each loop while active (reads g_g)
  bool enabled;        // runtime toggle
};

#define MAX_MODULES 40
static Module g_modules[MAX_MODULES];
static int    g_moduleCount = 0;

static void registerModule(const char* id, const char* name, const char* glyph,
                           VoidFn enter, VoidFn tick) {
  if (g_moduleCount >= MAX_MODULES) return;
  Module &m = g_modules[g_moduleCount++];
  m.id = id; m.name = name; m.glyph = glyph;
  m.enter = enter; m.tick = tick;
  // enabled flag persisted under "en_<id>" (default true)
  char key[24]; snprintf(key, sizeof(key), "en_%s", id);
  m.enabled = setGetBool(key, true);
}

static void moduleSetEnabled(const char* id, bool en) {
  for (int i = 0; i < g_moduleCount; i++) {
    if (strcmp(g_modules[i].id, id) == 0) {
      g_modules[i].enabled = en;
      char key[24]; snprintf(key, sizeof(key), "en_%s", id);
      setPutBool(key, en);
      return;
    }
  }
}

// App run state shared with modules / launcher / net
enum AppState { APP_LAUNCHER, APP_MODULE };
static AppState g_app = APP_LAUNCHER;
static int g_activeModule = -1;     // index into g_modules
// Find a registered module index by id (-1 if none). Defined here so modules
// (e.g. lovebox ESP-NOW handler) can use it before net.h is included.
static int findModuleIdx(const String& id) {
  for (int i = 0; i < g_moduleCount; i++)
    if (id == g_modules[i].id) return i;
  return -1;
}

static volatile int g_requestOpen = -1;   // net can request opening a module
static volatile bool g_requestHome = false;

// ----------------------------------------------------------------------------
// Module auto-rotation (slideshow): cycle selected modules on per-step timers,
// looping. Configured from the phone (/rotate), listed + started on the device
// (mod_rotate). The advance logic lives in the main loop (it calls openModule).
// ----------------------------------------------------------------------------
#define ROT_MAX 12
struct RotStep { int modIdx; uint16_t secs; };
static RotStep   g_rot[ROT_MAX];
static int       g_rotN = 0;             // configured steps
static bool      g_rotActive = false;    // currently cycling
static int       g_rotCur = 0;
static uint32_t  g_rotStepAt = 0;
static volatile bool g_rotReqStart = false;  // deferred start (set by net/module)

// Parse "id:secs,id:secs,..." into the rotation list (ids -> module indices).
static void rotSetFromStr(const String& seq) {
  g_rotN = 0;
  int i = 0;
  while (i < (int)seq.length() && g_rotN < ROT_MAX) {
    int comma = seq.indexOf(',', i); if (comma < 0) comma = seq.length();
    String tok = seq.substring(i, comma);
    int colon = tok.indexOf(':');
    if (colon > 0) {
      int mi = findModuleIdx(tok.substring(0, colon));
      int secs = tok.substring(colon + 1).toInt();
      if (mi >= 0 && secs > 0) { g_rot[g_rotN].modIdx = mi; g_rot[g_rotN].secs = (uint16_t)secs; g_rotN++; }
    }
    i = comma + 1;
  }
}
static void rotSave(const String& seq) { setPutStr("rotseq", seq); }
static void rotLoad() { String s = setGetStr("rotseq", ""); if (s.length()) rotSetFromStr(s); }
