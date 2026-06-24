// ============================================================================
// deadreckon.h - rough ("kaba") dead-reckoning route logger.
//   Combines the IMU step counter with the fused compass+gyro heading to walk
//   an estimated path: each step advances DR_STRIDE metres along the current
//   heading. Stores a decimated breadcrumb polyline (local metres, north-up).
//   NO GPS correction -> it drifts over distance; it's a "which way did we turn"
//   sketch of the route, not a survey. The Harita module draws it as an inset.
//
//   drUpdate() must be called every loop iteration (background) so the route is
//   logged even while other modules are open. Include AFTER mpu.h + heading.h.
// ============================================================================
#pragma once

#include "mpu.h"
#include "heading.h"

#define DR_MAXPTS 96
static float    dr_x[DR_MAXPTS], dr_y[DR_MAXPTS];  // metres, +x=east +y=north
static int      dr_n = 0;
static float    dr_px = 0, dr_py = 0;              // current estimated position
static uint32_t dr_lastSteps = 0;
static bool     dr_init = false;
static float    dr_dist = 0;                       // total path length (m)
static float    DR_STRIDE = 0.40f;                 // metres per step (pet-sized)

static void drReset() {
  dr_px = dr_py = 0; dr_dist = 0;
  dr_x[0] = 0; dr_y[0] = 0; dr_n = 1;
  dr_lastSteps = motionStepsLife();
  dr_init = true;
}

static void drUpdate() {
  if (!motionPresent()) return;
  if (!dr_init) drReset();
  float h = headingFused();                  // also advances the shared heading
  uint32_t s = motionStepsLife();
  if (s <= dr_lastSteps) return;
  int ds = (int)(s - dr_lastSteps); dr_lastSteps = s;
  if (h < 0) h = 0;                          // no compass -> assume north
  float a = h * DEG_TO_RAD;
  dr_px += sinf(a) * DR_STRIDE * ds;         // east
  dr_py += cosf(a) * DR_STRIDE * ds;         // north
  dr_dist += DR_STRIDE * ds;
  // append, decimating points within ~1.5 m of the last stored vertex
  float lx = dr_x[dr_n - 1], ly = dr_y[dr_n - 1];
  if ((dr_px - lx) * (dr_px - lx) + (dr_py - ly) * (dr_py - ly) < 1.5f * 1.5f) return;
  if (dr_n < DR_MAXPTS) { dr_x[dr_n] = dr_px; dr_y[dr_n] = dr_py; dr_n++; }
  else {                                     // ring: drop the oldest vertex
    for (int i = 1; i < DR_MAXPTS; i++) { dr_x[i - 1] = dr_x[i]; dr_y[i - 1] = dr_y[i]; }
    dr_x[DR_MAXPTS - 1] = dr_px; dr_y[DR_MAXPTS - 1] = dr_py;
  }
}

static float drDistance() { return dr_dist; }
static int   drCount()    { return dr_n; }

// Draw the north-up trail auto-scaled into a circle at (cx,cy) radius rad.
static void drDrawTrail(int cx, int cy, int rad) {
  if (dr_n < 1) return;
  float minx = dr_x[0], maxx = dr_x[0], miny = dr_y[0], maxy = dr_y[0];
  for (int i = 1; i < dr_n; i++) {
    minx = min(minx, dr_x[i]); maxx = max(maxx, dr_x[i]);
    miny = min(miny, dr_y[i]); maxy = max(maxy, dr_y[i]);
  }
  float span = max(maxx - minx, maxy - miny);
  if (span < 2) span = 2;
  float sc = (rad * 1.7f) / span;            // metres -> px
  float ofx = (minx + maxx) * 0.5f, ofy = (miny + maxy) * 0.5f;
  #define DRX(mx) (cx + (int)(((mx) - ofx) * sc))
  #define DRY(my) (cy - (int)(((my) - ofy) * sc))
  for (int i = 1; i < dr_n; i++)
    gfx->drawLine(DRX(dr_x[i - 1]), DRY(dr_y[i - 1]), DRX(dr_x[i]), DRY(dr_y[i]), C_ACCENT);
  gfx->fillCircle(DRX(dr_x[0]), DRY(dr_y[0]), 4, C_OK);        // start
  gfx->fillCircle(DRX(dr_px),  DRY(dr_py),  5, C_DANGER);      // current
  #undef DRX
  #undef DRY
}
