// ============================================================================
// mod_draw.h - Cizim (Shared Whiteboard)
//   Several Vecta devices draw together over ESP-NOW. What you draw on the
//   touchscreen appears live on every paired device. Two side tool columns:
//   LEFT = color palette, RIGHT = brush thickness. The chosen color + thickness
//   travel with the stroke so peers render it identically (not just one accent).
//
//   Strokes are sent as scaled point batches (not pixels) so they fit ESP-NOW.
//   The per-point "ns" byte carries the new-stroke flag AND the brush style:
//   0 = continuation; non-zero = new stroke where (ns-1) packs color+thickness.
//   Any non-zero ns is still "new stroke" to pre-palette firmware (style ignored).
//   "Temizle" (clear) sits at the bottom. Long-press = home.
// ============================================================================
#pragma once

#include "platform.h"
#include "espnow.h"

#define DRAW_MAGIC 0xD0
#define DRAW_T_POINTS 1
#define DRAW_T_CLEAR  2
#define DRAW_CLEAR_Y  428          // y >= this = clear button (not canvas)
#define DRAW_NCOLORS  6            // color palette size (left column)
#define DRAW_NTHICK   4            // brush thickness steps (right column)
#define DRAW_SIDE_W   72           // each side tool column's touch-zone width

struct DrawPt { uint8_t x, y, ns; };
// ns: 0 = continuation. Non-zero = new stroke; (ns-1) packs color + thickness:
//   v = ns-1;  color = v % DRAW_NCOLORS;  thick = (v / DRAW_NCOLORS) % DRAW_NTHICK

// ---- tool palette (left = color, right = brush thickness) -------------------
static uint16_t draw_palette[DRAW_NCOLORS];
static bool     draw_palInit  = false;
static int      draw_colorIdx = 0;       // selected color (local)
static int      draw_thickIdx = 0;       // selected brush thickness (local)
static const int DRAW_THICK[DRAW_NTHICK] = { 2, 4, 7, 11 };   // brush radii (px)
static void drawPaletteInit() {
  if (draw_palInit) return;
  draw_palette[0] = C_TEXT;              // white (the classic default)
  draw_palette[1] = rgb(244,  80,  80);  // red
  draw_palette[2] = rgb(245, 205,  60);  // yellow
  draw_palette[3] = rgb( 80, 210, 120);  // green
  draw_palette[4] = rgb( 70, 200, 220);  // cyan
  draw_palette[5] = rgb(220, 110, 220);  // magenta
  draw_palInit = true;
}
static const int DRAW_SW_R   = 14;                 // swatch / ring radius
static const int DRAW_COL_LX = 40;                 // left column center x (colors)
static const int DRAW_COL_RX = LCD_W - 40;         // right column center x (brush)
static const int DRAW_CPITCH = 40;                 // color column vertical pitch
static const int DRAW_TPITCH = 50;                 // thickness column vertical pitch
// y center of item i within a vertical column of n items.
static inline int drawColY(int i, int n, int pitch) {
  return CYi - (n - 1) * pitch / 2 + i * pitch;
}

// incoming queue (filled by ESP-NOW task, drained by main loop)
static QueueHandle_t draw_q = nullptr;
static volatile bool draw_clearReq = false;
static volatile bool g_drawIncoming = false;   // main loop auto-opens module

// local (tx) state
static bool  draw_pressing = false;
static int   draw_lastX = 0, draw_lastY = 0;
static bool  draw_newStroke = true;
static DrawPt draw_txBuf[80];
static int   draw_txN = 0;
static uint32_t draw_lastFlush = 0;

// remote (rx) render state
static bool     draw_rxValid = false;
static int      draw_rxLastX = 0, draw_rxLastY = 0;
static uint16_t draw_rxColor = C_ACCENT;   // color of the peer stroke in progress
static int      draw_rxBr    = 2;          // brush radius of the peer stroke

static inline uint8_t sc(int v)   { return (uint8_t)((long)v * 255 / 465); }
static inline int     unsc(uint8_t b) { return (int)((long)b * 465 / 255); }

// ---- ESP-NOW handler ----
static void drawHandlePacket(const esp_now_recv_info_t*, const uint8_t* data, int len) {
  if (len < 2 || data[0] != DRAW_MAGIC) return;
  if (!draw_q) return;
  if (data[1] == DRAW_T_CLEAR) { draw_clearReq = true; g_drawIncoming = true; return; }
  if (data[1] == DRAW_T_POINTS && len >= 3) {
    int n = data[2];
    const uint8_t* p = data + 3;
    for (int i = 0; i < n && (3 + i * 3 + 3) <= len; i++) {   // need 3 bytes/point
      DrawPt pt = { p[i*3], p[i*3+1], p[i*3+2] };
      xQueueSend(draw_q, &pt, 0);
    }
    g_drawIncoming = true;
  }
}

static void drawSendClear() {
  uint8_t pkt[2] = { DRAW_MAGIC, DRAW_T_CLEAR };
  espnowSend(pkt, 2);
  cloudEnqueueDrawClear();
}
static void drawFlushTx() {
  if (draw_txN == 0) return;
  uint8_t pkt[3 + 80 * 3];
  pkt[0] = DRAW_MAGIC; pkt[1] = DRAW_T_POINTS; pkt[2] = draw_txN;
  // also build a CSV for the cloud relay (HTTPS in a worker task)
  String csv; csv.reserve(draw_txN * 12);
  for (int i = 0; i < draw_txN; i++) {
    pkt[3 + i*3]   = draw_txBuf[i].x;
    pkt[3 + i*3+1] = draw_txBuf[i].y;
    pkt[3 + i*3+2] = draw_txBuf[i].ns;
    if (i) csv += ';';
    csv += String(draw_txBuf[i].x); csv += ',';
    csv += String(draw_txBuf[i].y); csv += ',';
    csv += String(draw_txBuf[i].ns);
  }
  espnowSend(pkt, 3 + draw_txN * 3);
  cloudEnqueueDrawPoints(csv);
  draw_txN = 0;
}

// --- Inbound cloud dispatch (called from cloudPollTask) ---
static void cloudHandleDrawPoints(const String& csv) {
  if (!draw_q) return;
  int i = 0, n = csv.length();
  while (i < n) {
    int e = csv.indexOf(';', i); if (e < 0) e = n;
    int c1 = csv.indexOf(',', i); int c2 = (c1 >= 0) ? csv.indexOf(',', c1 + 1) : -1;
    if (c1 > 0 && c2 > 0 && c2 < e) {
      DrawPt pt;
      pt.x  = (uint8_t) csv.substring(i, c1).toInt();
      pt.y  = (uint8_t) csv.substring(c1 + 1, c2).toInt();
      pt.ns = (uint8_t) csv.substring(c2 + 1, e).toInt();
      xQueueSend(draw_q, &pt, 0);
    }
    i = e + 1;
  }
  g_drawIncoming = true;
}
static void cloudHandleDrawClear() { draw_clearReq = true; g_drawIncoming = true; }
static void drawQueueTx(int x, int y, bool ns) {
  // new stroke packs color + thickness into ns; continuation = 0
  uint8_t nsByte = ns ? (uint8_t)(1 + draw_colorIdx + draw_thickIdx * DRAW_NCOLORS) : 0;
  draw_txBuf[draw_txN++] = { sc(x), sc(y), nsByte };
  if (draw_txN >= 80) drawFlushTx();
}

// Thick stroke: a line of radius r. (r<=1 falls back to a 1px line.)
static void drawThick(int x0, int y0, int x1, int y1, int r, uint16_t col) {
  if (r <= 1) { gfx->drawLine(x0, y0, x1, y1, col); return; }
  int dx = x1 - x0, dy = y1 - y0;
  int steps = max(1, max(abs(dx), abs(dy)));
  for (int i = 0; i <= steps; i++)
    gfx->fillCircle(x0 + dx * i / steps, y0 + dy * i / steps, r, col);
}

// LEFT column: color swatches + selection ring.
static void drawColorCol() {
  for (int i = 0; i < DRAW_NCOLORS; i++) {
    int x = DRAW_COL_LX, y = drawColY(i, DRAW_NCOLORS, DRAW_CPITCH);
    gfx->fillCircle(x, y, DRAW_SW_R, draw_palette[i]);
    if (draw_palette[i] == C_TEXT) gfx->drawCircle(x, y, DRAW_SW_R, C_DIM);  // white on dark
    if (i == draw_colorIdx) {
      gfx->drawCircle(x, y, DRAW_SW_R + 3, C_TEXT);
      gfx->drawCircle(x, y, DRAW_SW_R + 4, C_TEXT);
    }
  }
}

// RIGHT column: brush thickness, each shown as a dot sized to its radius.
static void drawThickCol() {
  for (int i = 0; i < DRAW_NTHICK; i++) {
    int x = DRAW_COL_RX, y = drawColY(i, DRAW_NTHICK, DRAW_TPITCH);
    uint16_t col = (i == draw_thickIdx) ? draw_palette[draw_colorIdx] : C_TEXT;
    gfx->fillCircle(x, y, DRAW_THICK[i], col);
    if (i == draw_thickIdx) {
      gfx->drawCircle(x, y, DRAW_SW_R + 3, C_TEXT);
      gfx->drawCircle(x, y, DRAW_SW_R + 4, C_TEXT);
    }
  }
}

// Both tool columns + the bottom clear bar (drawn over a clean band).
static void drawChrome() {
  gfx->fillRect(0, 0, DRAW_SIDE_W, DRAW_CLEAR_Y, C_BG);           // left band
  gfx->fillRect(LCD_W - DRAW_SIDE_W, 0, DRAW_SIDE_W, DRAW_CLEAR_Y, C_BG); // right band
  drawColorCol();
  drawThickCol();
  gfx->fillRect(0, DRAW_CLEAR_Y, LCD_W, LCD_H - DRAW_CLEAR_Y, C_BG2);
  textCenter("TEMIZLE", CXi, (DRAW_CLEAR_Y + LCD_H) / 2, 2, C_DIM);
}

// Pick the color swatch nearest the touched y; true if it changed.
static bool drawSelectColorAt(int y) {
  int best = 0, bestD = 0x7fffffff;
  for (int i = 0; i < DRAW_NCOLORS; i++) {
    int dy = y - drawColY(i, DRAW_NCOLORS, DRAW_CPITCH), d = dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  bool changed = (best != draw_colorIdx);
  draw_colorIdx = best;
  drawColorCol(); drawThickCol();   // thickness dot tints with color -> refresh both
  present();
  return changed;
}

// Pick the brush thickness nearest the touched y; true if it changed.
static bool drawSelectThickAt(int y) {
  int best = 0, bestD = 0x7fffffff;
  for (int i = 0; i < DRAW_NTHICK; i++) {
    int dy = y - drawColY(i, DRAW_NTHICK, DRAW_TPITCH), d = dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  bool changed = (best != draw_thickIdx);
  draw_thickIdx = best;
  drawThickCol();
  present();
  return changed;
}

static void drawClearScreen() {
  gfx->fillScreen(C_BG);
  drawChrome();
  present();
  draw_rxValid = false;
}

static void draw_enter() {
  drawPaletteInit();
  if (!draw_q) draw_q = xQueueCreate(512, sizeof(DrawPt));
  espnowBegin();
  espnowAddHandler(drawHandlePacket);
  draw_pressing = false; draw_newStroke = true; draw_txN = 0;
  draw_clearReq = false; g_drawIncoming = false;
  drawClearScreen();
}

static void draw_tick() {
  bool dirty = false;

  // 1) handle remote clear
  if (draw_clearReq) { draw_clearReq = false; drawClearScreen(); }

  // 2) drain incoming points -> draw in the stroke's palette color
  if (draw_q) {
    DrawPt pt;
    while (xQueueReceive(draw_q, &pt, 0) == pdTRUE) {
      int x = unsc(pt.x), y = unsc(pt.y);
      if (pt.ns) {                                 // new stroke: adopt color+thickness
        draw_rxValid = true;
        int v = pt.ns - 1;
        int ci = v % DRAW_NCOLORS;
        int ti = (v / DRAW_NCOLORS) % DRAW_NTHICK;
        draw_rxColor = (ci >= 0 && ci < DRAW_NCOLORS) ? draw_palette[ci] : C_ACCENT;
        draw_rxBr    = DRAW_THICK[ti];
        gfx->fillCircle(x, y, draw_rxBr, draw_rxColor);   // show single-tap dots too
      } else if (!draw_rxValid) {
        draw_rxValid = true;
      } else {
        drawThick(draw_rxLastX, draw_rxLastY, x, y, draw_rxBr, draw_rxColor);
      }
      draw_rxLastX = x; draw_rxLastY = y; dirty = true;
    }
  }

  // 3) local drawing
  RawTouch t = touchRead();
  uint32_t now = millis();
  if (t.down) {
    if (t.y >= DRAW_CLEAR_Y) {
      // pressing the clear bar; handle on release via flag
      draw_pressing = false;
      if (!draw_newStroke) { drawFlushTx(); draw_newStroke = true; }
      // simple: clear immediately
      drawClearScreen(); drawSendClear();
      delay(120);
      return;
    }
    if (t.x < DRAW_SIDE_W) {
      // left column: pick a color, end any stroke in progress (don't draw)
      if (draw_pressing) { draw_pressing = false; draw_newStroke = true; drawFlushTx(); }
      if (drawSelectColorAt(t.y)) soundTap();
      delay(120);
      return;
    }
    if (t.x > LCD_W - DRAW_SIDE_W) {
      // right column: pick brush thickness
      if (draw_pressing) { draw_pressing = false; draw_newStroke = true; drawFlushTx(); }
      if (drawSelectThickAt(t.y)) soundTap();
      delay(120);
      return;
    }
    uint16_t col = draw_palette[draw_colorIdx];
    int      br  = DRAW_THICK[draw_thickIdx];
    if (!draw_pressing) {
      draw_pressing = true; draw_newStroke = true;
      draw_lastX = t.x; draw_lastY = t.y;
      gfx->fillCircle(t.x, t.y, br, col);
      drawQueueTx(t.x, t.y, true);
      dirty = true;
    } else {
      drawThick(draw_lastX, draw_lastY, t.x, t.y, br, col);
      drawQueueTx(t.x, t.y, false);
      draw_lastX = t.x; draw_lastY = t.y;
      dirty = true;
    }
  } else {
    if (draw_pressing) { draw_pressing = false; draw_newStroke = true; drawFlushTx(); }
  }

  // 4) periodic flush so peers see strokes live
  if (draw_txN > 0 && now - draw_lastFlush > 90) { drawFlushTx(); draw_lastFlush = now; }

  if (dirty) present();
  delay(6);
}
