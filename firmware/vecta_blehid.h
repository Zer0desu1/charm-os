// ============================================================================
// vecta_blehid.h - Minimal BLE HID "consumer control" (media keys) using the
//   ESP32 Arduino core's own BLE library. Replaces the T-vK ESP32-BLE-Keyboard
//   library, which does NOT compile on core 3.x (it passes std::string where
//   the core now expects String). No external library needed.
//
//   Sends 16-bit Consumer Usage codes: Volume Up acts as a camera shutter on
//   iOS/Android; Play/Pause/Next/Prev drive the media knob.
// ============================================================================
#pragma once

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <esp_gap_ble_api.h>

// AMS (Apple Media Service) hooks - defined in ble_ams.h (same TU, later).
static void amsNoteCentral(const uint8_t* bda, uint8_t addrType);
static void amsOnCentralDisconnect();

// Consumer-control usage codes (names kept compatible with the old code).
#define KEY_MEDIA_VOLUME_UP      0x00E9
#define KEY_MEDIA_VOLUME_DOWN    0x00EA
#define KEY_MEDIA_PLAY_PAUSE     0x00CD
#define KEY_MEDIA_NEXT_TRACK     0x00B5
#define KEY_MEDIA_PREVIOUS_TRACK 0x00B6

// HID report map: one Consumer Control collection, a single 16-bit usage field.
static const uint8_t VECTA_HID_REPORT_MAP[] = {
  0x05, 0x0C,                    // Usage Page (Consumer)
  0x09, 0x01,                    // Usage (Consumer Control)
  0xA1, 0x01,                    // Collection (Application)
  0x85, 0x01,                    //   Report ID (1)
  0x15, 0x00,                    //   Logical Minimum (0)
  0x26, 0xFF, 0x03,              //   Logical Maximum (0x3FF)
  0x19, 0x00,                    //   Usage Minimum (0)
  0x2A, 0xFF, 0x03,              //   Usage Maximum (0x3FF)
  0x75, 0x10,                    //   Report Size (16)
  0x95, 0x01,                    //   Report Count (1)
  0x81, 0x00,                    //   Input (Data,Array)
  0xC0                           // End Collection
};

class VectaBleHid : public BLEServerCallbacks {
 public:
  VectaBleHid(const char* name = "Vecta", const char* manuf = "Vecta", uint8_t batt = 100)
      : _name(name), _manuf(manuf), _batt(batt) {}

  void begin() {
    if (_started) return;
    _started = true;
    BLEDevice::init(String(_name));
    _server = BLEDevice::createServer();
    _server->setCallbacks(this);

    _hid = new BLEHIDDevice(_server);
    _input = _hid->inputReport(1);                 // report ID 1 (matches map)
    _hid->manufacturer()->setValue(String(_manuf));
    _hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
    _hid->hidInfo(0x00, 0x01);
    _hid->reportMap((uint8_t*)VECTA_HID_REPORT_MAP, sizeof(VECTA_HID_REPORT_MAP));
    _hid->startServices();
    _hid->setBatteryLevel(_batt);

    // REBOOT FIX: the CCCD (notify subscription) on the input report lives in
    // RAM, so it resets to "off" every power cycle. A bonded phone does NOT
    // rewrite it on reconnect (HID-over-GATT says the server must remember it
    // for bonded clients), and notify() silently drops reports while the CCCD
    // is off -> HID looked "broken" after reboot until you unpaired/re-paired.
    // Force-arm it at startup so notifies always go out to whoever connects.
    forceCccdOn();

    // "Just works" bonding so iOS/Android accept the HID without a PIN screen.
    // BOTH Init and Resp key masks must be set, otherwise the identity/encryption
    // keys aren't fully exchanged/stored and the phone can't reconnect with the
    // saved bond -> you'd have to unpair + re-pair every time. Distributing the
    // ID key (IRK) lets the phone resolve the device across reconnects.
    BLESecurity* sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    sec->setCapability(ESP_IO_CAP_NONE);
    sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    sec->setKeySize(16);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);                    // HID Keyboard
    adv->addServiceUUID(_hid->hidService()->getUUID());
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
  }

  // Connected if EITHER signal says so: the callback flag OR the live count.
  // (Belt-and-suspenders - the callback signature differs across core versions.)
  bool isConnected() { return _connected || (_server && _server->getConnectedCount() > 0); }

  // Expose the GATT server so other features (e.g. the navigation data service
  // in ble_nav.h) can add their own service to the SAME connection - no second
  // advertiser, no extra ACL.
  BLEServer* server() { return _server; }

  // Call every frame: while disconnected, keep re-advertising so a previously
  // bonded phone reconnects AUTOMATICALLY (no forget/re-pair needed).
  void keepAlive() {
    if (!_started) return;
    if (isConnected()) { _connected = true; return; }
    _connected = false;
    uint32_t now = millis();
    if (now - _lastAdv > 3000) { BLEDevice::startAdvertising(); _lastAdv = now; }
  }

  // Press + release a consumer key (e.g. KEY_MEDIA_VOLUME_UP for the shutter).
  void write(uint16_t usage) {
    if (!_input || !isConnected()) return;          // use the live count, not the flag
    Serial.printf("[hid] key 0x%03X\n", usage);     // teshis: telefona giden her tus
    uint8_t down[2] = { (uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8) };
    _input->setValue(down, 2); _input->notify();
    delay(12);
    sendRelease();
    delay(12);
    sendRelease();   // notify is unacknowledged: a lost release = phone holds the
                     // key forever (volume creeps, camera bursts) -> send it twice
  }

  // All-zero report = no key held. Safe to send any time.
  void sendRelease() {
    if (!_input) return;
    uint8_t up[2] = { 0, 0 };
    _input->setValue(up, 2); _input->notify();
  }

  // On reconnect from a saved bond the link is NOT encrypted yet; HID input
  // reports are silently ignored until it is. Force encryption with the stored
  // keys so the buttons work again after a reboot/auto-reconnect.
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    _connected = true;
    Serial.println("[hid] central baglandi");
    forceCccdOn();  // belt-and-suspenders: re-arm in case anything cleared it
    esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
    // Relax the BLE connection interval so the shared radio leaves Wi-Fi enough
    // airtime: a fast HID interval (iOS picks 7.5-15 ms) keeps the radio busy and
    // helps drop the Wi-Fi STA. 30-50 ms keeps media keys responsive.
    {
      esp_ble_conn_update_params_t cp = {};
      memcpy(cp.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
      cp.min_int = 0x18;   // 24 * 1.25 ms = 30 ms
      cp.max_int = 0x28;   // 40 * 1.25 ms = 50 ms
      cp.latency = 0;
      cp.timeout = 600;    // 6 s supervision timeout
      esp_ble_gap_update_conn_params(&cp);
    }
    sendRelease();  // clear any phantom held key left over from the last session
    // iPhone ise ayni link uzerinden AMS (now playing) istemcisini baslat
    amsNoteCentral(param->connect.remote_bda, (uint8_t)param->connect.ble_addr_type);
  }
  void onConnect(BLEServer*) override { _connected = true; forceCccdOn(); sendRelease(); }
  void onDisconnect(BLEServer*) override {
    _connected = false;
    Serial.println("[hid] central koptu");
    amsOnCentralDisconnect();
    BLEDevice::startAdvertising();
  }

 private:
  void forceCccdOn() {
    if (!_input) return;
    BLE2902* cccd = (BLE2902*)_input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    if (cccd) cccd->setNotifications(true);
  }

  const char* _name; const char* _manuf; uint8_t _batt;
  bool _started = false;
  volatile bool _connected = false;
  uint32_t _lastAdv = 0;
  BLEServer* _server = nullptr;
  BLEHIDDevice* _hid = nullptr;
  BLECharacteristic* _input = nullptr;
};
