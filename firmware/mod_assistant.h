// ============================================================================
// mod_assistant.h - Voice AI assistant (Sesli Asistan)
//   Phone-relayed: the phone app taps "Konus", the device records ~4s from its
//   mic (GET /assistant/rec returns a WAV), the phone sends it to Gemini and
//   POSTs the reply back (/assistant/say). The device shows the reply; the
//   phone reads it aloud. (Device-speaker TTS can be added later.)
// ============================================================================
#pragma once

#include "platform.h"
#include "audio.h"

static int      asst_state = 0;          // 0 idle, 1 listening, 2 thinking, 3 answer
static char     asst_heard[160] = "";    // what the user said (from Gemini)
static char     asst_reply[256] = "";
static uint32_t asst_at = 0;

// Recording captured when the user taps the device screen; the phone polls
// GET /assistant/audio, fetches this WAV, asks Gemini, then /say + /play.
static uint8_t* g_asstWav = nullptr;
static size_t   g_asstWavLen = 0;
static bool     g_asstReady = false;

static void assistantSetState(int s) { asst_state = s; asst_at = millis(); }
static void assistantSetConv(const String& heard, const String& reply) {
  heard.toCharArray(asst_heard, sizeof(asst_heard));
  reply.toCharArray(asst_reply, sizeof(asst_reply));
  asst_state = 3; asst_at = millis();
}

static void assistant_enter() { asst_state = 0; }

// Greedy word-wrap, centered, size 2. Returns the next free y.
static int asstDrawWrapped(const String& s, int y, uint16_t col) {
  int per = 22, i = 0, len = s.length();
  while (i < len && y < 430) {
    int end = min(i + per, len);
    if (end < len) { int sp = end; while (sp > i && s[sp] != ' ') sp--; if (sp > i) end = sp; }
    textCenter(s.substring(i, end).c_str(), CXi, y, 2, col);
    y += 28;
    i = (end < len && s[end] == ' ') ? end + 1 : end;
  }
  return y;
}

static void assistant_tick() {
  uint32_t now = millis();

  // Tap (from idle or after an answer) -> record from the device mic. The phone
  // is polling /assistant/audio and will fetch this, ask Gemini, then reply.
  if ((asst_state == 0 || asst_state == 3) && g_g.tap) {
    asst_state = 1;
    if (g_asstWav) { heap_caps_free(g_asstWav); g_asstWav = nullptr; }
    g_asstReady = false;
    // records until the user taps again (or 15 s) - so you finish your sentence
    if (audioRecordWavInteractive(&g_asstWav, &g_asstWavLen, 15)) g_asstReady = true;
    asst_state = 2;
    return;
  }

  gfx->fillScreen(C_BG);

  if (asst_state == 1) {                       // listening
    textCenter("Dinliyorum...", CXi, 120, 3, C_OK);
    float p = 0.5f + 0.5f * sinf(now * 0.009f);
    for (int r = 0; r < 4; r++)
      gfx->drawCircle(CXi, CYi + 40, 30 + r * 22 + (int)(14 * p), C_OK);
    textCenter("konus", CXi, 430, 2, C_DIM);
  } else if (asst_state == 2) {                // thinking
    textCenter("Dusunuyorum", CXi, CYi - 10, 3, C_GOLD);
    int n = (now / 300) % 4;
    for (int i = 0; i < n; i++) gfx->fillCircle(CXi - 18 + i * 18, CYi + 40, 6, C_GOLD);
  } else if (asst_state == 3) {                // answer (shows both sides)
    int y = 64;
    if (asst_heard[0]) {
      textCenter("Sen:", CXi, y, 2, C_DIM); y += 26;
      y = asstDrawWrapped(asst_heard, y, C_OK) + 12;
    }
    textCenter("Asistan:", CXi, y, 2, C_ACCENT); y += 26;
    asstDrawWrapped(asst_reply, y, C_TEXT);
    textCenter("dokun: tekrar konus", CXi, 448, 2, C_DIM);
  } else {                                     // idle
    gfx->fillCircle(CXi, CYi - 30, 46, C_BG2);
    gfx->drawCircle(CXi, CYi - 30, 46, C_ACCENT);
    gfx->fillRoundRect(CXi - 10, CYi - 55, 20, 34, 10, C_GOLD);   // mic
    gfx->fillRect(CXi - 2, CYi - 18, 4, 12, C_GOLD);
    gfx->fillRect(CXi - 12, CYi - 6, 24, 4, C_GOLD);
    textCenter("AI Asistan", CXi, CYi + 60, 3, C_GOLD);
    textCenter("DOKUN ve konus", CXi, CYi + 100, 2, C_OK);
  }
  present();
  delay(40);
}
