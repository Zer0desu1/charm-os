// ============================================================================
// mod_edgeai.h - Edge AI: cihaz-ustu, OGRETILEBILIR ses tanima (TinyML)
//
//   Tamamen CIHAZDA, internetsiz calisan few-shot ses sinifi tanima. Buluta tek
//   bayt gitmez; ESP32-S3 hem dinler hem ogrenir hem taniar.
//
//   PIPELINE (gercek DSP + gercek ML):
//     1) Yakalama : ES8311 mikrofonundan 24 kHz, 512 ornekli cerceveler.
//     2) Ozellik  : her cerceve icin 8 bantli GOERTZEL filtre bankasi (log enerji)
//                   + RMS. Bir "olay" (ses patlamasi) enerji-esigiyle yakalanir
//                   ve 3 zaman-segmentine bolunur -> 8x3 = 24 boyutlu, sesin
//                   tinisini+kabaca zamanlamasini tutan L2-normalize "parmak izi".
//     3) Siniflama: parmak izi, ogretilmis sinif PROTOTIPLERINE kosinus
//                   benzerligiyle eslestirilir (en yakin-merkez / 1-NN). Esigin
//                   altindaysa "Bilinmiyor".
//     4) Ogrenme  : OGREN modunda her ornek secili sinifin prototipine katilir
//                   (calisan ortalama). Prototipler NVS'e blob olarak yazilir,
//                   yeniden baslatinca hatirlanir.
//
//   Bu, "akilli kapi zili"nin ses surumu: birkac ornekle bir sesi (zil, alkis,
//   bir kelime, kopek havlamasi...) ogretir, sonra tanir. TFLite Micro modeli
//   istenirse eaiClassify()'in govdesi degisir; gerisi (yakalama/ozellik/UI) ayni.
//
//   Kontroller: ust sekmeler TANI / OGREN (dokun). Kaydir = sinif sec.
//   OGREN'de alt-dokunma = secili sinifi temizle. TANI'da alt-dokunma = gurultu
//   tabanini sifirla. Uzun-bas = ana ekran (global).
// ============================================================================
#pragma once

#include "platform.h"
#include "audio.h"
#include "edgeai_tflite.h"   // gercek TFLite Micro cikarimi (varsa); yoksa fallback

// ---- dimensions ------------------------------------------------------------
#define EAI_WIN      512        // analysis frame (~21 ms @ 24 kHz)
#define EAI_BANDS    8          // Goertzel filterbank size
#define EAI_SEGS     3          // temporal segments per event
#define EAI_FEAT     (EAI_BANDS * EAI_SEGS)   // 24-dim fingerprint
#define EAI_MAXFR    48         // max frames in one event (~1 s)
#define EAI_CLASSES  5          // teachable slots
#define EAI_DB_MAGIC 0x45414932 // "EAI2" (label alani eklendi -> eski blob atilir)
#define EAI_TARGET   5          // bir sesi "ogrenmis" saymak icin yeter ornek sayisi

// Kullanici-dostu etiket katalogu (<=6 harf, ekrana sigsin). Her slota bir
// etiket atanir; sonuc "Kapi" gibi anlamli gosterilir, "Ses 1" gibi degil.
static const char* EAI_LABELS[] = {
  "Ses", "Alkis", "Islik", "Kapi", "Zil", "Kopek",
  "Kedi", "Bebek", "Muzik", "Alarm", "Isim", "Oksur",
};
static const int EAI_LABEL_N = sizeof(EAI_LABELS) / sizeof(EAI_LABELS[0]);

// Goertzel center frequencies (Hz), log-ish spaced across the voice/whistle band
static const float EAI_FREQS[EAI_BANDS] = { 250, 400, 600, 900, 1300, 1900, 2800, 4200 };
static float eai_coeff[EAI_BANDS];
static bool  eai_coeffReady = false;

// ---- learned database (persisted to NVS) -----------------------------------
struct EaiClass { uint16_t count; uint8_t label; float proto[EAI_FEAT]; };
struct EaiDB    { uint32_t magic; EaiClass cls[EAI_CLASSES]; };
static EaiDB eai_db;

// ---- capture / frame state -------------------------------------------------
static int16_t eai_fr[EAI_WIN];
static int     eai_frFill = 0;
static float   eai_bandLive[EAI_BANDS];     // last frame bands (for the spectrum UI)
static float   eai_noiseFloor = 0.01f;

// ---- event accumulator -----------------------------------------------------
static float    eai_evFrames[EAI_MAXFR][EAI_BANDS];
static int      eai_evN      = 0;
static bool     eai_inEvent  = false;
static int      eai_quiet    = 0;           // consecutive sub-gate frames

// ---- mode / selection / results --------------------------------------------
static bool     eai_teach    = false;       // false = TANI (recognize), true = OGREN
static int      eai_sel      = 0;           // selected class slot
static int      eai_resLabel = -1;          // -1 none, -2 unknown, >=0 class idx
static float    eai_resConf  = 0;
static uint32_t eai_resAt    = 0;
static char     eai_flash[20] = {0};        // transient toast (e.g. "ornek +1")
static uint32_t eai_flashAt   = 0;

static const char* eaiClassName(int i) {
  int l = eai_db.cls[i].label;
  if (l < 0 || l >= EAI_LABEL_N) l = 0;
  return EAI_LABELS[l];
}
static bool eaiClassReady(int i) { return eai_db.cls[i].count >= EAI_TARGET; }
static bool eaiAnyTrained() {
  for (int c = 0; c < EAI_CLASSES; c++) if (eai_db.cls[c].count > 0) return true;
  return false;
}

// ---- NVS persistence -------------------------------------------------------
static void eaiSave() { g_prefs.putBytes("eai_db", &eai_db, sizeof(eai_db)); }
static void eaiLoad() {
  size_t n = g_prefs.getBytesLength("eai_db");
  if (n == sizeof(eai_db) && g_prefs.getBytes("eai_db", &eai_db, sizeof(eai_db)) == sizeof(eai_db)
      && eai_db.magic == EAI_DB_MAGIC) return;
  memset(&eai_db, 0, sizeof(eai_db));        // fresh / corrupt -> empty
  eai_db.magic = EAI_DB_MAGIC;
  for (int c = 0; c < EAI_CLASSES; c++)      // her slota farkli varsayilan etiket
    eai_db.cls[c].label = (uint8_t)(c % EAI_LABEL_N);
}

static void edgeai_enter() {
  audioBegin();
  if (!eai_coeffReady) {
    for (int b = 0; b < EAI_BANDS; b++)
      eai_coeff[b] = 2.0f * cosf(2.0f * (float)PI * EAI_FREQS[b] / (float)AUD_SR);
    eai_coeffReady = true;
  }
  eaiLoad();
  eai_frFill = 0; eai_inEvent = false; eai_evN = 0; eai_quiet = 0;
  eai_resLabel = -1; eai_resConf = 0; eai_noiseFloor = 0.01f;
  memset(eai_bandLive, 0, sizeof(eai_bandLive));
  i2s_flush_rx();
}

// ---------------------------------------------------------------------------
// Per-frame DSP: RMS + Goertzel band energies (log-compressed). Cheap (~512*8).
// ---------------------------------------------------------------------------
static float eaiFrameRms(float outBands[EAI_BANDS]) {
  // RMS
  double e = 0;
  for (int i = 0; i < EAI_WIN; i++) e += (double)eai_fr[i] * eai_fr[i];
  float rms = sqrtf((float)(e / EAI_WIN)) / 32768.0f;
  // Goertzel per band
  for (int b = 0; b < EAI_BANDS; b++) {
    float c = eai_coeff[b], s1 = 0, s2 = 0;
    for (int i = 0; i < EAI_WIN; i++) {
      float s0 = (eai_fr[i] / 32768.0f) + c * s1 - s2;
      s2 = s1; s1 = s0;
    }
    float power = s1 * s1 + s2 * s2 - c * s1 * s2;
    if (power < 0) power = 0;
    outBands[b] = logf(1.0f + power);
  }
  return rms;
}

// Build the 24-dim L2-normalized fingerprint from the accumulated event frames.
static void eaiBuildFeature(float feat[EAI_FEAT]) {
  for (int s = 0; s < EAI_SEGS; s++) {
    int lo = s * eai_evN / EAI_SEGS, hi = (s + 1) * eai_evN / EAI_SEGS;
    if (hi <= lo) hi = lo + 1;
    if (hi > eai_evN) hi = eai_evN;
    for (int b = 0; b < EAI_BANDS; b++) {
      float sum = 0;
      for (int f = lo; f < hi; f++) sum += eai_evFrames[f][b];
      feat[s * EAI_BANDS + b] = sum / (hi - lo);
    }
  }
  float norm = 0;
  for (int i = 0; i < EAI_FEAT; i++) norm += feat[i] * feat[i];
  norm = sqrtf(norm) + 1e-6f;
  for (int i = 0; i < EAI_FEAT; i++) feat[i] /= norm;
}

// ---------------------------------------------------------------------------
// CLASSIFY (1-NN / nearest-centroid, cosine).  <-- TFLite Micro invoke() buraya.
//   Prototip L2-normalize, feat L2-normalize -> kosinus = nokta carpimi.
// ---------------------------------------------------------------------------
static void eaiClassify(const float feat[EAI_FEAT], int& outLabel, float& outConf) {
  // 1) Gercek TFLite Micro modeli varsa onu kullan (egitilmis sinir agi).
  int tfL; float tfC;
  if (eaiTfliteInfer(feat, EAI_FEAT, tfL, tfC)) {
    if (tfC < 0.60f) { outLabel = -2; outConf = tfC; }      // dusuk guven -> bilinmiyor
    else             { outLabel = tfL; outConf = tfC; }
    return;
  }
  // 2) Model yoksa: cihaz-ustu prototip eslestirme (kosinus / 1-NN).
  int   best = -1, second = -1;
  float bestS = -1, secondS = -1;
  for (int c = 0; c < EAI_CLASSES; c++) {
    if (eai_db.cls[c].count == 0) continue;
    float pn = 0; for (int i = 0; i < EAI_FEAT; i++) pn += eai_db.cls[c].proto[i] * eai_db.cls[c].proto[i];
    pn = sqrtf(pn) + 1e-6f;
    float dot = 0; for (int i = 0; i < EAI_FEAT; i++) dot += feat[i] * eai_db.cls[c].proto[i];
    float sim = dot / pn;                          // feat already unit-norm
    if (sim > bestS) { secondS = bestS; second = best; bestS = sim; best = c; }
    else if (sim > secondS) { secondS = sim; second = c; }
  }
  // accept only a confident, unambiguous match
  if (best < 0 || bestS < 0.55f || (second >= 0 && bestS - secondS < 0.04f)) {
    outLabel = -2; outConf = best < 0 ? 0 : bestS; return;
  }
  outLabel = best; outConf = bestS;
}

// Fold a fresh example into the selected class prototype (running average).
static void eaiTeach(int slot, const float feat[EAI_FEAT]) {
  EaiClass& k = eai_db.cls[slot];
  float n = (float)k.count;
  for (int i = 0; i < EAI_FEAT; i++) k.proto[i] = (k.proto[i] * n + feat[i]) / (n + 1.0f);
  if (k.count < 65000) k.count++;
  eaiSave();
}

// Called when an event finishes: either teach the selected slot or recognize.
static void eaiOnEvent() {
  if (eai_evN < 3) return;                          // too short -> ignore blip
  float feat[EAI_FEAT];
  eaiBuildFeature(feat);
  if (eai_teach) {
    // Egitim verisi disa aktarimi: bu satirlari bir CSV'ye topla, sonra
    // tools/edgeai_train.py ile gercek TFLite modelini uret (edgeai_model.h).
    Serial.printf("EAIFEAT,%d", eai_sel);
    for (int i = 0; i < EAI_FEAT; i++) Serial.printf(",%.5f", feat[i]);
    Serial.println();
    eaiTeach(eai_sel, feat);
    snprintf(eai_flash, sizeof(eai_flash), "%s +1", eaiClassName(eai_sel));
    eai_flashAt = millis(); soundOk();
  } else {
    eaiClassify(feat, eai_resLabel, eai_resConf);
    eai_resAt = millis();
    if (eai_resLabel >= 0) soundOk(); else soundTap();
  }
}

// Feed one freshly-filled frame through the event state machine.
static void eaiProcessFrame() {
  float bands[EAI_BANDS];
  float rms = eaiFrameRms(bands);
  memcpy(eai_bandLive, bands, sizeof(bands));

  // adaptive noise floor (down fast, up slow)
  if (rms < eai_noiseFloor) eai_noiseFloor = eai_noiseFloor * 0.9f + rms * 0.1f;
  else                      eai_noiseFloor = eai_noiseFloor * 0.997f + rms * 0.003f;
  if (eai_noiseFloor < 0.003f) eai_noiseFloor = 0.003f;

  float gateOn  = eai_noiseFloor * 3.5f + 0.018f;
  float gateOff = eai_noiseFloor * 2.0f + 0.010f;

  if (!eai_inEvent) {
    if (rms > gateOn) { eai_inEvent = true; eai_evN = 0; eai_quiet = 0; }
    else return;
  }
  // in event: accumulate
  if (eai_evN < EAI_MAXFR) { memcpy(eai_evFrames[eai_evN], bands, sizeof(bands)); eai_evN++; }
  if (rms < gateOff) eai_quiet++; else eai_quiet = 0;
  if (eai_quiet >= 5 || eai_evN >= EAI_MAXFR) {     // end of utterance (~105ms quiet)
    eai_inEvent = false;
    eaiOnEvent();
  }
}

// Pull mic samples and assemble full frames. Drain the I2S DMA each tick (one
// read caps at 512 mono samples, but 24 kHz outpaces a single read per frame),
// so loop a few times to avoid buffer overrun / choppy capture.
static void eaiCapture() {
  if (!(g_audioReal && g_i2sRx)) return;
  static int16_t chunk[512];
  for (int iter = 0; iter < 6; iter++) {
    size_t mb = i2s_read_mono_from_stereo(chunk, sizeof(chunk), iter == 0 ? 12 : 0);
    int n = (int)(mb / 2);
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      eai_fr[eai_frFill++] = chunk[i];
      if (eai_frFill >= EAI_WIN) { eaiProcessFrame(); eai_frFill = 0; }
    }
  }
}

// ----------------------------------- input ---------------------------------
static void eaiInput() {
  if (g_g.swipeLeft)  { eai_sel = (eai_sel + 1) % EAI_CLASSES;               soundTap(); }
  if (g_g.swipeRight) { eai_sel = (eai_sel + EAI_CLASSES - 1) % EAI_CLASSES; soundTap(); }
  if (!g_g.tap) return;

  // top tabs: DINLE / OGRET
  if (g_g.y < 72) {
    bool t = (g_g.x > CXi);
    if (t != eai_teach) { eai_teach = t; soundTap(); }
    return;
  }
  // class chip row -> select which sound slot
  if (g_g.y >= 345 && g_g.y <= 390) {
    int bw = 78, gap = 6, total = EAI_CLASSES * bw + (EAI_CLASSES - 1) * gap;
    int c = (g_g.x - (CXi - total / 2)) / (bw + gap);
    if (c >= 0 && c < EAI_CLASSES) { eai_sel = c; soundTap(); }
    return;
  }
  if (eai_teach) {
    // tap the big name -> rename the sound (cycle the friendly-label catalog)
    if (g_g.y >= 178 && g_g.y <= 236) {
      EaiClass& k = eai_db.cls[eai_sel];
      k.label = (uint8_t)((k.label + 1) % EAI_LABEL_N);
      eaiSave(); soundTap();
      return;
    }
    // bottom: forget this sound (keep its chosen name)
    if (g_g.y > 396) {
      EaiClass& k = eai_db.cls[eai_sel]; uint8_t lb = k.label;
      memset(&k, 0, sizeof(k)); k.label = lb; eaiSave();
      snprintf(eai_flash, sizeof(eai_flash), "%s silindi", eaiClassName(eai_sel));
      eai_flashAt = millis(); soundTap();
      return;
    }
  }
}

// ----------------------------------- render ---------------------------------
static void eaiDrawTabs() {
  textCenter("DINLE", CXi - 90, 40, eai_teach ? 2 : 3, eai_teach ? C_DIM : C_GOLD);
  textCenter("OGRET", CXi + 90, 40, eai_teach ? 3 : 2, eai_teach ? C_GOLD : C_DIM);
  gfx->drawFastHLine(CXi - 130, 62, 260, C_BG2);
}

static void eaiDrawSpectrum(int cy) {
  float mx = 0.05f;   // floor so a silent room shows short bars, not full noise
  for (int b = 0; b < EAI_BANDS; b++) mx = max(mx, eai_bandLive[b]);
  int bw = 20, gap = 8, total = EAI_BANDS * bw + (EAI_BANDS - 1) * gap;
  int x0 = CXi - total / 2;
  for (int b = 0; b < EAI_BANDS; b++) {
    int h = (int)(72.0f * (eai_bandLive[b] / mx));
    if (h < 2) h = 2;
    int bx = x0 + b * (bw + gap);
    gfx->fillRoundRect(bx, cy - h, bw, h, 4, eai_inEvent ? C_ACCENT : C_BG2);
  }
}

static void eaiDrawClassChips(int cy) {
  int bw = 78, gap = 6, total = EAI_CLASSES * bw + (EAI_CLASSES - 1) * gap;
  int x0 = CXi - total / 2;
  bool freshRes = (!eai_teach && eai_resLabel >= 0 && millis() - eai_resAt < 1500);
  for (int c = 0; c < EAI_CLASSES; c++) {
    int bx = x0 + c * (bw + gap);
    bool trained = eai_db.cls[c].count > 0;
    bool sel = eai_teach && c == eai_sel;
    bool hit = freshRes && c == eai_resLabel;
    uint16_t face = hit ? C_OK : (sel ? C_ACCENT : (trained ? C_BG2 : C_BG));
    gfx->fillRoundRect(bx, cy, bw, 40, 8, face);
    gfx->drawRoundRect(bx, cy, bw, 40, 8, eaiClassReady(c) ? C_OK : (trained ? C_TEXT : C_DIM));
    textCenter(eaiClassName(c), bx + bw / 2, cy + 13, 2, trained ? C_TEXT : C_DIM);
    char cb[8];
    if (eaiClassReady(c)) snprintf(cb, sizeof(cb), "hazir");
    else                  snprintf(cb, sizeof(cb), "%u/%d", (unsigned)eai_db.cls[c].count, EAI_TARGET);
    textCenter(cb, bx + bw / 2, cy + 30, 1, eaiClassReady(c) ? C_OK : C_DIM);
  }
}

// progress dots toward EAI_TARGET examples (centered)
static void eaiDrawProgress(int cy) {
  int n = EAI_TARGET, r = 7, gap = 22, x0 = CXi - (n - 1) * gap / 2;
  int got = min((int)eai_db.cls[eai_sel].count, n);
  for (int i = 0; i < n; i++)
    gfx->fillCircle(x0 + i * gap, cy, r, i < got ? C_OK : C_BG2);
}

static void edgeai_tick() {
  eaiCapture();
  eaiInput();

  gfx->fillScreen(C_BG);
  eaiDrawTabs();

  if (!g_audioReal) {
    textCenter("Mikrofon hazir degil", CXi, CYi - 10, 2, C_WARN);
    textCenter("lutfen tekrar dene", CXi, CYi + 20, 2, C_DIM);
    present(); delay(40); return;
  }

  eaiDrawSpectrum(150);

  if (eai_teach) {
    // OGRET: anlamli isim (dokununca degisir) + ilerleme + rehber
    textCenter(eaiClassName(eai_sel), CXi, 200, 3, C_ACCENT);
    textCenter("ismi degistir: isme dokun", CXi, 226, 1, C_DIM);
    eaiDrawProgress(262);
    if (eaiClassReady(eai_sel))
      textCenter("Bu sesi taniyabilirim", CXi, 298, 2, C_OK);
    else
      textCenter(eai_inEvent ? "...dinliyor" : "bu sesi birkac kez cikar",
                 CXi, 298, 2, eai_inEvent ? C_OK : C_TEXT);
    textCenter("< kaydir: baska ses >", CXi, 326, 1, C_DIM);
  } else {
    // DINLE: son tanima sonucu / bos durum
    if (eai_resLabel >= 0 && millis() - eai_resAt < 2500) {
      textCenter("Bu ses:", CXi, 188, 2, C_DIM);
      textCenter(eaiClassName(eai_resLabel), CXi, 224, 4, C_OK);
      int bx = CXi - 70, by = 266, bw = 140;
      gfx->fillRoundRect(bx, by, bw, 12, 6, C_BG2);
      gfx->fillRoundRect(bx, by, (int)(bw * eai_resConf), 12, 6, C_OK);
      char pb[20]; snprintf(pb, sizeof(pb), "%%%d emin", (int)(eai_resConf * 100));
      textCenter(pb, CXi, 296, 1, C_DIM);
    } else if (eai_resLabel == -2 && millis() - eai_resAt < 1800) {
      textCenter("Taniyamadim", CXi, 224, 3, C_WARN);
      textCenter("OGRET'ten ogretebilirsin", CXi, 262, 1, C_DIM);
    } else if (!eaiAnyTrained()) {
      textCenter("Once bir ses ogret", CXi, 214, 2, C_WARN);
      textCenter("yukaridan OGRET'e gec", CXi, 248, 1, C_DIM);
    } else {
      textCenter("Dinliyorum...", CXi, 224, 2, C_DIM);
      textCenter("tanidigim bir sesi cikar", CXi, 258, 1, C_DIM);
    }
  }

  eaiDrawClassChips(348);

  textCenter(eai_teach ? "alta dokun: bu sesi unut" : "ses cikar, taniyayim",
             CXi, 410, 1, C_DIM);

  // transient toast
  if (eai_flash[0] && millis() - eai_flashAt < 1000) {
    gfx->fillRoundRect(CXi - 90, 96, 180, 30, 8, C_OK);
    textCenter(eai_flash, CXi, 111, 2, C_BG);
  } else eai_flash[0] = 0;

  present();
  delay(20);
}
