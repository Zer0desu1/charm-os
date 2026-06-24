// ============================================================================
// mod_nav.h - Module: Navigasyon (Beeline Moto II tarzi)
//   The phone sends only compact vector/number data over BLE (ble_nav.h); this
//   module draws EVERYTHING locally on the AMOLED: a big maneuver arrow, a
//   vector "road strip" of the road ahead, and a minimalist bottom panel
//   (distance / ETA / clock). Dark theme, high contrast, smooth motion.
//
//   Smoothness: BLE packets arrive ~1 Hz, but the on-screen arrow is updated
//   every frame. The displayed angle LERPs toward its target each frame, and -
//   when a QMC5883L compass is present (mag.h) - the device heading is low-pass
//   filtered and subtracted so the arrow spins as the rider turns, with no
//   per-packet jerk (a complementary/interpolation scheme, ~15 FPS feel).
//
//   Include AFTER ble_nav.h and mag.h.
// ============================================================================
#pragma once

#include "platform.h"
#include "mag.h"
#include "ble_nav.h"
#include <time.h>

// ---- arrow placement (leave room for the bottom panel) ----
#define NAV_AX  CXi
#define NAV_AY  (CYi - 34)

// ---- smoothing state (sensor fusion + interpolation) ----
static float    nav_headFilt  = 0;     // low-pass filtered compass heading
static bool     nav_headInit  = false;
static float    nav_arrowShown= 0;     // currently displayed arrow angle (deg)

// shortest signed angular difference target-cur, in (-180,180]
static float navAngDiff(float target, float cur) {
  float d = target - cur;
  while (d > 180)  d -= 360;
  while (d <= -180) d += 360;
  return d;
}
// move cur toward target by fraction a (angle-aware) -> smooth interpolation
static float navLerpAngle(float cur, float target, float a) {
  return cur + navAngDiff(target, cur) * a;
}

static void nav_enter() {
  nav_headInit = false;
  nav_arrowShown = 0;
}

// rotate a local point (lx,ly; local up = -y = forward) by angDeg clockwise and
// translate to (ox,oy) screen space.
static inline void navRot(float lx, float ly, float angDeg, int ox, int oy, int* sx, int* sy) {
  float a = angDeg * DEG_TO_RAD, c = cosf(a), s = sinf(a);
  *sx = ox + (int)lroundf(lx * c - ly * s);
  *sy = oy + (int)lroundf(lx * s + ly * c);
}

// one thick, round-capped segment between two screen points
static void navThickSeg(int x0, int y0, int x1, int y1, int w, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy); if (len < 0.5f) return;
  float nx = -dy / len * (w / 2.0f), ny = dx / len * (w / 2.0f);  // perpendicular
  int ax = x0 + (int)nx, ay = y0 + (int)ny;
  int bx = x0 - (int)nx, by = y0 - (int)ny;
  int cx = x1 - (int)nx, cy = y1 - (int)ny;
  int dx2= x1 + (int)nx, dy2= y1 + (int)ny;
  gfx->fillTriangle(ax, ay, bx, by, cx, cy, col);
  gfx->fillTriangle(ax, ay, cx, cy, dx2, dy2, col);
  gfx->fillCircle(x0, y0, w / 2, col);            // round joints/caps
  gfx->fillCircle(x1, y1, w / 2, col);
}

// arrowhead at (ex,ey) pointing along unit dir (dx,dy)
static void navHead(int ex, int ey, float dx, float dy, int sz, uint16_t col) {
  float len = sqrtf(dx * dx + dy * dy); if (len < 0.001f) { dy = -1; len = 1; }
  dx /= len; dy /= len;
  float px = -dy, py = dx;                          // perpendicular
  int bx = ex - (int)(dx * sz),       by = ey - (int)(dy * sz);
  int lx = bx + (int)(px * sz * 0.7f),ly = by + (int)(py * sz * 0.7f);
  int rx = bx - (int)(px * sz * 0.7f),ry = by - (int)(py * sz * 0.7f);
  gfx->fillTriangle(ex, ey, lx, ly, rx, ry, col);
}

// Draw a maneuver path (local coords, forward = up) rotated by angDeg.
static void navPath(const float* xy, int n, float angDeg, int w, uint16_t col) {
  int px = 0, py = 0, ppx = 0, ppy = 0;
  for (int i = 0; i < n; i++) {
    int sx, sy; navRot(xy[i * 2], xy[i * 2 + 1], angDeg, NAV_AX, NAV_AY, &sx, &sy);
    if (i > 0) navThickSeg(px, py, sx, sy, w, col);
    ppx = px; ppy = py; px = sx; py = sy;
  }
  // arrowhead at the last vertex, along the last segment direction
  navHead(px, py, (float)(px - ppx), (float)(py - ppy), w + 12, col);
}

// big plain arrow (compass mode) - stem + head, rotated by angDeg
static void navCompassArrow(float angDeg, uint16_t col) {
  // stem quad
  int s0x,s0y,s1x,s1y,s2x,s2y,s3x,s3y;
  navRot(-15, 62,  angDeg, NAV_AX, NAV_AY, &s0x,&s0y);
  navRot( 15, 62,  angDeg, NAV_AX, NAV_AY, &s1x,&s1y);
  navRot( 15,-14,  angDeg, NAV_AX, NAV_AY, &s2x,&s2y);
  navRot(-15,-14,  angDeg, NAV_AX, NAV_AY, &s3x,&s3y);
  gfx->fillTriangle(s0x,s0y, s1x,s1y, s2x,s2y, col);
  gfx->fillTriangle(s0x,s0y, s2x,s2y, s3x,s3y, col);
  // head triangle
  int hx,hy,lx,ly,rx,ry;
  navRot(  0,-80, angDeg, NAV_AX, NAV_AY, &hx,&hy);
  navRot(-44,-12, angDeg, NAV_AX, NAV_AY, &lx,&ly);
  navRot( 44,-12, angDeg, NAV_AX, NAV_AY, &rx,&ry);
  gfx->fillTriangle(hx,hy, lx,ly, rx,ry, col);
}

static void navRoundabout(uint16_t col) {
  for (int r = 36; r <= 40; r++) gfx->drawCircle(NAV_AX, NAV_AY, r, col);
  // exit arrow leaving top-right
  navThickSeg(NAV_AX, NAV_AY + 60, NAV_AX, NAV_AY + 36, 14, col);   // entry
  navThickSeg(NAV_AX + 26, NAV_AY - 26, NAV_AX + 58, NAV_AY - 58, 14, col);
  navHead(NAV_AX + 58, NAV_AY - 58, 1, -1, 22, col);
}

static void navArrivePin(uint16_t col) {
  int cx = NAV_AX, cy = NAV_AY - 6;
  gfx->fillTriangle(cx - 22, cy - 8, cx + 22, cy - 8, cx, cy + 30, col);
  gfx->fillCircle(cx, cy - 14, 26, col);
  gfx->fillCircle(cx, cy - 14, 11, C_BG);
}

// draw the maneuver glyph for the current turn action
static void navDrawManeuver(uint16_t col) {
  const int W = 16;
  switch (g_nav.turnAction) {
    case TURN_STRAIGHT:    { float p[] = {0,58, 0,-58};                 navPath(p, 2, 0, W, col); break; }
    case TURN_SLIGHT_RIGHT:{ float p[] = {0,58, 0,0, 32,-46};          navPath(p, 3, 0, W, col); break; }
    case TURN_RIGHT:       { float p[] = {0,58, 0,-4, 50,-4};          navPath(p, 3, 0, W, col); break; }
    case TURN_SHARP_RIGHT: { float p[] = {0,58, 0,-16, 30,12};         navPath(p, 3, 0, W, col); break; }
    case TURN_SLIGHT_LEFT: { float p[] = {0,58, 0,0, -32,-46};         navPath(p, 3, 0, W, col); break; }
    case TURN_LEFT:        { float p[] = {0,58, 0,-4, -50,-4};         navPath(p, 3, 0, W, col); break; }
    case TURN_SHARP_LEFT:  { float p[] = {0,58, 0,-16, -30,12};        navPath(p, 3, 0, W, col); break; }
    case TURN_UTURN:       { float p[] = {26,58, 26,-6, -26,-6, -26,30}; navPath(p, 4, 0, W, col); break; }
    case TURN_ROUNDABOUT:  navRoundabout(col); break;
    case TURN_ARRIVE:      navArrivePin(C_OK); break;
    default:               navCompassArrow(nav_arrowShown, col); break;  // TURN_NONE
  }
}

// vector road strip: the shape of the road AHEAD (Beeline route view). Drawn
// prominently: a dark casing (road edges) + a bright centre line + a "you are
// here" dot at the rider end. Points come pre-oriented (route forward = up).
static void navDrawRoadStrip(float angDeg) {
  if (g_nav.nPts < 2) return;
  const int   AY = NAV_AY + 95;     // rider low-centre; road runs up to ~top
  const float SC = 1.9f;            // px per normalized unit (bigger = closer/larger)
  int xs[NAV_MAXPTS], ys[NAV_MAXPTS];
  for (uint8_t i = 0; i < g_nav.nPts; i++)
    navRot(g_nav.px[i] * SC, -g_nav.py[i] * SC, angDeg, NAV_AX, AY, &xs[i], &ys[i]);

  for (uint8_t i = 1; i < g_nav.nPts; i++)         // casing (road body)
    navThickSeg(xs[i - 1], ys[i - 1], xs[i], ys[i], 18, C_BG2);
  for (uint8_t i = 1; i < g_nav.nPts; i++)         // bright centre line
    navThickSeg(xs[i - 1], ys[i - 1], xs[i], ys[i], 8, C_ACCENT);

  gfx->fillCircle(xs[0], ys[0], 11, C_TEXT);       // rider position
  gfx->fillCircle(xs[0], ys[0], 5,  C_ACCENT);
}

// Compact turn cue for route mode - a small icon chip near the top so the road
// strip stays the focus, while the maneuver type is still shown.
static void navManeuverBadge(uint8_t action, uint16_t col) {
  int cx = NAV_AX, cy = 92;
  gfx->fillRoundRect(cx - 46, cy - 32, 92, 64, 16, C_BG2);
  switch (action) {
    case TURN_LEFT: case TURN_SLIGHT_LEFT: case TURN_SHARP_LEFT:
      gfx->fillTriangle(cx - 24, cy, cx - 2, cy - 17, cx - 2, cy + 17, col);
      gfx->fillRect(cx - 2, cy - 6, 26, 12, col); break;
    case TURN_RIGHT: case TURN_SLIGHT_RIGHT: case TURN_SHARP_RIGHT:
      gfx->fillTriangle(cx + 24, cy, cx + 2, cy - 17, cx + 2, cy + 17, col);
      gfx->fillRect(cx - 24, cy - 6, 26, 12, col); break;
    case TURN_UTURN:
      for (int r = 13; r <= 15; r++) gfx->drawCircle(cx + 4, cy, r, col);
      gfx->fillTriangle(cx - 9, cy - 2, cx - 1, cy - 14, cx + 7, cy - 2, col); break;
    case TURN_ROUNDABOUT:
      for (int r = 14; r <= 16; r++) gfx->drawCircle(cx, cy, r, col);
      gfx->fillRect(cx - 3, cy + 12, 6, 16, col); break;
    case TURN_ARRIVE:
      gfx->fillCircle(cx, cy, 15, C_OK); gfx->fillCircle(cx, cy, 6, C_BG); break;
    default:  // straight
      gfx->fillTriangle(cx, cy - 18, cx - 13, cy - 1, cx + 13, cy - 1, col);
      gfx->fillRect(cx - 5, cy - 1, 10, 24, col); break;
  }
}

static String navFmtDist(uint32_t m) {
  if (m < 1000) return String(m) + " m";
  char b[16]; snprintf(b, sizeof(b), "%.1f km", m / 1000.0f); return String(b);
}

static void nav_tick() {
  uint32_t now = millis();

  // ---- sensor fusion: low-pass the compass heading (drift-free pointer) ----
  bool haveCompass = magPresent();
  if (haveCompass) {
    float h = magHeading();
    if (h >= 0) {
      if (!nav_headInit) { nav_headFilt = h; nav_headInit = true; }
      else nav_headFilt = navLerpAngle(nav_headFilt, h, 0.25f);   // complementary LP
    }
  }

  // ---- arrow target angle + per-frame interpolation (LERP) ----
  bool compassMode = (g_nav.turnAction == TURN_NONE);
  if (compassMode) {
    float tgt = nav_arrowShown;
    if (g_nav.targetBearing >= 0)
      tgt = haveCompass ? (g_nav.targetBearing - nav_headFilt) : g_nav.targetBearing;
    nav_arrowShown = navLerpAngle(nav_arrowShown, tgt, 0.30f);
  } else {
    nav_arrowShown = navLerpAngle(nav_arrowShown, 0, 0.30f);      // settle upright
  }

  // ---- render ----
  gfx->fillScreen(C_BG);

  if (!navFresh()) {
    // waiting for the phone to start sending route data
    navCompassArrow(0, C_DIM);
    textCenter("NAVIGASYON", CXi, 70, 3, C_GOLD);
    textCenter("telefon rota verisi bekleniyor", CXi, 360, 2, C_DIM);
    textCenter(haveCompass ? "pusula: hazir" : "pusula yok (yon: kuzey)", CXi, 392, 2,
               haveCompass ? C_OK : C_WARN);
    textCenter("uzun bas: cikis", CXi, 430, 2, C_DIM);
    present();
    delay(40);
    return;
  }

  // Route mode (phone sent road geometry): the road shape ahead is the star -
  // draw it prominently and use a COMPACT turn glyph at the top. Without geometry
  // (e.g. Google Maps mode) fall back to the big centred maneuver arrow.
  bool routeMode = (g_nav.nPts >= 2);
  if (routeMode) {
    navDrawRoadStrip(compassMode ? nav_arrowShown : 0);
    if (!compassMode) {
      navManeuverBadge(g_nav.turnAction, C_TEXT);   // small turn cue, top-centre
    }
  } else {
    navDrawManeuver(C_TEXT);                          // big centred arrow
  }

  // ---- bottom panel: distance-to-turn (big), total + ETA, clock ----
  textCenter(navFmtDist(g_nav.distToTurn).c_str(), CXi, 326, 5, C_TEXT);

  // total remaining + ETA minutes (Google Maps modunda bunlar gelmez -> gizle)
  String line2 = g_nav.totalDist ? navFmtDist(g_nav.totalDist) : String("");
  time_t tnow = time(nullptr);
  if (g_nav.etaUnix > (uint32_t)tnow && tnow > 1700000000) {
    int mins = (int)((g_nav.etaUnix - (uint32_t)tnow) / 60);
    line2 += "   ~" + String(mins) + " dk";
  }
  if (line2.length()) textCenter(line2.c_str(), CXi, 372, 2, C_DIM);

  if (timeValid()) {
    char hb[6]; snprintf(hb, sizeof(hb), "%02d:%02d", timeHour(), timeMin());
    textCenter(hb, CXi, 416, 2, C_DIM);
  }

  present();
  delay(40);     // ~25 fps cap; arrow already interpolates smoothly between
}
