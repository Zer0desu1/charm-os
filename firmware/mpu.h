// ============================================================================
// mpu.h - MPU6050 IMU (accel + gyro) + an always-on MOTION ENGINE.
//   Shares the touch I2C bus (Wire, pins TOUCH_SDA/TOUCH_SCL). Runtime-probed
//   at 0x68 (AD0=GND) or 0x69 (AD0=VCC). If absent, mpuPresent()==false and
//   all consumers no-op.
//
//   motionTick() must be called every loop iteration (cheap: it self-throttles
//   to ~20 Hz). It runs in the BACKGROUND regardless of the active module, so
//   step counting + activity/scratch detection keep working like a real collar.
//
//   What it derives from raw accel (and gyro):
//     * step count (today + lifetime)        -> pedometer
//     * activity class: REST/WALK/RUN/SCRATCH -> by motion energy + the
//       sign-change rate of the dynamic (gravity-removed) accel, i.e. a cheap
//       dominant-frequency proxy. Scratching/ear-shaking is high-frequency
//       jitter with little net translation -> distinct from walking/running.
//     * daily "scratch seconds" + a rolling baseline -> health alert
//       ("Sparky bugun normalden %X daha fazla kasindi").
//     * orientation flip (worn upside down) -> drives global screen rotation.
//
//   Include AFTER platform.h. Thresholds are in g and are tunable below.
// ============================================================================
#pragma once

#include "platform.h"
#include <math.h>

#define MPU_ADDR_LO 0x68
#define MPU_ADDR_HI 0x69

enum MotionAct { ACT_REST = 0, ACT_WALK, ACT_RUN, ACT_SCRATCH };

static uint8_t _mpuAddr = 0;
static bool    _mpuOk   = false;

// --- tunables (g / per-second) ---
static const float MOT_REST_E    = 0.045f;  // below this mean|dyn| = resting
static const float MOT_RUN_E     = 0.34f;   // above this = running
static const float MOT_SCRATCH_E = 0.13f;   // min energy to call it scratching
static const int   MOT_SCRATCH_SC= 11;      // min sign-changes/sec for scratch
static const float MOT_STEP_TH   = 0.12f;   // step peak threshold on |dyn|
static const uint32_t MOT_STEP_REFRACT = 260; // ms between steps

// --- live state ---
static float    _motGravLP = 1.0f;          // low-passed accel magnitude (~g)
static float    _motDyn    = 0;             // last dynamic (gravity-removed) accel
static float    _motPrevDyn= 0;
static float    _motEnergyAcc = 0;          // accumulator over the 1 s window
static int      _motSampleN   = 0;
static int      _motSignChg   = 0;          // sign changes this window
static uint32_t _motWinStart  = 0;
static uint32_t _motLastSample= 0;
static uint32_t _motLastStep  = 0;

static float    _motEnergy = 0;             // last completed-window mean|dyn|
static int      _motScRate = 0;             // last completed-window changes/sec
static MotionAct _motAct   = ACT_REST;

static uint32_t _motSteps      = 0;         // lifetime
static uint32_t _motStepsToday = 0;
static uint32_t _motScratchToday = 0;       // scratch SECONDS today
static float    _motScratchBase  = 0;       // rolling avg of prior days (sec)
static int      _motLastHour   = -1;        // for midnight rollover
static uint32_t _motDayStartMs = 0;         // fallback 24h rollover
static uint32_t _motLastPersist= 0;

static float    _motAx = 0, _motAy = 1.0f, _motAz = 0;  // smoothed accel (g)
static bool     _motFlip = false;           // hysteretic upside-down state (legacy)
static int      _motQuarter = 0;            // hysteretic quarter-turn (0..3)

// --- wrist-raise detector (drives tilt-to-wake) ---
static float    _motPitchRing[12];          // ~600 ms of pitch samples
static int      _motPitchRingN = 0, _motPitchRingI = 0;
static uint32_t _motRaiseAt  = 0;
static bool     _motRaiseEvt = false;

// --- 7-day step history (index 0 = oldest .. last = yesterday), NVS-persisted ---
static const int MOT_HIST_DAYS = 7;
static uint16_t _motStepHist[MOT_HIST_DAYS] = {0};

// --- AHRS (complementary filter) + impact/shake/freefall ---
static float    _motPitch = 0, _motRoll = 0;  // deg, fused accel+gyro
static float    _motMag   = 1.0f;             // last accel magnitude (g)
static float    _motGz    = 0;                // last gyro Z (dps), for heading fusion
static uint32_t _motShakeAt = 0;              // last shake time
static bool     _motShakeEvt = false;         // unconsumed shake edge
static float    _motShakeMag = 0;             // peak |dyn| of the last shake
static const float MOT_SHAKE_TH = 0.95f;      // |dyn-accel| g for a deliberate shake

// Which in-plane device axis points to the BOTTOM of the screen when worn
// normally, and its sign. Default: gravity along +Y means upright. If your
// MPU is mounted rotated and auto-rotate flips the wrong way, negate this or
// switch to _motAx in motionFlipUpdate().
#define MOT_FLIP_FLAT 0.80f                 // |az| above this = lying flat (hold state)
#define MOT_FLIP_ON   (-0.35f)              // ay below -> flipped
#define MOT_FLIP_OFF  ( 0.35f)              // ay above -> upright

// 4-way auto-rotate: quarter-turn orientation from the in-plane gravity angle.
#define MOT_ORIENT_HYST 18.0f               // deg past the 45 boundary before switching
// Map canonical quarter (0=upright, 1=right-edge-down, 2=upside-down,
// 3=left-edge-down) to the display setRotation() value. If the landscape sides
// turn the wrong way, swap entries [1] and [3]; if they are 180-off, also swap.
static const int MOT_ROT_MAP[4] = {0, 1, 2, 3};

// --- step-history persistence (CSV of MOT_HIST_DAYS ints in NVS "step_hist") ---
static void _motHistLoad() {
  for (int i = 0; i < MOT_HIST_DAYS; i++) _motStepHist[i] = 0;
  String s = setGetStr("step_hist", "");
  int idx = 0, from = 0, len = (int)s.length();
  while (idx < MOT_HIST_DAYS && from < len) {
    int c = s.indexOf(',', from); if (c < 0) c = len;
    if (c > from) _motStepHist[idx++] = (uint16_t)s.substring(from, c).toInt();
    from = c + 1;
  }
}
static void _motHistSave() {
  String s;
  for (int i = 0; i < MOT_HIST_DAYS; i++) { if (i) s += ','; s += _motStepHist[i]; }
  setPutStr("step_hist", s);
}
static void _motHistPush(uint16_t daySteps) {     // shift left, append yesterday
  for (int i = 0; i < MOT_HIST_DAYS - 1; i++) _motStepHist[i] = _motStepHist[i + 1];
  _motStepHist[MOT_HIST_DAYS - 1] = daySteps;
  _motHistSave();
}

static bool _mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(_mpuAddr);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

static void mpuBegin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  _mpuAddr = 0;
  for (uint8_t a = MPU_ADDR_LO; a <= MPU_ADDR_HI; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { _mpuAddr = a; break; }
  }
  if (!_mpuAddr) { _mpuOk = false; return; }
  _mpuWrite(0x6B, 0x00);   // PWR_MGMT_1: wake, internal 8MHz clock
  _mpuWrite(0x1A, 0x03);   // CONFIG: DLPF ~44 Hz (smooths gear noise)
  _mpuWrite(0x1C, 0x08);   // ACCEL_CONFIG: +-4 g
  _mpuWrite(0x1B, 0x08);   // GYRO_CONFIG: +-500 dps
  _mpuOk = true;

  // restore persisted daily counters
  _motStepsToday   = (uint32_t)setGetInt("mot_steps", 0);
  _motSteps        = (uint32_t)setGetInt("mot_life", 0);
  _motScratchToday = (uint32_t)setGetInt("mot_scr", 0);
  _motScratchBase  = (float)setGetInt("mot_scrbase", 0);
  _motHistLoad();
  _motDayStartMs   = millis();
  _motWinStart     = millis();
}

static bool mpuPresent() { return _mpuOk; }

// raw read: accel in g, gyro in dps. Returns false if absent/failed.
static bool mpuRead(float& ax, float& ay, float& az,
                    float& gx, float& gy, float& gz) {
  if (!_mpuOk) return false;
  Wire.beginTransmission(_mpuAddr);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(_mpuAddr, (uint8_t)14);
  if (Wire.available() < 14) return false;
  int16_t rax = (Wire.read() << 8) | Wire.read();
  int16_t ray = (Wire.read() << 8) | Wire.read();
  int16_t raz = (Wire.read() << 8) | Wire.read();
  (void)((Wire.read() << 8) | Wire.read());            // temp
  int16_t rgx = (Wire.read() << 8) | Wire.read();
  int16_t rgy = (Wire.read() << 8) | Wire.read();
  int16_t rgz = (Wire.read() << 8) | Wire.read();
  const float aS = 1.0f / 8192.0f;   // +-4 g  -> 8192 LSB/g
  const float gS = 1.0f / 65.5f;     // +-500 dps -> 65.5 LSB/dps
  ax = rax * aS; ay = ray * aS; az = raz * aS;
  gx = rgx * gS; gy = rgy * gS; gz = rgz * gS;
  return true;
}

// ---- accessors for UI ----
static bool        motionPresent()       { return _mpuOk; }
static uint32_t    motionStepsToday()    { return _motStepsToday; }
static uint32_t    motionStepsLife()     { return _motSteps; }
static MotionAct   motionActivity()      { return _motAct; }
static uint32_t    motionScratchToday()  { return _motScratchToday; }
static float       motionScratchBase()   { return _motScratchBase; }
// % over baseline (0 if no baseline yet). e.g. +40 => 40% more than usual.
static int         motionScratchPct() {
  if (_motScratchBase < 1) return 0;
  return (int)(((float)_motScratchToday - _motScratchBase) / _motScratchBase * 100.0f);
}
static const char* motionActivityStr() {
  switch (_motAct) {
    case ACT_WALK:    return "Yuruyor";
    case ACT_RUN:     return "Kosuyor";
    case ACT_SCRATCH: return "Kasiniyor";
    default:          return "Dinleniyor";
  }
}
// 7-day step history for the phone's weekly graph (index 0 = oldest day).
static int      motionHistDays()         { return MOT_HIST_DAYS; }
static uint16_t motionStepHistory(int i) { return (i >= 0 && i < MOT_HIST_DAYS) ? _motStepHist[i] : 0; }
static String   motionStepHistoryCsv()   {
  String s; for (int i = 0; i < MOT_HIST_DAYS; i++) { if (i) s += ','; s += _motStepHist[i]; }
  return s;
}
// One-shot wrist-raise edge (true once when the device is lifted to be viewed).
static bool motionRaiseEvent() {
  if (_motRaiseEvt) { _motRaiseEvt = false; return true; }
  return false;
}
// true when the device is held upside down (legacy 180-only accessor).
static bool motionFlipped() { return _motFlip; }
// 4-way screen orientation (setRotation value 0..3) from in-plane gravity,
// hysteretic; holds the last orientation while the device lies flat.
static int motionOrient() { return MOT_ROT_MAP[_motQuarter]; }

// --- AHRS / gesture accessors (mounting-dependent signs; tune in mascot if odd) ---
static float    motionPitch()   { return _motPitch; }   // nose up/down, deg
static float    motionRoll()    { return _motRoll; }    // left/right tilt, deg
static float    motionGyroZ()   { return _motGz; }      // yaw rate, dps
static bool     motionShaking() { return (millis() - _motShakeAt) < 500; }
static float    motionShakeMag(){ return _motShakeMag; }
static bool     motionFreefall(){ return _motMag < 0.40f; }  // in (near) free fall
// consume a one-shot shake edge (true once per deliberate shake)
static bool motionShakeEvent() {
  if (_motShakeEvt) { _motShakeEvt = false; return true; }
  return false;
}

static void _motRolloverDay() {
  // archive yesterday's steps into the 7-day history BEFORE resetting
  _motHistPush((uint16_t)(_motStepsToday > 65535 ? 65535 : _motStepsToday));
  // fold today's scratch into the rolling baseline (EMA), reset daily counts
  if (_motScratchBase < 1) _motScratchBase = (float)_motScratchToday;
  else _motScratchBase = _motScratchBase * 0.7f + (float)_motScratchToday * 0.3f;
  _motScratchToday = 0;
  _motStepsToday   = 0;
  setPutInt("mot_scrbase", (int)_motScratchBase);
  setPutInt("mot_scr", 0);
  setPutInt("mot_steps", 0);
  _motDayStartMs = millis();
}

// Call EVERY loop. Self-throttles to ~20 Hz; classifies on a 1 s window.
static void motionTick() {
  if (!_mpuOk) return;
  uint32_t now = millis();
  uint32_t prev = _motLastSample;
  if (now - prev < 50) return;                  // ~20 Hz
  _motLastSample = now;
  float dt = prev ? (now - prev) / 1000.0f : 0.05f;
  if (dt > 0.5f) dt = 0.05f;                     // ignore long gaps

  float ax, ay, az, gx, gy, gz;
  if (!mpuRead(ax, ay, az, gx, gy, gz)) return;
  _motGz = gz;

  // smooth accel for orientation; resolve a hysteretic 4-way quarter-turn
  _motAx = _motAx * 0.85f + ax * 0.15f;
  _motAy = _motAy * 0.85f + ay * 0.15f;
  _motAz = _motAz * 0.85f + az * 0.15f;
  if (fabsf(_motAz) < MOT_FLIP_FLAT) {          // only decide when not flat
    // in-plane gravity angle: 0 = upright, +90 = right edge down, 180 = flip
    float a = atan2f(_motAx, _motAy) * RAD_TO_DEG;
    int cand;
    if      (a >  -45.0f && a <=  45.0f)  cand = 0;
    else if (a >   45.0f && a <= 135.0f)  cand = 1;   // right edge down
    else if (a <  -45.0f && a >= -135.0f) cand = 3;   // left edge down
    else                                  cand = 2;   // upside down
    // switch only when the angle is clearly past the 45 sector boundary
    static const float QC[4] = {0.0f, 90.0f, 180.0f, -90.0f};
    float d = a - QC[_motQuarter];
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    if (fabsf(d) > 45.0f + MOT_ORIENT_HYST) _motQuarter = cand;
    _motFlip = (_motQuarter == 2);
  }

  // AHRS complementary filter: gyro integration corrected by accel gravity dir
  float accRoll  = atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
  float accPitch = atan2f(ay, sqrtf(ax * ax + az * az)) * RAD_TO_DEG;
  _motRoll  = 0.94f * (_motRoll  + gy * dt) + 0.06f * accRoll;
  _motPitch = 0.94f * (_motPitch + gx * dt) + 0.06f * accPitch;

  // wrist-raise: a >=26 deg pitch swing over the last ~600 ms that settles at
  // one extreme = the device was lifted/turned to be looked at. Mounting-
  // agnostic (fires at either end of the swing); 1.2 s refractory.
  _motPitchRing[_motPitchRingI] = _motPitch;
  _motPitchRingI = (_motPitchRingI + 1) % 12;
  if (_motPitchRingN < 12) _motPitchRingN++;
  if (_motPitchRingN >= 6 && (now - _motRaiseAt) > 1200) {
    float mn = 1e9f, mx = -1e9f;
    for (int k = 0; k < _motPitchRingN; k++) {
      float v = _motPitchRing[k]; if (v < mn) mn = v; if (v > mx) mx = v;
    }
    bool atEnd = (_motPitch > mx - 7.0f) || (_motPitch < mn + 7.0f);
    // ...and the device must NOT be lying flat (screen-normal axis near gravity):
    // kills false wakes from a watch on a table or jostling in a pocket.
    bool notFlat = fabsf(_motAz) < 0.85f;
    if ((mx - mn) > 26.0f && atEnd && notFlat) { _motRaiseEvt = true; _motRaiseAt = now; }
  }

  float mag = sqrtf(ax * ax + ay * ay + az * az);
  _motMag = mag;
  // deliberate shake = large dynamic accel spike, with refractory
  if (fabsf(mag - _motGravLP) > MOT_SHAKE_TH && (now - _motShakeAt) > 350) {
    _motShakeAt = now; _motShakeEvt = true; _motShakeMag = fabsf(mag - _motGravLP);
  }
  _motGravLP = _motGravLP * 0.9f + mag * 0.1f;  // gravity estimate
  _motPrevDyn = _motDyn;
  _motDyn = mag - _motGravLP;                   // dynamic accel (gravity removed)

  // window accumulation
  _motEnergyAcc += fabsf(_motDyn);
  _motSampleN++;
  if ((_motPrevDyn <= 0 && _motDyn > 0) || (_motPrevDyn >= 0 && _motDyn < 0))
    _motSignChg++;

  // step peak detect (rising through threshold, with refractory)
  if (_motPrevDyn < MOT_STEP_TH && _motDyn >= MOT_STEP_TH &&
      (now - _motLastStep) > MOT_STEP_REFRACT &&
      (_motAct == ACT_WALK || _motAct == ACT_RUN)) {
    _motLastStep = now;
    _motSteps++; _motStepsToday++;
  }

  // close the 1 s classification window
  if (now - _motWinStart >= 1000) {
    _motEnergy = (_motSampleN > 0) ? _motEnergyAcc / _motSampleN : 0;
    _motScRate = _motSignChg;                    // ~changes per second
    _motEnergyAcc = 0; _motSampleN = 0; _motSignChg = 0;
    _motWinStart = now;

    if (_motEnergy < MOT_REST_E) _motAct = ACT_REST;
    else if (_motScRate >= MOT_SCRATCH_SC && _motEnergy >= MOT_SCRATCH_E)
      _motAct = ACT_SCRATCH;
    else if (_motEnergy >= MOT_RUN_E) _motAct = ACT_RUN;
    else _motAct = ACT_WALK;

    if (_motAct == ACT_SCRATCH) _motScratchToday++;   // +1 scratch-second

    // midnight rollover if phone time is synced; else 24 h of uptime
    if (timeValid()) {
      int h = timeHour();
      if (_motLastHour == 23 && h == 0) _motRolloverDay();
      _motLastHour = h;
    } else if (now - _motDayStartMs > 24UL * 3600UL * 1000UL) {
      _motRolloverDay();
    }

    // periodic persist (NVS wear-friendly)
    if (now - _motLastPersist > 30000) {
      _motLastPersist = now;
      setPutInt("mot_steps", (int)_motStepsToday);
      setPutInt("mot_life",  (int)_motSteps);
      setPutInt("mot_scr",   (int)_motScratchToday);
    }
  }
}
