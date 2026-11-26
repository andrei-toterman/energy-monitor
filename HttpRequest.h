#pragma once

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <initializer_list>

struct JsonFetchResult {
  int status;        // HTTP status, 0 on transport failure
  String error;      // transport error or non-200 body, for logging
  JsonDocument doc;  // parsed body, valid when status == HTTP_CODE_OK
};

struct Header {
    String name;
    String value;
};

JsonFetchResult fetchJson(const String& url, const std::initializer_list<Header> headers = {}) {
    static WiFiClientSecure wifi;
    static HTTPClient http;

    Serial.println("Fetching " + url);

    wifi.setInsecure();
    // Smaller TLS buffers free heap vs the 16KB defaults — needed so a
    // multi-KB JSON body can be buffered and parsed on the ESP8266. RX must
    // still fit the server's certificate chain (~4KB for Let's Encrypt).
    wifi.setBufferSizes(8192, 1024);
    http.begin(wifi, url);

    for (const auto& header : headers) {
        http.addHeader(header.name, header.value);
    }

    JsonFetchResult result;
    result.status = http.GET();
    String body = http.getString();
    http.end();

    if (result.status == HTTP_CODE_OK) {
        // A parse error can leave a partially-filled (non-null) doc, so callers
        // must check result.error, not just doc.isNull().
        if (const auto err = deserializeJson(result.doc, body.c_str())) {
            result.error = "JSON parse error: " + String(err.c_str());
        }
    } else {
        result.error = "HTTP " + String(result.status) + " " + body;
    }

    return result;
}
