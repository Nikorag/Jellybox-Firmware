#pragma once

#include <Wire.h>
#include <Adafruit_PN532.h>
#include "Config.h"

class NFCReader {
public:
  // Polling mode: -1 means no IRQ / no RST pin required.
  NFCReader() : _nfc(-1, -1) {}

  bool begin() {
    Wire.begin(21, 22);
    delay(100);
    _nfc.begin();

    uint32_t ver = 0;
    for (uint8_t attempt = 0; attempt < 5 && !ver; attempt++) {
      ver = _nfc.getFirmwareVersion();
      if (!ver) delay(50);
    }

    if (!ver) {
      Serial.println("[NFC] PN532 not found — check wiring and jumper (I2C: SW1=ON, SW2=OFF)");
      return false;
    }

    Serial.printf("[NFC] PN532 firmware v%d.%d\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
    // No setPassiveActivationRetries — let the PN532 use its default behaviour.
    // Setting 0xFF with a timeout caused the detection cycle to outrun the timeout.
    _nfc.SAMConfig();
    return true;
  }

  // Returns a hex UID string when a tag is present, "" otherwise.
  // Blocks for up to 100 ms per call — keeps the main loop responsive
  // while giving the PN532 enough time to complete a detection cycle.
  String readUID() {
    uint8_t uid[7];
    uint8_t uidLen = 0;

    if (!_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100)) {
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
