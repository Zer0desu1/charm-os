// ============================================================================
// mod_openclaw.h - OpenClaw ajan sohbeti.
//   Vecta, OpenClaw Gateway'in OpenAI-uyumlu HTTP API'sine baglanir
//   (POST <gateway>/v1/chat/completions, model="openclaw"). Soru telefonun
//   TARAYICISINDAN sorulur (uygulama gerekmez): cihazin kendi sundugu
//   http://vecta.local/claw sayfasi. Cevap hem tarayicide hem cihazin yuvarlak
//   ekraninda gosterilir; cihazda parmakla kaydirilabilir.
//
//   Gateway istegi ayri bir FreeRTOS gorevinde calisir (UI/HTTP sunucusu
//   bloklanmaz). "user" alanina sabit cihaz kimligi yazilir ki OpenClaw ayni
//   oturumu surdursun (hafizali sohbet).
//
//   Ayar: /claw sayfasindaki form veya /claw/cfg?url=http://IP:18789&token=..
//   (ESP-Claw notu: Espressif'in esp-claw'i ayri bir IDF firmware'idir; bu
//   modul ayni kullanim amacini Vecta icinde karsilar.)
// ============================================================================
#pragma once

#include "platform.h"
#include <HTTPClient.h>

static String        oc_q, oc_a;
static volatile int  oc_state = 0;        // 0 hazir, 1 dusunuyor, 2 cevap, 3 hata
static volatile bool oc_pending = false;
static TaskHandle_t  oc_task = nullptr;

// cevabi ekrana sigan satirlara bol
#define OC_MAXLINES 60
#define OC_LINECHARS 32
static char oc_lines[OC_MAXLINES][OC_LINECHARS + 1];
static int  oc_lineN = 0;
static Scroller oc_scroll;

static void ocWrapAnswer() {
  oc_lineN = 0;
  const char* s = oc_a.c_str();
  int n = strlen(s), i = 0;
  while (i < n && oc_lineN < OC_MAXLINES) {
    int take = min(OC_LINECHARS, n - i);
    int nl = -1;                                   // paragraf sonlarini koru
    for (int k = 0; k < take; k++) if (s[i + k] == '\n') { nl = k; break; }
    if (nl >= 0) take = nl;
    else if (i + take < n) {
      int sp = take;
      while (sp > 8 && s[i + sp] != ' ') sp--;
      if (sp > 8) take = sp;
    }
    memcpy(oc_lines[oc_lineN], s + i, take);
    oc_lines[oc_lineN][take] = 0;
    oc_lineN++;
    i += take;
    while (i < n && (s[i] == ' ' || s[i] == '\n')) i++;
  }
  scrollInit(oc_scroll, 0, 0, max(0, (oc_lineN - 9)) * 26.0f, false, true);
}

// "content":"..." alanini kacis karakterlerini cozerek cek
static String ocExtractContent(const String& js) {
  int i = js.indexOf("\"content\":\"");
  if (i < 0) return "";
  i += 11;
  String out;
  while (i < (int)js.length()) {
    char c = js[i];
    if (c == '\\' && i + 1 < (int)js.length()) {
      char x = js[i + 1];
      if      (x == 'n') out += '\n';
      else if (x == 't') out += ' ';
      else if (x == 'u') { out += '?'; i += 4; }
      else out += x;
      i += 2;
      continue;
    }
    if (c == '"') break;
    out += c;
    i++;
  }
  return out;
}

static String ocJsonEscape(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c >= 0x20) o += c;     // kontrol karakterlerini at
  }
  return o;
}

static void ocWorker(void*) {
  for (;;) {
    if (!oc_pending) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    oc_pending = false;

    String url = setGetStr("oc_url", "");
    if (!url.length()) { oc_a = "Gateway ayarli degil (/claw sayfasindan kaydet)"; oc_state = 3; ocWrapAnswer(); continue; }
    while (url.endsWith("/")) url = url.substring(0, url.length() - 1);

    HTTPClient http;
    http.begin(url + "/v1/chat/completions");
    http.setTimeout(65000);                       // ajan arastirma yapabilir (uint16_t tavani)
    http.addHeader("Content-Type", "application/json");
    String tok = setGetStr("oc_tok", "");
    if (tok.length()) http.addHeader("Authorization", String("Bearer ") + tok);

    String body = String("{\"model\":\"") + setGetStr("oc_model", "openclaw") +
        "\",\"user\":\"vecta-" + g_devId + "\",\"messages\":[{\"role\":\"user\",\"content\":\"" +
        ocJsonEscape(oc_q) + "\"}]}";

    int code = http.POST(body);
    if (code == 200) {
      String content = ocExtractContent(http.getString());
      if (content.length()) { oc_a = trAscii(content); oc_state = 2; }
      else { oc_a = "Bos yanit geldi"; oc_state = 3; }
    } else {
      oc_a = String("Hata: HTTP ") + code +
             (code < 0 ? String(" (") + http.errorToString(code) + ")" : String(""));
      oc_state = 3;
    }
    http.end();
    ocWrapAnswer();
    if (oc_state == 2) soundNotifyChirp();
  }
}

// /claw/ask girisi (net.h cagirir). false = mesgul.
static bool ocAsk(const String& q) {
  if (oc_state == 1) return false;
  oc_q = q; oc_a = "";
  oc_state = 1;
  oc_pending = true;
  if (!oc_task)
    xTaskCreatePinnedToCore(ocWorker, "oc_worker", 8192, nullptr, 1, &oc_task, 0);
  return true;
}

static void openclaw_enter() {
  if (oc_state == 2 || oc_state == 3) ocWrapAnswer();
}

static void openclaw_tick() {
  // cevap kaydirma
  if (g_g.down) {
    if (!oc_scroll.active) scrollGrab(oc_scroll);
    if (g_g.dragDY != 0) scrollDrag(oc_scroll, -g_g.dragDY);
  } else if (oc_scroll.active) {
    scrollRelease(oc_scroll, g_g.released ? -g_g.velY : 0.0f);
  }
  scrollUpdate(oc_scroll);

  gfx->fillScreen(C_BG);
  textCenter("OPENCLAW", CXi, 34, 2, rgb(255, 120, 80));

  if (oc_state == 0) {
    // pence logosu
    gfx->fillCircle(CXi, 150, 40, rgb(255, 120, 80));
    gfx->fillCircle(CXi - 26, 110, 18, rgb(255, 120, 80));
    gfx->fillCircle(CXi + 26, 110, 18, rgb(255, 120, 80));
    gfx->fillCircle(CXi, 142, 16, C_BG);
    textCenter("telefon tarayicisindan sor:", CXi, 260, 2, C_DIM);
    textCenter("vecta.local/claw", CXi, 295, 3, C_TEXT);
    String gw = setGetStr("oc_url", "");
    textCenter(gw.length() ? "gateway: ayarli" : "gateway: AYARSIZ", CXi, 360,
               2, gw.length() ? C_OK : C_WARN);
  } else if (oc_state == 1) {
    float t = millis() * 0.005f;
    for (int i = 0; i < 3; i++) {
      int r = 30 + ((int)(t * 40) + i * 40) % 120;
      gfx->drawCircle(CXi, 170, r, C_BG2);
    }
    textCenter("dusunuyor...", CXi, 170, 2, C_GOLD);
    char qb[36]; strncpy(qb, oc_q.c_str(), 33); qb[33] = 0;
    if (oc_q.length() > 33) strcat(qb, "..");
    textCenter(qb, CXi, 320, 2, C_DIM);
  } else {
    // soru (ust, sabit) + cevap (kaydirilabilir)
    char qb[36]; strncpy(qb, oc_q.c_str(), 33); qb[33] = 0;
    if (oc_q.length() > 33) strcat(qb, "..");
    textCenter(qb, CXi, 66, 2, C_DIM);
    int y0 = 100 - (int)oc_scroll.pos;
    uint16_t col = oc_state == 3 ? C_DANGER : C_TEXT;
    for (int i = 0; i < oc_lineN; i++) {
      int y = y0 + i * 26;
      if (y < 88 || y > 440) continue;
      textCenter(oc_lines[i], CXi, y, 2, col);
    }
  }
  present();
  delay(oc_state == 1 ? 30 : 16);
}
