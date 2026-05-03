#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Config.h"

// Result from GET /api/device/me
struct BootstrapResult {
  bool   ok       = false;
  String name;
  bool   scanMode = false;
  int    httpCode = 0;
  // Latest firmware advertised by the server. Empty when the server hasn't
  // populated `latestFirmware` yet — OTAUpdater treats empty as "no update".
  String latestFirmwareVersion;
  String latestFirmwareUrl;
};

// Result from POST /api/play
struct PlayResult {
  bool   ok       = false;   // success (played or captured)
  bool   captured = false;   // scan-capture mode: tag was registered, not played
  String content;            // media title from a successful play
  String error;              // human-readable error, if !ok
  int    httpCode = 0;
};

class APIClient {
public:
  void configure(const String& serverUrl, const String& apiKey) {
    _serverUrl = serverUrl;
    _apiKey    = apiKey;
  }

  // GET /api/device/me — called on boot and every BOOTSTRAP_INTERVAL_MS
  BootstrapResult bootstrap() {
    BootstrapResult result;
    String url = _serverUrl + "/api/device/me";

    WiFiClientSecure client;
    client.setInsecure();  // no CA bundle on device — trust any cert

    HTTPClient http;
    if (!http.begin(client, url)) {
      Serial.println("[API] bootstrap: http.begin() failed");
      return result;
    }

    http.addHeader("Authorization", "Bearer " + _apiKey);
    http.setTimeout(6000);  // keep under task WDT window (default ~5–8 s); TLS handshake included

    result.httpCode = http.GET();
    Serial.printf("[API] GET /api/device/me → %d\n", result.httpCode);

    if (result.httpCode == 200) {
      String body = http.getString();
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err == DeserializationError::Ok) {
        result.name     = doc["name"].as<String>();
        result.scanMode = doc["scanMode"] | false;
        JsonObject fw = doc["latestFirmware"];
        if (fw) {
          result.latestFirmwareVersion = fw["version"] | "";
          result.latestFirmwareUrl     = fw["url"]     | "";
        }
        result.ok       = true;
      } else {
        Serial.println("[API] bootstrap JSON error: " + String(err.c_str()));
      }
    }

    http.end();
    return result;
  }

  // POST /api/play  { "tagId": "..." }
  PlayResult play(const String& tagId) {
    PlayResult result;
    String url = _serverUrl + "/api/play";

    JsonDocument bodyDoc;
    bodyDoc["tagId"] = tagId;
    String bodyStr;
    serializeJson(bodyDoc, bodyStr);

    Serial.println("[API] POST /api/play body: " + bodyStr);

    WiFiClientSecure client;
    client.setInsecure();  // no CA bundle on device — trust any cert (same rationale as bootstrap)

    HTTPClient http;
    if (!http.begin(client, url)) {
      result.error = "begin() failed";
      return result;
    }

    http.addHeader("Authorization", "Bearer " + _apiKey);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);  // keep under task WDT window (default ~5–8 s); TLS handshake included

    result.httpCode = http.POST(bodyStr);
    Serial.printf("[API] POST /api/play → %d\n", result.httpCode);

    String body = http.getString();
    http.end();

    if (body.isEmpty()) {
      result.error = "Empty response";
      return result;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err != DeserializationError::Ok) {
      result.error = "Invalid JSON";
      return result;
    }

    if (result.httpCode == 200) {
      // Scan-capture mode: tag UID was stored, not played
      if (doc["captured"] | false) {
        result.ok       = true;
        result.captured = true;
        return result;
      }
      // Normal play
      if (doc["success"] | false) {
        result.ok      = true;
        result.content = doc["content"] | String("");
        return result;
      }
    }

    result.error = doc["error"] | String("HTTP " + String(result.httpCode));
    return result;
  }

private:
  String _serverUrl;
  String _apiKey;
};
