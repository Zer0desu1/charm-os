// ============================================================================
// mod_xiaozhi.h - XiaoZhi AI sesli asistan istemcisi.
//   Cihaz, bir XiaoZhi sunucusuna (hosted xiaozhi.me veya self-hosted
//   xiaozhi-esp32-server) WebSocket ile baglanir; mikrofon sesi Opus'a
//   kodlanip akitilir (16 kHz/60 ms), donen TTS Opus'u cozulup hoparlorden
//   calinir. STT metni, asistan cumleleri ve "emotion" ekranda gosterilir
//   (maskot yuz ifadesine cevrilir).
//
//   Kullanim: dokun = dinlemeyi baslat/bitir (manual mod). Konusma sirasinda
//   dokun = yaniti kes (abort). Long-press = ana ekran (global).
//
//   Sunucu ayari telefondan:  http://vecta.local/xiaozhi?url=ws://IP:8000/xiaozhi/v1/&token=TOKEN
//   (hosted icin wss://... adresi ve cihaz aktivasyonu gerekir.)
//
//   Gereken kutuphaneler (platformio.ini lib_deps'te):
//     links2004/WebSockets + pschatzmann/arduino-libopus
// ============================================================================
#pragma once

#include "platform.h"
#include "audio.h"
#include "mascot_fw.h"

#if defined(__has_include) && __has_include(<WebSocketsClient.h>) && __has_include(<opus.h>)
#  define HAVE_XIAOZHI 1
#endif

#ifdef HAVE_XIAOZHI
#include <WebSocketsClient.h>
#include <opus.h>

// ---- audio framing ----
#define XZ_MIC_SR    16000              // XiaoZhi mic format
#define XZ_FRAME_MS  60
#define XZ_MIC_N     (XZ_MIC_SR * XZ_FRAME_MS / 1000)        // 960 @16k
#define XZ_CAP_N     (AUD_SR    * XZ_FRAME_MS / 1000)        // 1440 @24k

enum XzState { XZ_NOCFG, XZ_CONNECTING, XZ_IDLE, XZ_LISTEN, XZ_THINK, XZ_SPEAK };

static WebSocketsClient xz_ws;
static bool         xz_started = false;
static XzState      xz_state = XZ_NOCFG;
static char         xz_session[40] = "";
static OpusEncoder* xz_enc = nullptr;
static OpusDecoder* xz_dec = nullptr;
static char         xz_stt[96]  = "";    // son taninan kullanici cumlesi
static char         xz_tts[120] = "";    // asistanin o anki cumlesi
static char         xz_emotion[16] = "neutral";
static float        xz_micLvl = 0;
static uint32_t     xz_lastDraw = 0;
static bool         xz_dirty = true;

// ---- mini JSON yardimcilari (tam parser yerine alan cekme) ----
static String xzJsonStr(const String& js, const char* key) {
  String pat = String("\"") + key + "\":\"";
  int i = js.indexOf(pat);
  if (i < 0) return "";
  i += pat.length();
  String out;
  while (i < (int)js.length()) {
    char c = js[i];
    if (c == '\\' && i + 1 < (int)js.length()) {
      char n = js[i + 1];
      if      (n == 'n') out += ' ';
      else if (n == 'u') { i += 4; }       // \uXXXX -> atla (trAscii zaten ? yapar)
      else out += n;
      i += 2;
      continue;
    }
    if (c == '"') break;
    out += c;
    i++;
  }
  return out;
}

static void xzSetState(XzState s) { xz_state = s; xz_dirty = true; }

static void xzSendListen(const char* state) {
  String m = String("{\"session_id\":\"") + xz_session +
             "\",\"type\":\"listen\",\"state\":\"" + state + "\",\"mode\":\"manual\"}";
  xz_ws.sendTXT(m);
}

static void xzHandleText(const String& js) {
  String type = xzJsonStr(js, "type");
  if (type == "hello") {
    xzJsonStr(js, "session_id").toCharArray(xz_session, sizeof(xz_session));
    xzSetState(XZ_IDLE);
  } else if (type == "stt") {
    trAscii(xzJsonStr(js, "text")).toCharArray(xz_stt, sizeof(xz_stt));
    xzSetState(XZ_THINK);
  } else if (type == "llm") {
    xzJsonStr(js, "emotion").toCharArray(xz_emotion, sizeof(xz_emotion));
    xz_dirty = true;
  } else if (type == "tts") {
    String st = xzJsonStr(js, "state");
    if (st == "start") xzSetState(XZ_SPEAK);
    else if (st == "sentence_start")
      trAscii(xzJsonStr(js, "text")).toCharArray(xz_tts, sizeof(xz_tts));
    else if (st == "stop") { xz_tts[0] = 0; xzSetState(XZ_IDLE); }
    xz_dirty = true;
  }
}

// Gelen TTS opus karesi: 24k'ya coz (opus decoder istenen Fs'e kendisi
// orneklemeyi uyarlar) ve I2S'e yaz.
static void xzHandleAudio(const uint8_t* d, size_t len) {
  if (!xz_dec || !g_audioReal) return;
  static int16_t pcm[XZ_CAP_N * 2];
  int n = opus_decode(xz_dec, d, len, pcm, XZ_CAP_N * 2, 0);
  if (n > 0) i2s_write_mono_as_stereo(pcm, n);
}

static void xzEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      // cihaz hello'su
      String h = "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
                 "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,"
                 "\"channels\":1,\"frame_duration\":60}}";
      xz_ws.sendTXT(h);
      break;
    }
    case WStype_TEXT:
      xzHandleText(String((char*)payload, len));
      break;
    case WStype_BIN:
      xzHandleAudio(payload, len);
      break;
    case WStype_DISCONNECTED:
      if (xz_state != XZ_NOCFG) xzSetState(XZ_CONNECTING);
      break;
    default: break;
  }
}

// ws[s]://host[:port]/path cozumle ve baglan
static bool xzConnect() {
  String url = setGetStr("xz_url", "");
  if (!url.length()) { xzSetState(XZ_NOCFG); return false; }
  bool ssl = url.startsWith("wss://");
  int off = ssl ? 6 : (url.startsWith("ws://") ? 5 : -1);
  if (off < 0) { xzSetState(XZ_NOCFG); return false; }
  int slash = url.indexOf('/', off);
  String hostport = (slash < 0) ? url.substring(off) : url.substring(off, slash);
  String path     = (slash < 0) ? "/" : url.substring(slash);
  int colon = hostport.indexOf(':');
  String host = (colon < 0) ? hostport : hostport.substring(0, colon);
  int port = (colon < 0) ? (ssl ? 443 : 80) : hostport.substring(colon + 1).toInt();

  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macs[18];
  snprintf(macs, sizeof(macs), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  String tok = setGetStr("xz_tok", "test-token");
  String hdr = String("Authorization: Bearer ") + tok +
               "\r\nProtocol-Version: 1\r\nDevice-Id: " + macs +
               "\r\nClient-Id: vecta-" + g_devId;
  xz_ws.setExtraHeaders(hdr.c_str());
  if (ssl) xz_ws.beginSSL(host.c_str(), port, path.c_str());
  else     xz_ws.begin(host.c_str(), port, path.c_str());
  xz_ws.onEvent(xzEvent);
  xz_ws.setReconnectInterval(4000);
  xzSetState(XZ_CONNECTING);
  return true;
}

static void xiaozhi_enter() {
  if (!g_audioReady) audioBegin();
  if (!xz_enc) {
    int err = 0;
    xz_enc = opus_encoder_create(XZ_MIC_SR, 1, OPUS_APPLICATION_VOIP, &err);
    if (xz_enc) {
      opus_encoder_ctl(xz_enc, OPUS_SET_COMPLEXITY(2));   // S3'te gercek zamanli
      opus_encoder_ctl(xz_enc, OPUS_SET_BITRATE(24000));
    }
    xz_dec = opus_decoder_create(AUD_SR, 1, &err);        // cikis dogrudan 24k
  }
  if (!xz_started) { xz_started = xzConnect(); }
  else if (xz_state == XZ_NOCFG) xzConnect();             // belki yeni ayarlandi
  xz_dirty = true;
}

// net.h: /xiaozhi?url=&token= -> kaydet + yeniden baglan
static void xiaozhiConfig(const String& url, const String& token) {
  if (url.length())   setPutStr("xz_url", url);
  if (token.length()) setPutStr("xz_tok", token);
  if (xz_started) { xz_ws.disconnect(); xz_started = false; }
}

// emotion -> maskot yuzu
static MascotFace xzFace() {
  if (xz_state == XZ_LISTEN) return F_EXCITED;
  String e = xz_emotion;
  if (e == "happy" || e == "laughing" || e == "funny" || e == "loving") return F_HAPPY;
  if (e == "sad" || e == "crying" || e == "angry") return F_SAD;
  if (e == "sleepy" || e == "relaxed") return F_SLEEPY;
  if (e == "surprised" || e == "shocked" || e == "cool") return F_EXCITED;
  return F_NEUTRAL;
}

// uzun metni satirlara bolerek ortala (max 3 satir)
static void xzWrapText(const char* s, int cy, uint16_t col) {
  const int MAXC = 30;
  char line[MAXC + 1];
  int n = strlen(s), i = 0, row = 0;
  while (i < n && row < 3) {
    int take = min(MAXC, n - i);
    if (i + take < n) {                       // kelime ortasinda kesme
      int sp = take;
      while (sp > 8 && s[i + sp] != ' ') sp--;
      if (sp > 8) take = sp;
    }
    memcpy(line, s + i, take); line[take] = 0;
    textCenter(line, CXi, cy + row * 26, 2, col);
    i += take;
    while (s[i] == ' ') i++;
    row++;
  }
}

static void xzRender() {
  gfx->fillScreen(C_BG);
  textCenter("XIAOZHI", CXi, 34, 2, C_DIM);

  if (xz_state == XZ_NOCFG) {
    textCenter("sunucu ayarli degil", CXi, 170, 2, C_WARN);
    textCenter("telefon tarayicisindan:", CXi, 220, 2, C_DIM);
    textCenter("vecta.local/xiaozhi", CXi, 252, 2, C_TEXT);
    textCenter("?url=ws://IP:8000/xiaozhi/v1/", CXi, 284, 2, C_TEXT);
    textCenter("&token=...", CXi, 316, 2, C_TEXT);
    present(); return;
  }

  // maskot: durum/duyguya gore yuz
  mascotDraw(CXi, 170, 0.62f, xzFace(), (int)(4 * sinf(millis() * 0.004f)), 0, 0);

  if (xz_state == XZ_CONNECTING) {
    textCenter("baglaniyor...", CXi, 320, 2, C_WARN);
  } else if (xz_state == XZ_LISTEN) {
    int r = 10 + (int)(xz_micLvl * 50);
    gfx->fillCircle(CXi, 330, r, C_OK);
    textCenter("dinliyorum - dokun: bitir", CXi, 400, 2, C_OK);
  } else if (xz_state == XZ_THINK) {
    if (xz_stt[0]) xzWrapText(xz_stt, 300, C_TEXT);
    textCenter("dusunuyor...", CXi, 400, 2, C_GOLD);
  } else if (xz_state == XZ_SPEAK) {
    if (xz_tts[0]) xzWrapText(xz_tts, 300, C_TEXT);
    textCenter("dokun: sustur", CXi, 400, 2, C_DIM);
  } else {
    if (xz_stt[0]) xzWrapText(xz_stt, 300, C_DIM);
    textCenter("dokun: konus", CXi, 400, 2, C_ACCENT);
  }
  present();
}

static void xiaozhi_tick() {
  xz_ws.loop();
  uint32_t now = millis();

  if (g_g.tap) {
    if (xz_state == XZ_IDLE)        { xzSendListen("start"); xzSetState(XZ_LISTEN); soundTap(); i2s_flush_rx(); }
    else if (xz_state == XZ_LISTEN) { xzSendListen("stop");  xzSetState(XZ_THINK); soundTap(); }
    else if (xz_state == XZ_SPEAK) {
      String m = String("{\"session_id\":\"") + xz_session + "\",\"type\":\"abort\"}";
      xz_ws.sendTXT(m);
      xzSetState(XZ_IDLE);
    }
  }

  // dinleme: 60 ms'lik kare oku (24k) -> 16k'ya indir -> opus -> ws
  if (xz_state == XZ_LISTEN && xz_enc && g_audioReal) {
    static int16_t cap[XZ_CAP_N];
    static int16_t mic[XZ_MIC_N];
    size_t got = 0;
    while (got < sizeof(cap)) {
      size_t m = i2s_read_mono_from_stereo((int16_t*)((uint8_t*)cap + got), sizeof(cap) - got, 80);
      if (!m) break;
      got += m;
    }
    int capN = got / 2;
    if (capN >= 3) {
      int outN = 0;                              // 3:2 basit indirgeme (24k->16k)
      for (int i = 0; i + 2 < capN && outN + 1 < XZ_MIC_N; i += 3) {
        mic[outN++] = cap[i];
        mic[outN++] = (int16_t)(((int)cap[i + 1] + cap[i + 2]) / 2);
      }
      int32_t pk = 0;
      for (int i = 0; i < outN; i += 8) { int v = mic[i]; if (v < 0) v = -v; if (v > pk) pk = v; }
      xz_micLvl = xz_micLvl * 0.6f + (pk / 32768.0f) * 0.4f;

      static uint8_t enc[400];
      int eb = opus_encode(xz_enc, mic, XZ_MIC_N > outN ? outN : XZ_MIC_N, enc, sizeof(enc));
      if (eb > 0) xz_ws.sendBIN(enc, eb);
      xz_dirty = true;                           // seviye halkasi canli kalsin
    }
  }

  if (xz_dirty || now - xz_lastDraw > 250) {
    xzRender();
    xz_dirty = false;
    xz_lastDraw = now;
  }
  if (xz_state != XZ_LISTEN) delay(10);          // dinlemede I2S okuma zaten 60ms
}

#else  // ---- kutuphaneler eksik ----
static void xiaozhiConfig(const String&, const String&) {}
static void xiaozhi_enter() {}
static void xiaozhi_tick() {
  gfx->fillScreen(C_BG);
  textCenter("XIAOZHI", CXi, 150, 3, C_GOLD);
  textCenter("kutuphane eksik:", CXi, 220, 2, C_DIM);
  textCenter("WebSockets + arduino-libopus", CXi, 255, 2, C_TEXT);
  textCenter("platformio.ini lib_deps", CXi, 290, 2, C_DIM);
  present();
  delay(200);
}
#endif
