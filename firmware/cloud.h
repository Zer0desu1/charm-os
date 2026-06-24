// ============================================================================
// cloud.h - Internet relay scaffold (Firestore over HTTPS REST).
//   Lets Vecta talk to other Vectas over the internet (unlimited range) using
//   Firebase Firestore as a free-tier relay.
//
//   This file is a SCAFFOLD: it sets up Wi-Fi STA, anonymous Firebase auth,
//   and offers cloudWrite()/cloudRead() helpers. Wiring this into the Draw,
//   Lovebox, and Buddy modules (so a drawing made here appears on a peer
//   thousands of km away) is the next step.
//
//   Setup (one-time, in the phone app):
//     1) Home Wi-Fi SSID + password   -> POST /wifi?ssid=&pass=
//     2) Firebase API key + projectId -> POST /cloud?apiKey=&project=
//   All stored in NVS so the device remembers across reboots.
//
//   The device runs WIFI_AP_STA: keeps its own AP for the phone, and joins
//   home Wi-Fi for internet. Note: ESP-NOW peers must share a channel, and
//   when STA connects, the softAP channel follows the home AP - so two
//   Vectas on different home networks lose ESP-NOW (they use cloud instead).
// ============================================================================
#pragma once

#include "platform.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

static String cloud_apiKey, cloud_projectId, cloud_room;
static String cloud_idToken;          // anonymous-auth Firebase token
static uint32_t cloud_tokenAt = 0;
static const uint32_t TOKEN_TTL = 50 * 60 * 1000;  // refresh after 50 min

static bool cloud_staJoined = false;

// Baked-in Vecta Firebase project (user only needs to pick a room name).
// Restrict the API key in Google Cloud Console + lock Firestore rules.
#define VECTA_FIREBASE_API_KEY  "AIzaSyCYAYNF-J9teGrht2WkzabiyrYaVU5hOsI"
#define VECTA_FIREBASE_PROJECT  "vecta-b6f5b"

// ---- Home Wi-Fi (development) -------------------------------------------
// Fill these in to make Vecta join YOUR network straight from boot - no AP
// setup dance, no mid-session disconnect. Leave empty to use the panel/NVS.
// Your Expo app (on the same network) then reaches Vecta at its shown IP.
#define HOME_WIFI_SSID  "VodafoneNet-303b10"
#define HOME_WIFI_PASS  "5209403306v"

static void cloudLoadConfig() {
  cloud_apiKey    = setGetStr("c_key",  VECTA_FIREBASE_API_KEY);
  cloud_projectId = setGetStr("c_proj", VECTA_FIREBASE_PROJECT);
  cloud_room      = setGetStr("c_room", g_devId);   // default room = own id
}
static void cloudSaveConfig(const String& key, const String& proj, const String& room) {
  if (key.length())  { setPutStr("c_key", key); cloud_apiKey = key; }
  if (proj.length()) { setPutStr("c_proj", proj); cloud_projectId = proj; }
  if (room.length()) { setPutStr("c_room", room); cloud_room = room; }
}

static void wifiJoinFromNvs() {
  // NVS creds (set from the AP panel /wifi) WIN, so the user can fix the network
  // without reflashing; fall back to the compile-time HOME_WIFI_* only if NVS
  // is empty. (Previously the hardcoded SSID always won and could not be fixed
  // from the phone -> "baglanmiyor" when the home network had changed.)
  String ssid = setGetStr("wifi_ssid", "");
  String pass = setGetStr("wifi_pass", "");
  if (!ssid.length()) { ssid = HOME_WIFI_SSID; pass = HOME_WIFI_PASS; }
  if (!ssid.length()) return;
  WiFi.setHostname("vecta");                 // router shows the device as "vecta"
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
  cloud_staJoined = (WiFi.status() == WL_CONNECTED);
  if (cloud_staJoined) {
    // Drop the softAP -> STA-only. The S3's ONE radio was running softAP (beacons)
    // + STA at the same time; adding BLE overloaded it and the STA link dropped
    // ("BT baglaninca WiFi kopuyor"). With BLE on we can't afford 3 radio roles,
    // so once home Wi-Fi is up we shed the AP. ESP-NOW then runs on the STA iface.
    // (If the join FAILS we stay in AP_STA so the user can recover via the AP.)
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);                      // keep modem sleep (coexistence)
    // STA now has a home-network IP. Re-announce mDNS so the phone can reach the
    // device at http://vecta.local from home Wi-Fi.
    MDNS.end();
    if (MDNS.begin("vecta")) MDNS.addService("http", "tcp", 80);
    Serial.print("[wifi] STA-only, IP="); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[wifi] home join FAIL (SSID/sifre? AP panelinden /wifi ile gir)");
  }
}
static void wifiSetCredentials(const String& ssid, const String& pass) {
  setPutStr("wifi_ssid", ssid); setPutStr("wifi_pass", pass);
}

static bool cloudConfigured() {
  return cloud_apiKey.length() && cloud_projectId.length() && cloud_staJoined;
}

// ---- Anonymous Firebase Auth (REST) ----
static bool cloudAuthRefresh() {
  if (!cloud_apiKey.length()) return false;
  WiFiClientSecure tls; tls.setInsecure();   // TODO: pin root cert for production
  HTTPClient http;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=" + cloud_apiKey;
  if (!http.begin(tls, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"returnSecureToken\":true}");
  if (code != 200) { http.end(); return false; }
  String body = http.getString(); http.end();
  int i = body.indexOf("\"idToken\":\"");
  if (i < 0) return false;
  int j = body.indexOf("\"", i + 11);
  cloud_idToken = body.substring(i + 11, j);
  cloud_tokenAt = millis();
  return true;
}
static bool cloudAuthOk() {
  if (!cloud_idToken.length() || millis() - cloud_tokenAt > TOKEN_TTL) return cloudAuthRefresh();
  return true;
}

// ---- Firestore REST: write a tiny JSON doc (string field "msg") ----
static bool cloudWrite(const String& collection, const String& jsonFields) {
  if (!cloudConfigured() || !cloudAuthOk()) return false;
  WiFiClientSecure tls; tls.setInsecure();
  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + cloud_projectId +
               "/databases/(default)/documents/" + collection;
  if (!http.begin(tls, url)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + cloud_idToken);
  String body = String("{\"fields\":") + jsonFields + "}";
  int code = http.POST(body);
  http.end();
  return code == 200;
}

// ---- Firestore REST: read latest documents (simple list) ----
static String cloudReadList(const String& collection, int pageSize = 5) {
  if (!cloudConfigured() || !cloudAuthOk()) return "";
  WiFiClientSecure tls; tls.setInsecure();
  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + cloud_projectId +
               "/databases/(default)/documents/" + collection +
               "?pageSize=" + String(pageSize) + "&orderBy=createTime desc";
  if (!http.begin(tls, url)) return "";
  http.addHeader("Authorization", "Bearer " + cloud_idToken);
  int code = http.GET();
  String body = (code == 200) ? http.getString() : String("");
  http.end();
  return body;
}

static void cloudBegin() {
  cloudLoadConfig();
  WiFi.mode(WIFI_AP_STA);                   // keep softAP for phone + join home
  // Thermal: let the STA modem sleep between beacons and drop TX power. The
  // watch sits right next to the phone, so full ~20 dBm TX just makes heat.
  WiFi.setSleep(true);                      // modem sleep (WIFI_PS_MIN_MODEM)
  WiFi.setTxPower(WIFI_POWER_13dBm);        // ~plenty for watch<->phone range
  wifiJoinFromNvs();
}

// Light helper: build Firestore field JSON for {msg:"...", from:"..."}
static String cloudFieldsMsg(const String& msg, const String& from) {
  String esc = msg; esc.replace("\"", "\\\"");
  return String("{\"msg\":{\"stringValue\":\"") + esc +
         "\"},\"from\":{\"stringValue\":\"" + from + "\"}}";
}

// ====================== Friend list (paired device IDs) ======================
// Stored in NVS as comma-separated string, e.g. "A1B2,7F9E,..."

static String friendsList() { return setGetStr("friends", ""); }

static bool friendsContains(const String& id) {
  String l = friendsList();
  if (!l.length()) return false;
  String tok = id; tok.toUpperCase();
  String list = "," + l + ",";
  return list.indexOf("," + tok + ",") >= 0;
}

static void friendsAdd(const String& id) {
  if (!id.length() || id.length() > 8) return;
  String up = id; up.toUpperCase();
  if (friendsContains(up)) return;
  String l = friendsList();
  if (l.length()) l += ",";
  l += up;
  setPutStr("friends", l);
}

static void friendsRemove(const String& id) {
  String up = id; up.toUpperCase();
  String l = friendsList();
  if (!l.length()) return;
  String wrapped = "," + l + ",";
  wrapped.replace("," + up + ",", ",");
  if (wrapped.length() >= 2) wrapped = wrapped.substring(1, wrapped.length() - 1);
  setPutStr("friends", wrapped);
}

// ====================== Cloud send queue + poll task ======================
// Pushing to cloud is HTTPS and slow (~500ms); we never block drawing for it.
// A small FreeRTOS task drains a queue and posts to Firestore. A second task
// polls room collections every few seconds and dispatches incoming events.

struct CloudOut {
  uint8_t kind;        // 1=draw points, 2=draw clear, 3=love
  char    text[160];   // payload (CSV points / love text)
  char    extra[16];   // love emoji
};
static QueueHandle_t cloud_outQ = nullptr;

// Forward declarations of dispatch hooks defined where their state lives
// (mod_draw.h, mod_lovebox.h). They are weakly used only if their headers
// were already included.
static void cloudHandleDrawPoints(const String& csv);
static void cloudHandleDrawClear();
static void cloudHandleLove(const String& text, const String& emoji);

static void cloudEnqueueDrawPoints(const String& csv) {
  if (!cloud_outQ) return;
  CloudOut o = {}; o.kind = 1; csv.toCharArray(o.text, sizeof(o.text));
  xQueueSend(cloud_outQ, &o, 0);
}
static void cloudEnqueueDrawClear() {
  if (!cloud_outQ) return;
  CloudOut o = {}; o.kind = 2; xQueueSend(cloud_outQ, &o, 0);
}
static void cloudEnqueueLove(const String& text, const String& emoji) {
  if (!cloud_outQ) return;
  CloudOut o = {}; o.kind = 3;
  text.toCharArray(o.text, sizeof(o.text));
  emoji.toCharArray(o.extra, sizeof(o.extra));
  xQueueSend(cloud_outQ, &o, 0);
}

static String esc(String s) { s.replace("\\", "\\\\"); s.replace("\"", "\\\""); return s; }

static void cloudSendDrawPoints(const String& csv) {
  String f = String("{\"from\":{\"stringValue\":\"") + g_devId +
             "\"},\"kind\":{\"stringValue\":\"points\"}," +
             "\"payload\":{\"stringValue\":\"" + esc(csv) + "\"}}";
  cloudWrite("rooms/" + cloud_room + "/draw", f);
}
static void cloudSendDrawClear() {
  String f = String("{\"from\":{\"stringValue\":\"") + g_devId +
             "\"},\"kind\":{\"stringValue\":\"clear\"}," +
             "\"payload\":{\"stringValue\":\"\"}}";
  cloudWrite("rooms/" + cloud_room + "/draw", f);
}
static void cloudSendLove(const String& text, const String& emoji) {
  String f = String("{\"from\":{\"stringValue\":\"") + g_devId +
             "\"},\"text\":{\"stringValue\":\"" + esc(text) +
             "\"},\"emoji\":{\"stringValue\":\"" + esc(emoji) + "\"}}";
  cloudWrite("rooms/" + cloud_room + "/love", f);
}

static void cloudOutTask(void*) {
  for (;;) {
    CloudOut o;
    if (xQueueReceive(cloud_outQ, &o, portMAX_DELAY) == pdTRUE) {
      if (!cloudConfigured()) continue;
      if (o.kind == 1) cloudSendDrawPoints(String(o.text));
      else if (o.kind == 2) cloudSendDrawClear();
      else if (o.kind == 3) cloudSendLove(String(o.text), String(o.extra));
    }
  }
}

// --- Polling --- tiny seen-id ring buffer to avoid replaying same doc.
#define CLOUD_SEEN_N 16
static String cloud_seen[CLOUD_SEEN_N];
static int    cloud_seenAt = 0;

static bool seenAdd(const String& id) {
  for (int i = 0; i < CLOUD_SEEN_N; i++) if (cloud_seen[i] == id) return false;
  cloud_seen[cloud_seenAt] = id;
  cloud_seenAt = (cloud_seenAt + 1) % CLOUD_SEEN_N;
  return true;
}

// Extract a JSON string field value (very small, line-oriented). Returns "".
static String jsonStr(const String& src, int start, const String& key) {
  int i = src.indexOf("\"" + key + "\"", start);
  if (i < 0) return "";
  i = src.indexOf("\"stringValue\"", i);
  if (i < 0) return "";
  i = src.indexOf("\"", i + 14);
  if (i < 0) return "";
  int j = i + 1;
  String out;
  while (j < (int)src.length()) {
    char c = src[j];
    if (c == '\\' && j + 1 < (int)src.length()) { out += src[j + 1]; j += 2; continue; }
    if (c == '"') break;
    out += c; j++;
  }
  return out;
}

static void parseAndDispatch(const String& body, bool isDraw) {
  int p = 0;
  while (true) {
    int n = body.indexOf("\"name\":", p);
    if (n < 0) break;
    int q1 = body.indexOf("\"", n + 7);
    int q2 = body.indexOf("\"", q1 + 1);
    if (q1 < 0 || q2 < 0) break;
    String docName = body.substring(q1 + 1, q2);
    int end = body.indexOf("\"name\":", q2);
    if (end < 0) end = body.length();
    String slice = body.substring(q2, end);
    String from = jsonStr(slice, 0, "from");
    if (from != g_devId && seenAdd(docName)) {
      if (isDraw) {
        String kind = jsonStr(slice, 0, "kind");
        String payload = jsonStr(slice, 0, "payload");
        if (kind == "clear") cloudHandleDrawClear();
        else if (kind == "points") cloudHandleDrawPoints(payload);
      } else {
        String text = jsonStr(slice, 0, "text");
        String emoji = jsonStr(slice, 0, "emoji");
        cloudHandleLove(text, emoji);
      }
    }
    p = end;
  }
}

static void cloudPollTask(void*) {
  // TLS ~45KB ic RAM ister; BLE aktifken cogu zaman yoktur ve her deneme
  // "SSL - Memory allocation failed" spam'i basar. Basarisizlikta geri cekil
  // (5s -> 10s -> ... -> 5dk), basarida normal tempoya don.
  uint32_t waitMs = 5000;
  for (;;) {
    bool ok = false;
    if (cloudConfigured() && cloud_room.length() && cloudAuthOk()) {
      ok = true;
      String draw = cloudReadList("rooms/" + cloud_room + "/draw", 5);
      if (draw.length()) parseAndDispatch(draw, true);
      String love = cloudReadList("rooms/" + cloud_room + "/love", 3);
      if (love.length()) parseAndDispatch(love, false);
    }
    waitMs = ok ? 5000 : min<uint32_t>(waitMs * 2, 300000);
    vTaskDelay(pdMS_TO_TICKS(waitMs));
  }
}

static void cloudTasksStart() {
  if (!cloud_outQ) cloud_outQ = xQueueCreate(16, sizeof(CloudOut));
  xTaskCreatePinnedToCore(cloudOutTask, "cloudOut", 6144, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(cloudPollTask, "cloudPoll", 6144, nullptr, 1, nullptr, 1);
}
