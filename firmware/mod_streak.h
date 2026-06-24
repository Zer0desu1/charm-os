// ============================================================================
// mod_streak.h - Daily-tap streak counter
//   Tap the screen once per day to keep your streak alive. Miss a day -> reset.
//   Stats stored in NVS so the streak survives reboots. The module also keeps a
//   32-day completion history (a row of dots for the last week), celebrates
//   milestones (1 week / 1 month / 100 days / 1 year) and shows progress toward
//   the next one on the ring.
//
//   Day math uses the epoch DAY NUMBER (days since 1970-01-01), not a YYYYMMDD
//   integer, so consecutive days are detected correctly across month/year
//   boundaries. Old YYYYMMDD records are migrated on first load.
// ============================================================================
#pragma once

#include "platform.h"

static int      streak_count   = 0;
static int      streak_longest = 0;
static uint32_t streak_lastDay = 0;     // epoch day number of last completed day (0=none)
static uint32_t streak_mask    = 0;     // bit k = (lastDay - k) completed
static uint32_t streak_celebrate = 0;
static int      streak_msHit   = 0;     // milestone just reached (for the banner)

static const int STREAK_MS[] = { 7, 30, 100, 365 };   // milestone day counts
static const int STREAK_NMS  = (int)(sizeof(STREAK_MS) / sizeof(STREAK_MS[0]));

// ---- day helpers -----------------------------------------------------------
// Days since the Unix epoch (1970-01-01); 0 means "no synced time".
static uint32_t timeDay() { return timeValid() ? (timeNow() / 86400) : 0; }

// Convert an epoch day number to a YYYYMMDD integer (0 -> 0). Used for display
// and for the backup blob (backup.h still reads strk_d as YYYYMMDD).
static int dayToYmd(uint32_t days) {
  if (days == 0) return 0;
  int Y = 1970; uint32_t left = days;
  while (true) {
    bool leap = (Y % 4 == 0 && Y % 100 != 0) || (Y % 400 == 0);
    uint32_t yd = leap ? 366 : 365;
    if (left < yd) break;
    left -= yd; Y++;
  }
  static const uint8_t md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (Y % 4 == 0 && Y % 100 != 0) || (Y % 400 == 0);
  int M = 1, D = 1;
  for (int i = 0; i < 12; i++) {
    int len = md[i] + (i == 1 && leap ? 1 : 0);
    if ((int)left < len) { M = i + 1; D = left + 1; break; }
    left -= len;
  }
  return Y * 10000 + M * 100 + D;
}

// Convert a YYYYMMDD integer back to an epoch day number (for migrating the old
// NVS format). 0 -> 0.
static uint32_t ymdToDay(int ymd) {
  if (ymd <= 0) return 0;
  int Y = ymd / 10000, M = (ymd / 100) % 100, D = ymd % 100;
  if (M < 1 || M > 12 || D < 1) return 0;
  uint32_t days = 0;
  for (int y = 1970; y < Y; y++) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    days += leap ? 366 : 365;
  }
  static const uint8_t md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (Y % 4 == 0 && Y % 100 != 0) || (Y % 400 == 0);
  for (int m = 1; m < M; m++) days += md[m - 1] + (m == 2 && leap ? 1 : 0);
  return days + (uint32_t)(D - 1);
}

// ---- persistence -----------------------------------------------------------
static void streakLoad() {
  streak_count   = setGetInt("strk_n",   0);
  streak_longest = setGetInt("strk_max", 0);
  uint32_t ld    = (uint32_t)setGetInt("strk_day", 0);   // new key (epoch day)
  if (ld == 0) {                                         // migrate old YYYYMMDD
    int old = setGetInt("strk_d", 0);
    if (old > 0) ld = ymdToDay(old);
  }
  streak_lastDay = ld;
  streak_mask    = (uint32_t)setGetInt("strk_mask", ld ? 1u : 0u);
}
static void streakSave() {
  setPutInt("strk_n",    streak_count);
  setPutInt("strk_max",  streak_longest);
  setPutInt("strk_day",  (int)streak_lastDay);
  setPutInt("strk_mask", (int)streak_mask);
  setPutInt("strk_d",    dayToYmd(streak_lastDay));      // keep backup.h happy
}

static void streak_enter() { streakLoad(); }

// Is `n` exactly a milestone? Returns the value (for the banner) or 0.
static int streakMilestone(int n) {
  for (int i = 0; i < STREAK_NMS; i++) if (n == STREAK_MS[i]) return STREAK_MS[i];
  return 0;
}
static const char* streakMsLabel(int ms) {
  switch (ms) {
    case 7:   return "1 HAFTA!";
    case 30:  return "1 AY!";
    case 100: return "100 GUN!";
    case 365: return "1 YIL!";
    default:  return "";
  }
}

// Completed-state of the day `j` days before today (j=0 = today), read from the
// rolling history mask which is anchored at streak_lastDay.
static bool streakDoneDaysAgo(uint32_t today, int j) {
  if (streak_lastDay == 0 || today == 0) return false;
  int off = (int)(today - streak_lastDay);     // days since last completion
  int bi  = j - off;                           // bit index in the mask
  if (bi < 0 || bi >= 32) return false;
  return (streak_mask >> bi) & 1u;
}

static void streak_tick() {
  uint32_t today = timeDay();
  bool haveTime    = today != 0;
  bool tappedToday = haveTime && streak_lastDay == today;
  bool alive       = haveTime && streak_lastDay != 0 && (today - streak_lastDay) <= 1;
  bool celebrating = millis() < streak_celebrate;

  if (g_g.tap && haveTime && !tappedToday) {
    uint32_t gap = (streak_lastDay == 0) ? 9999u : (today - streak_lastDay);
    if (gap == 1) streak_count++;            // consecutive day
    else          streak_count = 1;          // first ever, or a gap -> restart
    if (streak_count > streak_longest) streak_longest = streak_count;

    // roll the history window forward by `gap` days, then mark today done
    uint32_t shift = (streak_lastDay == 0) ? 0 : gap;
    streak_mask = (shift >= 32) ? 0u : (streak_mask << shift);
    streak_mask |= 1u;
    streak_lastDay = today;
    streakSave();

    streak_msHit = streakMilestone(streak_count);
    streak_celebrate = millis() + (streak_msHit ? 3600 : 2200);
    celebrating = true; tappedToday = true; alive = true;
  }

  // next milestone (for the progress ring)
  int nextMs = 0;
  for (int i = 0; i < STREAK_NMS; i++) if (streak_count < STREAK_MS[i]) { nextMs = STREAK_MS[i]; break; }

  gfx->fillScreen(C_BG);
  textCenter("STREAK", CXi, 48, 2, C_DIM);

  // progress ring: how far toward the next milestone (full when celebrating)
  fillRing(CXi, CYi, 190, 210, 0, 360, C_BG2);
  if (streak_count > 0) {
    int sweep = celebrating ? 360
              : (nextMs > 0 ? (int)(360.0f * streak_count / nextMs) : 360);
    fillRing(CXi, CYi, 190, 210, 0, constrain(sweep, 0, 360), C_GOLD);
  }

  // big day count
  char nb[8]; snprintf(nb, sizeof(nb), "%d", streak_count);
  textCenter(nb, CXi, CYi - 28, 9, celebrating ? C_OK : C_TEXT);
  textCenter("gun", CXi, CYi + 66, 2, C_DIM);

  // last-7-days history dots (leftmost = 6 days ago, rightmost = today)
  const int dotsY = CYi + 120, gap = 30, n = 7;
  int x0 = CXi - (n - 1) * gap / 2;
  for (int i = 0; i < n; i++) {
    int j = (n - 1) - i;                       // i=0 -> 6 days ago, i=6 -> today
    int x = x0 + i * gap;
    bool done = streakDoneDaysAgo(today, j);
    if (done) gfx->fillCircle(x, dotsY, 6, j == 0 ? C_OK : C_GOLD);
    else      gfx->drawCircle(x, dotsY, 6, C_BG2);
  }

  // status / call to action
  if (!haveTime) {
    textCenter("saat senkronu yok", CXi, 408, 2, C_WARN);
  } else if (celebrating && streak_msHit) {
    textCenter(streakMsLabel(streak_msHit), CXi, 408, 3, C_OK);
  } else if (tappedToday) {
    textCenter(celebrating ? "+1 GUN" : "bugun islendi", CXi, 408, 2,
               celebrating ? C_OK : C_DIM);
  } else if (alive) {
    textCenter("dokun: bugun isle", CXi, 408, 2, C_ACCENT);
  } else {
    textCenter("seri koptu - dokun: basla", CXi, 408, 2, C_WARN);
  }

  // record + next-milestone hint
  char rb[28];
  if (nextMs > 0 && !celebrating)
    snprintf(rb, sizeof(rb), "rekor: %d   hedef: %d", streak_longest, nextMs);
  else
    snprintf(rb, sizeof(rb), "rekor: %d", streak_longest);
  textCenter(rb, CXi, 440, 2, C_DIM);

  present();
  delay(40);
}
