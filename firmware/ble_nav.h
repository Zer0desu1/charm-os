// ============================================================================
// ble_nav.h - Beeline-style turn-by-turn NAV data over BLE GATT.
//   The phone renders nothing: it sends only COMPACT BINARY vector/number data
//   over a custom GATT service; the ESP32 draws the UI locally (mod_nav.h).
//
//   Transport: one writable+notify characteristic on the EXISTING BLE server
//   (vecta_blehid.h). The phone is the GATT client and WRITES a packed packet
//   (~27 bytes) whenever the route state changes (typ. 1 Hz). No JSON, no
//   floats on the wire -> tiny RAM + airtime. The phone must negotiate an ATT
//   MTU >= 32 (iOS/Android both do this automatically).
//
//   Include AFTER vecta_blehid.h (needs BLE + the server accessor).
// ============================================================================
#pragma once

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ---- UUIDs (custom 128-bit). Phone uses these to find the service/char. ----
#define NAV_SVC_UUID    "6e5a0001-b5a3-f393-e0a9-e50e24dcca9e"
#define NAV_CHR_UUID    "6e5a0002-b5a3-f393-e0a9-e50e24dcca9e"
// Second characteristic on the SAME service: arbitrary phone notifications
// (any app) mirrored into the notify feed (mod_notify.h). The Android
// NotificationListenerService writes here; works in the background / screen-off
// over the bonded link, no WiFi needed (unlike the HTTP /notify path).
#define NOTIFY_CHR_UUID "6e5a0003-b5a3-f393-e0a9-e50e24dcca9e"

// ---- Turn action ids (turn_action_id) --------------------------------------
enum TurnAction : uint8_t {
  TURN_NONE = 0,    // compass mode: arrow just points to destination bearing
  TURN_STRAIGHT,    // 1
  TURN_RIGHT,       // 2
  TURN_LEFT,        // 3
  TURN_ROUNDABOUT,  // 4
  TURN_SLIGHT_RIGHT,// 5
  TURN_SLIGHT_LEFT, // 6
  TURN_SHARP_RIGHT, // 7
  TURN_SHARP_LEFT,  // 8
  TURN_UTURN,       // 9
  TURN_ARRIVE       // 10
};

#define NAV_MAXPTS 6

// ---- Decoded navigation state (what the UI reads) --------------------------
struct NavState {
  uint8_t  turnAction   = TURN_NONE;
  uint16_t distToTurn   = 0;     // meters to the next maneuver
  uint32_t totalDist    = 0;     // meters remaining to destination
  uint32_t etaUnix      = 0;     // arrival time, unix seconds (0 = unknown)
  int16_t  targetBearing= -1;    // destination/turn geo bearing, deg (-1 none)
  uint8_t  nPts         = 0;     // road-strip vertex count
  int8_t   px[NAV_MAXPTS];       // normalized -100..100 (x = right+, y = fwd+)
  int8_t   py[NAV_MAXPTS];
  uint32_t rxAt         = 0;     // millis() of last packet (staleness check)
};

static NavState g_nav;

// Wire layout (little-endian, packed). Total 27 bytes for a full 6-pt strip.
//   [0]  u8  ver (=1)
//   [1]  u8  turn_action
//   [2]  u16 distance_to_turn
//   [4]  u32 total_distance
//   [8]  u32 eta
//   [12] i16 target_bearing
//   [14] u8  npts (0..6)
//   [15] i8[2*npts]  x0,y0, x1,y1, ...
static const int NAV_HDR = 15;

static inline uint16_t rdU16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline int16_t  rdI16(const uint8_t* p) { return (int16_t)rdU16(p); }
static inline uint32_t rdU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Parse one packet into g_nav. Returns false on malformed input (left intact).
static bool navParse(const uint8_t* d, size_t n) {
  if (n < NAV_HDR || d[0] != 1) return false;
  uint8_t np = d[14];
  if (np > NAV_MAXPTS) np = NAV_MAXPTS;
  if (n < (size_t)(NAV_HDR + 2 * np)) return false;   // truncated point list

  g_nav.turnAction    = d[1];
  g_nav.distToTurn    = rdU16(d + 2);
  g_nav.totalDist     = rdU32(d + 4);
  g_nav.etaUnix       = rdU32(d + 8);
  g_nav.targetBearing = rdI16(d + 12);
  g_nav.nPts          = np;
  for (uint8_t i = 0; i < np; i++) {
    g_nav.px[i] = (int8_t)d[NAV_HDR + 2 * i];
    g_nav.py[i] = (int8_t)d[NAV_HDR + 2 * i + 1];
  }
  g_nav.rxAt = millis();
  // Araç-modu: navigasyon verisi gelince, launcher'dayken Navigasyon modulunu
  // otomatik aç (acik bir uygulamayi bolme). Ana dongu g_requestOpen'i isler.
  if (g_app == APP_LAUNCHER) {
    int idx = findModuleIdx("nav");
    if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  }
  return true;
}

// True if a packet arrived recently enough to trust (route still live).
static inline bool navFresh() { return g_nav.rxAt && (millis() - g_nav.rxAt < 15000); }

// ---- GATT write callback: phone writes -> parse ----------------------------
class NavCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    navParse((const uint8_t*)v.c_str(), v.length());
  }
};

static BLECharacteristic* g_navChr = nullptr;

// ---- Notification mirroring (phone -> device notify feed) ------------------
// Compact packet written by the phone's NotificationListenerService:
//   [0] u8 ver(=1)  [1] u8 kind (NotifKind: 0 info,1 msg,2 call,3 mail,4 alarm)
//   [2] u8 appLen   [3] u8 titleLen  [4] u8 bodyLen
//   [5..] app, title, body  (UTF-8 bytes, in that order)
// Needs an ATT MTU large enough for the strings (phone negotiates ~247).
static void notifyBleParse(const uint8_t* d, size_t n) {
  if (n < 5 || d[0] != 1) return;
  uint8_t  kind = d[1];
  uint16_t al = d[2], tl = d[3], bl = d[4];
  if ((size_t)(5 + al + tl + bl) > n) return;        // truncated packet

  // Copy each field into a bounded, null-terminated buffer (offsets use the
  // real sent lengths; only the COPY is capped to fit the notify struct).
  char app[40], title[48], body[120];
  uint16_t a = al < sizeof(app)   - 1 ? al : sizeof(app)   - 1;
  uint16_t t = tl < sizeof(title) - 1 ? tl : sizeof(title) - 1;
  uint16_t b = bl < sizeof(body)  - 1 ? bl : sizeof(body)  - 1;
  memcpy(app,   d + 5,           a); app[a]   = 0;
  memcpy(title, d + 5 + al,      t); title[t] = 0;
  memcpy(body,  d + 5 + al + tl, b); body[b]  = 0;

  if (kind > NK_ALARM) kind = NK_INFO;
  // trAscii() folds Turkish/UTF-8 to the device's ASCII font (same as /notify).
  notifyPush(kind, trAscii(String(app)), trAscii(String(title)),
             trAscii(String(body)), 0);

  // Pop the notify module up like the HTTP /notify path does (smartwatch feel).
  int idx = findModuleIdx("notify");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
}

class NotifyCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    notifyBleParse((const uint8_t*)v.c_str(), v.length());
  }
};

static BLECharacteristic* g_notifyChr = nullptr;

// Create + start the nav service on the existing server. We deliberately do NOT
// add the 128-bit UUID to the advertisement (it would overflow the 31-byte adv
// payload that already carries the HID UUID + name); the phone discovers this
// service AFTER connecting via the device name.
static void navBleBegin(BLEServer* server) {
  if (!server) return;
  BLEService* svc = server->createService(NAV_SVC_UUID);
  g_navChr = svc->createCharacteristic(
      NAV_CHR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR |
      BLECharacteristic::PROPERTY_NOTIFY);
  g_navChr->setCallbacks(new NavCharCallbacks());

  g_notifyChr = svc->createCharacteristic(
      NOTIFY_CHR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_notifyChr->setCallbacks(new NotifyCharCallbacks());

  svc->start();
}
