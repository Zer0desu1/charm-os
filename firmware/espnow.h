// ============================================================================
// espnow.h - shared ESP-NOW layer for device-to-device features.
//   ESP-NOW allows only ONE recv callback, so we register a single dispatcher
//   here and let modules add their own handlers (each filters by its own magic
//   byte). Used by Buddy (finder), Draw (whiteboard) and Lovebox modules.
//
//   On top of the raw broadcast path this adds a small "Vecta agi" layer:
//     * HELLO beacons (id + name) -> automatic peer discovery; the peer table
//       (MAC, id, name, RSSI, last-seen) persists to NVS so paired Vectas are
//       remembered across reboots and shown in Settings.
//     * Reliable unicast: [0xE2][seq16][payload] is sent per peer, ACKed with
//       [0xE1][seq16] and retried up to 3x; receivers dedupe by (MAC, seq) so
//       a retry never double-delivers. Falls back to plain broadcast when no
//       peer has been discovered yet (legacy behaviour).
//   Channel note: peer.channel = 0 = "current radio channel"; AP + STA share
//   the single radio so sending on the always-up softAP interface is safe.
// ============================================================================
#pragma once

#include <esp_now.h>
#include <WiFi.h>
#include "platform.h"

static uint8_t EN_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// control magics (modules use 0x5A/0xB0/0xD0; keep these out of that space)
#define EN_M_HELLO 0xE0
#define EN_M_ACK   0xE1
#define EN_M_WRAP  0xE2

typedef void (*EnHandler)(const esp_now_recv_info_t*, const uint8_t*, int);
#define EN_MAX 8
static EnHandler en_handlers[EN_MAX];
static int  en_count = 0;
static bool en_inited = false;

// ---------------- peer registry ----------------
#define EN_PEERS_MAX 6
struct EnPeer {
  uint8_t  mac[6];
  char     id[5];
  char     name[17];
  int8_t   rssi;
  uint32_t lastSeen;     // millis() of last packet (0 = never this boot)
};
static EnPeer  g_enPeers[EN_PEERS_MAX];
static int     g_enPeerN = 0;

static bool enPeerOnline(const EnPeer& p) {
  return p.lastSeen && millis() - p.lastSeen < 15000;
}

struct EnHelloPkt { uint8_t magic; char id[5]; char name[16]; } __attribute__((packed));

static void enAddEspNowPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t pi = {};
  memcpy(pi.peer_addr, mac, 6);
  // Use whichever interface is up: STA when joined to home Wi-Fi (we drop the
  // softAP then), else the softAP.
  pi.channel = 0;
  pi.ifidx   = (WiFi.getMode() & WIFI_STA) ? WIFI_IF_STA : WIFI_IF_AP;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
}

// Persist the registry as "AABBCCDDEEFF,id,name;..." under one NVS key.
static void enPeersSave() {
  String s;
  for (int i = 0; i < g_enPeerN; i++) {
    char mb[13];
    snprintf(mb, sizeof(mb), "%02X%02X%02X%02X%02X%02X",
             g_enPeers[i].mac[0], g_enPeers[i].mac[1], g_enPeers[i].mac[2],
             g_enPeers[i].mac[3], g_enPeers[i].mac[4], g_enPeers[i].mac[5]);
    if (i) s += ';';
    s += mb; s += ','; s += g_enPeers[i].id; s += ','; s += g_enPeers[i].name;
  }
  setPutStr("en_peers", s);
}

static void enPeersLoad() {
  String s = setGetStr("en_peers", "");
  g_enPeerN = 0;
  int i = 0, n = s.length();
  while (i < n && g_enPeerN < EN_PEERS_MAX) {
    int e = s.indexOf(';', i); if (e < 0) e = n;
    int c1 = s.indexOf(',', i), c2 = (c1 >= 0) ? s.indexOf(',', c1 + 1) : -1;
    if (c1 > 0 && c2 > 0 && c2 < e && c1 - i == 12) {
      EnPeer& p = g_enPeers[g_enPeerN];
      for (int b = 0; b < 6; b++)
        p.mac[b] = (uint8_t) strtoul(s.substring(i + b*2, i + b*2 + 2).c_str(), nullptr, 16);
      s.substring(c1 + 1, c2).toCharArray(p.id, sizeof(p.id));
      s.substring(c2 + 1, e).toCharArray(p.name, sizeof(p.name));
      p.rssi = -127; p.lastSeen = 0;
      enAddEspNowPeer(p.mac);
      g_enPeerN++;
    }
    i = e + 1;
  }
}

// Upsert a peer from a received HELLO. Returns true if it was new.
static bool enPeerNote(const uint8_t* mac, const char* id, const char* name, int rssi) {
  for (int i = 0; i < g_enPeerN; i++) {
    if (memcmp(g_enPeers[i].mac, mac, 6) == 0) {
      bool changed = strncmp(g_enPeers[i].name, name, 16) != 0;
      strncpy(g_enPeers[i].id, id, 4); g_enPeers[i].id[4] = 0;
      strncpy(g_enPeers[i].name, name, 16); g_enPeers[i].name[16] = 0;
      g_enPeers[i].rssi = rssi; g_enPeers[i].lastSeen = millis();
      if (changed) enPeersSave();
      return false;
    }
  }
  // new peer; if full, evict the longest-unseen entry
  int slot = g_enPeerN;
  if (slot >= EN_PEERS_MAX) {
    slot = 0;
    for (int i = 1; i < EN_PEERS_MAX; i++)
      if (g_enPeers[i].lastSeen < g_enPeers[slot].lastSeen) slot = i;
  } else g_enPeerN++;
  EnPeer& p = g_enPeers[slot];
  memcpy(p.mac, mac, 6);
  strncpy(p.id, id, 4); p.id[4] = 0;
  strncpy(p.name, name, 16); p.name[16] = 0;
  p.rssi = rssi; p.lastSeen = millis();
  enAddEspNowPeer(p.mac);
  enPeersSave();
  return true;
}

static void enPeerForget(int idx) {
  if (idx < 0 || idx >= g_enPeerN) return;
  esp_now_del_peer(g_enPeers[idx].mac);
  for (int i = idx; i < g_enPeerN - 1; i++) g_enPeers[i] = g_enPeers[i + 1];
  g_enPeerN--;
  enPeersSave();
}

// ---------------- reliable unicast (ACK + retry) ----------------
#define EN_PEND_MAX   4
#define EN_RETRY_MS   140
#define EN_RETRIES    3
struct EnPending {
  bool     used;
  uint8_t  mac[6];
  uint16_t seq;
  uint8_t  buf[250];
  uint8_t  len;          // wrapped length
  uint8_t  tries;
  uint32_t sentAt;
};
static EnPending g_enPend[EN_PEND_MAX];
static uint16_t  g_enSeq = 1;

// rx dedupe: remember the last few (mac, seq) we already delivered
struct EnSeen { uint8_t mac[6]; uint16_t seq; };
static EnSeen g_enSeen[12];
static int    g_enSeenW = 0;

static bool enSeenBefore(const uint8_t* mac, uint16_t seq) {
  for (int i = 0; i < 12; i++)
    if (g_enSeen[i].seq == seq && memcmp(g_enSeen[i].mac, mac, 6) == 0) return true;
  memcpy(g_enSeen[g_enSeenW].mac, mac, 6);
  g_enSeen[g_enSeenW].seq = seq;
  g_enSeenW = (g_enSeenW + 1) % 12;
  return false;
}

static void enAckClear(const uint8_t* mac, uint16_t seq) {
  for (int i = 0; i < EN_PEND_MAX; i++)
    if (g_enPend[i].used && g_enPend[i].seq == seq &&
        memcmp(g_enPend[i].mac, mac, 6) == 0) g_enPend[i].used = false;
}

// ---------------- dispatcher ----------------
static void enDispatchPayload(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  for (int i = 0; i < en_count; i++) en_handlers[i](info, data, len);
}

static void enDispatch(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < 1) return;
  const uint8_t* src = info ? info->src_addr : nullptr;
  int rssi = (info && info->rx_ctrl) ? info->rx_ctrl->rssi : -127;

  if (data[0] == EN_M_HELLO && len >= (int)sizeof(EnHelloPkt) && src) {
    const EnHelloPkt* h = (const EnHelloPkt*)data;
    if (strncmp(h->id, g_devId, 4) != 0) {
      char id[5], nm[17];
      strncpy(id, h->id, 4); id[4] = 0;
      strncpy(nm, h->name, 16); nm[16] = 0;
      enPeerNote(src, id, nm, rssi);
    }
    return;
  }
  if (data[0] == EN_M_ACK && len >= 3 && src) {
    enAckClear(src, (uint16_t)(data[1] | (data[2] << 8)));
    return;
  }
  if (data[0] == EN_M_WRAP && len >= 4 && src) {
    uint16_t seq = (uint16_t)(data[1] | (data[2] << 8));
    uint8_t ack[3] = { EN_M_ACK, data[1], data[2] };
    enAddEspNowPeer(src);
    esp_now_send(src, ack, 3);               // always ack (even duplicates)
    if (enSeenBefore(src, seq)) return;       // retry -> already delivered
    enDispatchPayload(info, data + 3, len - 3);
    return;
  }
  // plain legacy broadcast packet
  enDispatchPayload(info, data, len);
}

static void espnowAddHandler(EnHandler h) {
  for (int i = 0; i < en_count; i++) if (en_handlers[i] == h) return;
  if (en_count < EN_MAX) en_handlers[en_count++] = h;
}

static void espnowBegin() {
  if (en_inited) return;
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(enDispatch);
  enAddEspNowPeer(EN_BCAST);
  enPeersLoad();
  en_inited = true;
}

// fire-and-forget broadcast (fast path: draw strokes, buddy beacons)
static void espnowSend(const uint8_t* d, int len) {
  if (en_inited) esp_now_send(EN_BCAST, d, len);
}

// Reliable delivery: unicast-with-ACK to every known peer. Returns the number
// of peers targeted; 0 = no peers known yet (fell back to one broadcast).
static int espnowSendReliable(const uint8_t* d, int len) {
  if (!en_inited || len > 247) return 0;
  int targeted = 0;
  for (int i = 0; i < g_enPeerN; i++) {
    int slot = -1;
    for (int s = 0; s < EN_PEND_MAX; s++) if (!g_enPend[s].used) { slot = s; break; }
    if (slot < 0) break;                       // table full; remaining peers skipped
    EnPending& p = g_enPend[slot];
    p.used = true; p.tries = 1; p.sentAt = millis();
    p.seq = g_enSeq++;
    memcpy(p.mac, g_enPeers[i].mac, 6);
    p.buf[0] = EN_M_WRAP; p.buf[1] = p.seq & 0xFF; p.buf[2] = p.seq >> 8;
    memcpy(p.buf + 3, d, len);
    p.len = len + 3;
    esp_now_send(p.mac, p.buf, p.len);
    targeted++;
  }
  if (!targeted) espnowSend(d, len);           // nobody discovered yet
  return targeted;
}

// Call every main-loop iteration: HELLO beacons + pending retries.
static void espnowLoop() {
  if (!en_inited) return;
  uint32_t now = millis();

  static uint32_t lastHello = 0;
  if (now - lastHello > 5000) {
    lastHello = now;
    EnHelloPkt h = {};
    h.magic = EN_M_HELLO;
    strncpy(h.id, g_devId, 5);
    deviceName().toCharArray(h.name, sizeof(h.name));
    esp_now_send(EN_BCAST, (uint8_t*)&h, sizeof(h));
  }

  for (int i = 0; i < EN_PEND_MAX; i++) {
    EnPending& p = g_enPend[i];
    if (!p.used || now - p.sentAt < EN_RETRY_MS) continue;
    if (p.tries >= EN_RETRIES) { p.used = false; continue; }   // gave up
    p.tries++; p.sentAt = now;
    esp_now_send(p.mac, p.buf, p.len);
  }
}
