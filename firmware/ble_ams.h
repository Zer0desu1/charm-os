// ============================================================================
// ble_ams.h - Apple Media Service (AMS) client over the EXISTING BLE HID link.
//   "Arabadaki gibi": iPhone'da muzik acilinca parca adi/sanatci/album/sure
//   otomatik ekrana gelir - API anahtari yok, telefon uygulamasi yok.
//
//   Nasil: iPhone HID icin baglanip bond olunca bu cihaz AYNI baglanti
//   uzerinden GATT istemcisi olur (esp_ble_gattc_open ayni adrese sanal
//   baglanti acar, yeni ACL kurulmaz), telefonun AMS servisini bulur, Entity
//   Update karakteristigine abone olur ve Track/Player bildirimlerini
//   musicSet()'e basar. Muzik calmaya baslayinca launcher'dan Muzik modulu
//   otomatik acilir.
//
//   Android'de AMS yoktur -> oradaki yol degismedi (telefon uygulamasi /np).
//   Include order: platform.h + mod_music.h + vecta_blehid.h'den SONRA.
// ============================================================================
#pragma once

#include <esp_gattc_api.h>
#include <esp_bt_defs.h>

// 128-bit UUIDs in Bluedroid little-endian byte order (reversed string order).
static const uint8_t AMS_SVC_UUID[16] = {   // 89D3502B-0F36-433A-8EF4-C502AD55F8DC
  0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E,
  0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89 };
static const uint8_t AMS_CHR_ENTITY_UPDATE[16] = {  // 2F7CABCE-808D-411F-9A0C-BB92BA96C102
  0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A,
  0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F };

// AMS entity / attribute ids (Apple spec)
#define AMS_ENT_PLAYER 0
#define AMS_ENT_TRACK  2
#define AMS_TRK_ARTIST 0
#define AMS_TRK_ALBUM  1
#define AMS_TRK_TITLE  2
#define AMS_TRK_DUR    3
#define AMS_PLY_NAME   0
#define AMS_PLY_PBINFO 1

enum AmsState { AMS_IDLE, AMS_PENDING_OPEN, AMS_OPENING, AMS_DISCOVER, AMS_READY };

static esp_gatt_if_t g_amsIf = ESP_GATT_IF_NONE;
static AmsState      g_amsState = AMS_IDLE;
static esp_bd_addr_t g_amsBda = {};
static esp_ble_addr_type_t g_amsAddrType = BLE_ADDR_TYPE_PUBLIC;
static uint16_t  g_amsConnId = 0;
static uint16_t  g_amsSvcStart = 0, g_amsSvcEnd = 0;
static uint16_t  g_amsHndEU = 0;          // Entity Update char handle
static bool      g_amsHaveCentral = false;
static uint32_t  g_amsOpenAt = 0;          // when to (re)try gattc_open
static uint8_t   g_amsTries = 0;
static uint8_t   g_amsSubStep = 0;         // 0=Track sub, 1=Player sub, 2=done

// latest now-playing fields (assembled from individual attribute notifies)
static char  ams_title[64]  = "";
static char  ams_artist[64] = "";
static char  ams_album[40]  = "";
static float ams_durS  = 0;
static float ams_elapS = 0;
static bool  ams_playing = false;

// musicSet + module-open glue (declared in mod_music.h / platform.h)
static void amsPush(bool stateChanged) {
  if (!ams_title[0]) return;
  musicSet(String(ams_title), String(ams_artist), String(ams_album), "iPhone",
           ams_playing, (uint32_t)(ams_elapS * 1000), (uint32_t)(ams_durS * 1000));
  // car-head-unit behaviour: when playback starts while on the launcher,
  // surface the Muzik screen automatically (never interrupt an open app)
  if (stateChanged && ams_playing && g_app == APP_LAUNCHER) {
    int idx = findModuleIdx("music");
    if (idx >= 0 && g_modules[idx].enabled) g_requestOpen = idx;
  }
}

// NVS anahtari: bu adreste AMS olmadigi (Android) bir kez ogrenildiyse bir
// daha deneme. Tekrarlanan gattc open/close dongusu Android tarafinda surekli
// "baglandi/koptu" gorunumune yol aciyordu.
static String amsBdaKey() {
  char k[12];
  snprintf(k, sizeof(k), "an%02x%02x%02x%02x",
           g_amsBda[2], g_amsBda[3], g_amsBda[4], g_amsBda[5]);
  return String(k);
}

static void amsScheduleRetry() {
  if (g_amsTries >= 3) {                                    // AMS yok (Android?)
    g_amsState = AMS_IDLE;
    setPutStr(amsBdaKey().c_str(), "1");                    // kalici: bir daha deneme
    Serial.println("[ams] AMS yok - bu telefon icin kalici olarak atlanacak");
    return;
  }
  g_amsState = AMS_PENDING_OPEN;
  g_amsOpenAt = millis() + 3000;
}

// Called from vecta_blehid when a central connects / disconnects.
static void amsNoteCentral(const uint8_t* bda, uint8_t addrType) {
  memcpy(g_amsBda, bda, 6);
  g_amsAddrType = (esp_ble_addr_type_t)addrType;
  g_amsHaveCentral = true;
  if (setGetStr(amsBdaKey().c_str(), "") == "1") {          // bilinen Android
    g_amsState = AMS_IDLE;
    return;
  }
  // Sayaci SADECE farkli bir telefon baglandiginda sifirla: ayni telefonun
  // yeniden baglanmasi denemeleri sifirlarsa kapak (3 deneme) hic devreye
  // girmez ve kop/baglan dongusu sonsuz olur.
  static uint8_t lastBda[6] = {};
  if (memcmp(lastBda, g_amsBda, 6) != 0) {
    memcpy(lastBda, g_amsBda, 6);
    g_amsTries = 0;
  }
  if (g_amsTries >= 3) { g_amsState = AMS_IDLE; return; }
  g_amsState = AMS_PENDING_OPEN;
  g_amsOpenAt = millis() + 2500;     // let bonding/encryption settle first
}
static void amsOnCentralDisconnect() {
  g_amsHaveCentral = false;
  g_amsState = AMS_IDLE;
  ams_title[0] = 0;
}

static void amsSubscribeNext() {
  // Entity Update write = "send me updates for these attributes"
  if (g_amsSubStep == 0) {
    uint8_t cmd[5] = { AMS_ENT_TRACK, AMS_TRK_ARTIST, AMS_TRK_ALBUM, AMS_TRK_TITLE, AMS_TRK_DUR };
    esp_ble_gattc_write_char(g_amsIf, g_amsConnId, g_amsHndEU, sizeof(cmd), cmd,
                             ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  } else if (g_amsSubStep == 1) {
    uint8_t cmd[3] = { AMS_ENT_PLAYER, AMS_PLY_NAME, AMS_PLY_PBINFO };
    esp_ble_gattc_write_char(g_amsIf, g_amsConnId, g_amsHndEU, sizeof(cmd), cmd,
                             ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  } else {
    g_amsState = AMS_READY;
    Serial.println("[ams] subscribed - now playing aktif");
  }
}

static void amsHandleNotify(const uint8_t* d, int len) {
  if (len < 3) return;
  uint8_t ent = d[0], attr = d[1];
  char val[96];
  int vn = min(len - 3, (int)sizeof(val) - 1);
  memcpy(val, d + 3, vn); val[vn] = 0;

  bool stateChanged = false;
  if (ent == AMS_ENT_TRACK) {
    String v = String(val);                  // raw UTF-8 (Turkish drawn on device)
    if      (attr == AMS_TRK_TITLE)  v.toCharArray(ams_title,  sizeof(ams_title));
    else if (attr == AMS_TRK_ARTIST) v.toCharArray(ams_artist, sizeof(ams_artist));
    else if (attr == AMS_TRK_ALBUM)  v.toCharArray(ams_album,  sizeof(ams_album));
    else if (attr == AMS_TRK_DUR)    ams_durS = atof(val);
  } else if (ent == AMS_ENT_PLAYER && attr == AMS_PLY_PBINFO) {
    // "PlaybackState,PlaybackRate,ElapsedTime" e.g. "1,1.000,42.5"
    int state = atoi(val);
    char* c1 = strchr(val, ',');
    char* c2 = c1 ? strchr(c1 + 1, ',') : nullptr;
    if (c2) ams_elapS = atof(c2 + 1);
    bool playing = (state == 1 || state == 2 || state == 3);
    stateChanged = (playing != ams_playing);
    ams_playing = playing;
  }
  amsPush(stateChanged);
}

static void amsGattcCb(esp_gattc_cb_event_t ev, esp_gatt_if_t gattc_if,
                       esp_ble_gattc_cb_param_t* p) {
  switch (ev) {
    case ESP_GATTC_REG_EVT:
      if (p->reg.status == ESP_GATT_OK) g_amsIf = gattc_if;
      break;

    case ESP_GATTC_OPEN_EVT:
      if (p->open.status != ESP_GATT_OK) {
        Serial.printf("[ams] open fail 0x%x (deneme %d)\n", p->open.status, g_amsTries);
        amsScheduleRetry();
        break;
      }
      g_amsConnId = p->open.conn_id;
      g_amsSvcStart = g_amsSvcEnd = 0;
      g_amsState = AMS_DISCOVER;
      {
        esp_bt_uuid_t u; u.len = ESP_UUID_LEN_128;
        memcpy(u.uuid.uuid128, AMS_SVC_UUID, 16);
        esp_ble_gattc_search_service(gattc_if, g_amsConnId, &u);
      }
      break;

    case ESP_GATTC_SEARCH_RES_EVT:
      if (p->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
          memcmp(p->search_res.srvc_id.uuid.uuid.uuid128, AMS_SVC_UUID, 16) == 0) {
        g_amsSvcStart = p->search_res.start_handle;
        g_amsSvcEnd   = p->search_res.end_handle;
      }
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (!g_amsSvcStart) {           // servis yok: Android. KAPATMA! gattc_close
        // ayni ACL'i dusurup telefonu sonsuz kop/baglan dongusune sokuyordu.
        // Sanal baglanti acik kalsin (zararsiz); bir daha hic deneme.
        Serial.println("[ams] AMS yok (Android) - kalici olarak atlanacak");
        setPutStr(amsBdaKey().c_str(), "1");
        g_amsState = AMS_IDLE;
        break;
      }
      esp_bt_uuid_t u; u.len = ESP_UUID_LEN_128;
      memcpy(u.uuid.uuid128, AMS_CHR_ENTITY_UPDATE, 16);
      esp_gattc_char_elem_t ce; uint16_t cnt = 1;
      if (esp_ble_gattc_get_char_by_uuid(gattc_if, g_amsConnId, g_amsSvcStart,
              g_amsSvcEnd, u, &ce, &cnt) == ESP_GATT_OK && cnt) {
        g_amsHndEU = ce.char_handle;
        esp_ble_gattc_register_for_notify(gattc_if, g_amsBda, g_amsHndEU);
      } else {
        esp_ble_gattc_close(gattc_if, g_amsConnId);
        amsScheduleRetry();
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      // enable notifications on the Entity Update CCCD
      esp_bt_uuid_t du; du.len = ESP_UUID_LEN_16;
      du.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
      esp_gattc_descr_elem_t de; uint16_t cnt = 1;
      if (esp_ble_gattc_get_descr_by_char_handle(g_amsIf, g_amsConnId, g_amsHndEU,
              du, &de, &cnt) == ESP_GATT_OK && cnt) {
        uint8_t en[2] = {0x01, 0x00};
        esp_ble_gattc_write_char_descr(g_amsIf, g_amsConnId, de.handle, 2, en,
                                       ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      } else amsScheduleRetry();
      break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT:
      if (p->write.status != ESP_GATT_OK) {   // tipik: 0x05 yetki -> bond bekle
        Serial.printf("[ams] cccd fail 0x%x\n", p->write.status);
        esp_ble_gattc_close(gattc_if, g_amsConnId);
        amsScheduleRetry();
        break;
      }
      g_amsSubStep = 0;
      amsSubscribeNext();
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (p->write.handle != g_amsHndEU) break;
      if (p->write.status != ESP_GATT_OK) {
        esp_ble_gattc_close(gattc_if, g_amsConnId);
        amsScheduleRetry();
        break;
      }
      g_amsSubStep++;
      amsSubscribeNext();
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (p->notify.handle == g_amsHndEU)
        amsHandleNotify(p->notify.value, p->notify.value_len);
      break;

    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      if (g_amsState == AMS_READY || g_amsState == AMS_DISCOVER)
        g_amsState = g_amsHaveCentral ? AMS_PENDING_OPEN : AMS_IDLE;
      if (g_amsState == AMS_PENDING_OPEN) g_amsOpenAt = millis() + 3000;
      break;

    default: break;
  }
}

// Register our GATT-client app. MUST run after BLEDevice::init() (bleKb.begin())
// because that call installs the Arduino library's gattc callback and ours has
// to replace it (the sketch never uses BLEClient, so nothing is lost).
static void amsBegin() {
  esp_ble_gattc_register_callback(amsGattcCb);
  esp_ble_gattc_app_register(0xA5);
}

// Drive open/retry from the main loop.
static void amsLoop() {
  if (g_amsState != AMS_PENDING_OPEN || g_amsIf == ESP_GATT_IF_NONE) return;
  if (!g_amsHaveCentral || millis() < g_amsOpenAt) return;
  g_amsTries++;
  g_amsState = AMS_OPENING;
  // ayni ACL uzerinde sanal GATTC baglantisi acilir (yeni baglanti kurulmaz)
  if (esp_ble_gattc_open(g_amsIf, g_amsBda, g_amsAddrType, true) != ESP_OK)
    amsScheduleRetry();
}
