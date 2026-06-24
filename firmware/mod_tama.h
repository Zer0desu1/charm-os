// ============================================================================
// mod_tama.h - Cevre Tamagotchi: gercek bir sanal evcil hayvan.
//   * Evrim: YUMURTA -> BEBEK -> COCUK -> YETISKIN (yasa gore buyur)
//   * Istatistikler: aclik / mutluluk / enerji / kilo / yas - NVS'de kalici
//   * Ilerleme: SEVIYE + XP, COIN ekonomisi, GUNLUK SERI (streak) - bagimlilik
//     dongusu: bak -> bakim yap -> coin/xp kazan -> seviye atla -> kutlama
//   * Kapaliyken de yasar: cihaz acilinca gecen sure kadar acikir, uyur,
//     kaka yapar, hatta hastalanabilir (telefonun /time senkronu ile)
//   * Mini oyun (bak-yakala): combo carpani, hizlanan turlar, rekor + coin/xp
//   * Animasyonlar: yeme (dusen yemek + cigneme + kirinti), partikul motoru
//     (kalp/yildiz/kirinti), seviye-atlama yildiz patlamasi + halka, bosta
//     gezinme/yurume, otomatik ruh hali efektleri
//   * Dukkan (yukari kaydir): SUPER MAMA satin al, kart istatistikleri
//   * Bakim butonlari: Yemek - Oyun - Temizle - Ilac - Uyku
//   Tap = butonlar/sevme.  Yukari kaydir = kart/dukkan.  Long-press = ana ekran.
// ============================================================================
#pragma once

#include "platform.h"
#include "mascot_fw.h"
#include "audio.h"
#include "mpu.h"

// ---- persistent state ----
static float    tama_hunger;     // 0(tok)..100(ac)
static float    tama_happy;      // 0..100
static float    tama_energy;     // 0..100
static int      tama_weight;     // 5..99 (kg gibi dusun)
static uint32_t tama_ageMin;     // toplam yas (dakika)
static bool     tama_hatched;    // yumurtadan cikti mi
static bool     tama_sick;
static int      tama_poopN;      // ekrandaki kaka sayisi (0..3)
// ilerleme / ekonomi
static int      tama_coins;      // para birimi
static int      tama_xp;         // mevcut seviyedeki XP
static int      tama_level;      // 1..
static int      tama_streak;     // ardisik gun bakim serisi
static int      tama_day;        // son giris gunu (timeNow/86400)
static int      tama_best;       // mini oyun rekoru

static const int SUPER_COST = 25;   // super mama fiyati

// ---- runtime ----
static uint32_t tama_lastTick = 0, tama_lastSave = 0, tama_lastMin = 0;
static int      tama_bob = 0;
static uint32_t tama_blinkAt = 0;
static bool     tama_blink = false;
static float    tama_loudAvg = 0;
static uint32_t tama_msgUntil = 0;       // gecici durum mesaji
static char     tama_msg[28] = "";
static int      tama_eggTaps = 0;
static uint32_t tama_fxUntil = 0;        // yumurta wobble efekti
static uint32_t tama_levelupUntil = 0;   // seviye atlama animasyon penceresi
static uint32_t tama_petXpAt = 0;        // sevme XP kisitlamasi
// IMU-driven reactions (tilt lean, gaze, shake-dizzy)
static uint32_t tama_dizzyUntil = 0;
static int      tama_leanX = 0, tama_gazeX = 0, tama_gazeY = 0;
// bosta gezinme / yurume
static float    tama_wanderX = 0, tama_wanderTgt = 0;
static uint32_t tama_wanderAt = 0;
static int      tama_faceDir = 0;        // -1 sol, +1 sag yuruyor
// yeme animasyonu
static uint32_t tama_eatStart = 0, tama_eatEnd = 0;
static char     tama_eatGlyph = 'm';     // 'm' ogun, 't' tatli, 's' super
static bool     tama_eatCrumb = false;

static bool     tama_panel = false;      // kart/dukkan acik mi

enum TamaMode { TM_IDLE, TM_FEED, TM_GAME, TM_SLEEP };
static TamaMode tama_mode = TM_IDLE;

// mini oyun (bak-yakala): pet bir yana bakar, dogru tarafa dokun
static int      tg_round, tg_score, tg_dir;     // dir: -1 sol, +1 sag
static int      tg_combo;                        // ardisik dogru
static uint32_t tg_showAt, tg_deadline;          // gosterim + cevap son ani
static bool     tg_answered;
static const int TG_ROUNDS = 6;

// ===========================================================================
//  Partikul motoru (kalp / yildiz / kirinti)
// ===========================================================================
struct TPart { float x, y, vx, vy; int16_t life, maxlife; uint16_t col; char kind; };
static const int TAMA_NPART = 20;
static TPart tparts[TAMA_NPART];

static void tpartSpawn(float x, float y, float vx, float vy, int life,
                       uint16_t col, char kind) {
  for (int i = 0; i < TAMA_NPART; i++) if (tparts[i].life <= 0) {
    tparts[i] = {x, y, vx, vy, (int16_t)life, (int16_t)life, col, kind};
    return;
  }
}
static void tpartBurst(int cx, int cy, char kind, uint16_t col, int n) {
  for (int i = 0; i < n; i++) {
    float a  = rnd(628) / 100.0f;
    float sp = 0.7f + rnd(170) / 100.0f;
    tpartSpawn(cx, cy, cosf(a) * sp, sinf(a) * sp - 1.4f, 28 + rnd(22), col, kind);
  }
}
static void tpartUpdate() {
  for (int i = 0; i < TAMA_NPART; i++) {
    TPart &p = tparts[i];
    if (p.life <= 0) continue;
    p.x += p.vx; p.y += p.vy;
    if (p.kind == 'c') p.vy += 0.14f;        // kirinti duser
    else if (p.kind == 'h') { p.vy -= 0.012f; p.vx *= 0.99f; }   // kalp suzulur
    p.life--;
  }
}
static void tpartDraw() {
  for (int i = 0; i < TAMA_NPART; i++) {
    TPart &p = tparts[i];
    if (p.life <= 0) continue;
    int x = (int)p.x, y = (int)p.y;
    float k = (float)p.life / p.maxlife;     // 1..0
    int r = (int)(2 + 4 * k);
    if (p.kind == 'h') {                      // kalp
      gfx->fillCircle(x - r / 2, y, r, p.col);
      gfx->fillCircle(x + r / 2, y, r, p.col);
      gfx->fillTriangle(x - r, y + r / 2, x + r, y + r / 2, x, y + r * 2, p.col);
    } else if (p.kind == 's') {               // yildiz parlamasi
      gfx->drawLine(x - r, y, x + r, y, p.col);
      gfx->drawLine(x, y - r, x, y + r, p.col);
      int h = r / 2;
      gfx->drawLine(x - h, y - h, x + h, y + h, p.col);
      gfx->drawLine(x - h, y + h, x + h, y - h, p.col);
    } else {                                  // kirinti
      gfx->fillCircle(x, y, max(1, r / 2), p.col);
    }
  }
}

static void tamaSay(const char* m, int ms = 1800) {
  strncpy(tama_msg, m, sizeof(tama_msg) - 1);
  tama_msg[sizeof(tama_msg) - 1] = 0;
  tama_msgUntil = millis() + ms;
}

static void tamaSave() {
  setPutInt("t_hun", (int)tama_hunger);
  setPutInt("t_hap", (int)tama_happy);
  setPutInt("t_ene", (int)tama_energy);
  setPutInt("t_wgt", tama_weight);
  setPutInt("t_age", (int)tama_ageMin);
  setPutInt("t_poop", tama_poopN);
  setPutBool("t_sick", tama_sick);
  setPutBool("t_hatch", tama_hatched);
  setPutInt("t_coin", tama_coins);
  setPutInt("t_xp",   tama_xp);
  setPutInt("t_lvl",  tama_level);
  setPutInt("t_strk", tama_streak);
  setPutInt("t_day",  tama_day);
  setPutInt("t_best", tama_best);
  if (timeValid()) setPutInt("t_seen", (int)timeNow());
}

// ---- ilerleme ----
static int tamaXpNeed(int lvl) { return 60 + lvl * 40; }

static void tamaAddXP(int n) {
  tama_xp += n;
  while (tama_xp >= tamaXpNeed(tama_level)) {
    tama_xp -= tamaXpNeed(tama_level);
    tama_level++;
    int bonus = 8 + tama_level * 3;
    tama_coins += bonus;
    tama_levelupUntil = millis() + 2400;
    tpartBurst(CXi + (int)tama_wanderX, CYi, 's', C_GOLD, 14);
    char b[28]; snprintf(b, sizeof(b), "Seviye %d! +%d", tama_level, bonus);
    tamaSay(b, 2600);
    soundCelebrate();
  }
}

// Kapali gecen sureyi yasama yansit (en fazla 24 saat etkili olur).
static void tamaCatchUp() {
  if (!timeValid()) return;
  uint32_t seen = (uint32_t) setGetInt("t_seen", 0);
  if (!seen || timeNow() <= seen) return;
  float hrs = (timeNow() - seen) / 3600.0f;
  if (hrs > 24) hrs = 24;
  if (hrs < 0.05f) return;
  tama_ageMin += (uint32_t)(hrs * 60);
  tama_hunger = min(100.0f, tama_hunger + 4.0f * hrs);
  tama_happy  = max(0.0f,   tama_happy  - 2.0f * hrs);
  tama_energy = min(100.0f, tama_energy + 6.0f * hrs);   // yokken uyudu
  int newPoop = (int)(hrs / 5);                          // ~5 saatte bir kaka
  tama_poopN = min(3, tama_poopN + newPoop);
  if (tama_hunger > 85 && rnd(100) < (uint32_t)(hrs * 6)) tama_sick = true;
  if (hrs > 1.0f) tamaSay(tama_sick ? "iyi degilim..." : "seni ozledim!", 3000);
}

// Gunluk seri: her yeni gun ilk acista coin odulu.
static void tamaDailyCheck() {
  if (!timeValid()) return;
  int day = (int)(timeNow() / 86400);
  if (tama_day == 0) { tama_day = day; return; }   // ilk calistirma, odul yok
  if (day <= tama_day) return;
  tama_streak = (day == tama_day + 1) ? tama_streak + 1 : 1;
  tama_day = day;
  int bonus = 10 + tama_streak * 2;
  tama_coins += bonus;
  char b[28]; snprintf(b, sizeof(b), "%d. gun! +%d coin", tama_streak, bonus);
  tamaSay(b, 3200);
}

static void tamaLoad() {
  tama_hunger  = setGetInt("t_hun", 30);
  tama_happy   = setGetInt("t_hap", 70);
  tama_energy  = setGetInt("t_ene", 80);
  tama_weight  = setGetInt("t_wgt", 12);
  tama_ageMin  = (uint32_t) setGetInt("t_age", 0);
  tama_poopN   = setGetInt("t_poop", 0);
  tama_sick    = setGetBool("t_sick", false);
  tama_coins   = setGetInt("t_coin", 0);
  tama_xp      = setGetInt("t_xp", 0);
  tama_level   = max(1, setGetInt("t_lvl", 1));
  tama_streak  = setGetInt("t_strk", 0);
  tama_day     = setGetInt("t_day", 0);
  tama_best    = setGetInt("t_best", 0);
  // gocus: eski surumden gelen (zaten yasayan) pet yumurtaya geri donmesin
  tama_hatched = setGetBool("t_hatch", g_prefs.isKey("t_hun"));
  tamaCatchUp();
  tamaDailyCheck();
}

// 0=bebek 1=cocuk 2=yetiskin
static int tamaStage() {
  if (tama_ageMin < 24UL * 60) return 0;
  if (tama_ageMin < 72UL * 60) return 1;
  return 2;
}
static const char* TAMA_STAGES[3] = {"Bebek", "Cocuk", "Yetiskin"};
static float TAMA_SCALES[3] = {0.62f, 0.86f, 1.08f};

static void tama_enter() {
  tamaLoad();
  tama_lastTick = tama_lastSave = tama_lastMin = millis();
  tama_mode = TM_IDLE;
  tama_eggTaps = 0;
  tama_panel = false;
  tama_wanderX = tama_wanderTgt = 0;
  tama_wanderAt = millis() + 1500;
  for (int i = 0; i < TAMA_NPART; i++) tparts[i].life = 0;
}

static MascotFace tamaFace() {
  if (tama_mode == TM_SLEEP) return F_SLEEPY;
  if (tama_sick)             return F_SAD;
  if (tama_energy < 18)      return F_SLEEPY;
  if (tama_hunger > 75)      return F_HUNGRY;
  if (tama_happy > 80 && tama_loudAvg > 0.45f) return F_EXCITED;
  if (tama_happy > 60)       return F_HAPPY;
  if (tama_happy < 30)       return F_SAD;
  return F_NEUTRAL;
}

// ---- drawing helpers ----
static void tamaMiniBar(int x, int y, int w, float v, uint16_t col) {
  gfx->fillRoundRect(x, y, w, 10, 5, C_BG2);
  int fw = (int)(w * constrain(v, 0.0f, 100.0f) / 100.0f);
  if (fw > 2) gfx->fillRoundRect(x, y, fw, 10, 5, col);
}

// stat barlarinin solundaki minik ikonlar: 'f' tokluk, 'h' mutluluk, 'e' enerji
static void tamaStatIcon(int x, int y, char k, uint16_t c) {
  switch (k) {
    case 'f':   // catal-bicak / tokluk
      gfx->fillRect(x - 4, y - 5, 2, 11, c);
      gfx->fillRect(x + 2, y - 5, 2, 11, c);
      gfx->fillRect(x - 6, y - 5, 2, 4, c);
      break;
    case 'h':   // kalp / mutluluk
      gfx->fillCircle(x - 3, y - 2, 3, c);
      gfx->fillCircle(x + 3, y - 2, 3, c);
      gfx->fillTriangle(x - 6, y - 1, x + 6, y - 1, x, y + 6, c);
      break;
    case 'e':   // simsek / enerji
      gfx->fillTriangle(x + 2, y - 6, x - 4, y + 1, x + 1, y + 1, c);
      gfx->fillTriangle(x - 2, y + 6, x + 4, y - 1, x - 1, y - 1, c);
      break;
  }
}

static void tamaCoinIcon(int x, int y, int r) {
  gfx->fillCircle(x, y, r, C_GOLD);
  gfx->drawCircle(x, y, r, rgb(180, 130, 30));
  gfx->fillRect(x - 1, y - r + 3, 2, 2 * r - 6, rgb(180, 130, 30));
}

static void tamaPoopDraw(int cx, int cy, uint32_t now) {
  uint16_t c = rgb(150, 95, 40);
  gfx->fillCircle(cx, cy + 6, 10, c);
  gfx->fillCircle(cx - 3, cy - 2, 7, c);
  gfx->fillCircle(cx + 1, cy - 9, 4, c);
  // ustunde vizildayan sinek
  int fx = cx + (int)(12 * sinf(now * 0.012f));
  int fy = cy - 16 + (int)(4 * sinf(now * 0.02f));
  gfx->fillCircle(fx, fy, 2, C_DIM);
}

// yiyecek glyph'i (dusen yemek animasyonu)
static void tamaFoodGlyph(int x, int y, char g) {
  if (g == 't') {                              // tatli kurabiye
    gfx->fillCircle(x, y, 11, rgb(208, 150, 80));
    gfx->fillCircle(x - 3, y - 2, 2, rgb(90, 50, 20));
    gfx->fillCircle(x + 4, y + 3, 2, rgb(90, 50, 20));
    gfx->fillCircle(x + 2, y - 5, 2, rgb(90, 50, 20));
  } else if (g == 's') {                        // super: altin elma
    gfx->fillCircle(x, y, 12, C_GOLD);
    gfx->fillCircle(x - 4, y - 4, 3, WHITE);
    gfx->fillRect(x - 1, y - 16, 2, 6, rgb(120, 90, 40));
    gfx->fillTriangle(x + 1, y - 13, x + 9, y - 16, x + 2, y - 9, C_OK);
  } else {                                      // ogun: kase + sebze
    gfx->fillRect(x - 12, y - 2, 24, 4, rgb(220, 220, 230));
    fillSector(x, y + 2, 12, 90, 270, rgb(180, 180, 190));
    gfx->fillCircle(x - 3, y - 4, 6, rgb(120, 200, 120));
    gfx->fillCircle(x + 5, y - 3, 5, rgb(230, 120, 90));
  }
}

// bottom action buttons: x positions inside the round bezel at y=410
struct TamaBtn { int x; char k; };
static const TamaBtn TAMA_BTNS[5] = {
  {130, 'F'}, {181, 'G'}, {233, 'C'}, {285, 'M'}, {336, 'S'} };

static void tamaBtnDraw(int x, int y, char k, bool alert) {
  gfx->fillCircle(x, y, 24, C_BG2);
  gfx->drawCircle(x, y, 24, alert ? C_DANGER : C_DIM);
  if (alert) gfx->drawCircle(x, y, 23, C_DANGER);
  uint16_t c = C_TEXT;
  switch (k) {
    case 'F':   // yemek: kase
      gfx->fillRect(x - 12, y - 2, 24, 4, c);
      fillSector(x, y + 2, 12, 90, 270, c);
      break;
    case 'G':   // oyun: top
      gfx->drawCircle(x, y, 11, c); gfx->drawCircle(x, y, 10, c);
      gfx->drawLine(x - 11, y, x + 11, y, c);
      break;
    case 'C':   // temizle: supurge
      gfx->drawLine(x - 8, y - 12, x + 4, y + 2, c);
      gfx->drawLine(x - 7, y - 12, x + 5, y + 2, c);
      gfx->fillTriangle(x + 1, y + 1, x + 12, y + 4, x + 4, y + 12, c);
      break;
    case 'M':   // ilac: kapsul
      gfx->fillRoundRect(x - 12, y - 5, 24, 11, 5, c);
      gfx->fillRoundRect(x, y - 5, 12, 11, 5, C_DANGER);
      break;
    case 'S':   // uyku: ay
      gfx->fillCircle(x, y, 11, c);
      gfx->fillCircle(x + 6, y - 4, 10, C_BG2);
      break;
  }
}

// ---- actions ----
static void tamaStartEat(char glyph) {
  tama_eatStart = millis();
  tama_eatEnd   = tama_eatStart + 1500;
  tama_eatGlyph = glyph;
  tama_eatCrumb = false;
}

static void tamaFeed(bool snack) {
  if (snack) {
    tama_hunger = max(0.0f, tama_hunger - 12);
    tama_happy  = min(100.0f, tama_happy + 10);
    tama_weight = min(99, tama_weight + 2);
    tamaSay("nyam! tatli!");
    tamaStartEat('t');
    tamaAddXP(2);
  } else {
    tama_hunger = max(0.0f, tama_hunger - 35);
    tama_weight = min(99, tama_weight + 1);
    tamaSay("afiyetle yedi");
    tamaStartEat('m');
    tamaAddXP(3);
  }
  if (g_soundTheme) { soundPlay(740, 40); soundPlay(880, 40); soundPlay(740, 40); }
  tamaSave();
}

static void tamaSuperFeed() {
  if (tama_coins < SUPER_COST) { tamaSay("yetersiz coin"); soundError(); return; }
  tama_coins -= SUPER_COST;
  tama_hunger = 0;
  tama_happy  = min(100.0f, tama_happy + 35);
  tama_energy = min(100.0f, tama_energy + 15);
  tama_weight = min(99, tama_weight + 2);
  tamaStartEat('s');
  tpartBurst(CXi + (int)tama_wanderX, CYi - 30, 'h', C_DANGER, 6);
  tamaSay("super mama! mmm!", 2400);
  soundCelebrate();
  tamaAddXP(5);
  tama_panel = false;
  tamaSave();
}

static void tamaClean() {
  if (tama_poopN == 0) { tamaSay("zaten temiz"); return; }
  tama_poopN = 0;
  tama_happy = min(100.0f, tama_happy + 6);
  tama_coins += 1;
  tamaSay("temizlendi! +1");
  soundOk();
  tamaAddXP(2);
  tamaSave();
}

static void tamaMedicine() {
  if (!tama_sick) { tamaSay("hasta degil"); return; }
  tama_sick = false;
  tama_happy = min(100.0f, tama_happy + 4);
  tama_coins += 2;
  tamaSay("iyilesti! +2");
  soundCelebrate();
  tamaAddXP(4);
  tamaSave();
}

static void tamaGameStart() {
  tama_mode = TM_GAME;
  tg_round = 0; tg_score = 0; tg_combo = 0;
  tg_dir = rnd(2) ? 1 : -1;
  tg_showAt = millis() + 700;
  tg_deadline = 0;
  tg_answered = false;
}

// ---- renders ----
static void tamaRenderEgg() {
  uint32_t now = millis();
  gfx->fillScreen(C_BG);
  int wob = (tama_eggTaps && now < tama_fxUntil) ? (int)(4 * sinf(now * 0.05f)) : 0;
  int cx = CXi + wob, cy = CYi - 10;
  // yumurta
  gfx->fillEllipse(cx, cy, 78, 96, rgb(250, 240, 215));
  gfx->fillEllipse(cx - 22, cy - 30, 20, 28, WHITE);
  // benekler
  gfx->fillCircle(cx + 26, cy + 18, 9, rgb(255, 196, 64));
  gfx->fillCircle(cx - 30, cy + 40, 7, rgb(120, 200, 255));
  gfx->fillCircle(cx + 6, cy - 52, 6, rgb(255, 140, 170));
  // catlaklar (dokundukca artar)
  uint16_t cr = rgb(120, 100, 70);
  if (tama_eggTaps >= 2) { gfx->drawLine(cx-20, cy-10, cx, cy+6, cr); gfx->drawLine(cx, cy+6, cx+18, cy-6, cr); }
  if (tama_eggTaps >= 4) { gfx->drawLine(cx+18, cy-6, cx+30, cy+10, cr); gfx->drawLine(cx-20, cy-10, cx-34, cy+8, cr); }
  if (tama_eggTaps >= 6) { gfx->drawLine(cx, cy+6, cx-8, cy+30, cr); gfx->drawLine(cx+30, cy+10, cx+22, cy+34, cr); }
  // ilerleme noktalari
  for (int i = 0; i < 7; i++)
    gfx->fillCircle(CXi - 54 + i * 18, 360, 4, i < tama_eggTaps ? C_GOLD : C_BG2);
  textCenter("dokun ve uyandir!", CXi, 400, 2, C_DIM);
  tpartUpdate(); tpartDraw();
  present();
}

// ust bilgi seridi: seviye + coin + evre/yas/kilo
static void tamaTopBar(int stage) {
  char lv[12]; snprintf(lv, sizeof(lv), "Lv %d", tama_level);
  textCenter(lv, CXi - 175, 16, 2, C_ACCENT);
  tamaCoinIcon(CXi + 120, 16, 8);
  char cb[12]; snprintf(cb, sizeof(cb), "%d", tama_coins);
  textCenter(cb, CXi + 150, 16, 2, C_GOLD);
  char top[40];
  uint32_t days = tama_ageMin / (24 * 60);
  snprintf(top, sizeof(top), "%s  %lu gun  %d kg", TAMA_STAGES[stage],
           (unsigned long)days, tama_weight);
  textCenter(top, CXi, 38, 2, C_DIM);
}

static void tamaRenderMain() {
  uint32_t now = millis();
  gfx->fillScreen(C_BG);
  int stage = tamaStage();
  float sc = TAMA_SCALES[stage];

  tamaTopBar(stage);

  // mini stat barlari + ikonlar
  tamaStatIcon(CXi - 96, 61, 'f', C_WARN);
  tamaStatIcon(CXi - 96, 77, 'h', C_OK);
  tamaStatIcon(CXi - 96, 93, 'e', C_ACCENT);
  tamaMiniBar(CXi - 80, 56, 160, 100 - tama_hunger, C_WARN);
  tamaMiniBar(CXi - 80, 72, 160, tama_happy,        C_OK);
  tamaMiniBar(CXi - 80, 88, 160, tama_energy,       C_ACCENT);

  // pet konumu (bosta gezinme offseti)
  int pcx = CXi + (int)tama_wanderX;
  int pcy = CYi + 16;
  int mouthY = pcy + (int)(20 * sc);

  // yuz: yeme sirasinda cigneme; sallaninca basi doner
  bool eating = now < tama_eatEnd;
  bool chewing = eating && (now - tama_eatStart) >= 700;
  MascotFace face = (now < tama_dizzyUntil) ? F_DIZZY : tamaFace();
  if (chewing) face = ((now / 140) % 2) ? F_HAPPY : F_NEUTRAL;

  int pdx = 0;
  if (tama_loudAvg > 0.4f && tama_mode != TM_SLEEP) pdx = (now / 200) % 2 ? 3 : -3;
  int wob = (now < tama_dizzyUntil) ? (int)(7 * sinf(now * 0.03f)) : 0;
  int gazeX = tama_gazeX + pdx + tama_faceDir * 4;
  mascotDraw(pcx + wob, pcy, sc, face, tama_bob,
             tama_blink ? 0 : gazeX, tama_gazeY, tama_leanX);

  // cigneme: sisik yanaklar + bir kez kirinti
  if (chewing) {
    gfx->fillCircle(pcx - (int)(48 * sc), pcy + (int)(2 * sc), (int)(11 * sc), M_CHEEK);
    gfx->fillCircle(pcx + (int)(48 * sc), pcy + (int)(2 * sc), (int)(11 * sc), M_CHEEK);
    if (!tama_eatCrumb) {
      tama_eatCrumb = true;
      tpartBurst(pcx, mouthY, 'c', rgb(210, 180, 120), 6);
    }
  } else if (eating) {                          // dusen yemek
    float p = (now - tama_eatStart) / 700.0f;
    int fy = 120 + (int)((mouthY - 120) * p);
    tamaFoodGlyph(pcx, fy, tama_eatGlyph);
  }

  // kakalar
  if (tama_poopN >= 1) tamaPoopDraw(CXi + 110, CYi + 88, now);
  if (tama_poopN >= 2) tamaPoopDraw(CXi - 116, CYi + 78, now);
  if (tama_poopN >= 3) tamaPoopDraw(CXi + 70,  CYi + 116, now);

  // hastalik gostergesi (nabiz gibi)
  if (tama_sick) {
    uint16_t hc = ((now / 350) % 2) ? C_DANGER : C_WARN;
    textCenter("HASTA!", pcx + 100, pcy - 76, 2, hc);
  }

  // seviye atlama: genisleyen altin halka
  if (now < tama_levelupUntil) {
    int age = 2400 - (int)(tama_levelupUntil - now);
    int rr = 40 + age / 6;
    gfx->drawCircle(pcx, pcy, rr, C_GOLD);
    gfx->drawCircle(pcx, pcy, rr + 1, C_GOLD);
  }

  tpartUpdate(); tpartDraw();

  // durum mesaji
  if (now < tama_msgUntil) textCenter(tama_msg, CXi, 360, 2, C_GOLD);

  if (tama_mode == TM_FEED) {
    // yemek secimi: iki buyuk kart
    gfx->fillRoundRect(48, 150, 160, 120, 16, C_BG2);
    gfx->fillRoundRect(258, 150, 160, 120, 16, C_BG2);
    textCenter("OGUN", 128, 190, 2, C_TEXT);
    textCenter("doyurur", 128, 235, 2, C_DIM);
    textCenter("TATLI", 338, 190, 2, C_TEXT);
    textCenter("mutlu eder", 338, 235, 2, C_DIM);
  }

  // alt buton cubugu
  tamaBtnDraw(TAMA_BTNS[0].x, 410, 'F', tama_hunger > 75);
  tamaBtnDraw(TAMA_BTNS[1].x, 410, 'G', false);
  tamaBtnDraw(TAMA_BTNS[2].x, 410, 'C', tama_poopN > 0);
  tamaBtnDraw(TAMA_BTNS[3].x, 410, 'M', tama_sick);
  tamaBtnDraw(TAMA_BTNS[4].x, 410, 'S', tama_energy < 18);
  textCenter("yukari kaydir: kart", CXi, 444, 1, C_BG2);
  present();
}

// kart / dukkan
static void tamaRenderPanel() {
  gfx->fillScreen(C_BG);
  gfx->fillRoundRect(28, 56, 410, 360, 24, C_BG2);
  textCenter("CHARM KARTI", CXi, 86, 2, C_GOLD);

  // seviye + xp bari
  char l[24]; snprintf(l, sizeof(l), "Seviye %d", tama_level);
  textCenter(l, CXi, 120, 2, C_TEXT);
  int need = tamaXpNeed(tama_level);
  tamaMiniBar(CXi - 110, 140, 220, 100.0f * tama_xp / need, C_ACCENT);
  char xb[20]; snprintf(xb, sizeof(xb), "%d / %d XP", tama_xp, need);
  textCenter(xb, CXi, 158, 1, C_DIM);

  // coin + seri + rekor
  tamaCoinIcon(CXi - 90, 192, 9);
  char cc[16]; snprintf(cc, sizeof(cc), "%d", tama_coins);
  textCenter(cc, CXi - 50, 192, 2, C_GOLD);
  char sk[16]; snprintf(sk, sizeof(sk), "Seri %dg", tama_streak);
  textCenter(sk, CXi + 70, 192, 2, C_OK);
  char bs[20]; snprintf(bs, sizeof(bs), "Oyun rekoru: %d", tama_best);
  textCenter(bs, CXi, 222, 2, C_DIM);

  // super mama satin al butonu
  bool afford = tama_coins >= SUPER_COST;
  gfx->fillRoundRect(CXi - 120, 256, 240, 78, 18, afford ? C_OK : C_BG);
  gfx->drawRoundRect(CXi - 120, 256, 240, 78, 18, afford ? WHITE : C_DIM);
  tamaFoodGlyph(CXi - 78, 295, 's');
  textCenter("SUPER MAMA", CXi + 24, 282, 2, afford ? C_TEXT : C_DIM);
  char pc[20]; snprintf(pc, sizeof(pc), "%d coin", SUPER_COST);
  textCenter(pc, CXi + 24, 308, 2, afford ? WHITE : C_DIM);

  textCenter("asagi kaydir: kapat", CXi, 392, 2, C_DIM);
  tpartUpdate(); tpartDraw();
  if (millis() < tama_msgUntil) textCenter(tama_msg, CXi, 432, 2, C_GOLD);
  present();
}

static void tamaRenderSleep() {
  uint32_t now = millis();
  gfx->fillScreen(BLACK);
  // ay + yildizlar
  gfx->fillCircle(CXi + 70, 110, 34, rgb(220, 220, 180));
  gfx->fillCircle(CXi + 86, 100, 30, BLACK);
  for (int i = 0; i < 8; i++) {
    int sx = 60 + (i * 47) % 350, sy = 60 + (i * 83) % 140;
    if (((now / 700) + i) % 3) gfx->fillCircle(sx, sy, 2, C_DIM);
  }
  mascotDraw(CXi, CYi + 30, TAMA_SCALES[tamaStage()] * 0.9f, F_SLEEPY,
             (int)(3 * sinf(now * 0.002f)), 0, 0);
  char eb[24]; snprintf(eb, sizeof(eb), "enerji %d%%", (int)tama_energy);
  textCenter(eb, CXi, 392, 2, C_DIM);
  textCenter("dokun: uyandir", CXi, 424, 2, C_BG2);
  present();
}

static void tamaRenderGame() {
  uint32_t now = millis();
  gfx->fillScreen(C_BG);
  char rb[24]; snprintf(rb, sizeof(rb), "tur %d/%d  puan %d",
                        tg_round + 1, TG_ROUNDS, tg_score);
  textCenter(rb, CXi, 40, 2, C_DIM);
  // combo alevi
  if (tg_combo >= 2) {
    char cb[10]; snprintf(cb, sizeof(cb), "x%d", tg_combo);
    uint16_t fc = tg_combo >= 4 ? C_DANGER : C_GOLD;
    textCenter(cb, CXi, 70, 3, fc);
  }
  bool show = now >= tg_showAt;
  // pet ortada, baktigi yone dev ok
  mascotDraw(CXi, CYi, 0.7f, show ? F_EXCITED : F_NEUTRAL, 0,
             show ? tg_dir * 6 : 0, 0);
  if (show && !tg_answered) {
    int ax = CXi + tg_dir * 150;
    gfx->fillTriangle(ax + tg_dir * 28, CYi, ax - tg_dir * 8, CYi - 30,
                      ax - tg_dir * 8, CYi + 30, C_GOLD);
    // sure cubugu (kisaliyor)
    if (tg_deadline > now) {
      int total = max(1, (int)(tg_deadline - tg_showAt));
      int wpx = 240 * (int)(tg_deadline - now) / total;
      gfx->fillRoundRect(CXi - 120, 390, 240, 8, 4, C_BG2);
      uint16_t bc = (tg_deadline - now < 350) ? C_DANGER : C_OK;
      if (wpx > 2) gfx->fillRoundRect(CXi - 120, 390, wpx, 8, 4, bc);
    }
    textCenter("baktigi yone dokun!", CXi, 412, 2, C_TEXT);
  } else if (!show) {
    textCenter("hazir ol...", CXi, 412, 2, C_DIM);
  }
  tpartUpdate(); tpartDraw();
  present();
}

// ---- main tick ----
static void tama_tick() {
  uint32_t now = millis();
  float loud = audioLevel();
  tama_loudAvg = tama_loudAvg * 0.9f + loud * 0.1f;

  // ------ YUMURTA ------
  if (!tama_hatched) {
    if (g_g.tap) {
      tama_eggTaps++;
      tama_fxUntil = now + 350;
      tpartBurst(CXi, CYi - 10, 's', WHITE, 4);
      soundPlay(500 + tama_eggTaps * 60, 25);
      if (tama_eggTaps >= 7) {
        tama_hatched = true;
        tama_ageMin = 0;
        tama_level = 1; tama_xp = 0;
        tpartBurst(CXi, CYi, 'h', C_DANGER, 6);
        soundCelebrate();
        tamaSay("merhaba dunya!", 2600);
        tamaSave();
      }
    }
    tamaRenderEgg();
    delay(24);
    return;
  }

  // ------ KART / DUKKAN ------
  if (tama_panel) {
    if (g_g.swipeDown) { tama_panel = false; soundBack(); }
    else if (g_g.tap) {
      // super mama buton bolgesi
      if (g_g.x >= CXi - 120 && g_g.x <= CXi + 120 &&
          g_g.y >= 256 && g_g.y <= 334) {
        tamaSuperFeed();
      } else if (g_g.y < 56 || g_g.y > 416) {
        tama_panel = false;               // disari dokun = kapat
      }
    }
    tamaRenderPanel();
    delay(28);
    return;
  }

  // ------ MINI OYUN ------
  if (tama_mode == TM_GAME) {
    // cevap suresi gosterimle baslar
    if (now >= tg_showAt && tg_deadline == 0 && !tg_answered) {
      int win = 1300 - tg_round * 130;        // turlar hizlanir
      tg_deadline = now + max(450, win);
    }
    if (g_g.tap && now >= tg_showAt && !tg_answered) {
      bool correct = (g_g.x < CXi) == (tg_dir < 0);
      tg_answered = true;
      if (correct) {
        tg_combo++;
        int gain = min(5, tg_combo);
        tg_score += gain;
        tpartBurst(CXi, CYi - 20, 's', C_GOLD, 4);
        soundOk();
      } else { tg_combo = 0; soundError(); }
      tg_showAt = now + 450;
    } else if (!tg_answered && tg_deadline && now >= tg_deadline) {
      // sure doldu = kacirildi
      tg_answered = true; tg_combo = 0;
      soundError();
      tg_showAt = now + 450;
    }
    if (tg_answered && now >= tg_showAt) {
      tg_round++;
      if (tg_round >= TG_ROUNDS) {
        int reward = tg_score;
        tama_coins += reward;
        tama_happy  = min(100.0f, tama_happy + 6 + tg_score * 3);
        tama_energy = max(0.0f, tama_energy - 8);
        tama_weight = max(5, tama_weight - 1);
        tama_mode = TM_IDLE;
        if (tg_score > tama_best) {
          tama_best = tg_score;
          tamaSay("YENI REKOR!", 2600);
          tpartBurst(CXi, CYi, 's', C_GOLD, 16);
          soundCelebrate();
        } else {
          char b[28]; snprintf(b, sizeof(b), "+%d coin!", reward);
          tamaSay(b, 2000);
          if (tg_score >= TG_ROUNDS) soundCelebrate();
        }
        tamaAddXP(tg_score);
        tamaSave();
      } else {
        tg_dir = rnd(2) ? 1 : -1;
        tg_showAt = now + 500 + rnd(800);
        tg_deadline = 0;
        tg_answered = false;
      }
    }
    tamaRenderGame();
    delay(20);
    return;
  }

  // ------ UYKU ------
  if (tama_mode == TM_SLEEP) {
    if (now - tama_lastTick > 1000) {
      float dt = (now - tama_lastTick) / 1000.0f;
      tama_lastTick = now;
      tama_energy = min(100.0f, tama_energy + 3.0f * dt);
      tama_hunger = min(100.0f, tama_hunger + 0.1f * dt);
      if (tama_energy >= 100) { tama_mode = TM_IDLE; tamaSay("gunaydin!"); soundOk(); }
    }
    if (g_g.tap) { tama_mode = TM_IDLE; tamaSay("uyandi"); }
    tamaRenderSleep();
    delay(40);
    return;
  }

  // ------ NORMAL / YEMEK MENUSU ------
  if (g_g.swipeUp && tama_mode == TM_IDLE) {
    tama_panel = true; soundTap();
  } else if (g_g.tap) {
    bool handled = false;
    if (tama_mode == TM_FEED) {
      if (g_g.y >= 150 && g_g.y <= 270) {
        tamaFeed(g_g.x > CXi);
        tama_mode = TM_IDLE; handled = true;
      } else { tama_mode = TM_IDLE; handled = true; }     // disari = vazgec
    } else if (g_g.y > 386) {
      // alt butonlar
      for (int i = 0; i < 5; i++) {
        int dx = g_g.x - TAMA_BTNS[i].x, dy = g_g.y - 410;
        if (dx * dx + dy * dy <= 30 * 30) {
          handled = true;
          soundTap();
          switch (TAMA_BTNS[i].k) {
            case 'F': tama_mode = TM_FEED; break;
            case 'G':
              if (tama_energy < 12) tamaSay("cok yorgun...");
              else tamaGameStart();
              break;
            case 'C': tamaClean(); break;
            case 'M': tamaMedicine(); break;
            case 'S': tama_mode = TM_SLEEP; tamaSay("iyi geceler"); break;
          }
          break;
        }
      }
    }
    if (!handled) {
      // pet'i sev
      tama_happy  = min(100.0f, tama_happy + 8);
      tama_energy = min(100.0f, tama_energy + 2);
      tpartBurst(CXi + (int)tama_wanderX, CYi - 40, 'h', C_DANGER, 3);
      if (now - tama_petXpAt > 1500) { tamaAddXP(1); tama_petXpAt = now; }
      if (g_soundTheme) soundPlay(1175, 30);
    }
  }

  // gurultu heyecanlandirir (uyanikken)
  if (loud > 0.55f && tama_energy > 20) {
    tama_happy  = min(100.0f, tama_happy + 0.4f);
    tama_energy = max(0.0f, tama_energy - 0.15f);
  }

  // yavas bozulma + yasam olaylari (saniyede bir)
  if (now - tama_lastTick > 1000) {
    float dt = (now - tama_lastTick) / 1000.0f;
    tama_lastTick = now;
    tama_hunger = min(100.0f, tama_hunger + 0.6f * dt);
    float drain = 0.3f * dt
                + (tama_hunger > 70 ? 0.5f * dt : 0)
                + (tama_poopN  > 0  ? 0.3f * dt * tama_poopN : 0)
                + (tama_sick        ? 0.8f * dt : 0);
    tama_happy = max(0.0f, tama_happy - drain);
    if (tama_loudAvg < 0.2f) tama_energy = min(100.0f, tama_energy + 0.8f * dt);
    else                     tama_energy = max(0.0f, tama_energy - 0.2f * dt);

    // kaka: tok pet ~6-10 dakikada bir ihtiyac duyar
    if (tama_poopN < 3 && tama_hunger < 60 && rnd(500) == 0) {
      tama_poopN++;
      tamaSay("oops...");
      if (g_soundTheme) soundPlay(300, 60);
    }
    // hastalik riski: pislik + acliktan
    if (!tama_sick && (tama_poopN >= 2 || tama_hunger > 92) && rnd(900) == 0) {
      tama_sick = true;
      tamaSay("hasta oldu!", 2600);
      soundError();
    }
    // otomatik ruh hali efektleri
    if (tama_happy > 85 && tama_mode == TM_IDLE && rnd(7) == 0)
      tpartSpawn(CXi + (int)tama_wanderX + rnd(40) - 20, CYi - 50,
                 (rnd(20) - 10) / 20.0f, -0.7f, 34, C_DANGER, 'h');
    if (tama_hunger > 80 && rnd(8) == 0)
      tpartSpawn(CXi + (int)tama_wanderX + 40, CYi - 30, 0.3f, 0.6f, 22,
                 rgb(120, 200, 255), 'c');
  }

  // yas sayaci (acikken gercek zamanli)
  if (now - tama_lastMin > 60000) { tama_lastMin = now; tama_ageMin++; }

  // ------ bosta gezinme / yurume ------
  if (tama_mode == TM_IDLE && now < tama_eatEnd) {
    // yerken durur
  } else if (tama_mode == TM_IDLE && now > tama_wanderAt) {
    tama_wanderTgt = (rnd(2) ? 1 : -1) * (float)(rnd(70));
    tama_wanderAt = now + 2500 + rnd(4000);
  }
  if (tama_mode == TM_IDLE && now >= tama_eatEnd) {
    float d = tama_wanderTgt - tama_wanderX;
    if (fabsf(d) > 0.6f) {
      tama_wanderX += (d > 0 ? 0.6f : -0.6f);
      tama_faceDir = (d > 0 ? 1 : -1);
    } else tama_faceDir = 0;
  } else tama_faceDir = 0;

  // ------ IMU: egilme / bakis / sallayinca basi doner ------
  if (motionPresent()) {
    if (motionShakeEvent() && now > tama_dizzyUntil) {
      tama_dizzyUntil = now + 2400;
      tama_happy = max(0.0f, tama_happy - 3);
      tamaSay("basim dondu!", 2400);
      if (g_soundTheme) { soundPlay(300, 50); soundPlay(220, 70); }
    } else if (motionFreefall() && now > tama_msgUntil && now > tama_dizzyUntil) {
      tamaSay("iiii dusuyorum!", 700);
    }
    float roll = motionRoll();
    tama_leanX = (int)constrain(roll * 0.7f, -44.0f, 44.0f);   // body lean
    tama_gazeX = (int)constrain(roll * 0.12f, -6.0f, 6.0f);    // gaze toward gravity
    tama_gazeY = (int)constrain(motionPitch() * 0.10f, -5.0f, 5.0f);
  }

  // hoplama + goz kirpma (yururken daha canli ziplar)
  float bobAmp = (tama_faceDir != 0) ? 9.0f : 6.0f;
  tama_bob = (int)(bobAmp * sinf(now * (tama_faceDir != 0 ? 0.012f : 0.004f)));
  if (now > tama_blinkAt) {
    tama_blink = !tama_blink;
    tama_blinkAt = now + (tama_blink ? 120 : (1500 + rnd(2500)));
  }

  if (now - tama_lastSave > 15000) { tamaSave(); tama_lastSave = now; }

  tamaRenderMain();
  delay(24);
}
