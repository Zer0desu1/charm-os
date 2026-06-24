// ============================================================================
// mod_buddy.h - Module 11: Ikiz / Pusula (Buddy Pointer)
//   Two CharmOS devices find each other over ESP-NOW (no AP join needed).
//   They broadcast a beacon containing id + GPS (pushed from each phone).
//
//   Display:
//     * Magnetometer + both GPS  -> a real-world ARROW pointing to the peer
//                                   + distance in meters.
//     * Otherwise                -> distance-only "hot/cold" ring from RSSI.
//
//   Tap = "ping" the peer (its screen flashes briefly).
//   Long-press = home.
//
//   NOTE: both devices must run on the SAME Wi-Fi channel for ESP-NOW
//   broadcast to reach each other (CharmOS fixes softAP to channel 1).
// ============================================================================
#pragma once

#include "platform.h"
#include "mag.h"
#include "espnow.h"

#define BUDDY_MAGIC 0xB0
struct BuddyPkt {
  uint8_t  magic;     // BUDDY_MAGIC
  uint8_t  type;      // 0 = beacon, 1 = ping
  char     id[5];
  uint8_t  hasLoc;
  double   lat;
  double   lon;
};

// peer state
static bool     buddy_seen = false;
static char     buddy_id[5] = "----";
static bool     buddy_hasLoc = false;
static double   buddy_lat = 0, buddy_lon = 0;
static int      buddy_rssi = -127;
static uint32_t buddy_lastRx = 0;
static uint32_t buddy_pingUntil = 0;   // incoming ping -> flash

// ESP-NOW handler (filters by BUDDY_MAGIC)
static void buddyHandlePacket(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(BuddyPkt)) return;
  const BuddyPkt* p = (const BuddyPkt*)data;
  if (p->magic != BUDDY_MAGIC) return;
  if (strncmp(p->id, g_devId, 4) == 0) return;  // ignore self
  buddy_seen = true;
  strncpy(buddy_id, p->id, 5);
  buddy_hasLoc = p->hasLoc;
  buddy_lat = p->lat; buddy_lon = p->lon;
  buddy_lastRx = millis();
  if (info && info->rx_ctrl) buddy_rssi = info->rx_ctrl->rssi;
  if (p->type == 1) buddy_pingUntil = millis() + 2500;
}

static void buddySend(uint8_t type) {
  BuddyPkt p = {};
  p.magic = BUDDY_MAGIC; p.type = type;
  strncpy(p.id, g_devId, 5);
  p.hasLoc = g_myLoc.valid ? 1 : 0;
  p.lat = g_myLoc.lat; p.lon = g_myLoc.lon;
  espnowSend((uint8_t*)&p, sizeof(p));
}

static uint32_t buddy_lastBeacon = 0;

static void buddy_enter() { espnowBegin(); espnowAddHandler(buddyHandlePacket); buddy_lastBeacon = 0; }

// distance bucket from RSSI: returns 0..1 (1=very close)
static float rssiCloseness(int rssi) {
  // -40 ~ touching, -90 ~ far
  float c = (float)(rssi + 90) / 50.0f;
  return constrain(c, 0.0f, 1.0f);
}

static void drawArrow(float screenAngleDeg, uint16_t col) {
  float a = screenAngleDeg * DEG_TO_RAD;
  int tipR = 150, tailR = 90, wR = 40;
  int tx = CXi + (int)(tipR * sinf(a)),  ty = CYi - (int)(tipR * cosf(a));
  int bx = CXi - (int)(tailR * sinf(a)), by = CYi + (int)(tailR * cosf(a));
  // wings
  float aL = (screenAngleDeg - 150) * DEG_TO_RAD;
  float aR = (screenAngleDeg + 150) * DEG_TO_RAD;
  int lx = tx + (int)(wR * sinf(aL)), ly = ty - (int)(wR * cosf(aL));
  int rx = tx + (int)(wR * sinf(aR)), ry = ty - (int)(wR * cosf(aR));
  gfx->fillTriangle(tx, ty, lx, ly, bx, by, col);
  gfx->fillTriangle(tx, ty, rx, ry, bx, by, col);
  gfx->fillCircle(bx, by, 10, col);
}

static void buddy_tick() {
  espnowBegin();
  uint32_t now = millis();

  // periodic beacon
  if (now - buddy_lastBeacon > 400) { buddySend(0); buddy_lastBeacon = now; }
  // tap -> ping peer
  if (g_g.tap) buddySend(1);

  // peer timeout
  if (buddy_seen && now - buddy_lastRx > 4000) buddy_seen = false;

  // got pinged?
  if (now < buddy_pingUntil) {
    bool on = ((now / 150) % 2) == 0;
    gfx->fillScreen(on ? C_GOLD : C_ACCENT);
    textCenter("CAGIRILDIN", CXi, CYi, 3, on ? C_BG : C_TEXT);
    present(); delay(20); return;
  }

  gfx->fillScreen(C_BG);
  textCenter("IKIZ", CXi, 36, 2, C_DIM);

  if (!buddy_seen) {
    // searching animation
    float t = now * 0.004f;
    for (int i = 0; i < 3; i++) {
      int r = 60 + ((int)(t * 60) + i * 50) % 150;
      gfx->drawCircle(CXi, CYi, r, C_BG2);
    }
    textCenter("araniyor...", CXi, CYi, 2, C_DIM);
    char me[16]; snprintf(me, sizeof(me), "ben: %s", g_devId);
    textCenter(me, CXi, 430, 2, C_DIM);
    present(); delay(40); return;
  }

  // We have a peer. Decide arrow vs distance.
  bool canArrow = magPresent() && g_myLoc.valid && buddy_hasLoc;
  float dist = (g_myLoc.valid && buddy_hasLoc)
      ? geoDistance(g_myLoc.lat, g_myLoc.lon, buddy_lat, buddy_lon) : -1;

  if (canArrow) {
    float bearing = geoBearing(g_myLoc.lat, g_myLoc.lon, buddy_lat, buddy_lon);
    float heading = magHeading();             // where device's +X points
    float screenAngle = bearing - heading;    // rotate arrow into device frame
    drawArrow(screenAngle, C_OK);
    char db[20];
    if (dist >= 1000) snprintf(db, sizeof(db), "%.1f km", dist / 1000);
    else snprintf(db, sizeof(db), "%d m", (int)dist);
    textCenter(db, CXi, 380, 3, C_TEXT);
    textCenter(buddy_id, CXi, 430, 2, C_DIM);
  } else {
    // distance-only hot/cold ring from RSSI
    float c = rssiCloseness(buddy_rssi);
    uint16_t col = c > 0.66f ? C_OK : (c > 0.33f ? C_WARN : C_DANGER);
    int r = 60 + (int)(140 * c);
    gfx->fillCircle(CXi, CYi, r, col);
    gfx->fillCircle(CXi, CYi, max(0, r - 16), C_BG);
    gfx->fillCircle(CXi, CYi, max(0, r - 16), col);
    const char* word = c > 0.66f ? "COK YAKIN" : (c > 0.33f ? "YAKIN" : "UZAK");
    textCenter(word, CXi, CYi, 3, C_BG);
    if (dist >= 0) {
      char db[20]; snprintf(db, sizeof(db), "~%d m", (int)dist);
      textCenter(db, CXi, 380, 2, C_DIM);
    } else {
      textCenter(magPresent() ? "GPS bekleniyor" : "pusula yok", CXi, 380, 2, C_DIM);
    }
    textCenter("dokun: cagir", CXi, 430, 2, C_DIM);
  }
  present();
  delay(30);
}
