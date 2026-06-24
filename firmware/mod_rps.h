// ============================================================================
// mod_rps.h - Tas Kagit Makas (Rock-Paper-Scissors) over ESP-NOW
//   Two devices on the same Wi-Fi channel auto-pair. Tap left/center/right
//   third to pick rock/paper/scissors, then tap bottom to submit. Reveals
//   when both have submitted. Long-press = home.
// ============================================================================
#pragma once
#include "platform.h"
#include "espnow.h"

#define RPS_MAGIC 0xE1
struct RpsPkt {
  uint8_t magic;        // RPS_MAGIC
  uint8_t kind;         // 0=presence, 1=submit, 2=reset
  uint8_t sel;          // 0=rock, 1=paper, 2=scissors (only if submit)
  char    id[5];
};

static int  rps_mySel = 0;
static bool rps_myReady = false;
static bool rps_peerSeen = false;
static char rps_peerId[5] = "----";
static bool rps_peerReady = false;
static int  rps_peerSel = 0;
static uint32_t rps_lastBroadcast = 0;
static uint32_t rps_revealAt = 0;          // 0 = waiting
static int  rps_score_me = 0, rps_score_peer = 0;

static void rpsHandle(const esp_now_recv_info_t*, const uint8_t* d, int len) {
  if (len < (int)sizeof(RpsPkt) || d[0] != RPS_MAGIC) return;
  const RpsPkt* p = (const RpsPkt*)d;
  if (strncmp(p->id, g_devId, 4) == 0) return;
  rps_peerSeen = true; strncpy(rps_peerId, p->id, 5);
  if (p->kind == 1) { rps_peerReady = true; rps_peerSel = p->sel; }
  else if (p->kind == 2) { rps_peerReady = false; rps_myReady = false; rps_revealAt = 0; }
}

static void rpsSend(uint8_t kind, uint8_t sel = 0) {
  RpsPkt p = { RPS_MAGIC, kind, sel, {0} };
  strncpy(p.id, g_devId, 5);
  espnowSend((uint8_t*)&p, sizeof(p));
}

static void rps_enter() {
  espnowBegin(); espnowAddHandler(rpsHandle);
  rps_mySel = 0; rps_myReady = false; rps_peerReady = false; rps_revealAt = 0;
}

static const char* rpsSelName(int s) { return s == 0 ? "TAS" : (s == 1 ? "KAGIT" : "MAKAS"); }
static const char* rpsSelEmoji(int s) { return s == 0 ? "[]" : (s == 1 ? "==" : "><"); }

static int rpsWinner(int a, int b) {     // 0=draw, 1=a wins, -1=b wins
  if (a == b) return 0;
  if ((a + 1) % 3 == b) return -1;       // b beats a
  return 1;
}

static void rps_tick() {
  uint32_t now = millis();
  // periodic presence
  if (now - rps_lastBroadcast > 600) { rpsSend(0); rps_lastBroadcast = now; }

  // input
  if (g_g.tap) {
    if (rps_revealAt > 0) {
      // tap after reveal -> reset for next round
      rps_revealAt = 0; rps_myReady = false; rps_peerReady = false;
      rpsSend(2);
    } else if (!rps_myReady) {
      // bottom row = submit
      if (g_g.y > 360) { rps_myReady = true; rpsSend(1, rps_mySel); }
      else if (g_g.x < LCD_W / 3) rps_mySel = 0;
      else if (g_g.x > 2 * LCD_W / 3) rps_mySel = 2;
      else rps_mySel = 1;
    }
  }
  // both ready -> schedule reveal
  if (rps_myReady && rps_peerReady && rps_revealAt == 0) {
    rps_revealAt = now + 600;
    int w = rpsWinner(rps_mySel, rps_peerSel);
    if (w > 0) rps_score_me++;
    else if (w < 0) rps_score_peer++;
  }

  // render
  gfx->fillScreen(C_BG);
  drawBatteryIcon(LCD_W - 56, 16);
  textCenter("TKM", CXi, 22, 2, C_DIM);

  if (!rps_peerSeen) {
    textCenter("rakip araniyor...", CXi, CYi, 2, C_DIM);
    present(); delay(60); return;
  }

  char sb[24]; snprintf(sb, sizeof(sb), "Sen %d - %d %s", rps_score_me, rps_score_peer, rps_peerId);
  textCenter(sb, CXi, 60, 2, C_DIM);

  if (rps_revealAt > 0 && now >= rps_revealAt) {
    int w = rpsWinner(rps_mySel, rps_peerSel);
    textCenter(rpsSelEmoji(rps_mySel), CXi - 90, CYi, 5, C_ACCENT);
    textCenter("vs",                   CXi,      CYi, 3, C_DIM);
    textCenter(rpsSelEmoji(rps_peerSel),CXi + 90, CYi, 5, C_WARN);
    textCenter(w > 0 ? "KAZANDIN" : w < 0 ? "KAYBETTIN" : "BERABERE",
               CXi, CYi + 80, 3, w > 0 ? C_OK : w < 0 ? C_DANGER : C_GOLD);
    textCenter("dokun: yeni el", CXi, 430, 2, C_DIM);
  } else {
    // selection row
    for (int i = 0; i < 3; i++) {
      int x = (i == 0 ? CXi - 130 : i == 1 ? CXi : CXi + 130);
      bool sel = (rps_mySel == i);
      gfx->fillCircle(x, CYi, 60, sel ? C_ACCENT : C_BG2);
      textCenter(rpsSelEmoji(i), x, CYi - 8, 4, sel ? C_BG : C_TEXT);
    }
    textCenter(rps_myReady ? "GONDERILDI" : "alta dokun: gonder", CXi, 400, 2, rps_myReady ? C_OK : C_DIM);
    textCenter(rps_peerReady ? "rakip hazir" : "rakip seciyor...", CXi, 430, 2, C_DIM);
  }
  present(); delay(30);
}
