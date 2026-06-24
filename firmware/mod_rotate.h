// ============================================================================
// mod_rotate.h - Module: Otomatik Gecis (auto-rotation / slideshow)
//   Lists the configured rotation sequence (modules + per-step seconds, set from
//   the phone via /rotate) and starts the loop on tap. While running, the main
//   loop auto-advances to the next module after each step's dwell time and loops
//   back to the start; a long-press exits and stops the rotation.
// ============================================================================
#pragma once

#include "platform.h"

static void rotate_enter() {}

static void rotate_tick() {
  if (g_g.tap && g_rotN > 0) { g_rotReqStart = true; soundOk(); return; }

  gfx->fillScreen(C_BG);
  textCenter("OTOMATIK GECIS", CXi, 54, 3, C_GOLD);

  if (g_rotN == 0) {
    gfx->drawCircle(CXi, CYi, 40, C_BG2);
    gfx->fillTriangle(CXi - 12, CYi - 16, CXi - 12, CYi + 16, CXi + 18, CYi, C_DIM);
    textCenter("liste bos", CXi, CYi + 80, 2, C_DIM);
    textCenter("telefondan ayarla", CXi, CYi + 110, 2, C_DIM);
    present(); delay(80); return;
  }

  // sequence list: "1. Saat - 5s"
  int y = 116;
  for (int i = 0; i < g_rotN && y <= 372; i++) {
    char line[48];
    snprintf(line, sizeof(line), "%d. %s  -  %ds",
             i + 1, g_modules[g_rot[i].modIdx].name, g_rot[i].secs);
    textCenter(line, CXi, y, 2, C_TEXT);
    y += 32;
  }

  textCenter("dokun: baslat   (uzun bas: cik)", CXi, 428, 2, C_OK);
  present();
  delay(60);
}
