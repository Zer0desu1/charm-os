// ============================================================================
// heading.h - fused device heading (compass + gyro), shared by the Pusula and
//   Harita modules + the dead-reckoning logger. The magnetometer gives the
//   absolute bearing; the MPU6050 gyro-Z rate fills in between compass updates
//   and smooths jitter (complementary filter).
//
//   headingFused() advances the estimate (call ~once per frame from whoever is
//   driving it). headingLast() just reads the current value without advancing,
//   so a second consumer in the same frame doesn't double-integrate.
//
//   Returns degrees [0,360), 0 = North (screen up). -1 if no sensor at all.
//   Include AFTER mag.h and mpu.h.
// ============================================================================
#pragma once

#include "mag.h"
#include "mpu.h"

static float    _hdg   = -1;
static uint32_t _hdgAt = 0;

static float headingLast() { return _hdg; }

static float headingFused() {
  uint32_t now = millis();
  float dt = (_hdgAt == 0) ? 0 : (now - _hdgAt) / 1000.0f;
  _hdgAt = now;
  if (dt > 0.5f) dt = 0;                          // ignore long gaps

  bool haveMpu = mpuPresent(), haveMag = magPresent();
  if (!haveMpu && !haveMag) { _hdg = -1; return -1; }

  // short-term: integrate gyro Z (+gz = CCW; screen bearing is CW -> subtract)
  if (haveMpu && dt > 0 && _hdg >= 0) {
    _hdg -= motionGyroZ() * dt;
    if (_hdg < 0) _hdg += 360; else if (_hdg >= 360) _hdg -= 360;
  }

  // absolute correction from the compass (or seed it)
  if (haveMag) {
    float mh = magHeading();
    if (mh >= 0) {
      if (_hdg < 0) _hdg = mh;
      else {
        float d = mh - _hdg;
        while (d > 180) d -= 360; while (d < -180) d += 360;
        _hdg += d * (haveMpu ? 0.04f : 0.3f);     // light pull if gyro present
        if (_hdg < 0) _hdg += 360; else if (_hdg >= 360) _hdg -= 360;
      }
    }
  }
  return _hdg;
}
