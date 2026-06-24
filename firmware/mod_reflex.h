// ============================================================================
// mod_reflex.h - Refleks (Reaction) game over ESP-NOW.
//   2+ devices: tap to "ready". The lowest-id device acts as host: starts a
//   countdown 3-2-1 + random delay, then broadcasts GO. First to tap after
//   GO wins. Anyone tapping before GO = false start.
// ============================================================================
#pragma once
#include "platform.h"
#include "espnow.h"

#define RFX_MAGIC 0xE2
enum { RFX_READY = 1, RFX_GO = 2, RFX_TAP = 3, RFX_RESET = 4 };

struct RfxPkt {
  uint8_t magic;        // RFX_MAGIC
  uint8_t kind;
  uint32_t t;           // host-relative ms (GO/TAP)
  char    id[5];
};

static bool     rfx_imHost = true;
static bool     rfx_myReady = false;
static int      rfx_peerCount = 0;
static char     rfx_peerId[5] = "----";
static uint32_t rfx_goAtLocal = 0;       // when GO will fire (host) or fired (peer)
static uint32_t rfx_myTapAt = 0;
static uint32_t rfx_winnerAt = 0;
static char     rfx_winnerId[5] = "----";
static bool     rfx_falseStart = false;

static void rfxReset() {
  rfx_myReady = false; rfx_goAtLocal = 0; rfx_myTapAt = 0;
  rfx_winnerAt = 0; rfx_falseStart = false;
  strcpy(rfx_winnerId, "----");
}

static void rfxHandle(const esp_now_recv_info_t*, const uint8_t* d, int len) {
  if (len < (int)sizeof(RfxPkt) || d[0] != RFX_MAGIC) return;
  const RfxPkt* p = (const RfxPkt*)d;
  if (strncmp(p->id, g_devId, 4) == 0) return;
  rfx_peerCount = 1; strncpy(rfx_peerId, p->id, 5);
  // host is whoever has the smaller id (string compare)
  if (strncmp(p->id, g_devId, 4) < 0) rfx_imHost = false;
  if (p->kind == RFX_GO)    rfx_goAtLocal = millis();
  if (p->kind == RFX_TAP && rfx_winnerAt == 0) {
    rfx_winnerAt = millis(); strncpy(rfx_winnerId, p->id, 5);
  }
  if (p->kind == RFX_RESET) rfxReset();
}

static void rfxSend(uint8_t kind) {
  RfxPkt p = { RFX_MAGIC, kind, millis(), {0} };
  strncpy(p.id, g_devId, 5);
  espnowSend((uint8_t*)&p, sizeof(p));
}

static void reflex_enter() {
  espnowBegin(); espnowAddHandler(rfxHandle);
  rfx_imHost = true; rfx_peerCount = 0;
  rfxReset();
}

static void reflex_tick() {
  uint32_t now = millis();

  // host schedules GO after both ready + random delay
  if (rfx_imHost && rfx_myReady && rfx_peerCount > 0 && rfx_goAtLocal == 0 && rfx_winnerAt == 0) {
    rfx_goAtLocal = now + 1500 + (int)rnd(3500);   // 1.5 - 5s after ready
  }
  if (rfx_imHost && rfx_goAtLocal > 0 && now >= rfx_goAtLocal && rfx_winnerAt == 0) {
    // fire GO once
    static uint32_t lastFire = 0;
    if (lastFire != rfx_goAtLocal) { rfxSend(RFX_GO); lastFire = rfx_goAtLocal; }
  }

  if (g_g.tap) {
    if (rfx_winnerAt > 0) { rfxReset(); rfxSend(RFX_RESET); }
    else if (!rfx_myReady) { rfx_myReady = true; rfxSend(RFX_READY); }
    else if (rfx_goAtLocal == 0 || now < rfx_goAtLocal) {
      rfx_falseStart = true; rfx_winnerAt = now;
      strncpy(rfx_winnerId, "PEER", 5);             // we lost
    } else if (rfx_myTapAt == 0) {
      rfx_myTapAt = now;
      if (rfx_winnerAt == 0) { rfx_winnerAt = now; strncpy(rfx_winnerId, g_devId, 5); }
      rfxSend(RFX_TAP);
    }
  }

  gfx->fillScreen(C_BG);
  textCenter("REFLEKS", CXi, 26, 2, C_DIM);

  if (rfx_winnerAt > 0) {
    bool meWon = (strncmp(rfx_winnerId, g_devId, 4) == 0);
    if (rfx_falseStart) {
      textCenter("ERKEN DOKUN", CXi, CYi - 20, 3, C_DANGER);
      textCenter("kaybettin", CXi, CYi + 20, 2, C_DIM);
    } else {
      textCenter(meWon ? "KAZANDIN" : "KAYBETTIN", CXi, CYi - 10, 4, meWon ? C_OK : C_DANGER);
      if (rfx_myTapAt > 0 && rfx_goAtLocal > 0) {
        char b[24]; snprintf(b, sizeof(b), "%lu ms", (unsigned long)(rfx_myTapAt - rfx_goAtLocal));
        textCenter(b, CXi, CYi + 40, 2, C_DIM);
      }
    }
    textCenter("dokun: tekrar", CXi, 430, 2, C_DIM);
  } else if (!rfx_myReady) {
    textCenter("dokun: HAZIR", CXi, CYi, 3, C_TEXT);
  } else if (rfx_peerCount == 0) {
    textCenter("rakip bekleniyor...", CXi, CYi, 2, C_DIM);
  } else if (rfx_goAtLocal == 0 || now < rfx_goAtLocal) {
    // waiting for GO
    bool flash = (now / 250) % 2;
    gfx->fillCircle(CXi, CYi, 90, flash ? C_DANGER : C_WARN);
    textCenter("BEKLE", CXi, CYi, 4, C_BG);
    textCenter("erken dokunma", CXi, 430, 2, C_DIM);
  } else {
    gfx->fillCircle(CXi, CYi, 110, C_OK);
    textCenter("DOKUN!", CXi, CYi, 5, C_BG);
  }
  present(); delay(20);
}
