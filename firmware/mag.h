// ============================================================================
// mag.h - magnetometer for compass heading (optional).
//   Shares the touch I2C bus (Wire, pins TOUCH_SDA/TOUCH_SCL). Runtime-probed
//   and auto-detects the common 3-axis mag chips sold as "compass" breakouts:
//     * QMC5883L  @ 0x0D   (GY-271/273 clones; most common)
//     * HMC5883L  @ 0x1E   (genuine Honeywell, different reg map + axis order)
//   If none answers, magPresent()==false and modules fall back to north-up.
//
//   IMPORTANT: wire the breakout to the SAME SDA/SCL as the touch controller
//   (TOUCH_SDA/TOUCH_SCL in platform.h). A separate "SDA/SCL" header that goes
//   to other GPIOs will NOT be seen by this bus. Use the Pusula module's
//   "sensor yok" screen — it shows a live I2C scan to confirm the address/bus.
//
//   Heading is uncalibrated until the Pusula module's spin calibration runs.
// ============================================================================
#pragma once

#include "platform.h"

#define QMC5883_ADDR 0x0D
#define HMC5883_ADDR 0x1E

enum MagChip { MAG_NONE = 0, MAG_QMC5883L, MAG_HMC5883L };

static MagChip _magChip = MAG_NONE;
static bool    _magOk   = false;
static String  _magScan = "";   // last I2C scan, for diagnostics

// hard-iron offsets. Loaded from NVS in magBegin(), refined by the Pusula
// module's calibration (a full 360 spin -> min/max midpoint per axis).
static int16_t MAG_OFF_X = 0, MAG_OFF_Y = 0;

static bool _i2cAck(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// scan the whole bus into _magScan (and return it) for the diagnostic screen
static String magScanBus() {
  String s = "";
  for (uint8_t a = 1; a < 0x7F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { char b[6]; snprintf(b, sizeof(b), "%02X ", a); s += b; }
  }
  if (!s.length()) s = "(bos)";
  _magScan = s;
  return s;
}

static void magBegin() {
  // assumes Wire already begun by touchBegin(); harmless to re-begin
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  magScanBus();

  if (_i2cAck(QMC5883_ADDR)) {
    _magChip = MAG_QMC5883L;
    // soft reset / set-reset period
    Wire.beginTransmission(QMC5883_ADDR); Wire.write(0x0B); Wire.write(0x01); Wire.endTransmission();
    // control1: OSR=512, RNG=8G, ODR=200Hz, MODE=continuous => 0x1D
    Wire.beginTransmission(QMC5883_ADDR); Wire.write(0x09); Wire.write(0x1D); Wire.endTransmission();
  } else if (_i2cAck(HMC5883_ADDR)) {
    _magChip = MAG_HMC5883L;
    Wire.beginTransmission(HMC5883_ADDR); Wire.write(0x00); Wire.write(0x70); Wire.endTransmission(); // CRA: 8avg,15Hz
    Wire.beginTransmission(HMC5883_ADDR); Wire.write(0x01); Wire.write(0xA0); Wire.endTransmission(); // CRB: gain
    Wire.beginTransmission(HMC5883_ADDR); Wire.write(0x02); Wire.write(0x00); Wire.endTransmission(); // continuous
  } else {
    _magChip = MAG_NONE;
    _magOk = false;
    return;
  }

  // restore saved hard-iron calibration (defaults 0 = uncalibrated)
  MAG_OFF_X = (int16_t)setGetInt("mag_ox", 0);
  MAG_OFF_Y = (int16_t)setGetInt("mag_oy", 0);
  _magOk = true;
}

static bool magPresent() { return _magOk; }

// human-readable diagnostic for the "sensor yok" screen
static String magScanStr() { return _magScan; }
static const char* magChipStr() {
  switch (_magChip) {
    case MAG_QMC5883L: return "QMC5883L 0x0D";
    case MAG_HMC5883L: return "HMC5883L 0x1E";
    default:           return "yok";
  }
}

// raw (offset-corrected) magnetometer X/Y. Returns false if absent/read failed.
static bool magRawXY(int16_t& x, int16_t& y) {
  if (!_magOk) return false;
  if (_magChip == MAG_QMC5883L) {
    Wire.beginTransmission(QMC5883_ADDR);
    Wire.write(0x00);                              // data starts at 0x00
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(QMC5883_ADDR, 6);
    if (Wire.available() < 6) return false;
    x = (int16_t)(Wire.read() | (Wire.read() << 8)); // LSB first: X,Y,Z
    y = (int16_t)(Wire.read() | (Wire.read() << 8));
    (void)(Wire.read() | (Wire.read() << 8));      // z unused
    return true;
  } else if (_magChip == MAG_HMC5883L) {
    Wire.beginTransmission(HMC5883_ADDR);
    Wire.write(0x03);                              // data starts at 0x03
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(HMC5883_ADDR, 6);
    if (Wire.available() < 6) return false;
    x = (int16_t)((Wire.read() << 8) | Wire.read()); // MSB first, order X,Z,Y
    (void)((Wire.read() << 8) | Wire.read());        // z unused
    y = (int16_t)((Wire.read() << 8) | Wire.read());
    return true;
  }
  return false;
}

// persist hard-iron offsets (call after a calibration spin)
static void magSetOffsets(int16_t ox, int16_t oy) {
  MAG_OFF_X = ox; MAG_OFF_Y = oy;
  setPutInt("mag_ox", ox);
  setPutInt("mag_oy", oy);
}

// returns heading in degrees [0,360), 0 = sensor's +X axis. -1 if absent.
static float magHeading() {
  int16_t x, y;
  if (!magRawXY(x, y)) return -1;
  x -= MAG_OFF_X; y -= MAG_OFF_Y;
  float h = atan2f((float)y, (float)x) / DEG_TO_RAD;
  if (h < 0) h += 360.0f;
  return h;
}
