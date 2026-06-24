// ============================================================================
// audio.h - ES8311 codec: microphone input + speaker output.
//   Verified pins for Waveshare ESP32-S3-Touch-AMOLED-1.32 (board name
//   "S3_AMOLED_1_32" in Waveshare's codec_board cfg):
//       i2c {sda:47, scl:48}   i2s {mclk:38, bclk:39, ws:41, din:40, dout:42}
//       ES8311 (in_out), PA enable GPIO 46, MCLK provided (256*fs).
//
//   Backends are chosen at RUNTIME:
//     * If the ES8311 ACKs on I2C -> real mic level + real tone playback.
//     * Otherwise -> simulated level so visuals still work, tone is a no-op.
//
//   I2S runs in STEREO mode because the ESP32-S3 DMA has a known bug in MONO
//   mode that causes frame-alignment glitches (robotic audio). All read/write
//   helpers convert between mono audio and stereo I2S frames in software.
// ============================================================================
#pragma once

#include "platform.h"
#include <driver/i2s_std.h>
#include <driver/gpio.h>

// ---- verified S3_AMOLED_1_32 audio pins ----
#define ES8311_ADDR  0x18
#define AUD_MCLK     38
#define AUD_BCLK     39
#define AUD_WS       41
#define AUD_DIN      40     // ADC (mic)  -> ESP
#define AUD_DOUT     42     // ESP -> DAC (speaker)
#define AUD_PA       46     // power-amplifier enable (HIGH = on)
#define AUD_SR       24000  // sample rate (mic + tone + TTS playback)
// 24 kHz: Gemini TTS'in native cikisi (app artik downsample ETMEZ) ve mikrofon
// bant genisligi 8k -> 12k'ya cikar; konusma belirgin sekilde daha net.
#define ES8311_MIC_GAIN 0x1A  // reg 0x14: mic select + PGA gain (original working value)

// Public API
void  audioBegin();
float audioLevel();                 // 0..1 smoothed loudness
void  audioBands(float* out8);      // fills 8 values 0..1
void  audioPlayTone(int freq, int ms);  // synth tone out the speaker (blocking)

// ---------------- internal state ----------------
static bool  g_audioReady = false;
static bool  g_audioReal  = false;
static String g_audioDiag = "";
static float _aLevel = 0.0f;
static float _aBands[8] = {0};
static i2s_chan_handle_t g_i2sTx = nullptr;
static i2s_chan_handle_t g_i2sRx = nullptr;
static int   g_micChannel = -1;       // -1 = auto-detect, 0 = Left, 1 = Right

// ---------------- ES8311 over I2C (shared Wire bus, 47/48) ----------------
static void es8311_w(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

static bool es8311_present() {
  Wire.beginTransmission(ES8311_ADDR);
  return Wire.endTransmission() == 0;
}

static void es8311_init() {
  es8311_w(0x00, 0x1F); delay(20);
  es8311_w(0x00, 0x00);
  es8311_w(0x0D, 0xFA);
  es8311_w(0x44, 0x08); es8311_w(0x44, 0x08);
  es8311_w(0x01, 0x30); es8311_w(0x02, 0x00); es8311_w(0x03, 0x10);
  es8311_w(0x16, 0x24); es8311_w(0x04, 0x10); es8311_w(0x05, 0x00);
  es8311_w(0x0B, 0x00); es8311_w(0x0C, 0x00);
  es8311_w(0x10, 0x1F); es8311_w(0x11, 0x7F);
  es8311_w(0x00, 0x80); es8311_w(0x01, 0x3F); es8311_w(0x06, 0x00);
  es8311_w(0x13, 0x10); es8311_w(0x1B, 0x0A); es8311_w(0x1C, 0x6A);
  es8311_w(0x44, 0x58);
  es8311_w(0x09, 0x0C); es8311_w(0x0A, 0x0C);
  es8311_w(0x02, 0x00); es8311_w(0x05, 0x00);
  es8311_w(0x03, 0x10); es8311_w(0x04, 0x20);
  es8311_w(0x07, 0x00); es8311_w(0x08, 0xFF); es8311_w(0x06, 0x03);
  es8311_w(0x00, 0x80); es8311_w(0x01, 0x3F);
  es8311_w(0x09, 0x0C); es8311_w(0x0A, 0x0C);
  es8311_w(0x17, 0xBF); es8311_w(0x0E, 0x02); es8311_w(0x12, 0x00);
  es8311_w(0x14, ES8311_MIC_GAIN); es8311_w(0x0D, 0x01);
  es8311_w(0x15, 0x40); es8311_w(0x37, 0x08); es8311_w(0x45, 0x00);
  es8311_w(0x32, 0xBF); es8311_w(0x16, 0x14); es8311_w(0x31, 0x00);
}

// ---------------- I2S (STEREO mode) ----------------
static bool i2s_init_duplex() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.dma_desc_num  = 12;    // ekstra tampon: UI cizimi DMA'yi tasirmasin
  chan.dma_frame_num = 480;
  chan.auto_clear    = true;
  esp_err_t e = i2s_new_channel(&chan, &g_i2sTx, &g_i2sRx);
  if (e != ESP_OK) { Serial.printf("[audio] i2s_new_channel FAIL 0x%x\n", e); return false; }

  i2s_std_config_t cfg = {};
  cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUD_SR);
  cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk = (gpio_num_t)AUD_MCLK;
  cfg.gpio_cfg.bclk = (gpio_num_t)AUD_BCLK;
  cfg.gpio_cfg.ws   = (gpio_num_t)AUD_WS;
  cfg.gpio_cfg.dout = (gpio_num_t)AUD_DOUT;
  cfg.gpio_cfg.din  = (gpio_num_t)AUD_DIN;

  e = i2s_channel_init_std_mode(g_i2sTx, &cfg);
  if (e != ESP_OK) { Serial.printf("[audio] init_std TX FAIL 0x%x\n", e); return false; }
  e = i2s_channel_init_std_mode(g_i2sRx, &cfg);
  if (e != ESP_OK) { Serial.printf("[audio] init_std RX FAIL 0x%x\n", e); return false; }
  i2s_channel_enable(g_i2sTx);
  i2s_channel_enable(g_i2sRx);
  Serial.println("[audio] I2S duplex OK (stereo)");
  return true;
}

static String i2c_scan() {
  String s = "";
  for (uint8_t a = 1; a < 0x7F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { char b[6]; snprintf(b, sizeof(b), "%02X ", a); s += b; }
  }
  if (!s.length()) s = "(none)";
  return s;
}

void audioBegin() {
  if (g_audioReady) return;
  g_audioReady = true;
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  String scan = i2c_scan();
  bool es = es8311_present();
  bool i2s_ok = false;
  if (es) { es8311_init(); i2s_ok = i2s_init_duplex(); pinMode(AUD_PA, OUTPUT); digitalWrite(AUD_PA, HIGH); }
  g_audioReal = es && i2s_ok && g_i2sTx && g_i2sRx;
  g_audioDiag  = "I2C: " + scan + "\n";
  g_audioDiag += String("ES8311(0x18): ") + (es ? "BULUNDU" : "YOK") + "\n";
  g_audioDiag += String("I2S: ") + (es ? (i2s_ok ? "OK" : "HATA") : "-") + "\n";
  g_audioDiag += String("Gercek ses: ") + (g_audioReal ? "EVET" : "HAYIR (sim)");
  Serial.println(g_audioDiag);
}

// ---------------------------------------------------------------------------
// Stereo helpers: I2S bus is L+R interleaved. Mic = LEFT channel.
// ---------------------------------------------------------------------------
static void i2s_write_mono_as_stereo(const int16_t* mono, int n) {
  static int16_t st[512];
  int off = 0;
  while (off < n) {
    int c = min(256, n - off);
    for (int i = 0; i < c; i++) { st[i*2] = mono[off+i]; st[i*2+1] = mono[off+i]; }
    size_t bw = 0;
    i2s_channel_write(g_i2sTx, st, c * 4, &bw, 200);
    off += c;
  }
}

static size_t i2s_read_mono_from_stereo(int16_t* dst, size_t maxMonoBytes, int tMs) {
  static int16_t st[1024];
  size_t want = min(sizeof(st), maxMonoBytes * 2);
  size_t br = 0;
  if (i2s_channel_read(g_i2sRx, st, want, &br, tMs) != ESP_OK || br == 0) return 0;
  int fr = br / 4;
  // Mix BOTH channels. The mic sits on one slot and the other is ~silent, so
  // L+R always captures the mic regardless of which slot it's on - no fragile
  // channel auto-detect (which produced silence when it guessed wrong).
  for (int i = 0; i < fr; i++) {
    int v = st[i * 2] + st[i * 2 + 1];
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    dst[i] = (int16_t)v;
  }
  return fr * 2;
}

static void i2s_flush_rx() {
  size_t br; static int16_t junk[1024]; uint32_t t = millis();
  while (millis() - t < 200) { if (i2s_channel_read(g_i2sRx, junk, sizeof(junk), &br, 10) != ESP_OK) break; }
}

// ---------------------------------------------------------------------------
// Speaker output
// ---------------------------------------------------------------------------
void audioPlayTone(int freq, int ms) {
  if (!g_audioReady) audioBegin();
  if (!g_audioReal || !g_i2sTx || freq <= 0 || ms <= 0) return;
  const int total = (int)((int64_t)AUD_SR * ms / 1000);
  static int16_t mono[256];
  float ph = 0.0f, dph = 2.0f * (float)PI * freq / AUD_SR;
  int sent = 0;
  while (sent < total) {
    int n = min((int)256, total - sent);
    for (int i = 0; i < n; i++) { mono[i] = (int16_t)(sinf(ph) * 9000.0f); ph += dph; if (ph > 2.0f*(float)PI) ph -= 2.0f*(float)PI; }
    i2s_write_mono_as_stereo(mono, n);
    sent += n;
  }
}

static void audioSpeakFrame(float lvl) {
  gfx->fillScreen(C_BG);
  uint32_t t = millis();
  int base = 46 + (int)(lvl * 90);
  gfx->fillCircle(CXi, CYi, base + 34, C_BG2);
  for (int i = 0; i < 24; i++) {
    float a = (TWO_PI * i) / 24;
    float amp = base + 18 + lvl * (40 + 30 * sinf(t * 0.02f + i));
    int x0 = CXi + (int)(base * cosf(a)),   y0 = CYi + (int)(base * sinf(a));
    int x1 = CXi + (int)(amp * cosf(a)),     y1 = CYi + (int)(amp * sinf(a));
    uint16_t col = (i%3==0) ? C_ACCENT : (i%3==1) ? rgb(120,160,255) : rgb(180,120,255);
    gfx->drawLine(x0, y0, x1, y1, col);
  }
  gfx->fillCircle(CXi, CYi, base, rgb(120, 140, 255));
  gfx->fillCircle(CXi, CYi, max(10, base - 18), C_TEXT);
  present();
}

void audioPlayPcm(const uint8_t* pcm, size_t bytes) {
  if (!g_audioReady) audioBegin();
  if (!g_audioReal || !g_i2sTx || !pcm || bytes < 2) return;
  digitalWrite(AUD_PA, HIGH);
  const int16_t* src = (const int16_t*)pcm;
  int total = bytes / 2, off = 0;
  uint32_t lastDraw = 0;
  while (off < total) {
    int c = min(512, total - off);
    i2s_write_mono_as_stereo(src + off, c);          // feed the DMA FIRST
    uint32_t nowm = millis();
    if (nowm - lastDraw > 140) {                      // throttle the animation so
      int32_t pk = 0;                                 // the speaker DMA never starves
      for (int i = 0; i < c; i += 8) { int v = src[off+i]; if (v<0) v=-v; if (v>pk) pk=v; }
      audioSpeakFrame(pk / 32768.0f);
      lastDraw = nowm;
    }
    off += c;
  }
}

// ---------------------------------------------------------------------------
// WAV recording + post-processing
// ---------------------------------------------------------------------------
static void wavHeader(uint8_t* h, uint32_t pcmBytes, uint32_t sr) {
  uint32_t byteRate = sr * 2; uint32_t chunk = 36 + pcmBytes;
  memcpy(h, "RIFF", 4);       h[4]=chunk; h[5]=chunk>>8; h[6]=chunk>>16; h[7]=chunk>>24;
  memcpy(h+8, "WAVEfmt ", 8); h[16]=16; h[17]=0; h[18]=0; h[19]=0;
  h[20]=1; h[21]=0; h[22]=1; h[23]=0;
  h[24]=sr; h[25]=sr>>8; h[26]=sr>>16; h[27]=sr>>24;
  h[28]=byteRate; h[29]=byteRate>>8; h[30]=byteRate>>16; h[31]=byteRate>>24;
  h[32]=2; h[33]=0; h[34]=16; h[35]=0;
  memcpy(h+36, "data", 4);    h[40]=pcmBytes; h[41]=pcmBytes>>8; h[42]=pcmBytes>>16; h[43]=pcmBytes>>24;
}

static void audioPostProcess(int16_t* s, int n) {
  if (n < 64) return;
  // 1) DC offset kaldir
  int64_t dcSum = 0;
  for (int i = 0; i < n; i++) dcSum += s[i];
  int16_t dc = (int16_t)(dcSum / n);
  for (int i = 0; i < n; i++) s[i] -= dc;
  // 2) HPF (~60Hz @ 24k): tik/rumble'i temizler
  const float a = 0.985f;
  float prevIn = 0, prevOut = 0;
  double sumSq = 0;
  for (int i = 0; i < n; i++) {
    float in = (float)s[i], out = a * (prevOut + in - prevIn);
    prevIn = in; prevOut = out;
    int v = (int)out; if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    s[i] = (int16_t)v;
    sumSq += (double)v * v;
  }
  // 3) RMS bazli normalizasyon + yumusak sinirlayici.
  //    Eski kod tepe degere normalize ediyordu: tek bir "tik" tum kaydi kisik
  //    birakiyordu. RMS konusma enerjisini olcer; hedef ~-18 dBFS.
  float rms = sqrtf((float)(sumSq / n));
  if (rms > 60.0f) {                       // sessiz kayitta gurultuyu yukseltme
    float gain = 4100.0f / rms;            // hedef RMS ~ -18 dBFS
    if (gain > 12.0f) gain = 12.0f;
    if (gain < 1.0f)  gain = 1.0f;
    const float knee = 24000.0f;           // bu seviyeden sonra yumusak kirp
    for (int i = 0; i < n; i++) {
      float v = s[i] * gain;
      float av = v < 0 ? -v : v;
      if (av > knee) {                     // soft-knee limiter (tanh benzeri)
        float over = (av - knee) / (32767.0f - knee);   // 0..inf
        av = knee + (32767.0f - knee) * (over / (1.0f + over));
        v = v < 0 ? -av : av;
      }
      if (v > 32767.0f) v = 32767.0f; else if (v < -32768.0f) v = -32768.0f;
      s[i] = (int16_t)v;
    }
  }
}

bool audioRecordWav(uint8_t** outBuf, size_t* outLen, int seconds) {
  if (!g_audioReady) audioBegin();
  g_micChannel = -1; // reset auto-detect
  if (!g_audioReal || !g_i2sRx) return false;
  seconds = constrain(seconds, 1, 12);
  size_t pcmBytes = (size_t)AUD_SR * seconds * 2;
  uint8_t* buf = (uint8_t*) heap_caps_malloc(44 + pcmBytes, MALLOC_CAP_SPIRAM);
  if (!buf) {
    Serial.printf("[audio] Malloc failed for %d bytes in SPIRAM\n", 44 + pcmBytes);
    return false;
  }
  i2s_flush_rx();
  size_t got = 0; uint32_t t0 = millis();
  while (got < pcmBytes && millis() - t0 < (uint32_t)(seconds * 1000 + 4000)) {
    size_t m = i2s_read_mono_from_stereo((int16_t*)(buf + 44 + got), pcmBytes - got, 1000);
    got += m;
  }
  audioPostProcess((int16_t*)(buf + 44), got / 2);
  wavHeader(buf, got, AUD_SR);
  *outBuf = buf; *outLen = 44 + got;
  return got > 0;
}

static void audioListenFrame(float lvl) {
  gfx->fillScreen(C_BG);
  int base = 26 + (int)(lvl * 90);
  for (int r = 0; r < 4; r++) gfx->drawCircle(CXi, CYi - 10, 40 + r * 20 + (int)(lvl * 30), C_OK);
  gfx->fillCircle(CXi, CYi - 10, max(10, base), C_OK);
  textCenter("Dinliyorum...", CXi, 360, 3, C_OK);
  textCenter("bitince DOKUN", CXi, 420, 2, C_DIM);
  present();
}

bool audioRecordWavInteractive(uint8_t** outBuf, size_t* outLen, int maxSec) {
  if (!g_audioReady) audioBegin();
  g_micChannel = -1; // reset auto-detect
  if (!g_audioReal || !g_i2sRx) return false;
  maxSec = constrain(maxSec, 2, 20);
  size_t pcmMax = (size_t)AUD_SR * maxSec * 2;
  uint8_t* buf = (uint8_t*) heap_caps_malloc(44 + pcmMax, MALLOC_CAP_SPIRAM);
  if (!buf) {
    Serial.printf("[audio] Malloc failed for %d bytes in SPIRAM\n", 44 + pcmMax);
    return false;
  }
  i2s_flush_rx();
  size_t got = 0; uint32_t t0 = millis();
  bool prevDown = false, stop = false;
  uint32_t lastDraw = 0;
  while (!stop && got < pcmMax && millis() - t0 < (uint32_t)maxSec * 1000) {
    // Keep reading continuously so the mic DMA never overflows. Drawing the
    // screen (a full framebuffer flush ~20ms) is THROTTLED - doing it every
    // read stalled the loop and dropped samples -> choppy/garbled recording.
    size_t m = i2s_read_mono_from_stereo(
        (int16_t*)(buf + 44 + got), min((size_t)2048, pcmMax - got), 50);
    got += m;
    uint32_t nowm = millis();
    if (nowm - lastDraw > 140) {
      int monoN = m / 2; int32_t pk = 0;
      int16_t* s = (int16_t*)(buf + 44 + got - m);
      for (int i = 0; i < monoN; i += 4) { int v = s[i]; if (v<0) v=-v; if (v>pk) pk=v; }
      audioListenFrame(pk / 32768.0f);
      lastDraw = nowm;
    }
    RawTouch rt = touchRead();                       // cheap (no flush) -> safe each loop
    if (rt.down && !prevDown && nowm - t0 > 600) stop = true;
    prevDown = rt.down;
  }
  audioPostProcess((int16_t*)(buf + 44), got / 2);
  wavHeader(buf, got, AUD_SR);
  *outBuf = buf; *outLen = 44 + got;
  return got > 0;
}

// ---------------------------------------------------------------------------
// Microphone level + pseudo-bands
// ---------------------------------------------------------------------------
float audioLevel() {
  if (!g_audioReady) audioBegin();
  if (g_audioReal && g_i2sRx) {
    static int16_t mono[256];
    size_t mb = i2s_read_mono_from_stereo(mono, sizeof(mono), 0);
    int n = mb / 2;
    if (n > 0) {
      uint64_t sum = 0;
      for (int i = 0; i < n; i++) sum += (int32_t)mono[i] * mono[i];
      float rms = sqrtf((float)sum / n) / 32768.0f;
      _aLevel = _aLevel * 0.7f + min(1.0f, rms * 8.0f) * 0.3f;
    }
    return _aLevel;
  }
  float t = millis() * 0.001f;
  float beat = powf(0.5f + 0.5f * sinf(t * 6.28f * 1.6f), 3.0f);
  float wob  = 0.25f * (0.5f + 0.5f * sinf(t * 2.1f));
  float jit  = (rnd(100) / 100.0f) * 0.15f;
  _aLevel = _aLevel * 0.6f + min(1.0f, beat * 0.7f + wob + jit) * 0.4f;
  return _aLevel;
}

void audioBands(float* out8) {
  float base = audioLevel();
  float t = millis() * 0.001f;
  for (int i = 0; i < 8; i++) {
    float f = 1.0f + i * 0.8f;
    float v = base * (0.45f + 0.55f * fabsf(sinf(t * f + i)));
    _aBands[i] = _aBands[i] * 0.55f + v * 0.45f;
    out8[i] = min(1.0f, _aBands[i]);
  }
}
