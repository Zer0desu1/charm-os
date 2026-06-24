// ============================================================================
// mod_finder.h - Module 12: Bulucu (Finder, AirTag-style)
//   Expo-Go-friendly approach: the phone connects to this device's Wi-Fi AP,
//   and the DEVICE reads the connected station's RSSI -> shows how close the
//   phone is. The phone app shows the same RSSI (via /status) + a "Caldir"
//   button (/find -> device flashes & shows a big ring) + saves the phone's
//   last-seen GPS for an out-of-range "last location".
//
//   (A BLE iBeacon can be added for true tag use, but BLE scanning on the
//    phone needs a dev-build, not Expo Go - so we use Wi-Fi RSSI here.)
//   Tap = toggle a loud visual "here I am" beacon. Long-press = home.
// ============================================================================
#pragma once

#include "platform.h"
#include <WiFi.h>
#include "esp_wifi.h"

static uint32_t finder_findUntil = 0;     // set by /find -> attention flash
static bool     finder_beacon = false;    // local toggle

static void finderFindMe(uint32_t ms = 6000) { finder_findUntil = millis() + ms; }

// RSSI of the (first) connected station, or -127 if none.
static int finderStationRssi() {
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK) return -127;
  if (staList.num == 0) return -127;
  int best = -127;
  for (int i = 0; i < staList.num; i++)
    if (staList.sta[i].rssi > best) best = staList.sta[i].rssi;
  return best;
}

static void finder_enter() {}

static void finder_tick() {
  if (g_g.tap) finder_beacon = !finder_beacon;
  uint32_t now = millis();

  // attention flash + loud locator beep, from phone "Caldir" (timed) OR the
  // local tap-toggled beacon (stays on until tapped again).
  if (now < finder_findUntil || finder_beacon) {
    bool on = ((now / 120) % 2) == 0;
    gfx->fillScreen(on ? C_TEXT : C_WARN);
    textCenter("BURADAYIM", CXi, CYi, 3, on ? C_BG : C_TEXT);
    if (finder_beacon) textCenter("dokun: durdur", CXi, 430, 2, on ? C_BG : C_TEXT);
    present();
    static uint32_t finder_lastBeep = 0;
    if (now - finder_lastBeep > 260) { soundPlay(2000, 110); finder_lastBeep = now; }  // ungated alarm
    delay(20); return;
  }

  int rssi = finderStationRssi();
  bool connected = rssi > -127;
  float c = constrain((float)(rssi + 90) / 50.0f, 0.0f, 1.0f);

  gfx->fillScreen(C_BG);
  textCenter("BULUCU", CXi, 36, 2, C_DIM);

  if (!connected) {
    if (qrAvailable() && g_wifiQR[0]) {
      textCenter("Okut, baglan", CXi, 70, 2, C_DIM);
      qrDraw(g_wifiQR, CXi, 250, 7);
      textCenter("telefon bagli degil", CXi, 430, 2, C_DIM);
    } else {
      textCenter("telefon", CXi, CYi - 20, 2, C_DIM);
      textCenter("bagli degil", CXi, CYi + 12, 2, C_DIM);
      char ssid[24]; snprintf(ssid, sizeof(ssid), "Vecta-%s", g_devId);
      textCenter(ssid, CXi, 430, 2, C_ACCENT);
    }
    present(); delay(120); return;
  }

  // concentric "radar" rings; inner filled by closeness
  uint16_t col = c > 0.66f ? C_OK : (c > 0.33f ? C_WARN : C_DANGER);
  float pulse = 0.5f + 0.5f * sinf(now * (0.004f + 0.01f * c));
  int r = 50 + (int)((100 + 60 * c) * (0.7f + 0.3f * pulse));
  gfx->drawCircle(CXi, CYi, 200, C_BG2);
  gfx->drawCircle(CXi, CYi, 150, C_BG2);
  gfx->fillCircle(CXi, CYi, r, col);
  gfx->fillCircle(CXi, CYi, max(0, r - 16), C_BG);
  gfx->fillCircle(CXi, CYi, max(0, r - 16), col);

  const char* word = c > 0.66f ? "COK YAKIN" : (c > 0.33f ? "YAKIN" : "UZAK");
  textCenter(word, CXi, CYi, 3, C_BG);
  textCenter("dokun: isaret ver", CXi, 440, 2, C_DIM);
  present();
  delay(40);
}
