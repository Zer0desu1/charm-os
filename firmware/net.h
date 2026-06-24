// ============================================================================
// net.h - WiFi AP + HTTP API
//   Endpoints:
//     GET  /              status + module list (JSON)
//     GET  /modules       JSON of modules {id,name,enabled}
//     GET  /module?id=..&en=0|1     enable/disable a module
//     GET  /open?id=..    open a module (or id=home)
//     GET  /mode?...      board-game config: tool,sides,count,slots,secs
//     GET  /charm?name=.. set bag-charm name tag
//     GET  /upload        in-browser photo/GIF uploader UI
//     POST /album         streamed 466x466 RGB565-BE photo -> Album
//     POST /animation?fc=&fm=&w=&h=   streamed RGB565-BE frames -> Charm (GIF)
// ============================================================================
#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>          // OTA firmware update (/ota uploader + /update POST)
// JPEG decoder for the Expo app's photo uploads (install "TJpg_Decoder" by
// Bodmer from Library Manager). If absent, /photo replies with a hint.
#if defined(__has_include) && __has_include(<TJpg_Decoder.h>)
#  include <TJpg_Decoder.h>
#  define HAVE_JPEG 1
#endif
#include "platform.h"
#include "mpu.h"             // step / activity stats for /stats
#include "mod_clock.h"       // watch-face gallery for /face
#include "mod_charm.h"
#include "mod_game.h"
#include "mod_collar.h"
#include "mod_badge.h"
#include "mod_notify.h"
#include "mod_finder.h"
#include "mod_lovebox.h"
#include "mod_album.h"
#include "mod_maps.h"
#include "mod_music.h"
#include "mod_assistant.h"
#include "backup.h"
#include "cloud.h"

static char AP_SSID[20] = "Vecta";    // gets "-XXXX" suffix in netBegin()
static const char* AP_PASS = "12345678";

static WebServer server(80);

// ---- helpers ----  (findModuleIdx now lives in platform.h)

static String modulesJson() {
  String s = "[";
  for (int i = 0; i < g_moduleCount; i++) {
    if (i) s += ",";
    s += "{\"id\":\""; s += g_modules[i].id;
    s += "\",\"name\":\""; s += g_modules[i].name;
    s += "\",\"enabled\":"; s += g_modules[i].enabled ? "true" : "false";
    s += "}";
  }
  s += "]";
  return s;
}

static void hInfo() {
  String s = "{\"device\":\"Vecta\",\"id\":\""; s += g_devId;
  s += "\",\"name\":\""; s += deviceName();
  s += "\",\"psramFree\":"; s += ESP.getFreePsram();
  s += ",\"psramSize\":"; s += ESP.getPsramSize();
  // Flash / app-partition usage so the phone can show free space. Modules are
  // baked into one firmware image, so "used" = current sketch size; the OTA slot
  // (appSlot) is the capacity a new firmware can occupy.
  s += ",\"sketchUsed\":"; s += ESP.getSketchSize();
  s += ",\"appSlot\":"; s += ESP.getFreeSketchSpace();
  s += ",\"flashSize\":"; s += ESP.getFlashChipSize();
  s += ",\"touch\":"; s += g_touchOk ? "true" : "false";
  s += ",\"sound\":"; s += g_soundTheme;
  s += ",\"staJoined\":"; s += cloud_staJoined ? "true" : "false";
  s += ",\"staSsid\":\""; s += setGetStr("wifi_ssid", "");
  s += "\",\"staIp\":\""; s += WiFi.localIP().toString();
  s += "\",\"modules\":"; s += modulesJson();
  s += "}";
  server.send(200, "application/json", s);
}

// Control panel served at "/" - forms & buttons for everything (no manual URLs).
static void hHome() {
  static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vecta</title>
<style>
body{font-family:sans-serif;background:#0b1018;color:#eef4fa;margin:0 auto;padding:16px;max-width:520px}
h1{color:#ffc440;text-align:center;margin:6px}h3{color:#9aa7ff;margin:0 0 8px}
.card{background:#131a2b;border-radius:14px;padding:14px;margin:12px 0}
input,select,button{font-size:16px;padding:11px;margin:5px 0;border-radius:10px;border:0;width:100%;box-sizing:border-box}
input,select{background:#0b1018;color:#eef;border:1px solid #2a3550}
button{background:#6366f1;color:#fff;font-weight:bold}
a.btn{display:block;text-align:center;background:#22c55e;color:#04210f;text-decoration:none;padding:13px;border-radius:10px;font-weight:bold}
.s{color:#9ad;font-size:14px;min-height:1.2em}.ok{color:#22c55e}.warn{color:#f59e0b}
</style></head><body>
<h1>Vecta</h1>
<div class="s" id="st" style="text-align:center">...</div>

<div class="card"><h3>Foto &amp; GIF</h3>
<a class="btn" href="/upload">Foto / GIF Yukle</a></div>

<div class="card"><h3>Ev Wi-Fi</h3>
<div class="s" id="wst">...</div>
<input id="ssid" placeholder="Wi-Fi adi (SSID)">
<input id="pass" type="password" placeholder="Wi-Fi sifresi">
<button onclick="joinWifi()">Baglan</button></div>

<div class="card"><h3>Cihaz adi</h3>
<input id="nm" maxlength="16" placeholder="isim">
<button onclick="saveName()">Kaydet</button></div>

<div class="card"><h3>Ses temasi</h3>
<select id="snd"><option value="0">Sessiz</option><option value="1">Klasik</option>
<option value="2">Yumusak</option><option value="3">Retro</option></select>
<button onclick="saveSound()">Kaydet</button></div>

<script>
function $(i){return document.getElementById(i);}
function api(path,p){return fetch(path+(p?('?'+new URLSearchParams(p)):''));}
async function refresh(){
  try{
    var j=await(await fetch('/info')).json();
    $('st').textContent='ID '+j.id+' | PSRAM '+Math.round(j.psramFree/1024)+'KB | dokunmatik '+(j.touch?'OK':'yok');
    $('nm').value=j.name||''; $('snd').value=j.sound||0; if(j.staSsid)$('ssid').value=j.staSsid;
    if(j.staJoined)$('wst').innerHTML='<span class=ok>Bagli: '+j.staSsid+' ('+j.staIp+')</span><br>http://vecta.local';
    else $('wst').innerHTML='<span class=warn>Ev Wi-Fi baglanmadi</span>';
  }catch(e){$('st').textContent='...';}
}
async function joinWifi(){
  $('wst').textContent='Baglaniyor... (10-15 sn, baglanti gecici kopabilir)';
  try{
    var j=await(await api('/wifi',{ssid:$('ssid').value,pass:$('pass').value})).json();
    if(j.joined)$('wst').innerHTML='<span class=ok>Baglandi! IP: '+j.ip+'</span><br>Telefonu ev Wi-Fi\'ina al, sonra <b>http://vecta.local</b>';
    else $('wst').innerHTML='<span class=warn>Baglanamadi. SSID/sifre dogru mu?</span>';
  }catch(e){
    $('wst').innerHTML='Baglanti koptu olabilir (AP kanali degisti). Telefonu <b>ev Wi-Fi</b>\'ina al ve <b>http://vecta.local</b> ac.';
  }
}
async function saveName(){await api('/name',{n:$('nm').value});$('st').textContent='Isim kaydedildi.';}
async function saveSound(){await api('/sound',{theme:$('snd').value});$('st').textContent='Ses temasi kaydedildi.';}
refresh();
</script></body></html>)HTML";
  server.send_P(200, "text/html", PAGE);
}

// Live status for the Finder phone UI.
static void hStatus() {
  String s = "{\"rssi\":"; s += finderStationRssi();
  s += ",\"id\":\""; s += g_devId; s += "\"}";
  server.send(200, "application/json", s);
}

// Phone pushes its GPS so device can compute bearing to the buddy.
static void hGps() {
  double lat = atof(server.arg("lat").c_str());
  double lon = atof(server.arg("lon").c_str());
  if (lat != 0 || lon != 0) setMyLoc(lat, lon);
  server.send(200, "text/plain", "ok");
}

// Phone "Caldir" -> finder attention flash (and open finder).
static void hFind() {
  finderFindMe();
  int idx = findModuleIdx("finder");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

// Lovebox: phone pushes a short message + emoji.
static void hLove() {
  String text = trAscii(server.arg("text")), emoji = server.arg("emoji");
  loveSet(text, emoji);
  loveBroadcast(text, emoji);          // -> nearby paired Vecta's notification (ESP-NOW)
  cloudEnqueueLove(text, emoji);       // also bridge to internet relay
  int idx = findModuleIdx("lovebox");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

// --- Friend list ---
static void hFriends() {
  if (server.hasArg("add"))    friendsAdd(server.arg("add"));
  if (server.hasArg("remove")) friendsRemove(server.arg("remove"));
  String s = "{\"list\":\""; s += friendsList(); s += "\",\"me\":\""; s += g_devId; s += "\"}";
  server.send(200, "application/json", s);
}

// Now-playing: phone pushes current track from Spotify (or other source).
static void hNowPlaying() {
  musicSet(server.arg("track"), server.arg("artist"), server.arg("album"),   // raw UTF-8 (accents drawn)
           server.hasArg("source") ? server.arg("source") : String("Spotify"),
           server.arg("playing") == "1",
           (uint32_t) strtoul(server.arg("pos").c_str(), nullptr, 10),
           (uint32_t) strtoul(server.arg("dur").c_str(), nullptr, 10));
  // auto-open only from the launcher (now-playing is pushed periodically;
  // opening on every push would keep yanking the user out of other modules).
  // When merged, open the unified Sozler view (lyrics-or-banner) instead of Muzik.
  if (g_app == APP_LAUNCHER) {
    int idx = g_mlMerge ? findModuleIdx("lyrics") : -1;
    if (idx < 0 || !g_modules[idx].enabled) idx = findModuleIdx("music");
    if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  }
  server.send(200, "text/plain", "ok");
}

// Friendly device name (NVS).
static void hName() {
  if (server.hasArg("n")) deviceSetName(trAscii(server.arg("n")));
  String s = "{\"name\":\""; s += deviceName(); s += "\"}";
  server.send(200, "application/json", s);
}

// Sound theme picker.
static void hSound() {
  if (server.hasArg("theme")) soundSetTheme(server.arg("theme").toInt());
  String s = "{\"theme\":"; s += g_soundTheme; s += "}";
  server.send(200, "application/json", s);
}

// ---- XiaoZhi AI server config: /xiaozhi?url=ws://IP:8000/xiaozhi/v1/&token=..
static void hXiaozhi() {
  xiaozhiConfig(server.arg("url"), server.arg("token"));
  String s = "{\"url\":\""; s += setGetStr("xz_url", ""); s += "\"}";
  server.send(200, "application/json", s);
}

// ---- OpenClaw: telefon tarayicisindan sohbet (uygulamasiz) ----------------
static const char CLAW_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Vecta + OpenClaw</title><style>
body{font-family:sans-serif;background:#0b0f1d;color:#eef;margin:0;padding:14px}
h2{color:#ff7850}#log{min-height:200px}
.q{background:#1c2444;border-radius:12px;padding:9px 12px;margin:8px 0}
.a{background:#15301d;border-radius:12px;padding:9px 12px;margin:8px 0;white-space:pre-wrap}
.e{background:#3a1820;border-radius:12px;padding:9px 12px;margin:8px 0}
input,button{font-size:16px;border-radius:10px;border:1px solid #345;padding:9px}
input{width:100%;box-sizing:border-box;background:#121830;color:#eef;margin:3px 0}
button{background:#ff7850;color:#100;border:0;font-weight:bold;width:100%}
details{margin-top:18px;color:#9ab}</style></head><body>
<h2>OpenClaw</h2><div id=log></div>
<input id=q placeholder="Sorunu yaz..."><button onclick=ask()>Gonder</button>
<details><summary>Gateway ayarlari</summary>
<input id=cu placeholder="http://192.168.1.50:18789">
<input id=ct placeholder="token (varsa)">
<input id=cm placeholder="model (varsayilan: openclaw)">
<button onclick=cfg()>Kaydet</button></details>
<script>
const L=document.getElementById('log');
function add(c,t){const d=document.createElement('div');d.className=c;d.textContent=t;L.appendChild(d);d.scrollIntoView();return d}
async function ask(){const q=document.getElementById('q').value.trim();if(!q)return;
document.getElementById('q').value='';add('q',q);
const r=await fetch('/claw/ask?q='+encodeURIComponent(q));
if(!r.ok){add('e','cihaz mesgul');return}
const d=add('a','...');
for(;;){await new Promise(s=>setTimeout(s,1200));
const p=await(await fetch('/claw/poll')).json();
if(p.state==1)continue;d.textContent=p.a;if(p.state==3)d.className='e';break}}
async function cfg(){const u=document.getElementById('cu').value,t=document.getElementById('ct').value,m=document.getElementById('cm').value;
await fetch('/claw/cfg?url='+encodeURIComponent(u)+'&token='+encodeURIComponent(t)+'&model='+encodeURIComponent(m));
add('a','ayar kaydedildi')}
document.getElementById('q').addEventListener('keydown',e=>{if(e.key=='Enter')ask()});
</script></body></html>)HTML";

static String clawJsonEsc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static void hClawPage() { server.send_P(200, "text/html", CLAW_PAGE); }

static void hClawAsk() {
  String q = trAscii(server.arg("q"));
  if (!q.length()) { server.send(400, "text/plain", "q?"); return; }
  if (!ocAsk(q))   { server.send(409, "text/plain", "busy"); return; }
  int idx = findModuleIdx("openclaw");          // cihazda sohbet ekranini ac
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

static void hClawPoll() {
  String s = String("{\"state\":") + oc_state + ",\"a\":\"" + clawJsonEsc(oc_a) + "\"}";
  server.send(200, "application/json", s);
}

static void hClawCfg() {
  if (server.arg("url").length())   setPutStr("oc_url", server.arg("url"));
  if (server.arg("token").length()) setPutStr("oc_tok", server.arg("token"));
  if (server.arg("model").length()) setPutStr("oc_model", server.arg("model"));
  server.send(200, "text/plain", "ok");
}

// Display brightness (?b=10..100), persisted to NVS.
static void hBright() {
  if (server.hasArg("b")) displaySetBrightness(server.arg("b").toInt());
  String s = "{\"bright\":"; s += g_brightPct; s += "}";
  server.send(200, "application/json", s);
}

// Phone pulls full state snapshot.
static void hBackup() {
  server.send(200, "application/json", backupExport());
}
// Phone pushes a previously-saved snapshot back into NVS.
static void hRestore() {
  String body = server.arg("plain");        // for POST body
  if (!body.length() && server.hasArg("json")) body = server.arg("json");
  bool ok = body.length() && backupRestore(body);
  server.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "bad");
}

// Schedule a time capsule. ?text=&emoji=&openAt=(unix seconds)
static void hCapsule() {
  uint32_t openAt = (uint32_t) strtoul(server.arg("openAt").c_str(), nullptr, 10);
  if (openAt < 1000000000UL) { server.send(400, "text/plain", "bad openAt"); return; }
  bool ok = capsAdd(openAt, trAscii(server.arg("text")), server.arg("emoji"));
  server.send(ok ? 200 : 507, "text/plain", ok ? "ok" : "full");
}

// Album art: 160x160 RGB565 big-endian (51200 bytes), streamed as a MULTIPART
// upload. (The ESP WebServer swallows a raw octet-stream body into an arg, and
// binary data full of null bytes can't survive that - so we use the file-upload
// callback like /album/photo/map do.)
static const size_t NP_ART_BYTES = (size_t)M_ART_W * M_ART_H * 2;
static uint8_t* g_npArtBuf  = nullptr;
static size_t   g_npArtRecv = 0;
static bool     g_npArtOom  = false;

static void hNowPlayingArtUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (g_npArtBuf) { heap_caps_free(g_npArtBuf); g_npArtBuf = nullptr; }
    g_npArtRecv = 0;
    g_npArtBuf  = (uint8_t*) heap_caps_malloc(NP_ART_BYTES, MALLOC_CAP_SPIRAM);
    g_npArtOom  = (g_npArtBuf == nullptr);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_npArtBuf && g_npArtRecv + up.currentSize <= NP_ART_BYTES) {
      memcpy(g_npArtBuf + g_npArtRecv, up.buf, up.currentSize);
      g_npArtRecv += up.currentSize;
    }
  }
}
static void hNowPlayingArtDone() {
  if (g_npArtOom || !g_npArtBuf) {
    if (g_npArtBuf) { heap_caps_free(g_npArtBuf); g_npArtBuf = nullptr; }
    server.send(507, "text/plain", "psram"); return;
  }
  if (g_npArtRecv != NP_ART_BYTES) {
    heap_caps_free(g_npArtBuf); g_npArtBuf = nullptr;
    server.send(400, "text/plain", String("incomplete ") + g_npArtRecv + "/" + (unsigned)NP_ART_BYTES);
    return;
  }
  musicSetArt(g_npArtBuf, NP_ART_BYTES);   // musicSetArt copies into m_art
  heap_caps_free(g_npArtBuf); g_npArtBuf = nullptr;
  server.send(200, "text/plain", "ok");
}

// Module auto-rotation. Configure: ?seq=clock:5,weather:10,music:8 (id:secs).
// Control: ?start=1 begins the loop, ?stop=1 returns home. Returns the current
// config as JSON so the phone can show/edit it.
static void hRotate() {
  if (server.hasArg("seq")) { String s = server.arg("seq"); rotSetFromStr(s); rotSave(s); }
  if (server.arg("start") == "1" && g_rotN > 0) g_rotReqStart = true;
  if (server.arg("stop") == "1") { g_rotActive = false; g_requestHome = true; }
  String j = "{\"steps\":[";
  for (int k = 0; k < g_rotN; k++) {
    if (k) j += ",";
    j += "{\"id\":\""; j += g_modules[g_rot[k].modIdx].id;
    j += "\",\"secs\":"; j += g_rot[k].secs; j += "}";
  }
  j += "],\"active\":"; j += g_rotActive ? "true" : "false"; j += "}";
  server.send(200, "application/json", j);
}

// Wi-Fi STA credentials (one-time setup).
static void hWifi() {
  if (server.hasArg("ssid")) {
    wifiSetCredentials(server.arg("ssid"), server.arg("pass"));
    wifiJoinFromNvs();
  }
  String s = "{\"joined\":";
  s += cloud_staJoined ? "true" : "false";
  s += ",\"ssid\":\""; s += setGetStr("wifi_ssid", "");
  s += "\",\"ip\":\""; s += WiFi.localIP().toString();
  s += "\",\"mdns\":\"vecta.local\"}";
  server.send(200, "application/json", s);
}

// Firebase project config (one-time setup).
static void hCloud() {
  cloudSaveConfig(server.arg("apiKey"), server.arg("project"), server.arg("room"));
  String s = "{\"configured\":";
  s += cloudConfigured() ? "true" : "false";
  s += ",\"project\":\""; s += cloud_projectId; s += "\"}";
  server.send(200, "application/json", s);
}

// Album: append one full-screen 466x466 RGB565-BE photo via a streamed
// multipart upload (so the 434 KB frame arrives in small chunks instead of
// the WebServer trying to buffer the whole POST body). GET ?clear=1 clears.
static uint8_t* g_albUpBuf  = nullptr;   // PSRAM buffer for the in-flight frame
static size_t   g_albUpRecv = 0;
static bool     g_albUpOom  = false;     // PSRAM alloc failed for this upload

// Streaming callback: fires START -> many WRITE -> END during the upload.
static void hAlbumUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (g_albUpBuf) { heap_caps_free(g_albUpBuf); g_albUpBuf = nullptr; }
    g_albUpRecv = 0;
    g_albUpBuf  = (uint8_t*) heap_caps_malloc(ALBUM_FRAME, MALLOC_CAP_SPIRAM);
    g_albUpOom  = (g_albUpBuf == nullptr);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_albUpBuf && g_albUpRecv + up.currentSize <= ALBUM_FRAME) {
      memcpy(g_albUpBuf + g_albUpRecv, up.buf, up.currentSize);
      g_albUpRecv += up.currentSize;
    }
  }
  // completion handled in hAlbumDone (called after the request finishes)
}

// Final reply after the upload completes.
static void hAlbumDone() {
  if (g_albUpOom || !g_albUpBuf) {
    if (g_albUpBuf) { heap_caps_free(g_albUpBuf); g_albUpBuf = nullptr; }
    server.send(507, "text/plain", "psram full"); return;
  }
  if (g_albUpRecv != ALBUM_FRAME) {
    heap_caps_free(g_albUpBuf); g_albUpBuf = nullptr;
    server.send(400, "text/plain", String("incomplete ") + g_albUpRecv + "/" + (unsigned)ALBUM_FRAME);
    return;
  }
  if (!albumAppend(g_albUpBuf)) {
    heap_caps_free(g_albUpBuf); g_albUpBuf = nullptr;
    server.send(409, "text/plain", "album full"); return;
  }
  g_albUpBuf = nullptr;                  // ownership moved to the album
  int idx = findModuleIdx("album");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", String("ok count=") + album_count);
}

static void hAlbumClear() {
  if (server.hasArg("clear")) albumClear();
  server.send(200, "text/plain", String("ok count=") + album_count);
}

// ---- JPEG photo upload (for the Expo app) --------------------------------
// POST /photo  multipart/form-data with a JPEG/PNG... actually JPEG file.
// The device decodes it, fits it to 466x466 (centered) and adds it to the
// Album. Expo just sends the picked image file - no RGB565 conversion needed.
static uint8_t* g_jpgBuf  = nullptr;
static size_t   g_jpgRecv = 0;
static bool     g_jpgErr  = false;
static const size_t JPG_MAX = 2 * 1024 * 1024;   // cap (resize big photos in Expo)

#ifdef HAVE_JPEG
static uint8_t* g_jpgTarget = nullptr;   // 466x466 RGB565-BE album frame being filled
// TJpgDec decode callback: write one block into the album frame (big-endian),
// clipped to the 466x466 canvas.
static bool jpgBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp) {
  if (!g_jpgTarget) return true;
  for (int j = 0; j < h; j++) {
    int py = y + j; if (py < 0 || py >= LCD_H) continue;
    for (int i = 0; i < w; i++) {
      int px = x + i; if (px < 0 || px >= LCD_W) continue;
      uint16_t c = bmp[j * w + i];
      size_t o = ((size_t)py * LCD_W + px) * 2;
      g_jpgTarget[o] = c >> 8; g_jpgTarget[o + 1] = c & 0xFF;   // big-endian
    }
  }
  return true;
}
#endif

static void hPhotoUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (g_jpgBuf) { heap_caps_free(g_jpgBuf); g_jpgBuf = nullptr; }
    g_jpgRecv = 0; g_jpgErr = false;
    g_jpgBuf = (uint8_t*) heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
    g_jpgErr = (g_jpgBuf == nullptr);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_jpgErr && g_jpgBuf) {
      if (g_jpgRecv + up.currentSize <= JPG_MAX) {
        memcpy(g_jpgBuf + g_jpgRecv, up.buf, up.currentSize);
        g_jpgRecv += up.currentSize;
      } else g_jpgErr = true;            // photo too big -> ask Expo to resize
    }
  }
}

static void hPhotoDone() {
#ifdef HAVE_JPEG
  if (g_jpgErr || !g_jpgBuf || g_jpgRecv < 100) {
    if (g_jpgBuf) { heap_caps_free(g_jpgBuf); g_jpgBuf = nullptr; }
    server.send(g_jpgErr ? 413 : 507, "text/plain", g_jpgErr ? "photo too big (resize in app)" : "jpeg buffer");
    return;
  }
  uint8_t* frame = (uint8_t*) heap_caps_malloc(ALBUM_FRAME, MALLOC_CAP_SPIRAM);
  if (!frame) { heap_caps_free(g_jpgBuf); g_jpgBuf = nullptr; server.send(507, "text/plain", "psram full"); return; }
  memset(frame, 0, ALBUM_FRAME);         // black background (letterbox)

  uint16_t jw = 0, jh = 0;
  TJpgDec.setSwapBytes(false);
  TJpgDec.getJpgSize(&jw, &jh, g_jpgBuf, g_jpgRecv);
  if (jw == 0 || jh == 0) {
    heap_caps_free(frame); heap_caps_free(g_jpgBuf); g_jpgBuf = nullptr;
    server.send(415, "text/plain", "not a jpeg"); return;
  }
  uint8_t scale = 1;                     // 1/1, 1/2, 1/4, 1/8 until it fits 466
  while ((jw / scale > LCD_W || jh / scale > LCD_H) && scale < 8) scale <<= 1;
  TJpgDec.setJpgScale(scale);
  TJpgDec.setCallback(jpgBlock);
  int ox = (LCD_W - jw / scale) / 2, oy = (LCD_H - jh / scale) / 2;
  g_jpgTarget = frame;
  TJpgDec.drawJpg(ox, oy, g_jpgBuf, g_jpgRecv);
  g_jpgTarget = nullptr;

  heap_caps_free(g_jpgBuf); g_jpgBuf = nullptr;

  // ?cam=1 -> deklansor cekimi: foto kamera modulunde onizleme olarak gosterilir
  // (album'e de kaydedilir; album doluysa frame dogrudan onizlemeye gider).
  bool toCam = server.hasArg("cam");
  bool saved = albumAppend(frame);               // owns frame on success
  if (toCam) {
    if (saved) {
      uint8_t* copy = (uint8_t*) heap_caps_malloc(ALBUM_FRAME, MALLOC_CAP_SPIRAM);
      if (copy) { memcpy(copy, frame, ALBUM_FRAME); cameraSetPreview(copy); }
    } else {
      cameraSetPreview(frame);                   // ownership moves to the preview
    }
    int ci = findModuleIdx("camera");
    if (ci >= 0 && g_modules[ci].enabled) g_requestOpen = ci;
    server.send(200, "text/plain", String("ok cam preview ") + jw + "x" + jh);
    return;
  }
  if (!saved) { heap_caps_free(frame); server.send(409, "text/plain", "album full"); return; }
  int idx = findModuleIdx("album");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", String("ok count=") + album_count + " " + jw + "x" + jh);
#else
  if (g_jpgBuf) { heap_caps_free(g_jpgBuf); g_jpgBuf = nullptr; }
  server.send(501, "text/plain", "TJpg_Decoder kutuphanesi gerekli");
#endif
}

// ---- Maps tile upload (Google Static Maps JPEG from the phone) -----------
// POST /map?lat=&lon=&zoom=  multipart JPEG -> decoded to a 466x466 frame shown
// by the Maps module. The phone fetches the static-map tile (its own GPS as the
// center) and pushes it here, refreshing as the user moves.
static uint8_t* g_mapJpg  = nullptr;
static size_t   g_mapRecv = 0;
static bool     g_mapErr  = false;
static const size_t MAPJPG_MAX = 1024 * 1024;   // static-map tiles are small

static void hMapUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (g_mapJpg) { heap_caps_free(g_mapJpg); g_mapJpg = nullptr; }
    g_mapRecv = 0; g_mapErr = false;
    g_mapJpg = (uint8_t*) heap_caps_malloc(MAPJPG_MAX, MALLOC_CAP_SPIRAM);
    g_mapErr = (g_mapJpg == nullptr);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_mapErr && g_mapJpg) {
      if (g_mapRecv + up.currentSize <= MAPJPG_MAX) {
        memcpy(g_mapJpg + g_mapRecv, up.buf, up.currentSize);
        g_mapRecv += up.currentSize;
      } else g_mapErr = true;
    }
  }
}

static void hMapDone() {
  // mirror=1 -> raw screen-mirror frame (no pin/coord overlay on the device)
  maps_raw = server.hasArg("mirror");
  // record where this tile is centered (and the zoom) for the overlay
  double lat = atof(server.arg("lat").c_str());
  double lon = atof(server.arg("lon").c_str());
  int    zm  = server.arg("zoom").toInt();
  if (lat != 0 || lon != 0) setMyLoc(lat, lon);
  if (!maps_raw) mapsSetLoc(lat, lon, zm);
  maps_refresh = false;                       // request satisfied

#ifdef HAVE_JPEG
  if (g_mapErr || !g_mapJpg || g_mapRecv < 100) {
    if (g_mapJpg) { heap_caps_free(g_mapJpg); g_mapJpg = nullptr; }
    server.send(g_mapErr ? 413 : 507, "text/plain", g_mapErr ? "tile too big" : "jpeg buffer");
    return;
  }
  // mirror akisi icin kare tamponu GERI DONUSUMLU: onceki kareyi free etmek
  // yerine bir sonraki decode icin sakla (434KB malloc+memset/kare ortadan
  // kalkar -> daha kisa kare suresi).
  static uint8_t* mapSpare = nullptr;
  uint8_t* frame = mapSpare ? mapSpare : mapsAllocFrame();
  mapSpare = nullptr;
  if (!frame) { heap_caps_free(g_mapJpg); g_mapJpg = nullptr; server.send(507, "text/plain", "psram full"); return; }

  uint16_t jw = 0, jh = 0;
  TJpgDec.setSwapBytes(false);
  TJpgDec.getJpgSize(&jw, &jh, g_mapJpg, g_mapRecv);
  if (jw == 0 || jh == 0) {
    mapSpare = frame;
    heap_caps_free(g_mapJpg); g_mapJpg = nullptr;
    server.send(415, "text/plain", "not a jpeg"); return;
  }
  uint8_t scale = 1;
  while ((jw / scale > LCD_W || jh / scale > LCD_H) && scale < 8) scale <<= 1;
  // letterbox temizligi sadece kare ekrani TAM kaplamiyorsa gerekli
  // (yansitma kareleri 466x466 gelir -> memset atlanir)
  if (jw / scale < LCD_W || jh / scale < LCD_H) memset(frame, 0, MAP_FRAME);
  TJpgDec.setJpgScale(scale);
  TJpgDec.setCallback(jpgBlock);
  int ox = (LCD_W - jw / scale) / 2, oy = (LCD_H - jh / scale) / 2;
  g_jpgTarget = frame;                         // reuse the photo decoder sink
  TJpgDec.drawJpg(ox, oy, g_mapJpg, g_mapRecv);
  g_jpgTarget = nullptr;

  heap_caps_free(g_mapJpg); g_mapJpg = nullptr;
  mapSpare = mapsSwapImage(frame);             // yeni kare girer, eskisi geri donusur
  int idx = findModuleIdx("maps");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", String("ok ") + jw + "x" + jh);
#else
  if (g_mapJpg) { heap_caps_free(g_mapJpg); g_mapJpg = nullptr; }
  maps_needLib = true;                          // surface a clear hint on the device
  int idx = findModuleIdx("maps");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(501, "text/plain", "TJpg_Decoder kutuphanesi gerekli");
#endif
}

// ---- Vecta -> telefon ekran yansitma --------------------------------------
// GET /screen -> o anki framebuffer, ham RGB565 (little-endian). ?s=2
// (varsayilan) yari cozunurluk: 233x233, kare basina ~106KB. /mirror sayfasi
// bunu dongulu fetch ile canvas'a cizer -> Vecta ekrani tarayicidan izlenir.
static void hScreenRaw() {
  if (!g_haveCanvas || !g_canvas) { server.send(503, "text/plain", "canvas yok"); return; }
  g_mirrorPollAt = millis();   // Yansit modulu "izleyici BAGLI" der + sesler susar
  int s = server.arg("s").toInt(); if (s != 1 && s != 2) s = 2;
  const int w = LCD_W / s, h = LCD_H / s;
  const size_t len = (size_t)w * h * 2;
  uint16_t* fb = g_canvas->getFramebuffer();

  uint16_t* buf = nullptr;
  if (s == 2) {
    buf = (uint16_t*) heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) { server.send(507, "text/plain", "psram"); return; }
    for (int y = 0; y < h; y++) {
      const uint16_t* src = fb + (size_t)(y * 2) * LCD_W;
      uint16_t* dst = buf + (size_t)y * w;
      for (int x = 0; x < w; x++) dst[x] = src[x * 2];
    }
  }
  server.setContentLength(len);
  server.send(200, "application/octet-stream", "");
  server.client().write((const uint8_t*)(buf ? buf : fb), len);
  if (buf) heap_caps_free(buf);
}

// GET /mirror -> tarayicida calisan izleme sayfasi (uygulama gerekmez).
static const char MIRROR_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vecta Ekran</title><style>
body{margin:0;background:#000;color:#8291aa;font:14px sans-serif;
display:flex;flex-direction:column;align-items:center;gap:10px;padding-top:14px}
canvas{width:92vmin;height:92vmin;border-radius:50%;background:#080c18}
</style></head><body>
<canvas id="c" width="233" height="233"></canvas><div id="s">baglaniyor...</div>
<script>
const c=document.getElementById('c'),g=c.getContext('2d'),st=document.getElementById('s');
const img=g.createImageData(233,233);let n=0,t0=Date.now();
const FRAME=166; // hedef ~6 fps: kare butcesi; fetch yavassa hic beklemeden surer
async function loop(){
 const t=Date.now();
 try{
  const r=await fetch('/screen?s=2',{cache:'no-store'});
  if(!r.ok)throw 0;
  const b=new Uint16Array(await r.arrayBuffer());
  const d=img.data;
  for(let i=0;i<b.length;i++){const v=b[i],j=i*4;
   d[j]=(v>>8)&0xF8;d[j+1]=(v>>3)&0xFC;d[j+2]=(v<<3)&0xF8;d[j+3]=255;}
  g.putImageData(img,0,0);
  if(++n&&Date.now()-t0>1000){st.textContent=n+' fps';n=0;t0=Date.now();}
  setTimeout(loop,Math.max(0,FRAME-(Date.now()-t)));
 }catch(e){st.textContent='baglanti yok, yeniden deneniyor...';setTimeout(loop,800);}
}
loop();
</script></body></html>)HTML";
static void hMirrorPage() { server.send_P(200, "text/html", MIRROR_HTML); }

// GET /touch -> telefon yansitmasi (mirror) sirasinda saatte biriken
// dokunuslar; uygulama ceker, AccessibilityService telefona enjekte eder.
static void hTouchPoll() {
  String j = "{\"ev\":[";
  int n = maps_tqN;
  for (int i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"x0\":"; j += maps_tq[i].x0; j += ",\"y0\":"; j += maps_tq[i].y0;
    j += ",\"x1\":"; j += maps_tq[i].x1; j += ",\"y1\":"; j += maps_tq[i].y1;
    j += ",\"ms\":"; j += maps_tq[i].ms; j += '}';
  }
  maps_tqN = 0;
  j += "]}";
  server.send(200, "application/json", j);
}

// GET /lyrics?c=<line>&s=<style>&new=1  -> show the current lyric line as a whole
static void hLyrics() {
  lyricsSet(server.arg("c"), server.arg("s").toInt(), server.hasArg("new"));
  server.send(200, "text/plain", "ok");
}

// Hizli Okuma (RSVP).
//   POST /reader?new=1[&title=..]  first chunk (resets)
//   POST /reader                   more chunks (raw text body, appended)
//   POST /reader?done=1            last chunk -> builds the word index, auto-opens
//   GET  /reader?wpm=|play=1|pause=1|restart=1|seek=DELTA  controls
//   GET  /reader                   status JSON {words,idx,wpm,playing}
static void hReader() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("new")) readerReset(server.arg("title"));
    String body = server.arg("plain");
    if (body.length()) readerAppend(body.c_str(), body.length());
    if (server.hasArg("done")) {
      readerFinalize();
      if (g_app == APP_LAUNCHER) {                  // auto-open only from launcher
        int idx = findModuleIdx("reader");
        if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
      }
    }
    server.send(200, "text/plain", String("ok ") + rd_nw);
    return;
  }
  if (server.hasArg("wpm"))     rdSetWpm(server.arg("wpm").toInt());
  if (server.hasArg("play"))    { rd_play = true; rd_nextMs = millis(); rd_dirty = true; }
  if (server.hasArg("pause"))   { rd_play = false; rd_dirty = true; }
  if (server.hasArg("restart")) { rd_idx = 0; rd_done = false; rd_dirty = true; }
  if (server.hasArg("seek"))    rdSeekTo(rd_idx + server.arg("seek").toInt());
  String j = String("{\"words\":") + rd_nw + ",\"idx\":" + rd_idx +
             ",\"wpm\":" + rd_wpm + ",\"playing\":" + (rd_play ? "true" : "false") + "}";
  server.send(200, "application/json", j);
}

// GET /map?poll=1 -> {refresh,zoom,lat,lon,has} so the phone knows when the
// device wants a fresh tile (e.g. after the user changed zoom on the device).
static void hMapPoll() {
  String j = "{\"refresh\":"; j += maps_refresh ? "true" : "false";
  j += ",\"zoom\":"; j += maps_zoom;
  j += ",\"lat\":"; j += String(maps_lat, 6);
  j += ",\"lon\":"; j += String(maps_lon, 6);
  j += ",\"has\":"; j += maps_has ? "true" : "false";
  j += "}";
  server.send(200, "application/json", j);
}

// In-browser uploader: pick a JPG/PNG (-> Album) or an animated GIF (decoded
// into frames in the browser via ImageDecoder -> Charm animation). Everything
// is converted to RGB565 big-endian client-side. Open http://192.168.4.1/upload
static void hUpload() {
  static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vecta Foto/GIF</title>
<style>
body{font-family:sans-serif;background:#0b1018;color:#eef4fa;text-align:center;margin:0;padding:16px}
h2{color:#ffc440}button,input{font-size:16px;padding:10px 14px;margin:6px;border-radius:10px;border:0}
button{background:#6366f1;color:#fff}button:disabled{background:#334;color:#889}
#clr{background:#ef4444}canvas{border-radius:50%;max-width:78vw;background:#000;margin:8px}
#log{white-space:pre-wrap;color:#9ad;min-height:2em;margin-top:8px}small{color:#89a}
</style></head><body>
<p style="text-align:left"><a href="/" style="color:#9aa7ff">&larr; Vecta paneli</a></p>
<h2>Vecta Foto / GIF</h2>
<p>Foto -> Album. GIF / kisa Video -> Charm'da donerek oynar.</p>
<input id="f" type="file" accept="image/*,video/*"><br>
<canvas id="c" width="466" height="466"></canvas><br>
<button id="send" disabled>Gonder</button>
<button id="clr">Albumu Temizle</button>
<div id="log"></div><small id="hint"></small>
<script>
var c=document.getElementById('c'),x=c.getContext('2d'),send=document.getElementById('send'),state=null;
function log(t){document.getElementById('log').textContent=t;}
function rgb565(data,n,out,off){
  for(var i=0;i<n;i++){var r=data[i*4],g=data[i*4+1],b=data[i*4+2];
    var v=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);out[off+i*2]=v>>8;out[off+i*2+1]=v&0xFF;}
}
function fit(ctx,img,S){
  ctx.fillStyle='#000';ctx.fillRect(0,0,S,S);
  var iw=img.videoWidth||img.displayWidth||img.codedWidth||img.width||img.naturalWidth;
  var ih=img.videoHeight||img.displayHeight||img.codedHeight||img.height||img.naturalHeight;
  var s=Math.min(S/iw,S/ih),w=iw*s,h=ih*s;ctx.drawImage(img,(S-w)/2,(S-h)/2,w,h);
}
// Extract `fc` frames from a video file at SxS, packed RGB565-BE for /animation.
function vidFrames(file,fc,S){return new Promise(function(resolve,reject){
  var v=document.createElement('video');v.muted=true;v.playsInline=true;v.preload='auto';
  v.src=URL.createObjectURL(file);
  var cc=document.createElement('canvas');cc.width=S;cc.height=S;var cx=cc.getContext('2d');
  var fb=S*S*2,out=new Uint8Array(fb*fc),idx=0,dur=1;
  function seek(){v.currentTime=Math.min(dur-0.05,dur*(idx+0.5)/fc);}
  v.onloadedmetadata=function(){dur=v.duration||fc*0.2;seek();};
  v.onseeked=function(){
    fit(cx,v,S);
    rgb565(cx.getImageData(0,0,S,S).data,S*S,out,idx*fb);
    idx++;
    if(idx>=fc)resolve({out:out,fc:fc,durMs:Math.max(80,Math.round(dur*1000/fc))});
    else seek();
  };
  v.onerror=function(){reject(new Error('video okunamadi'));};
});}
document.getElementById('f').onchange=async function(e){
  var file=e.target.files[0];if(!file)return;
  state=null;send.disabled=true;document.getElementById('hint').textContent='';log('Okunuyor...');
  var isGif=file.type==='image/gif'||/\.gif$/i.test(file.name);
  if(isGif&&('ImageDecoder' in window)){
    try{
      var buf=await file.arrayBuffer();
      var dec=new ImageDecoder({data:buf,type:'image/gif'});
      await dec.tracks.ready;
      var fcAll=dec.tracks.selectedTrack.frameCount||1;
      if(fcAll>1){
        var frames=[],dur=0;
        for(var i=0;i<fcAll;i++){var r=await dec.decode({frameIndex:i});frames.push(r.image);dur+=(r.image.duration||100000);}
        fit(x,frames[0],466);
        state={anim:true,frames:frames,durMs:Math.max(30,Math.round(dur/fcAll/1000))};
        send.textContent='GIF Gonder ('+fcAll+' kare)';send.disabled=false;
        log('GIF hazir: '+fcAll+' kare.');return;
      }
    }catch(err){log('GIF cozulemedi, ilk kare gonderilecek.');}
  }else if(isGif){
    document.getElementById('hint').textContent='Hareketli GIF icin Chrome/Android tarayici gerekir (ilk kare gider).';
  }
  var isVid=(file.type&&file.type.indexOf('video')===0)||/\.(mp4|mov|m4v|webm|3gp)$/i.test(file.name);
  if(isVid){
    var vp=document.createElement('video');vp.muted=true;vp.playsInline=true;vp.preload='auto';
    vp.src=URL.createObjectURL(file);
    vp.onloadeddata=function(){try{vp.currentTime=Math.min(0.1,(vp.duration||1)/2);}catch(e){}};
    vp.onseeked=function(){fit(x,vp,466);};
    vp.onerror=function(){log('Video okunamadi.');};
    state={video:true,file:file};
    send.textContent='Video Gonder';send.disabled=false;
    log('Video hazir (gif gibi doner). Gonder.');
    return;
  }
  var img=new Image();
  img.onload=function(){fit(x,img,466);state={anim:false};send.textContent='Albume Gonder';send.disabled=false;log('Hazir.');};
  img.onerror=function(){log('Resim okunamadi.');};
  img.src=URL.createObjectURL(file);
};
send.onclick=async function(){
  if(!state)return;send.disabled=true;
  try{
    if(state.video){
      var fcv=16;
      var Sv=Math.max(80,Math.min(260,Math.floor(Math.sqrt(2500000/(2*fcv)))));
      log('Video kareleri cikariliyor...');
      var rv=await vidFrames(state.file,fcv,Sv);
      log('Video gonderiliyor: '+rv.fc+' kare '+Sv+'x'+Sv);
      var fdv=new FormData();fdv.append('anim',new Blob([rv.out]),'a.raw');
      var resv=await fetch('/animation?fc='+rv.fc+'&fm='+rv.durMs+'&w='+Sv+'&h='+Sv,{method:'POST',body:fdv});
      log('Cevap: '+resv.status+' '+(await resv.text()));
    }else if(state.anim){
      var fr=state.frames,fc=fr.length,step=fc>48?Math.ceil(fc/48):1,sel=[];
      for(var i=0;i<fr.length;i+=step)sel.push(fr[i]);fc=sel.length;
      var S=Math.max(80,Math.min(300,Math.floor(Math.sqrt(2500000/(2*fc)))));
      var fb=S*S*2,out=new Uint8Array(fb*fc),cc=document.createElement('canvas');
      cc.width=S;cc.height=S;var cx=cc.getContext('2d');
      for(var f=0;f<fc;f++){fit(cx,sel[f],S);rgb565(cx.getImageData(0,0,S,S).data,S*S,out,f*fb);}
      log('GIF gonderiliyor: '+fc+' kare '+S+'x'+S+' ('+out.length+' bayt)');
      var fd=new FormData();fd.append('anim',new Blob([out]),'a.raw');
      var res=await fetch('/animation?fc='+fc+'&fm='+state.durMs+'&w='+S+'&h='+S,{method:'POST',body:fd});
      log('Cevap: '+res.status+' '+(await res.text()));
      for(var k=0;k<state.frames.length;k++){try{state.frames[k].close();}catch(e){}}
    }else{
      var n=466*466,b=new Uint8Array(n*2);
      rgb565(x.getImageData(0,0,466,466).data,n,b,0);
      log('Gonderiliyor... '+b.length+' bayt');
      var fd2=new FormData();fd2.append('photo',new Blob([b]),'p.raw');
      var res2=await fetch('/album',{method:'POST',body:fd2});
      log('Cevap: '+res2.status+' '+(await res2.text()));
    }
  }catch(err){log('Hata: '+err);}
  send.disabled=false;
};
document.getElementById('clr').onclick=async function(){
  try{var res=await fetch('/album?clear=1');log('Temizle: '+(await res.text()));}
  catch(err){log('Hata: '+err);}
};
</script></body></html>)HTML";
  server.send_P(200, "text/html", PAGE);
}

// AI assistant: record mic audio (~4s) and return it as a WAV for the phone
// to send to Gemini. Recording blocks the loop, so draw "listening" first.
static void hAssistantRec() {
  int secs = server.hasArg("secs") ? server.arg("secs").toInt() : 4;
  int idx = findModuleIdx("assistant");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  assistantSetState(1);
  gfx->fillScreen(C_BG); textCenter("Dinliyorum...", CXi, CYi, 3, C_OK); present();
  uint8_t* wav = nullptr; size_t len = 0;
  bool ok = audioRecordWav(&wav, &len, secs);
  assistantSetState(2);                          // thinking (phone calls Gemini next)
  gfx->fillScreen(C_BG); textCenter("Dusunuyorum", CXi, CYi, 3, C_GOLD); present();
  if (!ok || !wav) { server.send(500, "text/plain", "rec fail (mic?)"); return; }
  server.setContentLength(len);
  server.send(200, "audio/wav", "");
  server.client().write(wav, len);
  heap_caps_free(wav);
}

// AI assistant: phone polls this; if the user tapped the device to record,
// returns that WAV (and clears it). 204 when nothing is waiting.
static void hAssistantAudio() {
  if (!g_asstReady || !g_asstWav) { server.send(204, "text/plain", ""); return; }
  server.setContentLength(g_asstWavLen);
  server.send(200, "audio/wav", "");
  server.client().write(g_asstWav, g_asstWavLen);
  heap_caps_free(g_asstWav); g_asstWav = nullptr; g_asstReady = false;
}

// AI assistant: phone posts Gemini's reply text to show on the device.
static void hAssistantSay() {
  // NOTE: do NOT g_requestOpen here - re-opening calls assistant_enter() which
  // resets the state to idle and wipes the answer. The module is already open.
  assistantSetConv(trAscii(server.arg("heard")), trAscii(server.arg("text")));
  server.send(200, "text/plain", "ok");
}

// AI assistant: phone uploads the reply as raw 16-bit mono PCM @ AUD_SR
// (16 kHz); the device plays it on its speaker (Gemini TTS, resampled by phone).
static uint8_t* g_pcmBuf = nullptr;
static size_t   g_pcmRecv = 0;
static bool     g_pcmErr  = false;
static const size_t PCM_MAX = 1024 * 1024;   // ~32 s @ 16 kHz

static void hAssistantPlayUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (g_pcmBuf) { heap_caps_free(g_pcmBuf); g_pcmBuf = nullptr; }
    g_pcmRecv = 0; g_pcmErr = false;
    g_pcmBuf = (uint8_t*) heap_caps_malloc(PCM_MAX, MALLOC_CAP_SPIRAM);
    g_pcmErr = (g_pcmBuf == nullptr);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_pcmErr && g_pcmBuf) {
      if (g_pcmRecv + up.currentSize <= PCM_MAX) {
        memcpy(g_pcmBuf + g_pcmRecv, up.buf, up.currentSize);
        g_pcmRecv += up.currentSize;
      } else g_pcmErr = true;
    }
  }
}

static void hAssistantPlayDone() {
  if (g_pcmErr || !g_pcmBuf || g_pcmRecv < 2) {
    if (g_pcmBuf) { heap_caps_free(g_pcmBuf); g_pcmBuf = nullptr; }
    server.send(507, "text/plain", "pcm buffer"); return;
  }
  server.send(200, "text/plain", "ok");          // reply before the (blocking) playback
  audioPlayPcm(g_pcmBuf, g_pcmRecv);
  heap_caps_free(g_pcmBuf); g_pcmBuf = nullptr;
}

static void hModules() { server.send(200, "application/json", modulesJson()); }

static void hModule() {
  String id = server.arg("id");
  if (server.hasArg("en")) {
    bool en = server.arg("en") == "1";
    moduleSetEnabled(id.c_str(), en);
  }
  server.send(200, "application/json", modulesJson());
}

static void hOpen() {
  String id = server.arg("id");
  if (id == "home") { g_requestHome = true; server.send(200, "text/plain", "home"); return; }
  int idx = findModuleIdx(id);
  if (idx < 0) { server.send(404, "text/plain", "no module"); return; }
  if (!g_modules[idx].enabled) { server.send(409, "text/plain", "disabled"); return; }
  g_requestOpen = idx;
  server.send(200, "text/plain", "opening");
}

static void hMode() {
  // board game config; also opens the game module
  String tool = server.arg("tool");
  if (tool == "dice") gameSetTool(GT_DICE);
  else if (tool == "spinner") gameSetTool(GT_SPINNER);
  else if (tool == "timer") gameSetTool(GT_TIMER);
  else if (tool == "coin") gameSetTool(GT_COIN);
  int sides = server.arg("sides").toInt();
  int count = server.arg("count").toInt();
  int slots = server.arg("slots").toInt();
  int secs  = server.arg("secs").toInt();
  gameConfig(sides, count, slots, secs);
  int idx = findModuleIdx("game");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

static void hCharm() {
  if (server.hasArg("name")) {
    String n = trAscii(server.arg("name"));
    n = n.substring(0, 12);
    setPutStr("charm_name", n);
  }
  server.send(200, "text/plain", "ok");
}

// Phone syncs local time (epoch already adjusted for timezone).
static void hTime() {
  uint32_t epoch = (uint32_t) strtoul(server.arg("epoch").c_str(), nullptr, 10);
  if (epoch > 1000000000UL) timeSet(epoch);
  server.send(200, "text/plain", "ok");
}

static void hBadge() {
  if (server.hasArg("state")) {
    String s = server.arg("state");
    if (s == "free") badgeSetState(0);
    else if (s == "busy") badgeSetState(1);
    else if (s == "meet") badgeSetState(2);
    else badgeSetState(s.toInt());
  }
  if (server.hasArg("text")) badgeSetName(trAscii(server.arg("text")));
  int idx = findModuleIdx("badge");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

static void hCollar() {
  collarSetInfo(trAscii(server.arg("name")), server.arg("phone"), trAscii(server.arg("owner")));
  if (server.hasArg("lost")) collarSetLost(server.arg("lost") == "1");
  if (server.hasArg("find")) collarFindMe();
  int idx = findModuleIdx("collar");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

// Smartwatch-style notification forwarding. The phone pushes its own
// notifications here (calls / messages / app alerts):
//   /notify?type=call|msg|mail|alarm|info&app=&title=&body=&color=
// Backward compatible with the old orb call (/notify?count=&text=&color=) and
// with /notify?clear=1. A GET with ?poll=1 lets the phone read the last call
// action (1=answered 2=rejected) + unread count so it can act on the device's
// answer/reject, exactly like a paired watch reports back to the handset.
static uint8_t notifKindFromStr(const String& t) {
  if (t == "call")  return NK_CALL;
  if (t == "msg" || t == "message") return NK_MESSAGE;
  if (t == "mail" || t == "email")  return NK_MAIL;
  if (t == "alarm") return NK_ALARM;
  return NK_INFO;
}

static void hNotify() {
  if (server.hasArg("poll")) {
    int act = g_callAction; g_callAction = 0;   // consume
    char j[96];
    snprintf(j, sizeof(j), "{\"callAction\":%d,\"calling\":%d,\"unread\":%d}",
             act, call_state, notifUnread());
    server.send(200, "application/json", j);
    return;
  }
  if (server.hasArg("clear") || server.arg("count") == "0") {
    notifyClearAll();
    server.send(200, "text/plain", "ok");
    return;
  }

  uint32_t color = server.hasArg("color")
      ? (uint32_t) strtoul(server.arg("color").c_str(), nullptr, 10) : 0;

  if (server.hasArg("type") || server.hasArg("title") || server.hasArg("app") ||
      server.hasArg("body")) {
    uint8_t kind = notifKindFromStr(server.arg("type"));
    notifyPush(kind, trAscii(server.arg("app")), trAscii(server.arg("title")),
               trAscii(server.arg("body")), (uint16_t)color);
  } else {
    // legacy orb path
    notifySet(server.arg("count").toInt(), color, trAscii(server.arg("text")));
  }

  int idx = findModuleIdx("notify");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", "ok");
}

// Animation upload -> Module 1 (Charm). Streamed multipart upload; dimensions
// come as query args:  POST /animation?fc=<frames>&fm=<ms>&w=<W>&h=<H>
// body = fc * W*H*2 bytes of concatenated RGB565-BE frames (no header).
static uint8_t* g_animBuf  = nullptr;
static size_t   g_animRecv = 0, g_animTotal = 0;
static bool     g_animErr  = false;
static uint16_t g_animFc = 0, g_animFm = 100, g_animW = 0, g_animH = 0;

static void hAnimUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (g_animBuf) { heap_caps_free(g_animBuf); g_animBuf = nullptr; }
    g_animRecv = 0; g_animErr = false;
    g_animFc = server.arg("fc").toInt();
    g_animFm = server.arg("fm").toInt();
    g_animW  = server.arg("w").toInt();
    g_animH  = server.arg("h").toInt();
    if (g_animW == 0 || g_animH == 0 || g_animW > LCD_W || g_animH > LCD_H ||
        g_animFc == 0 || g_animFc > 48) { g_animErr = true; return; }
    g_animTotal = (size_t)g_animW * g_animH * 2 * g_animFc;
    g_animBuf = (uint8_t*) heap_caps_malloc(g_animTotal, MALLOC_CAP_SPIRAM);
    if (!g_animBuf) g_animErr = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_animErr && g_animBuf && g_animRecv + up.currentSize <= g_animTotal) {
      memcpy(g_animBuf + g_animRecv, up.buf, up.currentSize);
      g_animRecv += up.currentSize;
    }
  }
}

static void hAnimDone() {
  if (g_animErr || !g_animBuf) {
    if (g_animBuf) { heap_caps_free(g_animBuf); g_animBuf = nullptr; }
    server.send(507, "text/plain", "bad dims / psram full"); return;
  }
  if (g_animRecv != g_animTotal) {
    heap_caps_free(g_animBuf); g_animBuf = nullptr;
    server.send(400, "text/plain", String("incomplete ") + g_animRecv + "/" + (unsigned)g_animTotal);
    return;
  }
  charmStoreAnimation(g_animBuf, g_animTotal, g_animFc, g_animFm ? g_animFm : 100, g_animW, g_animH);
  g_animBuf = nullptr;                   // ownership moved to charm
  int idx = findModuleIdx("charm");
  if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  server.send(200, "text/plain", String("ok frames=") + g_animFc + " " + g_animW + "x" + g_animH);
}

// ---------------------------------------------------------------------------
// Stats snapshot for the phone (weekly step graph, streak, pomodoro, tama).
// Read-only:  GET /stats
// ---------------------------------------------------------------------------
static void hStats() {
  String s = "{";
  s += "\"imu\":";        s += motionPresent() ? "true" : "false";
  s += ",\"steps\":";     s += motionStepsToday();
  s += ",\"stepsLife\":"; s += motionStepsLife();
  s += ",\"activity\":\"";s += motionActivityStr(); s += "\"";
  s += ",\"scratch\":";   s += motionScratchToday();   // scratch-seconds today
  s += ",\"scratchPct\":";s += motionScratchPct();     // % vs rolling baseline
  s += ",\"history\":[";  s += motionStepHistoryCsv(); s += "]";   // oldest..yesterday
  s += ",\"streak\":";    s += setGetInt("strk_n", 0);
  s += ",\"streakMax\":"; s += setGetInt("strk_max", 0);
  s += ",\"pomo\":";      s += setGetInt("pm_n", 0);
  s += ",\"tama\":{\"hun\":"; s += setGetInt("t_hun", 30);
  s += ",\"hap\":";       s += setGetInt("t_hap", 70);
  s += ",\"ene\":";       s += setGetInt("t_ene", 80); s += "}";
  s += "}";
  server.send(200, "application/json", s);
}

// ---------------------------------------------------------------------------
// Battery-saver config:  GET /power?sleep=<sec 0..600>&tilt=0|1
// ---------------------------------------------------------------------------
static void hPower() {
  if (server.hasArg("sleep")) powerSetSleepSecs(server.arg("sleep").toInt());
  if (server.hasArg("tilt"))  powerSetTiltWake(server.arg("tilt") == "1");
  String s = "{\"sleep\":"; s += powerSleepSecs();
  s += ",\"tilt\":";    s += g_tiltWake ? "true" : "false";
  s += ",\"battery\":"; s += batteryPct(); s += "}";
  server.send(200, "application/json", s);
}

// ---------------------------------------------------------------------------
// Watch-face gallery:  GET /face            -> {face,count,names:[...]}
//                      GET /face?set=<idx>  -> select a face (opens the clock)
// ---------------------------------------------------------------------------
static void hFace() {
  if (server.hasArg("set")) {
    clockSetFace(server.arg("set").toInt());
    int idx = findModuleIdx("clock");
    if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  }
  String s = "{\"face\":"; s += clockFace();
  s += ",\"count\":"; s += CLK_FACES;
  s += ",\"names\":[";
  for (int i = 0; i < CLK_FACES; i++) { if (i) s += ","; s += "\""; s += CLK_FACE_NAMES[i]; s += "\""; }
  s += "]}";
  server.send(200, "application/json", s);
}

// ---------------------------------------------------------------------------
// Re-run the first-boot setup wizard:  GET /onboard?reset=1  (reboots)
// ---------------------------------------------------------------------------
static void hOnboard() {
  if (server.arg("reset") == "1") {
    setPutBool("onboarded", false);
    server.send(200, "text/plain", "ok, rebooting");
    delay(250); ESP.restart(); return;
  }
  String s = "{\"onboarded\":"; s += setGetBool("onboarded", false) ? "true" : "false"; s += "}";
  server.send(200, "application/json", s);
}

// ---------------------------------------------------------------------------
// OTA firmware update. GET /ota serves a tiny uploader page; POST /update takes
// the compiled .bin (multipart) and flashes it via the Update library, then
// reboots. Works from the phone app OR any browser - no USB re-flash needed.
// ---------------------------------------------------------------------------
static const char OTA_PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vecta - Guncelle</title><style>
body{font-family:sans-serif;background:#0b1018;color:#eef4fa;margin:0 auto;padding:18px;max-width:480px}
h1{color:#ffc440;text-align:center}.card{background:#131a2b;border-radius:14px;padding:16px}
input,button{font-size:16px;padding:12px;margin:6px 0;border-radius:10px;border:0;width:100%;box-sizing:border-box}
button{background:#6366f1;color:#fff;font-weight:bold}
#bar{height:14px;background:#0b1018;border-radius:8px;overflow:hidden;margin-top:10px}
#fill{height:100%;width:0;background:#22c55e;transition:width .2s}#s{color:#9ad;margin-top:8px;min-height:1.2em}
</style></head><body><h1>Vecta Guncelle</h1>
<div class=card>
<p>Derlenmis <b>.bin</b> dosyasini sec ve yukle. Cihaz otomatik yeniden baslar.</p>
<input type=file id=f accept=".bin"><button onclick=up()>Guncelle</button>
<div id=bar><div id=fill></div></div><div id=s></div>
</div><script>
function up(){var f=document.getElementById('f').files[0];if(!f)return;
var fd=new FormData();fd.append('firmware',f,f.name);
var x=new XMLHttpRequest();x.open('POST','/update');
x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);
document.getElementById('fill').style.width=p+'%';document.getElementById('s').textContent='Yukleniyor %'+p}};
x.onload=function(){document.getElementById('s').textContent=x.status==200?'Tamam! Cihaz yeniden basliyor...':'Hata: '+x.responseText};
x.onerror=function(){document.getElementById('s').textContent='Baglanti hatasi'};
x.send(fd)}
</script></body></html>)HTML";

static void hOtaPage() { server.send_P(200, "text/html", OTA_PAGE); }

static bool     g_otaOk    = false;
static bool     g_otaBegun = false;
static String   g_otaErr   = "";
static uint32_t g_otaDrawn = 0;
static void otaScreen(const char* title, uint32_t kb, uint16_t col) {
  if (!gfx) return;
  gfx->fillScreen(C_BG);
  textCenter("GUNCELLEME", CXi, CYi - 70, 3, C_GOLD);
  textCenter(title, CXi, CYi - 16, 2, C_TEXT);
  char b[24]; snprintf(b, sizeof(b), "%u KB", (unsigned)kb);
  textCenter(b, CXi, CYi + 44, 3, col);
  present();
}

static void hUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    displayWake();
    g_otaOk = false; g_otaBegun = false; g_otaErr = ""; g_otaDrawn = 0;
    otaScreen("Baslatiliyor...", 0, C_ACCENT);
    g_otaBegun = Update.begin(UPDATE_SIZE_UNKNOWN);
    if (!g_otaBegun) {                          // not enough space / busy
      g_otaErr = Update.errorString();
      otaScreen("HATA", 0, C_DANGER);
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_otaBegun) return;                     // begin failed -> swallow the rest
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      g_otaErr = Update.errorString();
    }
    if (millis() - g_otaDrawn > 250) {           // throttle (flash writes are slow)
      g_otaDrawn = millis();
      otaScreen("Yukleniyor", up.totalSize / 1024, C_ACCENT);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!g_otaBegun) return;
    g_otaOk = Update.end(true) && !Update.hasError();
    if (!g_otaOk && !g_otaErr.length()) g_otaErr = Update.errorString();
    otaScreen(g_otaOk ? "Tamamlandi" : "HATA", up.totalSize / 1024,
              g_otaOk ? C_OK : C_DANGER);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_otaBegun) Update.abort();
    g_otaBegun = false; g_otaErr = "iptal edildi";
  }
}
static void hUpdateDone() {
  bool ok = g_otaOk && g_otaBegun && !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain",
              ok ? "ok" : (g_otaErr.length() ? g_otaErr : "update failed"));
  if (ok) { delay(400); ESP.restart(); }
}

static void netBegin() {
  // SSID gets a per-device suffix so two units are distinguishable.
  snprintf(AP_SSID, sizeof(AP_SSID), "Vecta-%s", g_devId);
  // Wi-Fi join QR (standard format: phone camera -> "join this network").
  snprintf(g_wifiQR, sizeof(g_wifiQR), "WIFI:T:WPA;S:%s;P:%s;;", AP_SSID, AP_PASS);
  WiFi.mode(WIFI_AP);
  // Fixed channel 1 so ESP-NOW (buddy module) reaches another unit.
  WiFi.softAP(AP_SSID, AP_PASS, 1);
  server.on("/", hHome);                    // HTML control panel
  server.on("/info", hInfo);                // status JSON (used by the panel)
  server.on("/modules", hModules);
  server.on("/module", hModule);
  server.on("/open", hOpen);
  server.on("/mode", hMode);
  server.on("/charm", hCharm);
  server.on("/time", hTime);
  server.on("/badge", hBadge);
  server.on("/collar", hCollar);
  server.on("/notify", hNotify);
  server.on("/status", hStatus);
  server.on("/gps", hGps);
  server.on("/find", hFind);
  server.on("/love", hLove);
  server.on("/upload", hUpload);            // in-browser photo uploader UI
  server.on("/album", HTTP_POST, hAlbumDone, hAlbumUpload);  // streamed raw RGB565
  server.on("/album", HTTP_GET, hAlbumClear);                // ?clear=1
  server.on("/map", HTTP_POST, hMapDone, hMapUpload);        // Google static-map JPEG
  server.on("/map", HTTP_GET, hMapPoll);                     // ?poll=1 -> refresh/zoom
  server.on("/screen", hScreenRaw);          // framebuffer -> ham RGB565 (yansitma)
  server.on("/mirror", hMirrorPage);         // tarayicidan Vecta ekranini izle
  server.on("/touch", hTouchPoll);           // mirror dokunuslari (uygulama ceker)
  server.on("/lyrics", hLyrics);             // zaman-senkron karaoke sozler
  server.on("/reader", hReader);             // Hizli Okuma (RSVP) - PDF metni
  server.on("/photo", HTTP_POST, hPhotoDone, hPhotoUpload);  // JPEG upload (Expo)
  server.on("/assistant/rec", hAssistantRec);   // phone-triggered record -> WAV
  server.on("/assistant/audio", hAssistantAudio); // device-tap recording (phone polls)
  server.on("/assistant/say", hAssistantSay);   // show Gemini reply on device
  server.on("/assistant/play", HTTP_POST, hAssistantPlayDone, hAssistantPlayUpload);  // speak reply
  server.on("/np", hNowPlaying);
  server.on("/np_art", HTTP_POST, hNowPlayingArtDone, hNowPlayingArtUpload);  // raw RGB565, multipart
  server.on("/rotate", hRotate);             // module auto-rotation (slideshow)
  server.on("/capsule", hCapsule);
  server.on("/backup", HTTP_GET, hBackup);
  server.on("/restore", HTTP_POST, hRestore);
  server.on("/name", hName);
  server.on("/sound", hSound);
  server.on("/bright", hBright);
  server.on("/xiaozhi", hXiaozhi);
  server.on("/claw", hClawPage);
  server.on("/claw/ask", hClawAsk);
  server.on("/claw/poll", hClawPoll);
  server.on("/claw/cfg", hClawCfg);
  server.on("/wifi", hWifi);
  server.on("/cloud", hCloud);
  server.on("/friends", hFriends);
  server.on("/animation", HTTP_POST, hAnimDone, hAnimUpload);  // streamed GIF frames
  server.on("/stats", hStats);               // step/streak/pomo snapshot (weekly graph)
  server.on("/power", hPower);               // sleep timeout + tilt-to-wake config
  server.on("/face", hFace);                 // watch-face gallery select/list
  server.on("/onboard", hOnboard);           // re-run first-boot wizard (?reset=1)
  server.on("/ota", hOtaPage);               // firmware update uploader page
  server.on("/update", HTTP_POST, hUpdateDone, hUpdateUpload);  // OTA .bin flash
  server.begin();

  // mDNS: reach the device on your home network at http://vecta.local
  // (works once it has joined your Wi-Fi; on the AP it's still 192.168.4.1).
  if (MDNS.begin("vecta")) MDNS.addService("http", "tcp", 80);
}

static inline void netLoop() { server.handleClient(); }
// STA IP when joined to home Wi-Fi (we go STA-only then), else the softAP IP.
static IPAddress netIP() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}
