// ============================================================================
// mod_music.h - Now Playing (Muzik Caliyor)
//   Shows what's currently playing on the user's phone (Spotify, via the
//   phone app's OAuth integration). Phone POSTs /np with track + artist +
//   playback position; the device renders title, artist, a progress bar and
//   a play/pause indicator.
//   YouTube Music has no official "now playing" API; only Spotify is wired.
// ============================================================================
#pragma once

#include "platform.h"

static char m_track[64]   = "";
static char m_artist[64]  = "";
static char m_album[40]   = "";
static char m_source[12]  = "";
static bool m_playing     = false;
static uint32_t m_pos     = 0;     // ms into the track at m_at
static uint32_t m_dur     = 0;     // total ms
static uint32_t m_at      = 0;     // millis() when info arrived

// Optional album art: 160x160 RGB565 big-endian, in PSRAM.
#define M_ART_W 160
#define M_ART_H 160
static uint8_t* m_art = nullptr;
static bool     m_artHas = false;

static bool musicSetArt(uint8_t* buf, size_t bytes) {
  if (bytes != (size_t)M_ART_W * M_ART_H * 2) return false;
  if (!m_art) m_art = (uint8_t*) heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!m_art) return false;
  memcpy(m_art, buf, bytes);
  m_artHas = true;
  return true;
}
static void musicClearArt() { m_artHas = false; }

// Tapped-control feedback (prev / play-pause / next sent over BLE HID).
static uint32_t m_ctrlFlash = 0;
static int      m_ctrlWhich = 0;   // 1=prev 2=play 3=next

static void musicSet(const String& track, const String& artist, const String& album,
                     const String& source, bool playing, uint32_t pos, uint32_t dur) {
  // Clear old art when track changes
  if (strncmp(m_track, track.c_str(), sizeof(m_track)) != 0) musicClearArt();
  track.toCharArray(m_track, sizeof(m_track));
  artist.toCharArray(m_artist, sizeof(m_artist));
  album.toCharArray(m_album, sizeof(m_album));
  source.toCharArray(m_source, sizeof(m_source));
  m_playing = playing; m_pos = pos; m_dur = dur; m_at = millis();
}

static void music_enter() {}

static void drawNote(int cx, int cy, int s, uint16_t col) {
  gfx->fillEllipse(cx - s/2, cy + s/2, s/2, s/3, col);   // note head
  gfx->fillRect(cx + s/2 - 2, cy - s, 3, s + s/2 + 2, col); // stem
  gfx->fillTriangle(cx + s/2 + 1, cy - s,
                    cx + s/2 + 1, cy - s + s/2,
                    cx + s/2 + s/2 + 4, cy - s + s/3, col); // flag
}

// Blit the 160x160 album art into a filled circle of radius R (nearest-neighbor
// upscale + circular clip, byte-swapped from stored big-endian). Falls back to a
// note-on-disc placeholder when no art has been pushed.
static void drawArtCircle(int cx, int cy, int R) {
  if (m_artHas && m_art) {
    const uint16_t* p = (const uint16_t*)m_art;
    for (int y = -R; y <= R; y++) {
      int xw = (int)sqrtf((float)(R * R - y * y));      // half-width of this row
      int sy = (y + R) * M_ART_H / (2 * R);
      if (sy < 0) sy = 0; else if (sy >= M_ART_H) sy = M_ART_H - 1;
      for (int x = -xw; x <= xw; x++) {
        int sx = (x + R) * M_ART_W / (2 * R);
        if (sx < 0) sx = 0; else if (sx >= M_ART_W) sx = M_ART_W - 1;
        uint16_t be = p[sy * M_ART_W + sx];
        gfx->drawPixel(cx + x, cy + y, (uint16_t)((be >> 8) | (be << 8)));
      }
    }
    gfx->drawCircle(cx, cy, R, C_BG2);
  } else {
    gfx->fillCircle(cx, cy, R, C_BG2);
    gfx->drawCircle(cx, cy, R, C_ACCENT);
    drawNote(cx, cy + R / 6, R / 3, C_GOLD);
  }
}

// Full-bleed darkened album-art background: 160x160 -> full screen (nearest-
// neighbor), dimmed more toward the bottom so overlaid white text/controls stay
// readable. Matches the phone's notification look (cover fills the screen).
static void drawArtBackground() {
  const uint16_t* p = (const uint16_t*)m_art;
  for (int y = 0; y < LCD_H; y++) {
    int sy  = (y * M_ART_H / LCD_H) * M_ART_W;
    // SMOOTH vertical dim (/256, changes <1 per row) so there are no visible
    // horizontal banding lines. ~62% at the top -> ~22% at the bottom, keeping
    // overlaid white text/controls readable.
    int f = 158 - (y * 100 / LCD_H);
    for (int x = 0; x < LCD_W; x++) {
      uint16_t be = p[sy + (x * M_ART_W / LCD_W)];
      uint16_t c  = (uint16_t)((be >> 8) | (be << 8));
      int r = (((c >> 11) & 0x1F) * f) >> 8;
      int g = (((c >> 5)  & 0x3F) * f) >> 8;
      int b = (( c        & 0x1F) * f) >> 8;
      gfx->drawPixel(x, y, (uint16_t)((r << 11) | (g << 5) | b));
    }
  }
}

// Centered text with a dark drop-shadow (readable over any artwork).
static void textShadow(const char* s, int cx, int cy, uint8_t sz, uint16_t col) {
  textCenter(s, cx + 2, cy + 2, sz, rgb(0, 0, 0));
  textCenter(s, cx, cy, sz, col);
}

// Draw the full now-playing banner (cover background, title/artist, progress,
// transport controls) and handle the control taps. Pulled out of music_tick so
// the merged Sozler view can reuse it when no synced lyrics are available.
// Assumes m_track[0] != 0.
static void musicRenderNowPlaying(uint32_t now) {
  // --- control taps: drive the bonded phone over BLE HID (prev/play/next) ---
#ifdef HAVE_BLE_HID
  if (g_g.tap && abs(g_g.y - 426) < 44) {
    if      (abs(g_g.x - (CXi - 64)) < 36) { bleKb.write(KEY_MEDIA_PREVIOUS_TRACK); m_ctrlWhich = 1; m_ctrlFlash = now + 220; soundTap(); }
    else if (abs(g_g.x - CXi) < 36)        { bleKb.write(KEY_MEDIA_PLAY_PAUSE); m_playing = !m_playing; m_ctrlWhich = 2; m_ctrlFlash = now + 220; soundTap(); }
    else if (abs(g_g.x - (CXi + 64)) < 36) { bleKb.write(KEY_MEDIA_NEXT_TRACK); m_ctrlWhich = 3; m_ctrlFlash = now + 220; soundTap(); }
  }
#endif

  // background: full-bleed darkened cover, or a note-disc placeholder
  bool hasArt = m_artHas && m_art;
  if (hasArt) drawArtBackground();
  else { gfx->fillScreen(C_BG); drawArtCircle(CXi, 150, 100); }

  // --- title + artist (drop-shadow when over artwork) ---
  // UTF-8-safe truncate: cut at a char boundary (never split a multi-byte letter)
  // then append "..." so Turkish titles don't end in a broken '?'.
  auto trunc = [](char* out, int outSz, const char* src, int maxBytes) {
    int n = 0;
    while (src[n] && n < maxBytes) {
      int step = 1; uint8_t c = (uint8_t)src[n];
      if ((c & 0xE0) == 0xC0) step = 2; else if ((c & 0xF0) == 0xE0) step = 3; else if ((c & 0xF8) == 0xF0) step = 4;
      if (n + step > maxBytes) break;
      n += step;
    }
    int k = (n < outSz - 4) ? n : outSz - 4;
    memcpy(out, src, k);
    if (src[n]) { out[k] = out[k + 1] = out[k + 2] = '.'; out[k + 3] = 0; }
    else out[k] = 0;
  };
  char title[48];  trunc(title,  sizeof(title),  m_track,  24);
  char artist[48]; trunc(artist, sizeof(artist), m_artist, 32);
  if (hasArt) {
    textShadow(title, CXi, 300, 3, C_TEXT);
    if (artist[0]) textShadow(artist, CXi, 336, 2, rgb(220, 224, 230));
  } else {
    textCenter(title, CXi, 300, 3, C_TEXT);
    if (artist[0]) textCenter(artist, CXi, 336, 2, C_DIM);
  }

  // --- progress bar (thin) + knob ---
  uint32_t pos = m_pos + (m_playing ? (now - m_at) : 0);
  if (m_dur > 0 && pos > m_dur) pos = m_dur;
  float frac = m_dur > 0 ? (float)pos / m_dur : 0;
  int barX = 96, barY = 372, barW = LCD_W - 2 * barX, barH = 6;
  gfx->fillRoundRect(barX, barY, barW, barH, barH / 2, rgb(70, 74, 84));
  if (frac > 0) {
    int fw = (int)(barW * frac);
    gfx->fillRoundRect(barX, barY, fw, barH, barH / 2, C_TEXT);
    gfx->fillCircle(barX + fw, barY + barH / 2, 5, C_TEXT);
  }

  // time labels (small, at the bar ends)
  auto fmtTime = [](uint32_t ms, char* out, int n) {
    uint32_t s = ms / 1000;
    snprintf(out, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  };
  char tA[8], tB[8]; fmtTime(pos, tA, 8); fmtTime(m_dur, tB, 8);
  gfx->setFont(NULL); gfx->setTextSize(1); gfx->setTextColor(rgb(214, 218, 224));
  gfx->setCursor(barX, barY + 12); gfx->print(tA);
  gfx->setCursor(barX + barW - (int)strlen(tB) * 6, barY + 12); gfx->print(tB);

  // --- control row (white shapes; tapped control flashes accent) ---
  int by = 426;
  bool fl = now < m_ctrlFlash;
  uint16_t prevCol = (fl && m_ctrlWhich == 1) ? C_ACCENT : C_TEXT;
  uint16_t nextCol = (fl && m_ctrlWhich == 3) ? C_ACCENT : C_TEXT;
  bool playFl = fl && m_ctrlWhich == 2;
  int rx = CXi - 130;                          // repeat (decorative)
  gfx->drawCircle(rx, by, 9, C_TEXT); gfx->drawCircle(rx, by, 8, C_TEXT);
  gfx->fillTriangle(rx + 5, by - 12, rx + 12, by - 8, rx + 5, by - 4, C_TEXT);
  int px = CXi - 64;                            // previous
  gfx->fillRect(px - 9, by - 9, 3, 18, prevCol);
  gfx->fillTriangle(px + 7, by - 9, px + 7, by + 9, px - 5, by, prevCol);
  gfx->fillCircle(CXi, by, 22, playFl ? C_ACCENT : C_TEXT);   // play / pause
  if (m_playing) {
    gfx->fillRect(CXi - 7, by - 10, 5, 20, rgb(0, 0, 0));
    gfx->fillRect(CXi + 3, by - 10, 5, 20, rgb(0, 0, 0));
  } else {
    gfx->fillTriangle(CXi - 6, by - 10, CXi - 6, by + 10, CXi + 11, by, rgb(0, 0, 0));
  }
  int nx = CXi + 64;                            // next
  gfx->fillTriangle(nx - 7, by - 9, nx - 7, by + 9, nx + 5, by, nextCol);
  gfx->fillRect(nx + 6, by - 9, 3, 18, nextCol);
  int hx = CXi + 130;                           // heart (decorative)
  gfx->fillCircle(hx - 5, by - 3, 5, C_TEXT);
  gfx->fillCircle(hx + 5, by - 3, 5, C_TEXT);
  gfx->fillTriangle(hx - 9, by - 1, hx + 9, by - 1, hx, by + 10, C_TEXT);

  present();
}

static void music_tick() {
  uint32_t now = millis();

  if (m_track[0] == 0) {
    gfx->fillScreen(C_BG);
    drawArtCircle(CXi, 150, 96);
    textCenter("Muzik bekleniyor", CXi, 300, 2, C_DIM);
    textCenter("telefondan baslat", CXi, 332, 2, C_DIM);
    present(); delay(150); return;
  }

  musicRenderNowPlaying(now);
  delay(150);
}
