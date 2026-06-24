// ============================================================================
// mod_settings.h - Ayarlar
//   On-device settings, no phone needed:
//     * Parlaklik  : -/+ (CO5300 donanim parlakligi, NVS kalici)
//     * Ses temasi : Sessiz / Klasik / Yumusak / Retro (onizleme sesiyle)
//     * Renkler     : arka plan + vurgu (item) rengini hazir paletten sec
//                     (aninda uygulanir, NVS kalici - tum moduller etkilenir)
//     * Pil        : buyuk yuzde + sarj durumu
//     * WiFi       : ag tara -> sec -> ekran klavyesiyle sifre gir -> baglan
//                    (acik aglar dogrudan baglanir; kimlik NVS'ye yazilir,
//                    cloud.h boot'ta ayni anahtarlardan okur)
//     * Vecta Agi  : ESP-NOW ile eslesmis Vecta'lar (canli RSSI / cevrimdisi)
//     * Bilgi      : cihaz adi, ID, IP, bos bellek
//   Dikey kaydirma (momentum Scroller), dokunma = ayar degistir.
//   Long-press = ana ekran (global).
// ============================================================================
#pragma once

#include "platform.h"
#include "espnow.h"
#include <WiFi.h>

static Scroller set_scroll;
static bool     set_dirty = true;

// content-space row anchors (filled in by render, used by hit test)
static int set_yBright = 0, set_ySound = 0, set_ySleep = 0, set_yTilt = 0, set_yMl = 0, set_yBle = 0, set_yWifi = 0, set_yColor = 0;

// ======================= WiFi sub-screen state =======================
//  Main settings list shows a "WiFi" card; tapping it opens a full-screen
//  flow: scan -> pick network -> (on-screen keyboard) password -> connect.
enum SetView { SV_MAIN, SV_WIFI };
static SetView set_view = SV_MAIN;

enum WStage { WS_SCAN, WS_LIST, WS_PASS, WS_CONNECTING, WS_RESULT };
static WStage  w_stage  = WS_SCAN;

#define WIFI_MAX 18
struct WNet { char ssid[33]; int8_t rssi; bool open; };
static WNet     w_nets[WIFI_MAX];
static int      w_netN = 0;
static bool     w_scanRunning = false;
static Scroller w_listScroll;

static char     w_ssid[33] = {0};      // chosen network
static bool     w_ssidOpen = false;
static char     w_pass[65]  = {0};     // typed password
static int      w_passLen   = 0;
static int      w_kbMode    = 0;       // 0 lower, 1 UPPER, 2 symbols
static uint32_t w_connStart = 0;
static bool     w_connOk    = false;
static uint32_t w_resultAt  = 0;       // when WS_RESULT was entered (for auto-home)
static bool     w_dirty     = true;

// ---- non-blocking WiFi helpers (kept off the UI thread) ----
static void wifiStartScan() {
  w_netN = 0; w_scanRunning = true; w_stage = WS_SCAN;
  WiFi.scanDelete();
  WiFi.scanNetworks(true /* async */, false /* hidden */);
}

static void wifiPollScan() {
  if (!w_scanRunning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;          // still working
  w_scanRunning = false;
  if (n < 0) { w_netN = 0; w_stage = WS_LIST; w_dirty = true; return; }
  w_netN = 0;
  for (int i = 0; i < n && w_netN < WIFI_MAX; i++) {
    // de-dupe SSIDs, keep the strongest copy
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    int found = -1;
    for (int j = 0; j < w_netN; j++) if (s.equals(w_nets[j].ssid)) { found = j; break; }
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    if (found >= 0) { if (rssi > w_nets[found].rssi) { w_nets[found].rssi = rssi; w_nets[found].open = open; } continue; }
    strncpy(w_nets[w_netN].ssid, s.c_str(), 32); w_nets[w_netN].ssid[32] = 0;
    w_nets[w_netN].rssi = rssi; w_nets[w_netN].open = open;
    w_netN++;
  }
  WiFi.scanDelete();
  w_stage = WS_LIST; w_dirty = true;
}

static void wifiBeginConnect() {
  WiFi.setHostname("vecta");
  if (w_ssidOpen) WiFi.begin(w_ssid);
  else            WiFi.begin(w_ssid, w_pass);
  w_connStart = millis(); w_connOk = false;
  w_stage = WS_CONNECTING; w_dirty = true;
}

// signal bars from rssi (0..4)
static int wifiBars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

static const char* SND_NAMES[4] = {"Sessiz", "Klasik", "Yumusak", "Retro"};

// screen-sleep timeout presets (seconds; 0 = never)
static const int SLEEP_PRESETS[] = {0, 10, 20, 30, 60, 120, 300};
static const int SLEEP_PRESET_N  = sizeof(SLEEP_PRESETS) / sizeof(SLEEP_PRESETS[0]);
static int sleepPresetIdx() {                 // nearest preset to the current value
  int cur = powerSleepSecs(), best = 0, bd = 1 << 30;
  for (int i = 0; i < SLEEP_PRESET_N; i++) {
    int d = abs(SLEEP_PRESETS[i] - cur);
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

static void settings_enter() {
  scrollInit(set_scroll, 0, 0, 1, false, true);  // hi recomputed in render
  set_view = SV_MAIN;                              // always start on the main list
  set_dirty = true;
}

// a settings "card" background
static void setCard(int x, int y, int w, int h) {
  gfx->fillRoundRect(x, y, w, h, 14, C_BG2);
}

static void settingsRender() {
  gfx->fillScreen(C_BG);
  int y = 36 - (int)set_scroll.pos;     // content cursor (screen space)
  const int x = 48, w = LCD_W - 96;

  textCenter("AYARLAR", CXi, y, 3, C_GOLD);
  y += 44;

  // ---- battery card ----
  setCard(x, y, w, 96);
  int p = batteryPct();
  char pb[16];
  if (p >= 0) snprintf(pb, sizeof(pb), "%d%%", p); else snprintf(pb, sizeof(pb), "--");
  uint16_t pcol = p < 0 ? C_DIM : (p < 20 ? C_DANGER : (p < 40 ? C_WARN : C_OK));
  textCenter(pb, x + 70, y + 48, 4, pcol);
  drawBatteryIcon(x + w - 70, y + 18);
  textCenter(g_batCharging ? "sarj oluyor" : "pil", x + w - 80, y + 60, 2, C_DIM);
  y += 116;

  // ---- brightness ----
  set_yBright = y;
  setCard(x, y, w, 86);
  textCenter("Parlaklik", CXi, y + 20, 2, C_TEXT);
  // - / + buttons
  gfx->fillCircle(x + 36, y + 56, 22, C_BG);
  gfx->drawCircle(x + 36, y + 56, 22, C_DIM);
  gfx->fillRect(x + 26, y + 54, 20, 4, C_TEXT);
  gfx->fillCircle(x + w - 36, y + 56, 22, C_BG);
  gfx->drawCircle(x + w - 36, y + 56, 22, C_DIM);
  gfx->fillRect(x + w - 46, y + 54, 20, 4, C_TEXT);
  gfx->fillRect(x + w - 38, y + 46, 4, 20, C_TEXT);
  // bar
  int bx = x + 70, bw = w - 140;
  gfx->fillRoundRect(bx, y + 48, bw, 16, 8, C_BG);
  gfx->fillRoundRect(bx, y + 48, bw * g_brightPct / 100, 16, 8, C_GOLD);
  y += 106;

  // ---- sound theme ----
  set_ySound = y;
  setCard(x, y, w, 86);
  textCenter("Ses Temasi", CXi, y + 20, 2, C_TEXT);
  gfx->fillTriangle(x + 26, y + 56, x + 48, y + 42, x + 48, y + 70, C_TEXT);      // <
  gfx->fillTriangle(x + w - 26, y + 56, x + w - 48, y + 42, x + w - 48, y + 70, C_TEXT);  // >
  textCenter(SND_NAMES[g_soundTheme & 3], CXi, y + 56, 3, C_ACCENT);
  y += 106;

  // ---- screen sleep timeout ----
  set_ySleep = y;
  setCard(x, y, w, 86);
  textCenter("Ekran Uykusu", CXi, y + 20, 2, C_TEXT);
  gfx->fillTriangle(x + 26, y + 56, x + 48, y + 42, x + 48, y + 70, C_TEXT);            // <
  gfx->fillTriangle(x + w - 26, y + 56, x + w - 48, y + 42, x + w - 48, y + 70, C_TEXT); // >
  {
    int sec = powerSleepSecs();
    char tb[16];
    if (sec == 0) snprintf(tb, sizeof(tb), "Kapali");
    else          snprintf(tb, sizeof(tb), "%d sn", sec);
    textCenter(tb, CXi, y + 56, 3, C_ACCENT);
  }
  y += 106;

  // ---- tilt-to-wake toggle ----
  set_yTilt = y;
  setCard(x, y, w, 70);
  gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
  gfx->setCursor(x + 22, y + 26); gfx->print("Kaldirinca Uyan");
  {
    int pw = 64, ph = 30, px = x + w - pw - 22, py = y + 20;
    gfx->fillRoundRect(px, py, pw, ph, ph / 2, g_tiltWake ? C_OK : C_BG);
    gfx->fillCircle(g_tiltWake ? px + pw - ph / 2 : px + ph / 2, py + ph / 2, ph / 2 - 3, C_TEXT);
  }
  y += 90;

  // ---- merge Now-Playing + Lyrics toggle ----
  set_yMl = y;
  setCard(x, y, w, 70);
  gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
  gfx->setCursor(x + 22, y + 26); gfx->print("Muzik + Sozler");
  {
    int pw = 64, ph = 30, px = x + w - pw - 22, py = y + 20;
    gfx->fillRoundRect(px, py, pw, ph, ph / 2, g_mlMerge ? C_OK : C_BG);
    gfx->fillCircle(g_mlMerge ? px + pw - ph / 2 : px + ph / 2, py + ph / 2, ph / 2 - 3, C_TEXT);
  }
  y += 90;

  // ---- Bluetooth on/off (shares the radio with Wi-Fi; off = solid Wi-Fi) ----
  set_yBle = y;
  setCard(x, y, w, 70);
  gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
  gfx->setCursor(x + 22, y + 22); gfx->print("Bluetooth");
  gfx->setTextSize(1); gfx->setTextColor(C_DIM);
  gfx->setCursor(x + 22, y + 48); gfx->print(g_bleOn ? "acik (WiFi kopabilir)" : "kapali (WiFi sabit)");
  {
    int pw = 64, ph = 30, px = x + w - pw - 22, py = y + 20;
    gfx->fillRoundRect(px, py, pw, ph, ph / 2, g_bleOn ? C_OK : C_BG);
    gfx->fillCircle(g_bleOn ? px + pw - ph / 2 : px + ph / 2, py + ph / 2, ph / 2 - 3, C_TEXT);
  }
  y += 90;

  // ---- WiFi (tap to open scan/connect screen) ----
  set_yWifi = y;
  setCard(x, y, w, 86);
  textCenter("WiFi", x + 70, y + 24, 2, C_TEXT);
  {
    bool on = (WiFi.status() == WL_CONNECTED);
    gfx->fillCircle(x + 24, y + 24, 7, on ? C_OK : C_DIM);
    if (on) {
      String ss = WiFi.SSID();
      char sb[26];
      strncpy(sb, ss.c_str(), 25); sb[25] = 0;
      gfx->setTextSize(2); gfx->setTextColor(C_OK);
      gfx->setCursor(x + 22, y + 50); gfx->print(sb[0] ? sb : "bagli");
    } else {
      gfx->setTextSize(2); gfx->setTextColor(C_DIM);
      gfx->setCursor(x + 22, y + 50); gfx->print("bagli degil - dokun");
    }
    // chevron hint
    gfx->fillTriangle(x + w - 30, y + 33, x + w - 44, y + 23, x + w - 44, y + 43, C_DIM);
  }
  y += 106;

  // ---- Colors / theme (background + accent swatches) ----
  set_yColor = y;
  const int colCardH = 184;
  setCard(x, y, w, colCardH);
  textCenter("Renkler", CXi, y + 20, 2, C_TEXT);
  gfx->setTextSize(2); gfx->setTextColor(C_DIM);
  gfx->setCursor(x + 20, y + 40); gfx->print("Arka Plan");
  {
    int slot = w / BG_THEME_N, row = y + 86;
    for (int i = 0; i < BG_THEME_N; i++) {
      int cx = x + slot / 2 + i * slot;
      uint16_t c = rgb(BG_THEMES[i].bg[0], BG_THEMES[i].bg[1], BG_THEMES[i].bg[2]);
      gfx->fillCircle(cx, row, 16, c);
      gfx->drawCircle(cx, row, 16, C_DIM);
      if (i == g_bgTheme) { gfx->drawCircle(cx, row, 19, C_GOLD); gfx->drawCircle(cx, row, 20, C_GOLD); }
    }
  }
  gfx->setTextColor(C_DIM); gfx->setCursor(x + 20, y + 112); gfx->print("Vurgu");
  {
    int slot = w / ACC_THEME_N, row = y + 158;
    for (int i = 0; i < ACC_THEME_N; i++) {
      int cx = x + slot / 2 + i * slot;
      uint16_t c = rgb(ACC_THEMES[i].c[0], ACC_THEMES[i].c[1], ACC_THEMES[i].c[2]);
      gfx->fillCircle(cx, row, 14, c);
      if (i == g_accTheme) { gfx->drawCircle(cx, row, 17, C_TEXT); gfx->drawCircle(cx, row, 18, C_TEXT); }
    }
  }
  y += colCardH + 20;

  // ---- Vecta network (paired peers) ----
  setCard(x, y, w, 40 + max(1, g_enPeerN) * 34);
  textCenter("Vecta Agi", CXi, y + 20, 2, C_TEXT);
  if (g_enPeerN == 0) {
    textCenter("eslesen cihaz yok", CXi, y + 52, 2, C_DIM);
  } else {
    for (int i = 0; i < g_enPeerN; i++) {
      int ry = y + 46 + i * 34;
      bool on = enPeerOnline(g_enPeers[i]);
      gfx->fillCircle(x + 26, ry, 7, on ? C_OK : C_DIM);
      gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
      gfx->setCursor(x + 44, ry - 8);
      gfx->print(g_enPeers[i].name[0] ? g_enPeers[i].name : g_enPeers[i].id);
      char rb[20];
      if (on) snprintf(rb, sizeof(rb), "%d dBm", g_enPeers[i].rssi);
      else    snprintf(rb, sizeof(rb), "uzakta");
      gfx->setTextColor(C_DIM);
      gfx->setCursor(x + w - 110, ry - 8); gfx->print(rb);
    }
  }
  y += 60 + max(1, g_enPeerN) * 34;

  // ---- device info ----
  setCard(x, y, w, 118);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM);  gfx->setCursor(x + 20, y + 16);  gfx->print("Ad");
  gfx->setTextColor(C_TEXT); gfx->setCursor(x + 110, y + 16); gfx->print(deviceName());
  gfx->setTextColor(C_DIM);  gfx->setCursor(x + 20, y + 42);  gfx->print("ID");
  gfx->setTextColor(C_TEXT); gfx->setCursor(x + 110, y + 42); gfx->print(g_devId);
  gfx->setTextColor(C_DIM);  gfx->setCursor(x + 20, y + 68);  gfx->print("IP");
  gfx->setTextColor(C_TEXT); gfx->setCursor(x + 110, y + 68);
  gfx->print(WiFi.localIP() != IPAddress(0,0,0,0) ? WiFi.localIP().toString()
                                                  : WiFi.softAPIP().toString());
  gfx->setTextColor(C_DIM);  gfx->setCursor(x + 20, y + 94);  gfx->print("Bellek");
  gfx->setTextColor(C_TEXT); gfx->setCursor(x + 110, y + 94);
  gfx->printf("%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  y += 138;

  // scroll bounds from real content height
  int contentH = (y + (int)set_scroll.pos) - 36;
  float hi = (float)max(0, contentH - (LCD_H - 80));
  scrollSetBounds(set_scroll, 0, hi);

  present();
}

// ============================ WiFi sub-screen ============================
// On-screen keyboard: keys are rebuilt every frame in wifiRenderPass() and
// hit-tested by wifiTick() against the same array (same anchor pattern the
// main list uses). Codes >=32 are literal chars; small codes are specials.
#define KC_SHIFT 1
#define KC_SYM   2
#define KC_BKSP  3
#define KC_SPACE 4
#define KC_OK    5

struct KKey { int16_t x, y, w, h; int code; char lab[8]; };
static KKey w_keys[48];
static int  w_keyN = 0;

static const char* KB_ROWS[3][3] = {
  { "qwertyuiop", "asdfghjkl", "zxcvbnm" },
  { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" },
  { "1234567890", "@#$_&-+()", "*\"':;!?/" },
};

static void kbAddKey(int x, int y, int w, int h, int code, const char* lab) {
  if (w_keyN >= 48) return;
  KKey& k = w_keys[w_keyN++];
  k.x = x; k.y = y; k.w = w; k.h = h; k.code = code;
  strncpy(k.lab, lab, 7); k.lab[7] = 0;
}

static void kbBuild() {
  w_keyN = 0;
  const int topY = 154, rowH = 44, gap = 5, cw = 40;
  // rows 0 & 1 (plain char rows, centered)
  for (int rdx = 0; rdx < 2; rdx++) {
    const char* r = KB_ROWS[w_kbMode][rdx]; int n = strlen(r);
    int sx = CXi - (n * cw) / 2, y = topY + rdx * (rowH + gap);
    char lb[2] = {0, 0};
    for (int i = 0; i < n; i++) { lb[0] = r[i]; kbAddKey(sx + i * cw + 2, y, cw - 4, rowH, r[i], lb); }
  }
  // row 2: [shift] chars [bksp]
  {
    const char* r = KB_ROWS[w_kbMode][2]; int n = strlen(r);
    const int fnW = 54;
    int total = fnW + n * cw + fnW, sx = CXi - total / 2, y = topY + 2 * (rowH + gap);
    kbAddKey(sx + 2, y, fnW - 4, rowH, KC_SHIFT, w_kbMode == 1 ? "^^" : "^");
    char lb[2] = {0, 0};
    for (int i = 0; i < n; i++) { lb[0] = r[i]; kbAddKey(sx + fnW + i * cw + 2, y, cw - 4, rowH, r[i], lb); }
    kbAddKey(sx + fnW + n * cw + 2, y, fnW - 4, rowH, KC_BKSP, "<x");
  }
  // row 3: [?123/ABC] [space] [BAGLAN]
  {
    int y = topY + 3 * (rowH + gap);
    const int symW = 66, okW = 96, g = 6, spaceW = 190;
    int total = symW + g + spaceW + g + okW, sx = CXi - total / 2;
    kbAddKey(sx, y, symW, rowH, KC_SYM, w_kbMode == 2 ? "ABC" : "?123");
    kbAddKey(sx + symW + g, y, spaceW, rowH, KC_SPACE, "");
    kbAddKey(sx + symW + g + spaceW + g, y, okW, rowH, KC_OK, "BAGLAN");
  }
}

static void kbDraw() {
  for (int i = 0; i < w_keyN; i++) {
    KKey& k = w_keys[i];
    bool fn = (k.code < 32);
    uint16_t bg = fn ? C_BG2 : C_BG;
    if (k.code == KC_OK) bg = C_OK;
    gfx->fillRoundRect(k.x, k.y, k.w, k.h, 8, bg);
    gfx->drawRoundRect(k.x, k.y, k.w, k.h, 8, C_DIM);
    uint16_t fg = (k.code == KC_OK) ? C_BG : C_TEXT;
    if (k.code == KC_SPACE) { gfx->fillRect(k.x + 18, k.y + k.h / 2, k.w - 36, 3, C_DIM); }
    else textCenter(k.lab, k.x + k.w / 2, k.y + k.h / 2, fn ? 2 : 3, fg);
  }
}

// draw a small wifi signal glyph (4 bars) at (cx,cy bottom)
static void drawWifiBars(int x, int yb, int bars) {
  for (int b = 0; b < 4; b++) {
    int h = 4 + b * 4;
    uint16_t c = (b < bars) ? C_OK : C_BG2;
    gfx->fillRect(x + b * 7, yb - h, 5, h, c);
  }
}

static void wifiRenderPass() {
  gfx->fillScreen(C_BG);
  // back chevron (top-left, inside the circle)
  gfx->fillTriangle(70, 40, 84, 30, 84, 50, C_DIM);
  gfx->setTextSize(2); gfx->setTextColor(C_DIM); gfx->setCursor(92, 32); gfx->print("geri");

  if (w_stage == WS_SCAN) {
    textCenter("WiFi", CXi, 120, 4, C_GOLD);
    textCenter("araniyor...", CXi, 200, 3, C_DIM);
    // simple pulsing dot
    int n = (millis() / 300) % 4;
    for (int i = 0; i < 4; i++)
      gfx->fillCircle(CXi - 30 + i * 20, 250, 6, i <= n ? C_ACCENT : C_BG2);
    present(); return;
  }

  if (w_stage == WS_LIST) {
    textCenter("WiFi Aglari", CXi, 70, 3, C_GOLD);
    if (w_netN == 0) {
      textCenter("ag bulunamadi", CXi, 200, 2, C_DIM);
      textCenter("tekrar aramak icin dokun", CXi, 240, 2, C_DIM);
      present(); return;
    }
    const int rowH = 60, top = 110, x = 50, w = LCD_W - 100;
    bool   connOn  = (WiFi.status() == WL_CONNECTED);
    String connSsid = connOn ? WiFi.SSID() : String();
    int y0 = top - (int)w_listScroll.pos;
    for (int i = 0; i < w_netN; i++) {
      int ry = y0 + i * rowH;
      if (ry > LCD_H || ry + rowH < 90) continue;
      bool here = connOn && connSsid.equals(w_nets[i].ssid);   // currently joined
      gfx->fillRoundRect(x, ry, w, rowH - 8, 12, here ? C_BG : C_BG2);
      if (here) gfx->drawRoundRect(x, ry, w, rowH - 8, 12, C_OK);
      drawWifiBars(x + 16, ry + rowH - 22, wifiBars(w_nets[i].rssi));
      char sb[24];
      strncpy(sb, w_nets[i].ssid, 23); sb[23] = 0;
      gfx->setTextSize(2); gfx->setTextColor(here ? C_OK : C_TEXT);
      gfx->setCursor(x + 56, ry + 18); gfx->print(sb);
      int gx = x + w - 30;                          // right-side glyph slot
      if (here) {                                   // green check = connected
        int cy = ry + 22;
        gfx->fillTriangle(gx - 2, cy + 2, gx + 4, cy + 9, gx + 3, cy + 11, C_OK);
        gfx->fillTriangle(gx + 3, cy + 11, gx + 16, cy - 6, gx + 14, cy - 8, C_OK);
      } else if (!w_nets[i].open) {                 // lock glyph (secured)
        int ly = ry + 16;
        gfx->fillRoundRect(gx, ly + 6, 16, 12, 3, C_GOLD);
        gfx->drawCircle(gx + 8, ly + 5, 5, C_GOLD);
      }
    }
    int contentH = w_netN * rowH;
    scrollSetBounds(w_listScroll, 0, (float)max(0, contentH - (LCD_H - top - 40)));
    present(); return;
  }

  if (w_stage == WS_PASS) {
    char sb[26]; strncpy(sb, w_ssid, 25); sb[25] = 0;
    textCenter(sb, CXi, 62, 2, C_GOLD);
    // password field
    gfx->fillRoundRect(60, 92, LCD_W - 120, 40, 10, C_BG2);
    gfx->drawRoundRect(60, 92, LCD_W - 120, 40, 10, C_DIM);
    gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
    gfx->setCursor(74, 104);
    gfx->print(w_passLen ? w_pass : "sifre");
    kbDraw();
    present(); return;
  }

  if (w_stage == WS_CONNECTING) {
    textCenter("Baglaniyor", CXi, 150, 3, C_GOLD);
    char sb[26]; strncpy(sb, w_ssid, 25); sb[25] = 0;
    textCenter(sb, CXi, 210, 2, C_TEXT);
    int n = (millis() / 300) % 4;
    for (int i = 0; i < 4; i++)
      gfx->fillCircle(CXi - 30 + i * 20, 270, 6, i <= n ? C_ACCENT : C_BG2);
    present(); return;
  }

  // WS_RESULT
  textCenter(w_connOk ? "Baglandi!" : "Basarisiz", CXi, 150,
             4, w_connOk ? C_OK : C_DANGER);
  if (w_connOk) {
    textCenter(WiFi.localIP().toString().c_str(), CXi, 220, 2, C_TEXT);
    textCenter("ana ekrana donuluyor...", CXi, 300, 2, C_DIM);
  } else {
    textCenter("sifre/ag hatali", CXi, 215, 2, C_DIM);
    textCenter("dokun: tekrar ara", CXi, 300, 2, C_DIM);
  }
  present();
}

static void wifiTick() {
  // swipe-right or back-chevron tap returns to the main settings list
  if (g_g.swipeRight) { set_view = SV_MAIN; set_dirty = true; return; }

  if (w_stage == WS_SCAN) {
    wifiPollScan();
    wifiRenderPass();
    delay(16); return;
  }

  if (w_stage == WS_LIST) {
    // scroll
    if (g_g.down) {
      if (!w_listScroll.active) scrollGrab(w_listScroll);
      if (g_g.dragDY != 0) scrollDrag(w_listScroll, -g_g.dragDY);
    } else if (w_listScroll.active) {
      scrollRelease(w_listScroll, g_g.released ? -g_g.velY : 0.0f);
    }
    if (g_g.tap) {
      if (g_g.y < 60 && g_g.x < 160) { set_view = SV_MAIN; set_dirty = true; return; } // back
      if (w_netN == 0) { wifiStartScan(); w_dirty = true; }                            // re-scan
      else {
        const int rowH = 60, top = 110;
        int idx = (g_g.y - top + (int)w_listScroll.pos) / rowH;
        if (idx >= 0 && idx < w_netN && g_g.y >= 90) {
          strncpy(w_ssid, w_nets[idx].ssid, 32); w_ssid[32] = 0;
          w_ssidOpen = w_nets[idx].open;
          w_pass[0] = 0; w_passLen = 0; w_kbMode = 0;
          soundTap();
          if (w_ssidOpen) wifiBeginConnect();          // open net: connect now
          else { w_stage = WS_PASS; w_dirty = true; }  // secured: ask password
          return;   // don't fall through to the WS_LIST tail (it clears w_dirty
                    // before WS_PASS gets to build the keyboard -> dead keys)
        }
      }
    }
    bool anim = scrollUpdate(w_listScroll);
    if (w_dirty || anim || g_g.down) { wifiRenderPass(); w_dirty = false; }
    delay(16); return;
  }

  if (w_stage == WS_PASS) {
    if (g_g.tap) {
      if (g_g.y < 60 && g_g.x < 160) { w_stage = WS_LIST; w_dirty = true; return; } // back to list
      for (int i = 0; i < w_keyN; i++) {
        KKey& k = w_keys[i];
        if (g_g.x >= k.x && g_g.x <= k.x + k.w && g_g.y >= k.y && g_g.y <= k.y + k.h) {
          soundTap();
          switch (k.code) {
            case KC_SHIFT: w_kbMode = (w_kbMode == 0) ? 1 : 0; break;
            case KC_SYM:   w_kbMode = (w_kbMode == 2) ? 0 : 2; break;
            case KC_BKSP:  if (w_passLen > 0) w_pass[--w_passLen] = 0; break;
            case KC_SPACE: if (w_passLen < 64) { w_pass[w_passLen++] = ' '; w_pass[w_passLen] = 0; } break;
            case KC_OK:    if (w_passLen >= 1) wifiBeginConnect(); break;
            default:
              if (k.code >= 32 && w_passLen < 64) {
                w_pass[w_passLen++] = (char)k.code; w_pass[w_passLen] = 0;
                if (w_kbMode == 1) w_kbMode = 0;       // auto-unshift after a letter
              }
              break;
          }
          w_dirty = true;
          break;
        }
      }
    }
    if (w_dirty) { kbBuild(); wifiRenderPass(); w_dirty = false; }
    delay(16); return;
  }

  if (w_stage == WS_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      w_connOk = true;
      wifiSetCredentials(String(w_ssid), String(w_ssidOpen ? "" : w_pass)); // persist for boot
      cloud_staJoined = true;
      soundOk();
      w_stage = WS_RESULT; w_resultAt = millis(); w_dirty = true;
    } else if (millis() - w_connStart > 15000) {
      w_connOk = false; WiFi.disconnect();
      w_stage = WS_RESULT; w_resultAt = millis(); w_dirty = true;
    }
    wifiRenderPass();
    delay(16); return;
  }

  // WS_RESULT
  if (w_connOk) {
    // success -> show "Baglandi!" briefly, then auto-return to the home screen
    if (g_g.tap || millis() - w_resultAt > 1500) g_requestHome = true;
  } else if (g_g.tap) {
    wifiStartScan(); w_dirty = true;            // failed -> tap to rescan
  }
  wifiRenderPass();
  delay(16);
}

static void settings_tick() {
  // WiFi sub-screen owns the frame while open
  if (set_view == SV_WIFI) { wifiTick(); return; }

  // drag / fling
  if (g_g.down) {
    if (!set_scroll.active) scrollGrab(set_scroll);
    if (g_g.dragDY != 0) scrollDrag(set_scroll, -g_g.dragDY);
  } else if (set_scroll.active) {
    scrollRelease(set_scroll, g_g.released ? -g_g.velY : 0.0f);
  }

  if (g_g.tap) {
    int ty = g_g.y;   // screen space; row anchors are already screen space
    // brightness -/+
    if (ty >= set_yBright + 30 && ty <= set_yBright + 86) {
      if (g_g.x < CXi - 60)      { displaySetBrightness(g_brightPct - 10); soundTap(); }
      else if (g_g.x > CXi + 60) { displaySetBrightness(g_brightPct + 10); soundTap(); }
      set_dirty = true;
    }
    // sound theme < >
    else if (ty >= set_ySound + 30 && ty <= set_ySound + 86) {
      int t = g_soundTheme;
      if (g_g.x < CXi - 60)      t = (t + 3) % 4;
      else if (g_g.x > CXi + 60) t = (t + 1) % 4;
      if (t != g_soundTheme) {
        soundSetTheme(t);
        soundOk();             // theme preview
        set_dirty = true;
      }
    }
    // screen sleep timeout < >
    else if (ty >= set_ySleep + 30 && ty <= set_ySleep + 86) {
      int idx = sleepPresetIdx();
      if (g_g.x < CXi - 60)      idx = max(0, idx - 1);
      else if (g_g.x > CXi + 60) idx = min(SLEEP_PRESET_N - 1, idx + 1);
      powerSetSleepSecs(SLEEP_PRESETS[idx]); soundTap(); set_dirty = true;
    }
    // tilt-to-wake toggle (tap anywhere on the card)
    else if (ty >= set_yTilt + 10 && ty <= set_yTilt + 70) {
      powerSetTiltWake(!g_tiltWake); soundTap(); set_dirty = true;
    }
    // merge Now-Playing + Lyrics toggle
    else if (ty >= set_yMl + 10 && ty <= set_yMl + 70) {
      setMlMerge(!g_mlMerge); soundTap(); set_dirty = true;
    }
    // Bluetooth on/off -> applies cleanly on a quick restart (BLE can't be torn
    // down safely at runtime given how many places use the shared bleKb handle).
    else if (ty >= set_yBle + 10 && ty <= set_yBle + 70) {
      setBleOn(!g_bleOn); soundTap();
      gfx->fillScreen(C_BG);
      textCenter(g_bleOn ? "Bluetooth ACIK" : "Bluetooth KAPALI", CXi, CYi - 18, 2, C_TEXT);
      textCenter("yeniden baslatiliyor...", CXi, CYi + 18, 2, C_DIM);
      present();
      delay(900);
      ESP.restart();
    }
    // WiFi card -> open scan/connect screen
    else if (ty >= set_yWifi && ty <= set_yWifi + 86) {
      soundTap();
      set_view = SV_WIFI;
      scrollInit(w_listScroll, 0, 0, 1, false, true);
      wifiStartScan();
      w_dirty = true;
    }
    // color theme: background swatch row
    else if (ty >= set_yColor + 64 && ty <= set_yColor + 108) {
      int slot = (LCD_W - 96) / BG_THEME_N, i = (g_g.x - 48) / slot;
      if (i >= 0 && i < BG_THEME_N) { themeSetBg(i); soundTap(); set_dirty = true; }
    }
    // color theme: accent swatch row
    else if (ty >= set_yColor + 136 && ty <= set_yColor + 180) {
      int slot = (LCD_W - 96) / ACC_THEME_N, i = (g_g.x - 48) / slot;
      if (i >= 0 && i < ACC_THEME_N) { themeSetAcc(i); soundTap(); set_dirty = true; }
    }
  }

  bool animating = scrollUpdate(set_scroll);
  static uint32_t lastAuto = 0;
  if (millis() - lastAuto > 2000) { lastAuto = millis(); set_dirty = true; }  // pil/RSSI tazele
  if (set_dirty || animating || g_g.down) { settingsRender(); set_dirty = false; }
  delay(16);
}
