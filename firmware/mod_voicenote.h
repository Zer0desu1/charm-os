// ============================================================================
// mod_voicenote.h - Voice Notes / Reminders (Sesli Not)
//   Tap the red bar to record a note (tap again to stop). Notes are saved to
//   the device's flash (LittleFS) so they survive power-off. Swipe to pick a
//   note, tap the center to play it, tap SIL to delete.
//   Needs a partition with SPIFFS/LittleFS space (e.g. "Huge APP 3MB/1MB SPIFFS").
// ============================================================================
#pragma once

#include "platform.h"
#include "audio.h"
#include <LittleFS.h>

#define VN_MAX     5             // note slots
#define VN_MAX_SEC 8             // max seconds per note (8s ~ 256 KB)

static bool vn_fsOk = false;
static bool vn_slot[VN_MAX] = { false };
static int  vn_count = 0;
static int  vn_sel = 0;

static String vnPath(int i) { return "/vn" + String(i) + ".wav"; }

static void vnScan() {
  vn_count = 0;
  for (int i = 0; i < VN_MAX; i++) { vn_slot[i] = vn_fsOk && LittleFS.exists(vnPath(i)); if (vn_slot[i]) vn_count++; }
}

static void voicenote_enter() {
  if (!vn_fsOk) vn_fsOk = LittleFS.begin(true);   // mount, format if first time
  vnScan();
  vn_sel = 0;
  for (int i = 0; i < VN_MAX; i++) if (vn_slot[i]) { vn_sel = i; break; }
}

static int vnFirstFree() { for (int i = 0; i < VN_MAX; i++) if (!vn_slot[i]) return i; return -1; }

static void vnRecord() {
  if (!vn_fsOk) {
    Serial.println("[voicenote] Recording failed: LittleFS not mounted");
    return;
  }
  int slot = vnFirstFree();
  if (slot < 0) {
    Serial.println("[voicenote] Recording failed: all 5 slots are full");
    soundError();
    return;
  }
  Serial.printf("[voicenote] Starting interactive recording in slot %d...\n", slot);
  uint8_t* wav = nullptr; size_t len = 0;
  if (audioRecordWavInteractive(&wav, &len, VN_MAX_SEC) && wav && len > 44) {
    Serial.printf("[voicenote] Recording finished. Captured WAV: %d bytes. Saving to file...\n", len);
    File f = LittleFS.open(vnPath(slot), "w");
    if (f) {
      size_t w = f.write(wav, len);
      f.close();
      if (w != len) {
        Serial.println("[voicenote] File write failed: out of disk space!");
        LittleFS.remove(vnPath(slot));  // out of space -> drop partial
      } else {
        Serial.printf("[voicenote] Saved voice note successfully to %s\n", vnPath(slot).c_str());
      }
    } else {
      Serial.println("[voicenote] Failed to open voice note file for writing");
    }
  } else {
    Serial.println("[voicenote] Recording cancelled or empty buffer captured");
  }
  if (wav) heap_caps_free(wav);
  vnScan(); vn_sel = slot;
}

static void vnPlay(int i) {
  if (!vn_fsOk || !vn_slot[i]) return;
  File f = LittleFS.open(vnPath(i), "r");
  if (!f) return;
  size_t sz = f.size();
  uint8_t* buf = (uint8_t*) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (buf) { f.read(buf, sz); f.close(); audioPlayPcm(buf + 44, sz > 44 ? sz - 44 : 0); heap_caps_free(buf); }
  else f.close();
}

static void vnDelete(int i) {
  if (!vn_fsOk || !vn_slot[i]) return;
  LittleFS.remove(vnPath(i));
  vnScan();
  for (int k = 0; k < VN_MAX; k++) if (vn_slot[k]) { vn_sel = k; break; }
}

static void voicenote_tick() {
  // swipe = pick another note
  if ((g_g.swipeLeft || g_g.swipeRight) && vn_count > 0) {
    int dir = g_g.swipeLeft ? 1 : -1;
    for (int k = 0; k < VN_MAX; k++) { vn_sel = (vn_sel + dir + VN_MAX) % VN_MAX; if (vn_slot[vn_sel]) break; }
    soundTap();
  }
  if (g_g.tap) {
    if (g_g.y < 104) { soundOk(); vnRecord(); }                 // top bar: record
    else if (g_g.y > 400 && vn_count > 0) { soundError(); vnDelete(vn_sel); }  // bottom: delete
    else if (vn_count > 0) { soundTap(); vnPlay(vn_sel); }      // middle: play
  }

  gfx->fillScreen(C_BG);
  // record bar (top)
  gfx->fillRoundRect(CXi - 140, 16, 280, 72, 16, C_DANGER);
  gfx->fillCircle(CXi - 92, 52, 12, C_TEXT);                    // mic dot
  textCenter("YENI KAYIT", CXi + 10, 52, 2, C_TEXT);

  if (!vn_fsOk) {
    textCenter("Depo yok", CXi, CYi, 3, C_WARN);
    textCenter("Partition: Huge APP (SPIFFS)", CXi, CYi + 36, 2, C_DIM);
  } else if (vn_count == 0) {
    textCenter("Not yok", CXi, CYi - 10, 3, C_DIM);
    textCenter("ustteki kirmiziya dokun", CXi, CYi + 30, 2, C_DIM);
    textCenter("dokun: kaydet, tekrar dokun: bitir", CXi, CYi + 60, 2, C_DIM);
  } else {
    int ord = 0; for (int i = 0; i <= vn_sel; i++) if (vn_slot[i]) ord++;
    gfx->fillCircle(CXi, CYi - 6, 74, C_BG2);
    gfx->drawCircle(CXi, CYi - 6, 74, C_ACCENT);
    gfx->fillTriangle(CXi - 22, CYi - 38, CXi - 22, CYi + 26, CXi + 34, CYi - 6, C_OK);  // play
    char b[22]; snprintf(b, sizeof(b), "Not %d / %d", ord, vn_count);
    textCenter(b, CXi, CYi + 92, 3, C_TEXT);
    textCenter("dokun: dinle    kaydir: sec", CXi, 372, 2, C_DIM);
    gfx->fillRoundRect(CXi - 70, 408, 140, 50, 12, C_BG2);
    textCenter("SIL", CXi, 433, 2, C_DANGER);
  }
  present();
  delay(30);
}
