// ============================================================================
// mod_lyrics.h - Module: Sozler (time-synced karaoke lyrics)
//   TWO sources, device prefers its own:
//   1) DEVICE MODE (no phone app needed): the device already knows the
//      now-playing track/artist/position (mod_music.h m_*, pushed by /np on
//      Android or the AMS BLE client on iPhone). When the Sozler module is open
//      and the watch has internet (STA Wi-Fi), it fetches the LRC itself from
//      LRCLIB (lrclib.net, no key - same pattern as the Weather module) and runs
//      the line timing LOCALLY from the playback position. Start it FROM THE
//      DEVICE: just open the module.
//   2) PHONE PUSH (legacy fallback): the phone fetches the LRC and pushes each
//      line to /lyrics. Used only when device mode can't (no internet / no
//      synced lyrics found).
//
//   Wire (net.h hLyrics): /lyrics?c=<line>&dur=<lineMs>&off=<msIntoLine>&s=<style>&new=1
// ============================================================================
#pragma once

#include "platform.h"
#include "mod_music.h"          // now-playing metadata (m_track/m_artist/m_pos/...)
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define LYR_MAXW 32
static char     lyr_cur[180] = "";
static char     lyr_w[LYR_MAXW][40];        // tokenized words (UTF-8)
static int      lyr_wx[LYR_MAXW];           // x within its row
static int      lyr_wrow[LYR_MAXW];         // row index
static int      lyr_rowW[6];                // each row's pixel width
static int      lyr_cum[LYR_MAXW];          // cumulative char count incl. word
static int      lyr_nw = 0, lyr_nrow = 0, lyr_totChar = 1;
static uint8_t  lyr_ts = 4;
static int      lyr_style = 0;
static uint32_t lyr_lineAt = 0;             // when this line arrived (entrance anim)
static uint32_t lyr_rxAt = 0;
static bool     lyr_have = false;
static int      lyr_shownHl = -2;           // last drawn highlight (redraw guard)

static int lyrTextW(const char* s, uint8_t ts) {
  return gfxWidthAccented(s, ts);          // width of the ASCII-folded base
}
static void lyrDrawAt(const char* s, int x, int yc, uint8_t ts, uint16_t col) {
  char base[64]; trFold(s, base, sizeof(base));
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(ts);
  int16_t bx, by; uint16_t bw, bh; gfx->getTextBounds(base, 0, 0, &bx, &by, &bw, &bh);
  int originX = x - bx, originY = yc - bh / 2 - by;
  gfx->setFont(NULL);
  gfxDrawAccented(s, originX, originY, ts, col);   // base + Turkish accents
}

// Build the (cached) word layout once per line - measuring is done here, NOT
// every frame, so rendering stays cheap and never stalls.
static void lyricsLayout() {
  int len = (int)strlen(lyr_cur);
  lyr_ts = len > 42 ? 3 : (len > 22 ? 4 : 5);
  char buf[180]; strncpy(buf, lyr_cur, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  lyr_nw = 0; lyr_nrow = 0; lyr_totChar = 0;
  for (int i = 0; i < 6; i++) lyr_rowW[i] = 0;

  const int maxW = 440, sp = 7 * lyr_ts;
  int curW = 0, row = 0;
  for (char* p = strtok(buf, " "); p && lyr_nw < LYR_MAXW; p = strtok(nullptr, " ")) {
    strncpy(lyr_w[lyr_nw], p, 39); lyr_w[lyr_nw][39] = 0;
    int w = lyrTextW(lyr_w[lyr_nw], lyr_ts);
    int add = (curW == 0 ? 0 : sp) + w;
    if (curW + add > maxW && curW > 0 && row < 5) { lyr_rowW[row] = curW; row++; curW = 0; }
    lyr_wx[lyr_nw] = curW == 0 ? 0 : curW + sp;
    lyr_wrow[lyr_nw] = row;
    curW = lyr_wx[lyr_nw] + w;
    lyr_totChar += (int)strlen(lyr_w[lyr_nw]);
    lyr_cum[lyr_nw] = lyr_totChar;
    lyr_nw++;
  }
  lyr_rowW[row] = curW; lyr_nrow = row + 1;
  if (lyr_totChar < 1) lyr_totChar = 1;
}

// Called by net.h on each update. The PHONE computes the sung-word count from
// the exact playback position and pushes it; the device just displays it (no
// local clock -> no drift). w = sung words, isNew = line just changed.
static void lyricsSet(const String& cur, int style, bool isNew) {
  String c = cur;                          // raw UTF-8 (accents drawn on device)
  if (isNew || strcmp(c.c_str(), lyr_cur) != 0) {
    c.toCharArray(lyr_cur, sizeof(lyr_cur));
    lyricsLayout();
    lyr_lineAt = millis();
    lyr_shownHl = -2;                       // force redraw (new line arrived)
  }
  lyr_style = style;
  lyr_have = strlen(lyr_cur) > 0;
  lyr_rxAt = millis();
  if (g_app == APP_LAUNCHER) {
    int idx = findModuleIdx("lyrics");
    if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  }
}

static inline bool lyricsFresh() { return lyr_rxAt && (millis() - lyr_rxAt < 12000); }

// ============================================================================
// DEVICE MODE: fetch synced LRC from LRCLIB ourselves + sync from m_pos.
// ============================================================================
#define LYR_DEV_MAXL  120          // max LRC lines kept
#define LYR_DEV_LEAD  250          // ms: show the line slightly early
static uint32_t dev_ts[LYR_DEV_MAXL];       // line start (ms)
static char     dev_txt[LYR_DEV_MAXL][88];  // line text (trAscii)
static int      dev_n = 0;
static char     dev_key[132] = "";          // track|artist this fetch is for
static bool     dev_fetching = false, dev_failed = false, dev_noSync = false;
static int      dev_curIdx = -1;
static uint32_t dev_lastTry = 0;

static String lyrUrlEnc(const char* s) {
  String o; const char* hex = "0123456789ABCDEF";
  for (const char* p = s; *p; p++) {
    char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

// push one raw LRC line "[mm:ss.xx](more tags) text" -> dev_ts/dev_txt
static void lyrPushLrc(String ln) {
  ln.trim();
  if (ln.length() < 4 || ln[0] != '[') return;
  int p = 0; long firstMs = -1;
  while (p < (int)ln.length() && ln[p] == '[') {     // consume leading [..] tags
    int e = ln.indexOf(']', p); if (e < 0) break;
    String tag = ln.substring(p + 1, e);
    int colon = tag.indexOf(':');
    if (colon > 0 && tag[0] >= '0' && tag[0] <= '9') {   // numeric -> timestamp
      long mm = tag.substring(0, colon).toInt();
      float ss = tag.substring(colon + 1).toFloat();
      long ms = mm * 60000L + (long)(ss * 1000.0f);
      if (firstMs < 0) firstMs = ms;
    }
    p = e + 1;
  }
  if (firstMs < 0) return;                            // metadata-only line
  String text = ln.substring(p); text.trim();
  if (dev_n >= LYR_DEV_MAXL) return;
  dev_ts[dev_n] = (uint32_t)firstMs;
  text.toCharArray(dev_txt[dev_n], sizeof(dev_txt[0]));   // raw UTF-8 (accents drawn)
  dev_n++;
}

// parse the first "syncedLyrics":"..." JSON string out of a body (works for the
// /get object and the first hit of the /search array). Returns line count.
static int lyrParseSynced(const String& body) {
  dev_n = 0; dev_curIdx = -1;
  int k = body.indexOf("\"syncedLyrics\"");
  if (k < 0) return 0;
  k = body.indexOf(':', k); if (k < 0) return 0; k++;
  int len = body.length();
  while (k < len && body[k] == ' ') k++;
  if (k >= len || body[k] != '"') return 0;           // null / missing
  k++;                                                // past opening quote
  String cur = "";
  while (k < len) {
    char c = body[k++];
    if (c == '\\' && k < len) {
      char e = body[k++];
      if (e == 'n')      { lyrPushLrc(cur); cur = ""; }
      else if (e == 'r') { /* skip */ }
      else if (e == 't') cur += ' ';
      else if (e == '"') cur += '"';
      else if (e == '\\') cur += '\\';
      else if (e == 'u') {                            // \uXXXX (basic latin only)
        if (k + 4 <= len) { long cp = strtol(body.substring(k, k + 4).c_str(), nullptr, 16);
          k += 4; cur += (cp >= 32 && cp < 127) ? (char)cp : '?'; }
      } else cur += e;
    } else if (c == '"') { lyrPushLrc(cur); break; }  // closing quote
    else cur += c;
  }
  return dev_n;
}

static bool lyrHttpGet(const String& url, String& out) {
  WiFiClientSecure tls; tls.setInsecure();
  HTTPClient http; http.setTimeout(6000);
  if (!http.begin(tls, url)) return false;
  http.addHeader("User-Agent", "Vecta-Watch (github.com/vecta)");
  int rc = http.GET();
  out = (rc == 200) ? http.getString() : String("");
  http.end();
  return rc == 200 && out.length() > 0;
}

// fetch synced lyrics for the current now-playing track; /get then /search.
static bool lyricsDevFetch() {
  if (WiFi.status() != WL_CONNECTED || !m_track[0]) return false;
  String enc = String("track_name=") + lyrUrlEnc(m_track) +
               "&artist_name=" + lyrUrlEnc(m_artist);
  String getUrl = String("https://lrclib.net/api/get?") + enc;
  if (m_album[0]) getUrl += "&album_name=" + lyrUrlEnc(m_album);
  if (m_dur > 0)  getUrl += "&duration=" + String(m_dur / 1000);

  String body;
  if (lyrHttpGet(getUrl, body) && lyrParseSynced(body) > 0) return true;
  // fallback: search endpoint (returns an array; we take the first synced hit)
  if (lyrHttpGet(String("https://lrclib.net/api/search?") + enc, body) &&
      lyrParseSynced(body) > 0) return true;
  return false;
}

static void lyrics_enter() { lyr_shownHl = -2; dev_curIdx = -1; }

// Show the whole line at once (no per-word highlight / underline) - just the
// current lyric group, centered, with a subtle per-line colour + entrance.
static void lyrRender(int dy) {
  gfx->fillScreen(C_BG);
  uint16_t col = (lyr_style == 1) ? C_ACCENT : (lyr_style == 2) ? C_GOLD : C_TEXT;
  const int lh = 22 * lyr_ts;
  int y0 = CYi - (lyr_nrow * lh) / 2 + lh / 2 + dy;
  for (int i = 0; i < lyr_nw; i++) {
    if (lyr_wrow[i] >= 6) break;
    int rx = CXi - lyr_rowW[lyr_wrow[i]] / 2 + lyr_wx[i];
    int ry = y0 + lyr_wrow[i] * lh;
    lyrDrawAt(lyr_w[i], rx, ry, lyr_ts, col);
  }
  present();
}

static void lyrics_tick() {
  uint32_t now = millis();
  // When merged (Ayarlar > "Muzik + Sozler"), fall back to the now-playing
  // banner whenever synced lyrics aren't available, so this one module shows
  // lyrics-when-there, banner-otherwise.
  bool merged = g_mlMerge;

  // ---------------- DEVICE MODE (fetch + sync on-device) ----------------
  if (m_track[0]) {
    bool wifi = (WiFi.status() == WL_CONNECTED);
    String key = String(m_track) + "|" + m_artist;
    if (key != dev_key) {                       // track changed -> (re)fetch
      key.toCharArray(dev_key, sizeof(dev_key));
      dev_n = 0; dev_curIdx = -1; dev_failed = false; dev_noSync = false;
      dev_fetching = wifi;
    }
    // perform the (blocking) fetch, drawing a frame first. When merged, draw the
    // now-playing BANNER as that frame (not a "getiriliyor" screen) so the banner
    // shows immediately - just like opening Muzik did before; we only switch to
    // lyrics once they're actually found.
    if (dev_fetching && wifi && now - dev_lastTry > 1200) {
      if (merged && m_track[0]) {
        musicRenderNowPlaying(now);
      } else {
        gfx->fillScreen(C_BG);
        textCenter("SOZLER", CXi, 92, 3, C_GOLD);
        textCenter("getiriliyor...", CXi, CYi, 2, C_DIM);
        char tb[34]; strncpy(tb, m_track, 33); tb[33] = 0;
        textCenter(tb, CXi, CYi + 44, 2, C_DIM);
        present();
      }
      dev_lastTry = now;
      bool ok = lyricsDevFetch();
      dev_fetching = false;
      dev_failed = !ok;
      dev_noSync = !ok;
      dev_curIdx = -1;
      return;
    }
    if (dev_n > 0) {                            // synced lyrics ready: drive locally
      uint32_t pos = m_pos + (m_playing ? (now - m_at) : 0) + LYR_DEV_LEAD;
      int idx = -1;
      for (int i = 0; i < dev_n; i++) { if (dev_ts[i] <= pos) idx = i; else break; }
      if (idx >= 0 && idx != dev_curIdx) {
        strncpy(lyr_cur, dev_txt[idx], sizeof(lyr_cur) - 1);
        lyr_cur[sizeof(lyr_cur) - 1] = 0;
        lyricsLayout(); lyr_lineAt = now; lyr_style = idx % 3;
        dev_curIdx = idx; lyr_have = true;
      }
      if (idx < 0) {                            // before the first timed line: intro
        if (merged) { musicRenderNowPlaying(now); delay(60); return; }  // show banner until lyrics start
        gfx->fillScreen(C_BG);
        float s = 0.5f + 0.5f * sinf(now * 0.004f);
        uint16_t c = rgb(120 + (int)(120 * s), 120 + (int)(120 * s), 140);
        gfx->fillCircle(CXi - 18, CYi + 12, 13, c); gfx->fillRect(CXi - 8, CYi - 40, 6, 52, c);
        gfx->fillRect(CXi - 8, CYi - 40, 34, 6, c);
        char tb[34]; strncpy(tb, m_track, 33); tb[33] = 0;
        textCenter(tb, CXi, 360, 2, C_DIM);
        present(); delay(30);
      } else {
        float p = (now - lyr_lineAt) / 260.0f;
        int dy = (p < 1.0f) ? (int)((1.0f - (p < 0 ? 0 : p)) * 22) : 0;
        lyrRender(dy);
        delay(20);
      }
      return;
    }
    // device mode wanted but no synced lyrics yet; only show the error screen if
    // the phone isn't actively pushing (otherwise fall through to that).
    if ((dev_failed || dev_noSync || !wifi) && !lyricsFresh()) {
      // merged: no synced lyrics for this track -> just show the banner (a new
      // track auto-refetches via the dev_key change, so it upgrades on its own)
      if (merged) { musicRenderNowPlaying(now); delay(80); return; }
      gfx->fillScreen(C_BG);
      textCenter("SOZLER", CXi, 92, 3, C_GOLD);
      textCenter(!wifi ? "internet yok (WiFi)" :
                 dev_noSync ? "senkron soz bulunamadi" : "sozler bulunamadi",
                 CXi, CYi, 2, C_DIM);
      char tb[34]; strncpy(tb, m_track, 33); tb[33] = 0;
      textCenter(tb, CXi, CYi + 50, 2, C_DIM);
      textCenter("dokun: tekrar dene", CXi, 400, 2, wifi ? C_ACCENT : C_DIM);
      if (g_g.tap && wifi) { dev_fetching = true; dev_failed = false; }
      present(); delay(60);
      return;
    }
  }

  if (!lyricsFresh() || !lyr_have) {
    // merged + something is playing -> banner instead of the idle note
    if (merged && m_track[0]) { musicRenderNowPlaying(now); delay(80); return; }
    static uint32_t lastIdle = 0;
    if (now - lastIdle >= 40) {              // animate idle note ~25fps
      lastIdle = now;
      gfx->fillScreen(C_BG);
      float s = 0.5f + 0.5f * sinf(now * 0.004f);
      uint16_t c = rgb(120 + (int)(120 * s), 120 + (int)(120 * s), 140);
      gfx->fillCircle(CXi - 18, CYi + 12, 13, c); gfx->fillRect(CXi - 8, CYi - 40, 6, 52, c);
      gfx->fillRect(CXi - 8, CYi - 40, 34, 6, c);
      gfx->fillCircle(CXi + 20, CYi + 4, 13, c);   gfx->fillRect(CXi + 20, CYi - 48, 6, 52, c);
      textCenter("SOZLER", CXi, 92, 3, C_GOLD);
      textCenter(lyricsFresh() ? "muzik calmiyor / soz yok" : "telefonda muzik baslat",
                 CXi, 392, 2, C_DIM);
      present();
    }
    lyr_shownHl = -2;
    delay(10);
    return;
  }

  // redraw on a new line + during its entrance animation; otherwise idle (the
  // line just stays on screen until the next one arrives)
  float p = (now - lyr_lineAt) / 260.0f;
  bool entering = (p < 1.0f);
  int dy = entering ? (int)((1.0f - (p < 0 ? 0 : p)) * 22) : 0;

  if (entering || lyr_shownHl == -2) {
    lyrRender(dy);
    lyr_shownHl = 0;                         // marks "this line drawn"
  }
  delay(20);
}
