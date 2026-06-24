// ============================================================================
// mod_reader.h - Hizli Okuma (RSVP - Rapid Serial Visual Presentation)
//
//   Speed-reads a PDF / text that the phone extracted and pushed, ONE word at a
//   time, Spritz-style: the ORP (Optimal Recognition Point) pivot letter is
//   drawn in red and pinned to a fixed focus column, so the eye never moves and
//   normal reading's saccades (eye jumps) are eliminated. Timing is WPM-based
//   with dynamic pauses for long words and punctuation so the brain can keep up.
//
//   The phone does the heavy lifting (PDF -> plain text) and streams it in
//   chunks (HTTP body can't hold a whole book at once):
//       POST /reader?new=1[&title=..]   first chunk  -> resets the buffer
//       POST /reader                    more chunks  -> appended
//       POST /reader?done=1             last chunk   -> builds the word index
//   Controls (also over HTTP, used by the app):
//       GET /reader?wpm=N | ?play=1 | ?pause=1 | ?restart=1 | ?seek=DELTA
//       GET /reader                     -> {words,idx,wpm,playing} status JSON
//
//   On-device gestures:
//       tap            -> play / pause (when finished: restart)
//       swipe left     -> jump to next sentence
//       swipe right    -> jump to previous sentence
//       swipe up/down  -> WPM +/- (saved in NVS)
//       long-press     -> home (handled globally by the launcher)
//
//   Everything is drawn on-device; only UTF-8 text crosses the network. The
//   classic 6x8 monospace font is used on purpose: a fixed glyph width is what
//   keeps the pivot column exactly aligned word-to-word.
// ============================================================================
#pragma once

#include "platform.h"

// ---- text store (PSRAM-backed; allocated lazily on first use) --------------
// The whole document is pushed once and lives here; playback is then fully local
// (the phone never has to stream again). The S3 has 8 MB PSRAM, so we can be
// generous: 1 MB of text (~160k words, a full novel) + a ~1.2 MB word index.
// Allocated lazily on first use and kept; total budget ~2.2 MB PSRAM.
#define RD_CAP    1048576UL       // raw UTF-8 text capacity (1 MB)
#define RD_MAXW   200000          // max words indexed (~6 B each in the index)
#define RD_SPLIT  16              // tokens longer than this are split for display
#define RD_PIECE  13              // ...into pieces of at most this many chars

static char*     rd_buf = nullptr;   // raw text
static uint32_t  rd_len = 0;
static uint32_t* rd_off = nullptr;   // per-word start offset into rd_buf
static uint16_t* rd_wl  = nullptr;   // per-word length (bytes)
static int       rd_nw  = 0;         // number of words
static char      rd_title[28] = "";

// ---- playback state --------------------------------------------------------
static int       rd_idx      = 0;
static bool      rd_play     = false;
static bool      rd_done     = false;
static int       rd_wpm      = 350;
static uint32_t  rd_nextMs   = 0;
static int       rd_lastDraw = -1;
static bool      rd_dirty    = true;
static uint32_t  rd_wpmFlash = 0;    // show a big WPM indicator until this millis()
static int       rd_wpmDir   = 0;    // +1 = sped up, -1 = slowed down (arrow)

static const int RD_FOCUS_X = CXi;   // pivot column (screen center)

// ---------------------------------------------------------------------------
static bool rdAlloc() {
  if (rd_buf && rd_off && rd_wl) return true;
  if (!rd_buf) rd_buf = (char*)     heap_caps_malloc(RD_CAP, MALLOC_CAP_SPIRAM);
  if (!rd_off) rd_off = (uint32_t*) heap_caps_malloc(sizeof(uint32_t) * RD_MAXW, MALLOC_CAP_SPIRAM);
  if (!rd_wl)  rd_wl  = (uint16_t*) heap_caps_malloc(sizeof(uint16_t) * RD_MAXW, MALLOC_CAP_SPIRAM);
  return rd_buf && rd_off && rd_wl;
}

static inline bool rdIsSpace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
}

// ---- ingest (called by net.h) ---------------------------------------------
static bool readerReset(const String& title) {
  if (!rdAlloc()) return false;
  rd_len = 0; rd_nw = 0; rd_idx = 0; rd_done = false; rd_play = false;
  strncpy(rd_title, title.c_str(), sizeof(rd_title) - 1);   // raw UTF-8 (drawn via textCenter)
  rd_title[sizeof(rd_title) - 1] = 0;
  rd_dirty = true; rd_lastDraw = -1;
  return true;
}

static bool readerAppend(const char* p, size_t n) {
  if (!rdAlloc()) return false;
  if (rd_len + n > RD_CAP) n = RD_CAP - rd_len;   // clamp; silently drop overflow
  if (n == 0) return false;
  memcpy(rd_buf + rd_len, p, n);
  rd_len += n;
  return true;
}

// Build the word index: maximal runs of non-whitespace. Very long tokens (URLs,
// no-space strings) are split into RD_PIECE-sized pieces so the pivot stays sane.
static void readerFinalize() {
  if (!rd_buf) { rd_nw = 0; return; }
  rd_nw = 0;
  uint32_t i = 0;
  while (i < rd_len && rd_nw < RD_MAXW) {
    if (rdIsSpace(rd_buf[i])) { i++; continue; }
    uint32_t s = i;
    while (i < rd_len && !rdIsSpace(rd_buf[i])) i++;
    uint32_t len = i - s;
    if (len <= RD_SPLIT) {
      rd_off[rd_nw] = s; rd_wl[rd_nw] = (uint16_t)len; rd_nw++;
    } else {
      for (uint32_t o = 0; o < len && rd_nw < RD_MAXW; o += RD_PIECE) {
        uint32_t pl = len - o; if (pl > RD_PIECE) pl = RD_PIECE;
        rd_off[rd_nw] = s + o; rd_wl[rd_nw] = (uint16_t)pl; rd_nw++;
      }
    }
  }
  rd_idx = 0; rd_done = false; rd_play = false;
  rd_dirty = true; rd_lastDraw = -1;
}

// ---- engine ---------------------------------------------------------------
static inline char rdLastChar(int i) {
  return rd_buf[rd_off[i] + rd_wl[i] - 1];
}

// How long word i lingers, ms. Base = 60000/wpm, plus dynamic pauses.
static uint32_t rdDelay(int i) {
  int wpm = rd_wpm < 60 ? 60 : rd_wpm;
  uint32_t base = 60000UL / wpm;
  uint32_t d = base;
  int L = rd_wl[i];
  if (L >= 10) d += (uint32_t)((L - 9) * (base / 6));   // long words linger a bit
  char last = rdLastChar(i);
  if (last == '.' || last == '!' || last == '?' || last == ':' || last == ';')
    d += (base > 260 ? base : 260);                      // sentence end: a full beat
  else if (last == ',' || last == ')' || last == '"' || last == '\'')
    d += 140;                                            // clause break
  return d;
}

static int rdPivot(int L) {
  if (L <= 1)  return 0;
  if (L <= 5)  return 1;
  if (L <= 9)  return 2;
  if (L <= 13) return 3;
  return 4;
}

static void rdSeekTo(int idx) {
  if (idx < 0) idx = 0;
  if (idx > rd_nw - 1) idx = rd_nw - 1;
  if (rd_nw == 0) idx = 0;
  rd_idx = idx; rd_done = false; rd_dirty = true;
  if (rd_play) rd_nextMs = millis() + rdDelay(rd_idx);
}

static int rdNextSentence(int from) {
  for (int i = from; i < rd_nw; i++) {
    char c = rdLastChar(i);
    if (c == '.' || c == '!' || c == '?')
      return (i + 1 < rd_nw) ? i + 1 : rd_nw - 1;
  }
  return rd_nw - 1;
}

static int rdPrevSentence(int from) {
  for (int j = from - 2; j >= 0; j--) {
    char c = rdLastChar(j);
    if (c == '.' || c == '!' || c == '?') return j + 1;
  }
  return 0;
}

static void rdSetWpm(int w) {
  if (w < 100) w = 100; if (w > 900) w = 900;
  if (w == rd_wpm) return;
  rd_wpm = w; setPutInt("rd_wpm", rd_wpm); rd_dirty = true;
}

// ---- render ---------------------------------------------------------------
static void rdRender() {
  gfx->fillScreen(C_BG);

  textCenter(rd_title[0] ? rd_title : "HIZLI OKUMA", CXi, 60, 2, C_GOLD);

  if (rd_nw == 0) {
    // book icon
    gfx->fillRoundRect(CXi - 56, CYi - 42, 50, 84, 6, C_BG2);
    gfx->fillRoundRect(CXi + 6,  CYi - 42, 50, 84, 6, C_BG2);
    gfx->fillRect(CXi - 4, CYi - 42, 8, 84, C_DIM);
    for (int k = -28; k <= 28; k += 12) {
      gfx->drawFastHLine(CXi - 48, CYi + k, 36, C_DIM);
      gfx->drawFastHLine(CXi + 12, CYi + k, 36, C_DIM);
    }
    textCenter("Telefondan PDF gonder", CXi, CYi + 80, 2, C_TEXT);
    textCenter("uygulamada: Hizli Okuma", CXi, CYi + 108, 2, C_DIM);
    present();
    return;
  }

  // focus guides (Spritz-style) with a red tick pointing at the pivot column
  const int top = CYi - 56, bot = CYi + 56;
  gfx->drawFastHLine(CXi - 150, top, 300, C_BG2);
  gfx->drawFastHLine(CXi - 150, bot, 300, C_BG2);
  gfx->fillTriangle(RD_FOCUS_X - 7, top - 13, RD_FOCUS_X + 7, top - 13, RD_FOCUS_X, top - 1, C_DANGER);
  gfx->fillTriangle(RD_FOCUS_X - 7, bot + 13, RD_FOCUS_X + 7, bot + 13, RD_FOCUS_X, bot + 1, C_DANGER);

  // current word: UTF-8 in rd_buf; fold to an ASCII base for the mono font and
  // overlay Turkish accents per character (the base keeps the pivot alignment).
  int L = rd_wl[rd_idx]; if (L > 60) L = 60;
  char raw[64];
  memcpy(raw, rd_buf + rd_off[rd_idx], L); raw[L] = 0;
  char base[64]; int n = trFold(raw, base, sizeof(base));
  const char* s = base;

  if (n > 0) {
    int piv = rdPivot(n);
    if (piv > n - 1) piv = n - 1;
    // The word is pivot-anchored (pivot pinned to the focus column), so it is NOT
    // centered: the right side can reach further than the left. Pick the biggest
    // size where BOTH ends stay inside the screen margins - otherwise long words
    // run off the edge and the font wraps them onto the next line.
    const int LM = 8, RM = 458;               // left / right pixel margins
    uint8_t size = 7;
    int cw = 0, x0 = 0;
    while (true) {
      cw = 6 * size;
      int orpX = RD_FOCUS_X - (5 * size) / 2;  // pivot glyph centered on focus col
      x0 = orpX - piv * cw;
      int xr = x0 + n * cw;                     // right edge of the last cell
      if ((x0 >= LM && xr <= RM) || size <= 2) break;
      size--;
    }
    int y = CYi - 4 * size;                     // glyph top (cell height = 8*size)
    // print per character so the pivot can take its own colour; the fixed-width
    // classic font keeps every word's pivot pinned to the same column. Wrap OFF
    // so an over-long word clips at the edge instead of dropping to a new line.
    gfx->setFont(NULL); gfx->setTextSize(size); gfx->setTextWrap(false);
    for (int i = 0; i < n; i++) {
      gfx->setTextColor((i == piv) ? C_DANGER : C_TEXT);
      gfx->setCursor(x0 + i * cw, y);
      gfx->write((uint8_t)s[i]);
    }
    // overlay Turkish accents (mono: each base char is at x0 + i*cw)
    int aidx = 0;
    for (const char* p = raw; *p && aidx < n; ) {
      uint32_t cp; p += trUtf8(p, cp); uint8_t acc; trCp(cp, acc);
      if (acc)
        drawAccentMark(x0 + aidx * cw + (5 * size) / 2, y - 2 * size, y + 7 * size,
                       size, acc, (aidx == piv) ? C_DANGER : C_TEXT);
      aidx++;
    }
    gfx->setTextSize(1); gfx->setTextWrap(true);
  }

  // progress bar + counter
  int p = (rd_nw > 1) ? (rd_idx * 100) / (rd_nw - 1) : 100;
  int bx = CXi - 150, by = 406, bw = 300;
  gfx->fillRoundRect(bx, by, bw, 8, 4, C_BG2);
  if (p > 0) gfx->fillRoundRect(bx, by, (bw * p) / 100, 8, 4, C_ACCENT);

  char info[40];
  snprintf(info, sizeof(info), "%d/%d  %d wpm", rd_idx + 1, rd_nw, rd_wpm);
  textCenter(info, CXi, 430, 2, C_DIM);

  // state hint just below the word
  if (rd_done)        textCenter("bitti - dokun: bastan", CXi, CYi + 92, 2, C_GOLD);
  else if (!rd_play)  textCenter("dokun: oku", CXi, CYi + 92, 2, C_DIM);

  // WPM-change indicator (swipe up/down): a prominent chip near the top so the
  // pace change is obvious. Green arrow up = faster, amber arrow down = slower.
  if (rd_wpmFlash && millis() < rd_wpmFlash) {
    const int cy = 112, w = 210, h = 56;
    gfx->fillRoundRect(CXi - w / 2, cy - h / 2, w, h, 18, C_BG2);
    uint16_t ac = (rd_wpmDir > 0) ? C_OK : C_WARN;
    int ax = CXi - 64;                       // arrow center x
    if (rd_wpmDir > 0) gfx->fillTriangle(ax - 14, cy + 12, ax + 14, cy + 12, ax, cy - 14, ac);
    else               gfx->fillTriangle(ax - 14, cy - 12, ax + 14, cy - 12, ax, cy + 14, ac);
    char wb[12]; snprintf(wb, sizeof(wb), "%d", rd_wpm);
    textCenter(wb, CXi + 22, cy, 4, ac);
  }

  present();
}

// ---- lifecycle ------------------------------------------------------------
static void reader_enter() {
  rd_wpm = setGetInt("rd_wpm", 350);
  rd_play = false;
  rd_dirty = true; rd_lastDraw = -1;
}

static void reader_tick() {
  if (g_g.tap) {
    if (rd_nw > 0) {
      if (rd_done) { rd_idx = 0; rd_done = false; rd_play = true; rd_nextMs = millis() + rdDelay(0); }
      else { rd_play = !rd_play; if (rd_play) rd_nextMs = millis() + rdDelay(rd_idx); }
      soundTap();
    }
    rd_dirty = true;
  }
  if (rd_nw > 0) {
    if (g_g.swipeLeft)  { rdSeekTo(rdNextSentence(rd_idx)); soundTap(); }
    if (g_g.swipeRight) { rdSeekTo(rdPrevSentence(rd_idx)); soundTap(); }
  }
  // swipe up = faster, swipe down = slower; show a big indicator and apply the
  // new pace to the CURRENT word immediately so the change is felt at once.
  if (g_g.swipeUp || g_g.swipeDown) {
    int before = rd_wpm;
    rdSetWpm(rd_wpm + (g_g.swipeUp ? 25 : -25));
    if (rd_wpm != before) {
      rd_wpmDir = g_g.swipeUp ? 1 : -1;
      rd_wpmFlash = millis() + 850;
      if (rd_play && rd_nw > 0) rd_nextMs = millis() + rdDelay(rd_idx);
      soundTap();
    }
  }

  // keep redrawing while the WPM indicator is showing (so it stays visible even
  // when paused), then one final redraw to clear it
  if (rd_wpmFlash) { if (millis() < rd_wpmFlash) rd_dirty = true; else { rd_wpmFlash = 0; rd_dirty = true; } }

  if (rd_play && rd_nw > 0) {
    uint32_t now = millis();
    if (now >= rd_nextMs) {
      if (rd_idx + 1 >= rd_nw) { rd_play = false; rd_done = true; rd_dirty = true; }
      else { rd_idx++; rd_nextMs = now + rdDelay(rd_idx); rd_dirty = true; }
    }
  }

  if (rd_dirty || rd_idx != rd_lastDraw) {
    rdRender();
    rd_lastDraw = rd_idx;
    rd_dirty = false;
  }
  delay(8);
}
