/*
 * MacroPad Firmware for ESP32-C3 SuperMini
 * BLE HID Keyboard + BLE Config Service + SSD1306 OLED + NVS Keymap
 */

#include <soc/rtc_cntl_reg.h>
#include <Arduino.h>
#include <esp_system.h>
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>
#endif
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── Pin Configuration ──────────────────────────────────────────────
static constexpr uint8_t COL_PINS[2] = {10, 20};
static constexpr uint8_t ROW_PINS[4] = {0, 1, 2, 3};
static constexpr uint8_t I2C_SDA = 8;
static constexpr uint8_t I2C_SCL = 9;

// ─── OLED ───────────────────────────────────────────────────────────
static constexpr uint16_t OLED_W = 128;
static constexpr uint16_t OLED_H = 32;
static constexpr uint8_t  OLED_ADDR = 0x3C;

// ─── BLE UUIDs ──────────────────────────────────────────────────────
static constexpr const char* CFG_SVC_UUID = "12345678-1234-5678-1234-56789abcdef0";
static constexpr const char* KM_UUID      = "12345678-1234-5678-1234-56789abcdef1";

// ─── Keymap ─────────────────────────────────────────────────────────
static constexpr uint8_t KEY_COUNT = 8;
static uint16_t keymap[KEY_COUNT];

static constexpr uint16_t DEFAULT_KEYMAP[KEY_COUNT] = {
  0x04, 0x05, 0x06, 0x07,   // A B C D
  0x08, 0x09, 0x0A, 0x0B    // E F G H
};

// ─── Objects ────────────────────────────────────────────────────────
static Preferences          prefs;
static Adafruit_SSD1306     oled(OLED_W, OLED_H, &Wire, -1);
static bool                 oledOk = false;

static BLEServer*           pServer     = nullptr;
static BLEHIDDevice*        pHid        = nullptr;
static BLECharacteristic*   pKbdInput   = nullptr;
static BLECharacteristic*   pConInput   = nullptr;
static BLECharacteristic*   pKmChar     = nullptr;

// ─── State ──────────────────────────────────────────────────────────
static volatile bool bleConnected = false;
static volatile bool oledDirty    = true;

// Debounce
static bool          keyDown[4][2]   = {};
static bool          keyPrev[4][2]   = {};
static unsigned long keyTimer[4][2]  = {};
static constexpr unsigned long DEBOUNCE_MS = 15;

// Active key tracking (6KRO)
static uint16_t activeKeys[6] = {};
static int      activeCount   = 0;

// Mode
static bool isConfigMode = false;

// Activity tracking and display protection
static unsigned long lastActivity = 0;
static bool displayAsleep = false;
static constexpr unsigned long DISPLAY_TIMEOUT_MS = 60000; // n seconds * 1000

// ─── HID Report Descriptor ──────────────────────────────────────────
static const uint8_t HID_REPORT_MAP[] = {
  // Keyboard (Report ID 1)
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)

  // Modifiers (8 bits)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0xE0,        //   Usage Minimum (224)
  0x29, 0xE7,        //   Usage Maximum (231)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)

  // Reserved byte
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Constant)

  // LED output
  0x95, 0x05,        //   Report Count (5)
  0x75, 0x01,        //   Report Size (1)
  0x05, 0x08,        //   Usage Page (LEDs)
  0x19, 0x01,        //   Usage Minimum (1)
  0x29, 0x05,        //   Usage Maximum (5)
  0x91, 0x02,        //   Output (Data, Variable, Absolute)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x03,        //   Report Size (3)
  0x91, 0x03,        //   Output (Constant)

  // Key arrays (6 bytes)
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0x65,        //   Usage Maximum (101)
  0x81, 0x00,        //   Input (Data, Array)
  0xC0,              // End Collection

  // Consumer Control (Report ID 2)
  0x05, 0x0C,        // Usage Page (Consumer)
  0x09, 0x01,        // Usage (Consumer Control)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x02,        //   Report ID (2)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x09, 0x00,        //   Usage (Unassigned)
  0x81, 0x00,        //   Input (Data, Array)
  0xC0               // End Collection
};

// ─── Key Name Lookup ────────────────────────────────────────────────
static String keyName(uint16_t code) {
  if (code == 0x0000) return "---";

  bool    media = (code & 0x1000) != 0;
  uint8_t mods  = (code >> 8) & 0xFF;
  uint8_t key   = code & 0xFF;

  String s;
  if (mods & 1) s += 'C';
  if (mods & 2) s += 'S';
  if (mods & 4) s += 'A';
  if (mods & 8) s += 'G';
  if (mods & 0x40) s += "AG";
  if (mods)     s += '+';

  if (media) {
    switch (code & 0x0FFF) {
      case 0x00B0: case 0x00CD: s += "Play";  break;
      case 0x00B1:              s += "Pse";   break;
      case 0x00B5:              s += ">>";    break;
      case 0x00B6:              s += "<<";    break;
      case 0x00E9:              s += "Vol+";  break;
      case 0x00EA:              s += "Vol-";  break;
      case 0x00E2:              s += "Mut";   break;
      default: s += "M?"; break;
    }
  } else {
    if      (key >= 0x04 && key <= 0x1D) s += (char)('A' + key - 0x04);
    else if (key >= 0x1E && key <= 0x26) s += (char)('1' + key - 0x1E);
    else if (key == 0x27) s += '0';
    else if (key == 0x28) s += "Ent";
    else if (key == 0x29) s += "Esc";
    else if (key == 0x2A) s += "Bks";
    else if (key == 0x2B) s += "Tab";
    else if (key == 0x2C) s += "Spc";
    else if (key == 0x2D) s += '-';
    else if (key == 0x2E) s += '=';
    else if (key == 0x2F) s += '[';
    else if (key == 0x30) s += ']';
    else if (key == 0x31) s += '\\';
    else if (key == 0x33) s += ';';
    else if (key == 0x34) s += '\'';
    else if (key == 0x35) s += '`';
    else if (key == 0x36) s += ',';
    else if (key == 0x37) s += '.';
    else if (key == 0x38) s += '/';
    else if (key == 0x39) s += "Cap";
    else if (key >= 0x3A && key <= 0x45) s += "F" + String(key - 0x3A + 1);
    else if (key == 0x46) s += "PSc";
    else if (key == 0x47) s += "ScL";
    else if (key == 0x48) s += "Pau";
    else if (key == 0x49) s += "Ins";
    else if (key == 0x4A) s += "Hom";
    else if (key == 0x4B) s += "PgU";
    else if (key == 0x4C) s += "Del";
    else if (key == 0x4D) s += "End";
    else if (key == 0x4E) s += "PgD";
    else if (key == 0x4F) s += "Rgt";
    else if (key == 0x50) s += "Lft";
    else if (key == 0x51) s += "Dwn";
    else if (key == 0x52) s += "Up";
    else s += "0x" + String(key, HEX);
  }

  if (s.length() > 8) s = s.substring(0, 8);
  return s;
}

// ─── NVS ────────────────────────────────────────────────────────────
static void loadKeymap() {
  prefs.begin("macropad", true);
  if (prefs.getBytesLength("km") == sizeof(keymap)) {
    prefs.getBytes("km", keymap, sizeof(keymap));
  } else {
    memcpy(keymap, DEFAULT_KEYMAP, sizeof(keymap));
  }
  prefs.end();
}

static void saveKeymap() {
  prefs.begin("macropad", false);
  prefs.putBytes("km", keymap, sizeof(keymap));
  prefs.end();
}

// ─── HID Report Senders ─────────────────────────────────────────────
static void sendKbdReport(uint8_t mods, const uint8_t* keys, int n) {
  if (!pKbdInput || !bleConnected) return;
  uint8_t r[8] = {};
  r[0] = mods;
  for (int i = 0; i < (n < 6 ? n : 6); i++) r[2 + i] = keys[i];
  pKbdInput->setValue(r, 8);
  pKbdInput->notify();
}

static void sendConReport(uint16_t code) {
  if (!pConInput || !bleConnected) return;
  uint8_t r[2] = { (uint8_t)(code & 0xFF), (uint8_t)((code >> 8) & 0xFF) };
  pConInput->setValue(r, 2);
  pConInput->notify();
}

// ─── Key Events ─────────────────────────────────────────────────────
static void pressKey(int r, int c) {
  oledDirty = true;
  uint16_t code = keymap[r * 2 + c];
  Serial.printf("Key Press: R%d C%d (0x%04X)\n", r, c, code);
  if (!code) return;

  if (code & 0x1000) {
    sendConReport(code & 0x0FFF);
  } else {
    if (activeCount < 6) activeKeys[activeCount++] = code;
    uint8_t kbuf[6] = {};
    uint8_t mods = 0;
    for (int i = 0; i < activeCount; i++) {
      kbuf[i] = activeKeys[i] & 0xFF;
      mods |= (activeKeys[i] >> 8) & 0xFF;
    }
    sendKbdReport(mods, kbuf, activeCount);
  }
}

static void releaseKey(int r, int c) {
  oledDirty = true;
  uint16_t code = keymap[r * 2 + c];
  Serial.printf("Key Release: R%d C%d\n", r, c);
  if (!code) return;

  if (code & 0x1000) {
    sendConReport(0);
  } else {
    bool found = false;
    for (int i = 0; i < activeCount; i++) {
      if (activeKeys[i] == code) {
        for (int j = i; j < activeCount - 1; j++) activeKeys[j] = activeKeys[j + 1];
        activeCount--;
        found = true;
        break;
      }
    }
    
    if (found) {
      uint8_t kbuf[6] = {};
      uint8_t mods = 0;
      for (int i = 0; i < activeCount; i++) {
        kbuf[i] = activeKeys[i] & 0xFF;
        mods |= (activeKeys[i] >> 8) & 0xFF;
      }
      sendKbdReport(mods, kbuf, activeCount);
    }
  }
}

// ─── Matrix Scan ────────────────────────────────────────────────────
static void scanMatrix() {
  for (int c = 0; c < 2; c++) {
    pinMode(COL_PINS[c], OUTPUT);
    digitalWrite(COL_PINS[c], LOW);
    delayMicroseconds(20);

    for (int r = 0; r < 4; r++) {
      bool cur = (digitalRead(ROW_PINS[r]) == LOW);
      if (cur != keyPrev[r][c]) keyTimer[r][c] = millis();
      if ((millis() - keyTimer[r][c]) > DEBOUNCE_MS) {
        if (cur != keyDown[r][c]) {
          keyDown[r][c] = cur;
          lastActivity = millis();
          if (displayAsleep && oledOk) {
            oled.ssd1306_command(SSD1306_DISPLAYON);
            displayAsleep = false;
          }
          if (cur) pressKey(r, c);
          else     releaseKey(r, c);
        }
      }
      keyPrev[r][c] = cur;
    }

    pinMode(COL_PINS[c], INPUT_PULLUP);
  }
}

// ─── BLE Callbacks ──────────────────────────────────────────────────
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    oledDirty = true;
    lastActivity = millis();
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    oledDirty = true;
    activeCount = 0;
    delay(500);
    BLEDevice::startAdvertising();
  }
};

class KeymapCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* p) override {
    auto v = p->getValue();
    if (v.length() == sizeof(keymap)) {
      memcpy(keymap, v.c_str(), sizeof(keymap));
      saveKeymap();
      activeCount = 0;
      oledDirty = true;
      if (bleConnected) {
        uint8_t empty[6] = {};
        sendKbdReport(0, empty, 0);
      }
    }
  }
};

// ─── OLED ───────────────────────────────────────────────────────────
static bool flashOn = true;

static void updateOled() {
  if (!oledOk) return;

  oled.clearDisplay();
  oled.setTextSize(1);

  constexpr int COL_X[2] = {0, 64};
  constexpr int COL_W    = 64;

  // Key grid with pill highlights
  for (int r = 0; r < 4; r++) {
    int cy = r * 8;
    for (int c = 0; c < 2; c++) {
      int cx  = COL_X[c];
      String name = keyName(keymap[r * 2 + c]);
      int tw  = (int)name.length() * 6;
      int tx  = cx + (COL_W - tw) / 2;

      if (keyDown[r][c]) {
        int pw = min(tw + 6, COL_W);
        int px = cx + (COL_W - pw) / 2;
        int pr = min(3, pw / 2);
        oled.fillRoundRect(px, cy, pw, 8, pr, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(tx, cy);
        oled.print(name);
        oled.setTextColor(SSD1306_WHITE);
      } else {
        oled.setCursor(tx, cy);
        oled.print(name);
      }
    }
  }

  // BLE status: flashing 3px rect in top-right when disconnected, nothing when connected
  if (!bleConnected && (millis() / 1000 % 2 == 0)) {
    oled.fillRect(125, 1, 3, 3, SSD1306_WHITE);
  }

  // Config badge — center top
  if (isConfigMode) {
    oled.setCursor(55, 0);
    oled.print("CFG");
  }

  oled.display();
}

// ─── Setup ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // --- 1. CHECK BOOT MODES ---
  for (int r = 0; r < 4; r++) pinMode(ROW_PINS[r], INPUT_PULLUP);
  pinMode(COL_PINS[0], OUTPUT);
  digitalWrite(COL_PINS[0], LOW);
  delay(20);

  if (digitalRead(ROW_PINS[1]) == LOW) {
    Serial.println(">>> REBOOTING TO HARDWARE BOOTLOADER (FLASH MODE) <<<");
    delay(100);
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
  }

  bool configMode = false;
  if (digitalRead(ROW_PINS[0]) == LOW) {
    configMode = true;
    Serial.println(">>> BOOTING IN CONFIG MODE <<<");
  } else {
    Serial.println(">>> BOOTING IN KEYBOARD MODE <<<");
  }

  for (int c = 0; c < 2; c++) pinMode(COL_PINS[c], INPUT_PULLUP);
  for (int r = 0; r < 4; r++) pinMode(ROW_PINS[r], INPUT_PULLUP);

  // --- 2. OLED INIT ---
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledOk = true;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print(configMode ? "CONFIG MODE" : "MacroPad");
    oled.display();
  }

  loadKeymap();

  // --- 3. BLE ---
  if (configMode) {
    uint8_t newMac[6];
    esp_read_mac(newMac, ESP_MAC_BT);
    newMac[0] |= 0x02;
    newMac[5] ^= 0x0F;
    esp_base_mac_addr_set(newMac);

    BLEDevice::init("MacroPad-CFG");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCb());

    BLEService* pCfg = pServer->createService(CFG_SVC_UUID);
    pKmChar = pCfg->createCharacteristic(
      KM_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pKmChar->setValue((uint8_t*)keymap, sizeof(keymap));
    pKmChar->setCallbacks(new KeymapCb());
    pCfg->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(CFG_SVC_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();

  } else {
    BLEDevice::init("MacroPad");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCb());

    pHid = new BLEHIDDevice(pServer);
    pHid->reportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    pHid->hidInfo(0x00, 0x01);
    pHid->pnp(0x02, 0xe502, 0xa111, 0x0210);
    pHid->setBatteryLevel(100);

    pKbdInput = pHid->inputReport(1);
    pHid->outputReport(1);
    pConInput = pHid->inputReport(2);
    pHid->startServices();

    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);
    adv->addServiceUUID((uint16_t)0x1812);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
  }

  isConfigMode = configMode;

  unsigned long now = millis();
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 2; c++)
      keyTimer[r][c] = now;

  Serial.println("BLE Ready");
}

// ─── Loop ───────────────────────────────────────────────────────────
void loop() {
  scanMatrix();

  // Handle 1s flashing interval when disconnected
  static unsigned long flashTimer = 0;
  if (!bleConnected && (millis() - flashTimer >= 1000)) {
    flashTimer = millis();
    flashOn = !flashOn;
    oledDirty = true;
  }

  static unsigned long oledTimer = 0;
  if (oledDirty && (millis() - oledTimer >= 33)) {
    oledDirty = false;
    oledTimer = millis();
    updateOled();
  }

  if (oledOk && !displayAsleep && (millis() - lastActivity > DISPLAY_TIMEOUT_MS)) {
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
    displayAsleep = true;
  }
}