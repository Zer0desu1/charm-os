// ============================================================================
// mod_weather.h - Module: Hava Durumu (Weather)
//   Shows the weather for the device's CURRENT location. Location comes from the
//   phone (g_myLoc, pushed via /gps - same source the Maps/Buddy modules use).
//   Unlike Maps (phone pushes pixels), the device fetches the weather itself:
//   it has WiFi, and Open-Meteo is a free, no-API-key JSON service - matching
//   the project's no-key preference (cf. OSRM in mod_nav). The phone only needs
//   to share location once.
//
//     GET https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
//         &current=temperature_2m,relative_humidity_2m,apparent_temperature,
//                  weather_code,wind_speed_10m
//         &daily=temperature_2m_max,temperature_2m_min&timezone=auto
//
//   - Tap        -> refresh now.
//   - Long-press -> home (handled globally).
//   The screen is drawn entirely on-device (temp, condition icon, wind/humidity
//   chips, daily hi/lo); only a handful of numbers cross the network.
// ============================================================================
#pragma once

#include "platform.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

struct WeatherData {
  bool   valid;
  float  tempC;
  float  feelsC;
  float  windKph;
  int    humidity;     // %
  int    code;         // WMO weather code
  float  hiC, loC;     // daily max/min
  double lat, lon;     // location the data is for
  uint32_t at;         // millis() when fetched
};
static WeatherData g_wx = {false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static bool     wx_loading = false;   // a fetch is queued (drawn before blocking)
static bool     wx_failed  = false;   // last fetch errored
static uint32_t wx_lastTry = 0;

static const uint32_t WX_STALE_MS = 10UL * 60UL * 1000UL;   // auto-refresh after 10 min

// ---- tiny JSON helper: read the number that follows "key": ----
static bool wxNum(const String& s, const char* key, float& out) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k);
  if (i < 0) return false;
  i += k.length();
  // skip spaces, quotes, and an opening '[' (daily values arrive as arrays,
  // e.g. "temperature_2m_max":[12.3,...]) so we read the first number.
  while (i < (int)s.length() && (s[i] == ' ' || s[i] == '"' || s[i] == '[')) i++;
  int j = i;
  while (j < (int)s.length()) {
    char c = s[j];
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') j++;
    else break;
  }
  if (j == i) return false;
  out = s.substring(i, j).toFloat();
  return true;
}

// ---- WMO weather_code -> short Turkish label ----
static const char* wxLabel(int code) {
  switch (code) {
    case 0:  return "Acik";
    case 1:  return "Cogunlukla Acik";
    case 2:  return "Parcali Bulutlu";
    case 3:  return "Kapali";
    case 45: case 48: return "Sisli";
    case 51: case 53: case 55: return "Ciseliyor";
    case 56: case 57: return "Donan Cisenti";
    case 61: case 63: case 65: return "Yagmurlu";
    case 66: case 67: return "Donan Yagmur";
    case 71: case 73: case 75: case 77: return "Kar Yagisli";
    case 80: case 81: case 82: return "Saganak";
    case 85: case 86: return "Kar Saganagi";
    case 95: return "Gok Gurultulu";
    case 96: case 99: return "Dolu Firtinasi";
    default: return "Hava Durumu";
  }
}

// code category for icon choice: 0=clear 1=partly 2=cloud 3=rain 4=snow 5=storm
static int wxIconKind(int code) {
  if (code == 0 || code == 1) return 0;
  if (code == 2) return 1;
  if (code == 3 || code == 45 || code == 48) return 2;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 3;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 4;
  if (code >= 95) return 5;
  return 2;
}

// ---- icon primitives ----
static void wxSun(int cx, int cy, int r, uint16_t col) {
  for (int a = 0; a < 360; a += 45) {
    float rad = a * 3.14159f / 180.0f;
    gfx->drawLine(cx + cosf(rad) * (r + 6), cy + sinf(rad) * (r + 6),
                  cx + cosf(rad) * (r + 16), cy + sinf(rad) * (r + 16), col);
  }
  gfx->fillCircle(cx, cy, r, col);
}
static void wxCloud(int cx, int cy, uint16_t col) {
  gfx->fillCircle(cx - 18, cy + 4, 16, col);
  gfx->fillCircle(cx + 18, cy + 4, 16, col);
  gfx->fillCircle(cx, cy - 8, 22, col);
  gfx->fillRoundRect(cx - 30, cy + 2, 60, 18, 9, col);
}
static void wxDrops(int cx, int cy, uint16_t col) {
  for (int i = -1; i <= 1; i++)
    gfx->fillTriangle(cx + i * 22 - 3, cy, cx + i * 22 + 3, cy, cx + i * 22, cy + 16, col);
}
static void wxFlakes(int cx, int cy, uint16_t col) {
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * 22, y = cy + 8;
    gfx->drawLine(x - 6, y, x + 6, y, col);
    gfx->drawLine(x, y - 6, x, y + 6, col);
    gfx->drawLine(x - 4, y - 4, x + 4, y + 4, col);
    gfx->drawLine(x - 4, y + 4, x + 4, y - 4, col);
  }
}
static void wxBolt(int cx, int cy, uint16_t col) {
  gfx->fillTriangle(cx + 4, cy - 2, cx - 8, cy + 14, cx + 2, cy + 14, col);
  gfx->fillTriangle(cx - 2, cy + 2, cx + 8, cy + 2, cx - 4, cy + 20, col);
}
static void wxIcon(int cx, int cy, int kind) {
  switch (kind) {
    case 0: wxSun(cx, cy, 26, C_GOLD); break;
    case 1: wxSun(cx - 18, cy - 14, 16, C_GOLD); wxCloud(cx + 6, cy + 6, C_TEXT); break;
    case 2: wxCloud(cx, cy, C_DIM); break;
    case 3: wxCloud(cx, cy - 8, C_DIM); wxDrops(cx, cy + 22, C_ACCENT); break;
    case 4: wxCloud(cx, cy - 8, C_DIM); wxFlakes(cx, cy + 18, C_TEXT); break;
    case 5: wxCloud(cx, cy - 8, C_DIM); wxBolt(cx, cy + 16, C_GOLD); break;
  }
}

// ---- network fetch (blocking ~1-2 s; only on enter / tap / stale) ----
static bool wxFetch(double lat, double lon) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure tls; tls.setInsecure();
  HTTPClient http;
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
    String(lat, 4) + "&longitude=" + String(lon, 4) +
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m" +
    "&daily=temperature_2m_max,temperature_2m_min&timezone=auto";
  if (!http.begin(tls, url)) return false;
  http.setTimeout(8000);
  int rc = http.GET();
  String body = (rc == 200) ? http.getString() : String("");
  http.end();
  if (rc != 200 || !body.length()) return false;

  // "current":{...} then "daily":{...}; parse from each section so keys don't
  // collide (temperature_2m appears under both "current" and other blocks).
  int cs = body.indexOf("\"current\":");
  int ds = body.indexOf("\"daily\":");
  if (cs < 0) return false;
  String cur = body.substring(cs, ds > cs ? ds : body.length());
  String day = ds > 0 ? body.substring(ds) : String("");

  WeatherData w; w.valid = true; w.lat = lat; w.lon = lon; w.at = millis();
  float v;
  if (!wxNum(cur, "temperature_2m", v)) return false; w.tempC = v;
  w.feelsC   = wxNum(cur, "apparent_temperature", v) ? v : w.tempC;
  w.windKph  = wxNum(cur, "wind_speed_10m", v) ? v : 0;
  w.humidity = wxNum(cur, "relative_humidity_2m", v) ? (int)v : 0;
  w.code     = wxNum(cur, "weather_code", v) ? (int)v : -1;
  w.hiC      = wxNum(day, "temperature_2m_max", v) ? v : w.tempC;
  w.loC      = wxNum(day, "temperature_2m_min", v) ? v : w.tempC;
  g_wx = w;
  return true;
}

static void wxRunFetch() {
  wx_lastTry = millis();
  wx_failed  = !wxFetch(g_myLoc.lat, g_myLoc.lon);
  wx_loading = false;
}

static void weather_enter() {
  wx_failed = false;
  // refetch if we have a location and the data is missing / stale / for elsewhere
  bool stale = !g_wx.valid || (millis() - g_wx.at > WX_STALE_MS) ||
               (fabs(g_wx.lat - g_myLoc.lat) > 0.05 || fabs(g_wx.lon - g_myLoc.lon) > 0.05);
  if (g_myLoc.valid && stale) wx_loading = true;
}

// dark rounded chip with a small label (mirrors mapsChip styling)
static void wxChip(const char* s, int cx, int cy, uint16_t txt) {
  gfx->setFont(&FreeSansBold9pt7b); gfx->setTextSize(1);
  int16_t bx, by; uint16_t bw, bh;
  gfx->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  gfx->setFont(NULL); gfx->setTextSize(1);
  int w = bw + 26, h = 30, x = cx - w / 2, y = cy - h / 2;
  gfx->fillRoundRect(x, y, w, h, h / 2, C_BG2);
  textCenter(s, cx, cy, 2, txt);
}

static void weather_tick() {
  if (g_g.tap && g_myLoc.valid) { wx_loading = true; soundTap(); }

  // queued fetch: draw a loading frame first, then do the blocking GET so the
  // user sees feedback during the ~1-2 s request.
  if (wx_loading) {
    gfx->fillScreen(C_BG);
    wxIcon(CXi, CYi - 30, 2);
    textCenter("HAVA DURUMU", CXi, 70, 3, C_GOLD);
    textCenter("Yukleniyor...", CXi, CYi + 60, 2, C_DIM);
    present();
    wxRunFetch();
    return;
  }

  // auto-refresh when the data goes stale while the screen stays open
  if (g_wx.valid && g_myLoc.valid && millis() - g_wx.at > WX_STALE_MS &&
      millis() - wx_lastTry > 30000) {
    wx_loading = true; return;
  }

  gfx->fillScreen(C_BG);

  if (!g_wx.valid) {
    wxIcon(CXi, CYi - 30, g_myLoc.valid ? 2 : 0);
    textCenter("HAVA DURUMU", CXi, 70, 3, C_GOLD);
    if (!g_myLoc.valid) {
      textCenter("konum yok", CXi, CYi + 60, 2, C_DIM);
      textCenter("telefonda konumu paylas", CXi, CYi + 90, 2, C_DIM);
    } else if (wx_failed) {
      textCenter("baglanti hatasi", CXi, CYi + 60, 2, C_DANGER);
      textCenter("dokun -> tekrar dene", CXi, CYi + 90, 2, C_DIM);
    } else {
      textCenter("dokun -> getir", CXi, CYi + 60, 2, C_DIM);
    }
    present();
    delay(60);
    return;
  }

  // ---- weather card ----
  wxIcon(CXi, 150, wxIconKind(g_wx.code));

  char temp[12]; snprintf(temp, sizeof(temp), "%d", (int)lroundf(g_wx.tempC));
  textCenter(temp, CXi - 14, 250, 7, C_TEXT);
  textCenter("o", CXi + 46, 226, 3, C_GOLD);   // degree mark

  textCenter(wxLabel(g_wx.code), CXi, 300, 2, C_TEXT);

  char hilo[32];
  snprintf(hilo, sizeof(hilo), "Y:%d  D:%d",
           (int)lroundf(g_wx.hiC), (int)lroundf(g_wx.loC));
  textCenter(hilo, CXi, 332, 2, C_DIM);

  char wind[24]; snprintf(wind, sizeof(wind), "Ruzgar %d km/s", (int)lroundf(g_wx.windKph));
  char hum[16];  snprintf(hum,  sizeof(hum),  "Nem %%%d", g_wx.humidity);
  wxChip(wind, CXi - 78, 392, C_TEXT);
  wxChip(hum,  CXi + 78, 392, C_TEXT);

  char feels[24]; snprintf(feels, sizeof(feels), "Hissedilen %d", (int)lroundf(g_wx.feelsC));
  textCenter(feels, CXi, 430, 2, C_DIM);

  present();
  delay(60);
}
