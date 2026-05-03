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

static const char PORTAL_CSS[] PROGMEM = R"(
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#121214;color:#e5e7eb;font-family:system-ui,-apple-system,sans-serif;min-height:100vh}
.wrap{max-width:380px;margin:40px auto;padding:0 20px}
h1{font-size:1.6rem;font-weight:700;color:#f9fafb;text-align:center;letter-spacing:.08em;margin-bottom:4px}
h2,.info{font-size:.8rem;color:#6b7280;text-align:center;font-weight:400;margin-bottom:28px}
form,fieldset{background:#1c1c1f;border:1px solid #27272a;border-radius:14px;padding:24px}
fieldset{margin-top:20px;border-top-color:#27272a}
legend{font-size:.7rem;font-weight:600;color:#9ca3af;text-transform:uppercase;letter-spacing:.08em;padding:0 6px;margin-left:-6px}
label{display:block;font-size:.75rem;font-weight:500;color:#9ca3af;margin-bottom:6px;margin-top:16px}
input[type=text],input[type=password],input[type=search]{
  width:100%;background:#0d0d10;border:1px solid #3f3f46;border-radius:8px;
  color:#f9fafb;font-size:.875rem;padding:10px 13px;outline:none;
  transition:border-color .15s,box-shadow .15s}
input:focus{border-color:#7c3aed;box-shadow:0 0 0 3px rgba(124,58,237,.2)}
::placeholder{color:#52525b}
br{display:none}
input[type=submit]{
  display:block;width:100%;margin-top:22px;
  background:#7c3aed;color:#fff;border:none;border-radius:9px;
  font-size:.875rem;font-weight:600;padding:12px;cursor:pointer;
  transition:background .15s,transform .1s}
input[type=submit]:hover{background:#6d28d9}
input[type=submit]:active{transform:scale(.98)}
.msg{font-size:.72rem;color:#6b7280;text-align:center;margin-top:14px}
</style>
)";

void startWiFi(bool forcePortal) {
  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(WIFI_TIMEOUT_S);
  wm.setSaveParamsCallback(onPortalSave);
  wm.setCustomHeadElement(PORTAL_CSS);
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
    ? (eink.showUnpaired(), led.setState(LEDState::UNPAIRED), led.setBaseState(LEDState::UNPAIRED), wm.startConfigPortal(WIFI_AP_NAME, WIFI_AP_PASSWORD))
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
    checkCharging();
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

void checkCharging() {
  if (appState != AppState::READY) return;
  bool charging = (digitalRead(PIN_CHRG)  == LOW);
  bool charged  = (digitalRead(PIN_STDBY) == LOW);
  LEDState target = charging ? LEDState::CHARGING : charged ? LEDState::CHARGED : LEDState::IDLE;
  static LEDState lastChargeState = LEDState::IDLE;
  if (target != lastChargeState) {
    lastChargeState = target; led.setState(target);
    Serial.printf("[Charge] %s\n", charging ? "charging" : charged ? "full" : "unplugged");
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
  Serial.printf("\n\n=== Jellybox starting (firmware %s) ===\n", FIRMWARE_VERSION);
  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
  pinMode(PIN_CHRG,          INPUT_PULLUP);
  pinMode(PIN_STDBY,         INPUT_PULLUP);
  led.begin(); led.setState(LEDState::CONNECTING); led.update();
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
  static unsigned long lastChargeCheck = 0;
  if (millis() - lastChargeCheck > 5000) { lastChargeCheck = millis(); checkCharging(); }
  if (nfcReady && (appState == AppState::READY || appState == AppState::SCAN_MODE)) {
    String uid = nfc.readUID();
    if (uid.length() > 0) handleScan(uid);
  }
  delay(10);
}
