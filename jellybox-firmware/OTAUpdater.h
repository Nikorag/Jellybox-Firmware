#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include "Config.h"

// Over-the-air firmware updates.
//
// Driven by the server: every bootstrap response may include a
// `latestFirmware` object. If the version differs from FIRMWARE_VERSION
// the firmware downloads the new image directly from the URL (typically
// a GitHub release asset) and reboots into it.
//
// Rollback safety: a freshly-flashed image is marked PENDING_VERIFY by
// the bootloader. markCurrentAppValid() must be called after we've
// proven the new firmware works (i.e. completed a successful bootstrap)
// — otherwise the next reset rolls back to the previous partition.
class OTAUpdater {
public:
  // Compare server-advertised version against the compile-time version.
  // Returns false for empty input or for "dev" builds (so a developer
  // running an Arduino IDE build never auto-updates over their work).
  static bool isUpdateAvailable(const String& latestVersion) {
    if (latestVersion.isEmpty()) return false;
    if (String(FIRMWARE_VERSION) == "dev") return false;
    return latestVersion != String(FIRMWARE_VERSION);
  }

  // Download and apply the firmware at `url`. On success the device
  // reboots and this never returns. On failure returns false and the
  // caller should resume normal operation — the same update will be
  // retried on the next bootstrap cycle.
  static bool performUpdate(const String& url) {
    Serial.printf("[OTA] Starting update from %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setLedPin(-1, LOW);

    t_httpUpdate_return ret = httpUpdate.update(client, url, FIRMWARE_VERSION);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("[OTA] Failed: (%d) %s\n",
          httpUpdate.getLastError(),
          httpUpdate.getLastErrorString().c_str());
        return false;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("[OTA] Server reports no update");
        return false;
      case HTTP_UPDATE_OK:
        return true;  // unreachable — rebootOnUpdate(true) restarts the device
    }
    return false;
  }

  // Mark the running image as valid so the bootloader won't roll it
  // back on next reset. No-op when the partition is already marked
  // valid (i.e. on every boot after the first successful one).
  static void markCurrentAppValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
      if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        Serial.println("[OTA] Marking current image valid (was pending verify)");
        esp_ota_mark_app_valid_cancel_rollback();
      }
    }
  }
};
