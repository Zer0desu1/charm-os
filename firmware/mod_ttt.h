// ============================================================================
// mod_ttt.h - Tic-Tac-Toe (XOX) over ESP-NOW.
//   3x3 grid. Lower-id device plays X (starts), the other plays O. Tap a
//   cell to place your mark; opponent's mark arrives instantly.
//   Tap board area after game-over = reset (both sides).
// ============================================================================
#pragma once
#include "platform.h"
#include "espnow.h"

#define TTT_MAGIC 0xE3
enum { TTT_PLACE = 1, TTT_RESET = 2, TTT_PRESENCE = 3 };

struct TttPkt {
  uint8_t  magic;
  uint8_t  kind;
  uint8_t  cell;        // 0..8
  uint8_t  mark;        // 1=X, 2=O
  char     id[5];
};

static uint8_t ttt_board[9];
static bool    ttt_imX = true;       // role
static bool    ttt_myTurn = false;
static bool    ttt_peer = false;
static char    ttt_peerId[5] = "----";
static int     ttt_winner = 0;       // 0=none, 1=X, 2=O, 3=draw
static uint32_t ttt_lastPresence = 0;

static void tttReset() {
  for (int i = 0; i < 9; i++) ttt_board[i] = 0;
  ttt_winner = 0; ttt_myTurn = ttt_imX;
}

static int tttCheck() {
  static const int L[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
  for (int i = 0; i < 8; i++)
    if (ttt_board[L[i][0]] && ttt_board[L[i][0]] == ttt_board[L[i][1]] && ttt_board[L[i][0]] == ttt_board[L[i][2]])
      return ttt_board[L[i][0]];
  for (int i = 0; i < 9; i++) if (!ttt_board[i]) return 0;
  return 3;
}

static void tttHandle(const esp_now_recv_info_t*, const uint8_t* d, int len) {
  if (len < (int)sizeof(TttPkt) || d[0] != TTT_MAGIC) return;
  const TttPkt* p = (const TttPkt*)d;
  if (strncmp(p->id, g_devId, 4) == 0) return;
  ttt_peer = true; strncpy(ttt_peerId, p->id, 5);
  // role assignment: smaller id = X
  ttt_imX = strncmp(g_devId, p->id, 4) < 0;
  // Sync the opening turn once roles are known and the board is still empty,
  // so O waits for X instead of both starting as "my turn".
  bool empty = true; for (int i = 0; i < 9; i++) if (ttt_board[i]) { empty = false; break; }
  if (empty && ttt_winner == 0) ttt_myTurn = ttt_imX;
  if (p->kind == TTT_PLACE && ttt_winner == 0 && p->cell < 9 && !ttt_board[p->cell]) {
    ttt_board[p->cell] = p->mark;
    ttt_winner = tttCheck();
    ttt_myTurn = (ttt_winner == 0);
  } else if (p->kind == TTT_RESET) {
    tttReset();
  }
}

static void tttSend(uint8_t kind, uint8_t cell = 0, uint8_t mark = 0) {
  TttPkt p = { TTT_MAGIC, kind, cell, mark, {0} };
  strncpy(p.id, g_devId, 5);
  espnowSend((uint8_t*)&p, sizeof(p));
}

static void ttt_enter() {
  espnowBegin(); espnowAddHandler(tttHandle);
  tttReset(); ttt_peer = false; ttt_myTurn = true;     // assume X until peer announces
}

static void ttt_tick() {
  uint32_t now = millis();
  if (now - ttt_lastPresence > 600) { tttSend(TTT_PRESENCE); ttt_lastPresence = now; }

  // Tap input
  if (g_g.tap) {
    if (ttt_winner != 0) { tttReset(); tttSend(TTT_RESET); }
    else if (ttt_myTurn && ttt_peer) {
      int gx = -1, gy = -1;
      int x0 = CXi - 135, y0 = CYi - 135, cs = 90;
      int rx = g_g.x - x0, ry = g_g.y - y0;
      if (rx >= 0 && rx < 3 * cs && ry >= 0 && ry < 3 * cs) {
        gx = rx / cs; gy = ry / cs;
        int cell = gy * 3 + gx;
        if (!ttt_board[cell]) {
          uint8_t mark = ttt_imX ? 1 : 2;
          ttt_board[cell] = mark;
          tttSend(TTT_PLACE, cell, mark);
          ttt_winner = tttCheck();
          ttt_myTurn = false;
        }
      }
    }
  }

  // render
  gfx->fillScreen(C_BG);
  textCenter("XOX", CXi, 26, 2, C_DIM);
  if (!ttt_peer) {
    textCenter("rakip araniyor...", CXi, CYi, 2, C_DIM);
    present(); delay(60); return;
  }
  textCenter(ttt_imX ? "sen: X" : "sen: O", CXi, 60, 2, ttt_imX ? C_ACCENT : C_WARN);

  int x0 = CXi - 135, y0 = CYi - 135, cs = 90;
  // grid lines
  for (int i = 1; i < 3; i++) {
    gfx->drawLine(x0 + i * cs, y0,            x0 + i * cs, y0 + 3 * cs, C_DIM);
    gfx->drawLine(x0,            y0 + i * cs, x0 + 3 * cs, y0 + i * cs, C_DIM);
  }
  // marks
  for (int i = 0; i < 9; i++) {
    int cx = x0 + (i % 3) * cs + cs / 2;
    int cy = y0 + (i / 3) * cs + cs / 2;
    if (ttt_board[i] == 1) {
      gfx->drawLine(cx - 28, cy - 28, cx + 28, cy + 28, C_ACCENT);
      gfx->drawLine(cx - 28, cy + 28, cx + 28, cy - 28, C_ACCENT);
      gfx->drawLine(cx - 27, cy - 28, cx + 29, cy + 28, C_ACCENT);
    } else if (ttt_board[i] == 2) {
      gfx->drawCircle(cx, cy, 28, C_WARN);
      gfx->drawCircle(cx, cy, 27, C_WARN);
    }
  }
  if (ttt_winner) {
    const char* msg = ttt_winner == 3 ? "BERABERE"
      : (ttt_winner == 1 && ttt_imX) || (ttt_winner == 2 && !ttt_imX) ? "KAZANDIN" : "KAYBETTIN";
    uint16_t col = ttt_winner == 3 ? C_GOLD
      : (strcmp(msg, "KAZANDIN") == 0 ? C_OK : C_DANGER);
    textCenter(msg, CXi, 430, 3, col);
  } else {
    textCenter(ttt_myTurn ? "senin sira" : "rakibin sirasi", CXi, 430, 2, C_DIM);
  }
  present(); delay(30);
}
