#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdarg.h>

// Fire-and-forget UDP "syslog" for live debugging without a serial cable.
// Each call is ~10 ms, no TLS handshake, no per-call heap allocation —
// so logging from inside the loop doesn't perturb the timing we're often
// trying to measure.
//
// Listen on any machine on the LAN:
//   nc -u -l 5514
// or, preferred (adds a wall-clock timestamp to each line):
//   socat -u UDP-LISTEN:5514,fork - | ts
//
// Default target is the limited-broadcast address; set UDP_LOG_HOST in
// Config.h to a specific IP if your AP/router filters broadcast traffic.
class UDPLogger {
public:
  // Call once after WiFi is up. Host can be an IPv4 literal ("192.168.1.5"
  // or "255.255.255.255") or a hostname. Resolution happens here, not on
  // every send.
  void begin(const char* host, uint16_t port) {
    if (!WiFi.hostByName(host, _host)) {
      _host = IPAddress(255, 255, 255, 255);
    }
    _port = port;
    _enabled = true;
  }

  // printf-style. Prepends millis() so the receiver doesn't have to.
  void logf(const char* fmt, ...) {
    if (!_enabled || WiFi.status() != WL_CONNECTED) return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%lu ", (unsigned long)millis());
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
    va_end(args);
    _udp.beginPacket(_host, _port);
    _udp.write((const uint8_t*)buf, strlen(buf));
    _udp.endPacket();
  }

private:
  WiFiUDP   _udp;
  IPAddress _host;
  uint16_t  _port    = 0;
  bool      _enabled = false;
};
