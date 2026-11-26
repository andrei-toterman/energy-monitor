#pragma once

#include <AceTime.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <InfluxDbClient.h>
#include <base64.h>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>
#include <optional>

#include "AmsterdamTz.h"
#include "HttpRequest.h"
#include "secrets.h"

// C++ port of the solar side of solar_data.py, at power resolution: once per
// hour we fetch the day's inverter power curve (energy_level=power) and write
// it to InfluxDB as `solar_power w=... <epoch>` points. Hourly/daily kWh are
// derivable from this by integration.
struct APsystemsData {
  // Fetches today's power curve and writes it straight to InfluxDB.
  // `nowEpoch` is the current P1-anchored epoch seconds; whole seconds
  // suffice for the signing timestamp (the API only checks it's within its
  // skew window). Returns nullopt on success, otherwise an error message.
  static std::optional<String> fetchAndWriteToInfluxDB(int64_t nowEpoch, InfluxDBClient& client) {
    const auto zdtNow = ace_time::ZonedDateTime::forUnixSeconds64(nowEpoch, amsterdamTz());
    if (zdtNow.isError()) return "ERROR: APsystems invalid local time";
    const ace_time::PlainDate day = ace_time::PlainDate::forComponents(zdtNow.year(), zdtNow.month(), zdtNow.day());

    JsonDocument doc;
    if (const auto err = fetch(nowEpoch, day, doc)) return err;
    return writeToInfluxDB(doc, day, client);
  }

private:
  // HTTP + signing + JSON parse into `doc`.
  static std::optional<String> fetch(int64_t nowEpoch, const ace_time::PlainDate& day, JsonDocument& doc) {
    char dateStr[16];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", day.year(), day.month(), day.day());

    // Build the path first so the signature's RequestPath (last segment, per
    // the API spec) is just the ECU id, then attach the query string.
    const String path = String("/user/api/v2/systems/") + APSYSTEMS_SID +
                        "/devices/inverter/batch/energy/" + APSYSTEMS_ECU_ID;
    const auto url = String("https://api.apsystemsema.com:9282") + path +
                     "?energy_level=power&date_range=" + dateStr;

    const int64_t tsMs = nowEpoch * 1000;
    const auto nonce = randomNonce();

    const auto response = ::fetchJson(url, {
        {"X-CA-AppId", APSYSTEMS_APP_ID},
        {"X-CA-Timestamp", String(tsMs)},
        {"X-CA-Nonce", nonce},
        {"X-CA-Signature-Method", "HmacSHA256"},
        {"X-CA-Signature", computeSignature(String(tsMs), nonce, APSYSTEMS_ECU_ID)},
    });
    if (response.status != HTTP_CODE_OK || response.error.length() > 0) {
      return "ERROR: APsystems " + response.error;
    }
    doc = response.doc;  // move-assign the parsed document
    return std::nullopt;
  }

  // Writes one point per power sample; inverter channels are summed per
  // timestamp to get total system power.
  static std::optional<String> writeToInfluxDB(const JsonDocument& doc, const ace_time::PlainDate& day, InfluxDBClient& client) {
    if (doc["code"].as<int>() != 0) {
      return "ERROR: APsystems error code " + String(doc["code"].as<int>());
    }

    const auto times = doc["data"]["time"].as<JsonArrayConst>();
    const auto power = doc["data"]["power"].as<JsonObjectConst>();
    if (times.isNull() || power.isNull()) {
      return "ERROR: APsystems response has no time/power data";
    }

    bool ok = true;
    for (int i = 0; i < (int)times.size(); i++) {
      float watts = 0;
      for (const JsonPairConst channel : power) {
        const auto values = channel.value().as<JsonArrayConst>();
        if (i < (int)values.size()) watts += values[i].as<float>();
      }
      Point point("solar_power");
      point.addField("w", watts, 1);
      point.setTime((unsigned long long)timeToEpochSeconds(day, times[i]));
      ok = client.writePoint(point) && ok;
    }
    if (client.flushBuffer() && ok) return std::nullopt;
    return "ERROR: InfluxDB " + client.getLastErrorMessage();
  }

  // --- APsystems request signing (HMAC-SHA256, Base64) ---

  static String computeSignature(const String& tsMs, const String& nonce, const String& requestPath) {
    const String toSign = tsMs + "/" + nonce + "/" + APSYSTEMS_APP_ID + "/" +
                          requestPath + "/GET/HmacSHA256";
    br_hmac_key_context keyCtx;
    br_hmac_key_init(&keyCtx, &br_sha256_vtable, APSYSTEMS_APP_SECRET, strlen(APSYSTEMS_APP_SECRET));
    br_hmac_context ctx;
    br_hmac_init(&ctx, &keyCtx, 32);
    br_hmac_update(&ctx, toSign.c_str(), toSign.length());
    uint8_t digest[32];
    br_hmac_out(&ctx, digest);
    return base64::encode(digest, sizeof(digest));
  }

  // The API only needs a unique nonce, not a real UUID.
  static String randomNonce() {
    String nonce;
    nonce.reserve(32);
    for (int i = 0; i < 4; i++) {
      char buf[9];
      snprintf(buf, sizeof(buf), "%08lx", (unsigned long)ESP.random());
      nonce += buf;
    }
    return nonce;
  }

  // "HH:mm" on `day` (local) as Unix epoch seconds, via AceTime so DST is
  // handled correctly.
  static int64_t timeToEpochSeconds(const ace_time::PlainDate& day, const char* hhmm) {
    int hour, minute;
    if (sscanf(hhmm, "%2d:%2d", &hour, &minute) != 2) return 0;
    const auto zdt = ace_time::ZonedDateTime::forComponents(day.year(), day.month(), day.day(), hour, minute, 0, amsterdamTz());
    if (zdt.isError()) return 0;
    return zdt.toUnixSeconds64();
  }
};
