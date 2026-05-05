/**
 * Jellybox Firmware
 *
 * ESP32 companion device for the Jellybox Server SaaS.
 * Scans RFID/NFC tags → POST /api/play → Jellyfin playback.
 *
 * Required Arduino libraries (install via Library Manager):
 *   - WiFiManager         by tzapu          (v2.0.17+)
 *   - Adafruit NeoPixel   by Adafruit       (v1.12+)
 *   - Adafruit PN532      by Adafruit       (v1.3+)
 *   - GxEPD2              by ZinggJM        (v1.6+)
 *   - Adafruit GFX Library by Adafruit      (v1.11+)
 *   - ArduinoJson         by Benoit Blanchon (v7+ or v6 — see APIClient.h)
 *
 * Board: ESP32 Dev Module (Arduino ESP32 core v2.x or v3.x)
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>

#include "Config.h"
#include "LEDRing.h"
#include "EInkDisplay.h"
#include "NFCReader.h"
#include "APIClient.h"
#include "OTAUpdater.h"

LEDRing     led;
EInkDisplay eink;
NFCReader   nfc;
APIClient   api;
Preferences prefs;

DeviceConfig cfg;

enum class AppState {
  UNCONFIGURED,
  CONNECTING,
  BOOTSTRAPPING,
  READY,
  SCAN_MODE,
  UPDATING,
};
AppState appState = AppState::UNCONFIGURED;

String        deviceName   = "";
bool          nfcReady     = false;
unsigned long lastBootstrap = 0;

unsigned long bootBtnStart = 0;
bool          bootBtnHeld  = false;

WiFiManagerParameter* wm_serverUrl = nullptr;
WiFiManagerParameter* wm_apiKey    = nullptr;

void loadConfig() {
  prefs.begin(NVS_NAMESPACE, true);
  cfg.serverUrl = prefs.getString(NVS_KEY_SERVER, "");
  cfg.apiKey    = prefs.getString(NVS_KEY_APIKEY,  "");
  prefs.end();
  if (cfg.serverUrl.endsWith("/"))
    cfg.serverUrl = cfg.serverUrl.substring(0, cfg.serverUrl.length() - 1);
}

void saveConfig(const String& serverUrl, const String& apiKey) {
  String su = serverUrl;
  if (su.endsWith("/")) su = su.substring(0, su.length() - 1);
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_KEY_SERVER, su);
  prefs.putString(NVS_KEY_APIKEY,  apiKey);
  prefs.end();
  Serial.println("[Config] Saved — server: " + su);
}

void factoryReset() {
  Serial.println("[Config] Factory reset!");
  prefs.begin(NVS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  WiFi.disconnect(true, true);
  delay(300);
  ESP.restart();
}

void onPortalSave() {
  if (!wm_serverUrl || !wm_apiKey) return;
  String su = String(wm_serverUrl->getValue());
  String ak = String(wm_apiKey->getValue());
  su.trim(); ak.trim();
  saveConfig(su, ak);
  cfg.serverUrl = su;
  cfg.apiKey    = ak;
}

void startWiFi(bool forcePortal) {
  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(WIFI_TIMEOUT_S);
  wm.setSaveParamsCallback(onPortalSave);
  wm.setTitle("Jellybox Setup");
  wm.setShowInfoUpdate(false);
  wm_serverUrl = new WiFiManagerParameter("server", "Server URL", cfg.serverUrl.c_str(), 128, " placeholder=\"https://jellybox.example.com\"");
  wm_apiKey    = new WiFiManagerParameter("apikey",  "API Key",    cfg.apiKey.c_str(),    80,  " placeholder=\"jb_...\"");
  wm.addParameter(wm_serverUrl);
  wm.addParameter(wm_apiKey);
  wm.setWebServerCallback([&]() {
    wm.server->on("/hotspot-detect.html", HTTP_GET, [&]() {
      wm.server->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    wm.server->on("/generate_204", HTTP_GET, [&]() {
      wm.server->send(204, "text/plain", "");
    });
  });
  bool connected = forcePortal
    ? (eink.showUnpaired(), led.setState(LEDState::SETUP_PORTAL), led.setBaseState(LEDState::UNPAIRED), led.update(), wm.startConfigPortal(WIFI_AP_NAME, WIFI_AP_PASSWORD))
    : wm.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);
  delete wm_serverUrl; wm_serverUrl = nullptr;
  delete wm_apiKey;    wm_apiKey    = nullptr;
  if (!connected) { Serial.println("[WiFi] Failed — restarting"); delay(500); ESP.restart(); }
  eink.showConnecting(WiFi.SSID());
  Serial.println("[WiFi] Connected to " + WiFi.SSID() + " — IP: " + WiFi.localIP().toString());
}

void doBootstrap() {
  led.setState(LEDState::BOOTSTRAPPING);
  led.setBaseState(LEDState::BOOTSTRAPPING);
  api.configure(cfg.serverUrl, cfg.apiKey);
  BootstrapResult res = api.bootstrap();
  lastBootstrap = millis();
  if (!res.ok) {
    if (res.httpCode == 401) {
      Serial.println("[Boot] 401 — invalid API key");
      eink.showUnpaired(); led.setState(LEDState::ERROR); led.setBaseState(LEDState::UNPAIRED);
      appState = AppState::UNCONFIGURED;
    } else {
      Serial.printf("[Boot] Error HTTP %d — retrying later\n", res.httpCode);
      if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
      led.setState(LEDState::ERROR); led.setBaseState(LEDState::IDLE);
      if (deviceName.isEmpty()) eink.showConnecting();
      appState = AppState::READY;
    }
    return;
  }
  deviceName = res.name;
  Serial.println("[Boot] Device: " + deviceName + " scanMode=" + String(res.scanMode));

  // First successful bootstrap proves the running firmware can talk to the
  // network and the server — commit the partition so the bootloader doesn't
  // roll it back on next reset. No-op on subsequent boots.
  static bool firstBootstrapDone = false;
  if (!firstBootstrapDone) {
    firstBootstrapDone = true;
    OTAUpdater::markCurrentAppValid();
  }

  // Server-driven OTA. If an update is available, this either reboots into
  // the new firmware (success) or falls through and we resume normal flow
  // — the same update will be retried on the next bootstrap.
  if (OTAUpdater::isUpdateAvailable(res.latestFirmwareVersion)) {
    Serial.printf("[OTA] Update available: %s -> %s\n",
      FIRMWARE_VERSION, res.latestFirmwareVersion.c_str());
    appState = AppState::UPDATING;
    led.setState(LEDState::UPDATING);
    led.setBaseState(LEDState::UPDATING);
    eink.showUpdating(FIRMWARE_VERSION, res.latestFirmwareVersion);
    if (!OTAUpdater::performUpdate(res.latestFirmwareUrl)) {
      eink.showUpdateFailed("Update failed - will retry");
      unsigned long wait = millis();
      while (millis() - wait < 3000) { led.update(); checkFactoryReset(); delay(10); }
    }
    // On success the device has rebooted and we never get here.
  }

  if (res.scanMode) {
    appState = AppState::SCAN_MODE;
    led.setBaseState(LEDState::SCAN_MODE); led.setState(LEDState::SCAN_MODE);
    eink.showScanMode(deviceName);
  } else {
    appState = AppState::READY;
    led.setBaseState(LEDState::IDLE); led.setState(LEDState::IDLE);
    eink.showReady(deviceName);
  }
}

void handleScan(const String& uid) {
  led.setState(LEDState::CONNECTING); led.update();
  PlayResult res = api.play(uid);
  if (res.ok) {
    led.setState(LEDState::SUCCESS);
    if (res.captured) {
      Serial.println("[Play] Tag captured: " + uid);
      eink.showReady(deviceName); lastBootstrap = 0;
    } else {
      Serial.println("[Play] Playing: " + res.content);
      eink.showLastPlayed(deviceName, res.content);
    }
  } else {
    Serial.println("[Play] Error: " + res.error);
    led.setState(LEDState::ERROR);
    eink.showError(res.error.isEmpty() ? "Play failed" : res.error);
    unsigned long wait = millis();
    while (millis() - wait < 3000) { led.update(); checkFactoryReset(); delay(10); }
    appState == AppState::SCAN_MODE ? eink.showScanMode(deviceName) : eink.showReady(deviceName);
  }
}

void checkFactoryReset() {
  if (digitalRead(PIN_FACTORY_RESET) == LOW) {
    if (!bootBtnHeld) { bootBtnHeld = true; bootBtnStart = millis(); Serial.println("[Reset] BOOT held..."); }
    else if (millis() - bootBtnStart > FACTORY_RESET_HOLD_MS) {
      led.setState(LEDState::ERROR); led.update(); eink.showUnpaired(); factoryReset();
    }
  } else { bootBtnHeld = false; }
}

void setup() {
  Serial.begin(115200); delay(100);
  Serial.printf("\n\n=== Jellybox starting %s ===\n", FIRMWARE_VERSION);
  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
  led.begin(); led.setState(LEDState::BOOTING); led.update();
  // Pre-set eInk control pins as outputs at idle level so GxEPD2's init()
  // doesn't trip the "digitalWrite before pinMode" warning on ESP32 core 3.x.
  pinMode(PIN_EINK_CS,  OUTPUT); digitalWrite(PIN_EINK_CS,  HIGH);
  pinMode(PIN_EINK_DC,  OUTPUT); digitalWrite(PIN_EINK_DC,  HIGH);
  pinMode(PIN_EINK_RST, OUTPUT); digitalWrite(PIN_EINK_RST, HIGH);
  eink.begin(); eink.showSplash(); eink.showConnecting();
  loadConfig();
  { unsigned long h = millis();
    while (digitalRead(PIN_FACTORY_RESET) == LOW) {
      led.update();
      if (millis() - h > FACTORY_RESET_HOLD_MS) { Serial.println("[Reset] Factory reset on boot"); led.setState(LEDState::ERROR); led.update(); eink.showUnpaired(); factoryReset(); }
      delay(10);
    }
  }
  nfcReady = nfc.begin();
  if (!nfcReady) Serial.println("[NFC] Reader not found — NFC disabled");
  bool needPortal = cfg.serverUrl.isEmpty() || cfg.apiKey.isEmpty();
  startWiFi(needPortal);
  loadConfig();
  if (cfg.serverUrl.isEmpty() || cfg.apiKey.isEmpty()) {
    Serial.println("[Config] Incomplete — waiting for reset");
    eink.showUnpaired(); led.setBaseState(LEDState::UNPAIRED); led.setState(LEDState::UNPAIRED);
    while (true) { led.update(); checkFactoryReset(); delay(10); }
  }
  doBootstrap();
}

void loop() {
  checkFactoryReset();
  led.update();
  if (millis() - lastBootstrap > BOOTSTRAP_INTERVAL_MS) { doBootstrap(); return; }
  if (nfcReady && (appState == AppState::READY || appState == AppState::SCAN_MODE)) {
    String uid = nfc.readUID();
    if (uid.length() > 0) handleScan(uid);
  }
  delay(10);
}
