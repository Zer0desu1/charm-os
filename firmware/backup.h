// ============================================================================
// backup.h - Export / restore all persistent device state as JSON.
//   The phone can pull a backup (/backup) and store it in AsyncStorage, and
//   push it back (/restore) to recover everything (streak, pomodoro count,
//   tama stats, collar info, badge, charm name, friends, capsules, ...).
//   No external JSON library used; small hand-rolled writer + reader.
// ============================================================================
#pragma once

#include "platform.h"
// Relies on caps_arr / caps_count from mod_capsule.h (included earlier).

static String backupEsc(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", " ");
  return s;
}

static String backupExport() {
  String s = "{";
  s += "\"id\":\""; s += g_devId; s += "\",";
  s += "\"dev_name\":\""; s += backupEsc(setGetStr("dev_name", "")); s += "\",";
  s += "\"snd_th\":";    s += setGetInt("snd_th", 0); s += ",";
  s += "\"ts\":";   s += (unsigned long) timeNow(); s += ",";

  s += "\"streak\":{\"n\":"; s += setGetInt("strk_n", 0);
  s += ",\"max\":"; s += setGetInt("strk_max", 0);
  s += ",\"d\":";   s += setGetInt("strk_d", 0); s += "},";

  s += "\"pomo\":{\"n\":"; s += setGetInt("pm_n", 0); s += "},";

  s += "\"tama\":{\"hun\":"; s += setGetInt("t_hun", 30);
  s += ",\"hap\":"; s += setGetInt("t_hap", 70);
  s += ",\"ene\":"; s += setGetInt("t_ene", 80); s += "},";

  s += "\"collar\":{\"name\":\"";  s += backupEsc(setGetStr("col_name", ""));
  s += "\",\"phone\":\"";          s += backupEsc(setGetStr("col_phone", ""));
  s += "\",\"owner\":\"";          s += backupEsc(setGetStr("col_owner", "")); s += "\"},";

  s += "\"badge\":{\"state\":";    s += setGetInt("bdg_state", 0);
  s += ",\"name\":\"";             s += backupEsc(setGetStr("bdg_name", "")); s += "\"},";

  s += "\"charm\":{\"name\":\"";   s += backupEsc(setGetStr("charm_name", "")); s += "\"},";

  s += "\"friends\":\"";           s += backupEsc(setGetStr("friends", "")); s += "\",";

  s += "\"capsules\":[";
  for (int i = 0; i < caps_count; i++) {
    if (i) s += ",";
    s += "{\"openAt\":"; s += (unsigned long) caps_arr[i].openAt;
    s += ",\"opened\":"; s += (int) caps_arr[i].opened;
    s += ",\"read\":";   s += (int) caps_arr[i].read;
    s += ",\"emoji\":\"";s += backupEsc(String(caps_arr[i].emoji));
    s += "\",\"text\":\"";s+= backupEsc(String(caps_arr[i].text)); s += "\"}";
  }
  s += "]}";
  return s;
}

// --- Minimal JSON reader (matches our own backupExport schema only) ---

static int bk_find(const String& s, const String& key, int from = 0) {
  return s.indexOf("\"" + key + "\"", from);
}

static int bk_int(const String& s, const String& key, int defv, int from = 0) {
  int i = bk_find(s, key, from);
  if (i < 0) return defv;
  int c = s.indexOf(":", i);
  if (c < 0) return defv;
  c++;
  while (c < (int)s.length() && (s[c] == ' ' || s[c] == '\n')) c++;
  return atoi(s.c_str() + c);
}

static String bk_str(const String& s, const String& key, const String& defv, int from = 0) {
  int i = bk_find(s, key, from);
  if (i < 0) return defv;
  int c = s.indexOf(":", i);
  if (c < 0) return defv;
  int q = s.indexOf("\"", c);
  if (q < 0) return defv;
  String out;
  int j = q + 1;
  while (j < (int)s.length()) {
    char ch = s[j];
    if (ch == '\\' && j + 1 < (int)s.length()) { out += s[j + 1]; j += 2; continue; }
    if (ch == '"') break;
    out += ch; j++;
  }
  return out;
}

static bool backupRestore(const String& json) {
  if (json.length() < 5 || json[0] != '{') return false;

  setPutInt("strk_n",   bk_int(json, "n",   setGetInt("strk_n", 0), bk_find(json, "streak")));
  setPutInt("strk_max", bk_int(json, "max", setGetInt("strk_max", 0), bk_find(json, "streak")));
  setPutInt("strk_d",   bk_int(json, "d",   setGetInt("strk_d", 0), bk_find(json, "streak")));

  setPutInt("pm_n",     bk_int(json, "n",   setGetInt("pm_n", 0), bk_find(json, "pomo")));

  int tIdx = bk_find(json, "tama");
  setPutInt("t_hun",    bk_int(json, "hun", setGetInt("t_hun", 30), tIdx));
  setPutInt("t_hap",    bk_int(json, "hap", setGetInt("t_hap", 70), tIdx));
  setPutInt("t_ene",    bk_int(json, "ene", setGetInt("t_ene", 80), tIdx));

  int cIdx = bk_find(json, "collar");
  setPutStr("col_name", bk_str(json, "name",  setGetStr("col_name", ""),  cIdx));
  setPutStr("col_phone",bk_str(json, "phone", setGetStr("col_phone", ""), cIdx));
  setPutStr("col_owner",bk_str(json, "owner", setGetStr("col_owner", ""), cIdx));

  int bIdx = bk_find(json, "badge");
  setPutInt("bdg_state",bk_int(json, "state", setGetInt("bdg_state", 0), bIdx));
  setPutStr("bdg_name", bk_str(json, "name",  setGetStr("bdg_name", ""),  bIdx));

  int chIdx = bk_find(json, "charm");
  setPutStr("charm_name", bk_str(json, "name", setGetStr("charm_name", ""), chIdx));

  setPutStr("friends",  bk_str(json, "friends", setGetStr("friends", "")));
  setPutStr("dev_name", bk_str(json, "dev_name", setGetStr("dev_name", "")));
  setPutInt("snd_th",   bk_int(json, "snd_th",   setGetInt("snd_th", 0)));

  // capsules array: parse one object at a time inside "capsules":[ ... ]
  int caStart = json.indexOf("\"capsules\"");
  if (caStart >= 0) {
    int br = json.indexOf("[", caStart);
    if (br >= 0) {
      caps_count = 0;
      int p = br + 1;
      while (p < (int)json.length() && caps_count < CAPSULE_MAX) {
        int ob = json.indexOf("{", p);
        if (ob < 0) break;
        int oe = json.indexOf("}", ob);
        if (oe < 0) break;
        String obj = json.substring(ob, oe + 1);
        Capsule& c = caps_arr[caps_count];
        memset(&c, 0, sizeof(c));
        c.openAt = (uint32_t) bk_int(obj, "openAt", 0);
        c.opened = (uint8_t)  bk_int(obj, "opened", 0);
        c.read   = (uint8_t)  bk_int(obj, "read",   0);
        String em = bk_str(obj, "emoji", "");
        em.toCharArray(c.emoji, sizeof(c.emoji));
        String tx = bk_str(obj, "text", "");
        tx.toCharArray(c.text, sizeof(c.text));
        if (c.openAt > 0) caps_count++;
        p = oe + 1;
        int endBr = json.indexOf("]", p);
        if (endBr >= 0 && endBr < json.indexOf("{", p)) break;
      }
      capsSave();
    }
  }

  return true;
}
