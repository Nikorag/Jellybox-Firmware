#pragma once

#include <Wire.h>
#include <Adafruit_PN532.h>
#include "Config.h"

class NFCReader {
public:
  // I2C mode constructor — uses IRQ and RST pins; SDA/SCL are ESP32 defaults (21/22)
  NFCReader() : _nfc(PIN_PN532_IRQ, PIN_PN532_RST) {}

  // Returns true if the PN532 was found and configured.
  bool begin() {
    Wire.begin();  // init I2C with default ESP32 pins (SDA=21, SCL=22)
    _nfc.begin();

    uint32_t ver = _nfc.getFirmwareVersion();
    if (!ver) {
      Serial.println("[NFC] PN532 not found — check wiring");
      return false;
    }

    Serial.printf("[NFC] PN532 firmware v%d.%d\n",
      (ver >> 16) & 0xFF,
      (ver >>  8) & 0xFF);

    // Try once per poll, return quickly if no card present
    _nfc.setPassiveActivationRetries(1);
    _nfc.SAMConfig();
    return true;
  }

  // Non-blocking read — returns hex UID string, or "" if nothing / debounced.
  // Blocks for up to ~50 ms while the PN532 performs one activation attempt.
  String readUID() {
    uint8_t uid[7];
    uint8_t uidLen = 0;

    if (!_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
      return "";
    }

    String uidStr = "";
    for (uint8_t i = 0; i < uidLen; i++) {
      if (uid[i] < 0x10) uidStr += "0";
      uidStr += String(uid[i], HEX);
    }
    uidStr.toUpperCase();

    // Debounce: suppress repeats of the same UID within the window
    unsigned long now = millis();
    if (uidStr == _lastUID && (now - _lastScanTime) < SCAN_DEBOUNCE_MS) {
      return "";
    }

    _lastUID      = uidStr;
    _lastScanTime = now;
    Serial.println("[NFC] Tag: " + uidStr);
    return uidStr;
  }

private:
  Adafruit_PN532 _nfc;
  String         _lastUID      = "";
  unsigned long  _lastScanTime = 0;
};
