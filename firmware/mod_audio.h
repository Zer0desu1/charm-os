// ============================================================================
// mod_audio.h - Module 6: Audio Visualizer
//   Circular spectrum / VU on the round display, driven by audio.h.
//   Tap = cycle visual style (bars / pulse / wave).
//   Uses the real ES8311 mic when the codec is detected, else simulated audio.
// ============================================================================
#pragma once

#include "platform.h"
#include "audio.h"

static int av_style = 0;     // 0 bars, 1 pulse, 2 wave
static const int AV_STYLES = 3;
static float av_peak[8] = {0};

static void av_enter() { audioBegin(); av_style = setGetInt("av_style", 0); }

static uint16_t hue(int i, int n) {
  // simple rainbow across i/n
  float h = (float)i / n;
  float r = 0.5f + 0.5f * cosf(6.28f * (h + 0.0f));
  float g = 0.5f + 0.5f * cosf(6.28f * (h + 0.33f));
  float b = 0.5f + 0.5f * cosf(6.28f * (h + 0.66f));
  return rgb((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

static void drawBars() {
  float bands[8]; audioBands(bands);
  gfx->fillScreen(C_BG);
  int n = 32;                  // radial bars
  for (int i = 0; i < n; i++) {
    int bi = (i * 8) / n;
    float v = bands[bi];
    float a = (TWO_PI * i) / n;
    int r0 = 70;
    int r1 = 70 + (int)(135 * v);
    int x0 = CXi + (int)(r0 * cosf(a));
    int y0 = CYi + (int)(r0 * sinf(a));
    int x1 = CXi + (int)(r1 * cosf(a));
    int y1 = CYi + (int)(r1 * sinf(a));
    uint16_t col = hue(i, n);
    // thick-ish line
    gfx->drawLine(x0, y0, x1, y1, col);
    gfx->drawLine(x0 + 1, y0, x1 + 1, y1, col);
    gfx->drawLine(x0, y0 + 1, x1, y1 + 1, col);
    gfx->fillCircle(x1, y1, 3, col);
  }
  gfx->fillCircle(CXi, CYi, 60, C_BG2);
  present();
}

static void drawPulse() {
  float l = audioLevel();
  gfx->fillScreen(C_BG);
  int rings = 5;
  for (int i = rings; i >= 1; i--) {
    int r = (int)((40 + i * 30) * (0.7f + l));
    if (r > 225) r = 225;
    uint16_t col = hue(i, rings);
    gfx->fillCircle(CXi, CYi, r, col);
  }
  gfx->fillCircle(CXi, CYi, (int)(30 + 60 * l), C_TEXT);
  present();
}

static void drawWave() {
  float bands[8]; audioBands(bands);
  gfx->fillScreen(C_BG);
  // concentric arcs whose thickness follows bands
  for (int i = 0; i < 8; i++) {
    av_peak[i] = max(av_peak[i] * 0.92f, bands[i]);
    int rIn = 30 + i * 22;
    int rOut = rIn + (int)(4 + 16 * av_peak[i]);
    fillRing(CXi, CYi, rIn, rOut, 0, 360, hue(i, 8));
  }
  present();
}

static void audio_tick() {
  if (g_g.tap) { av_style = (av_style + 1) % AV_STYLES; setPutInt("av_style", av_style); }
  switch (av_style) {
    case 0: drawBars(); break;
    case 1: drawPulse(); break;
    default: drawWave(); break;
  }
  delay(16);
}
