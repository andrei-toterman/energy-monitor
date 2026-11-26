#pragma once

#include <AceTime.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <InfluxDbClient.h>
#include <optional>

#include "AmsterdamTz.h"
#include "HttpRequest.h"
#include "secrets.h"

struct HomeWizardData {
  int64_t timestampSeconds;
  float energyImportT1Kwh;
  float energyImportT2Kwh;
  float energyExportT1Kwh;
  float energyExportT2Kwh;

  // instantaneous power, signed: negative = export
  float powerW;

  // since gas data comes from a different reader, it comes in the "external" reading.
  // it has its own independent timestamp, and is updated less frequently
  float gasM3;
  int64_t gasTimestampSeconds;

  static std::optional<HomeWizardData> fetch() {
    const auto url = String("https://") + HOMEWIZARD_HOST + HOMEWIZARD_API_PATH;
    const auto response = ::fetchJson(url, {
        {"Authorization", String("Bearer ") + HOMEWIZARD_AUTH_TOKEN},
        {"X-Api-Version", "2"},
    });

    if (response.status != HTTP_CODE_OK || response.error.length() > 0) {
      Serial.println("ERROR: HomeWizard " + response.error);
      return std::nullopt;
    }

    const auto data = fromJson(response.doc);
    if (data.timestampSeconds == 0) {
      Serial.println("ERROR: HomeWizard response has no valid timestamp");
      return std::nullopt;
    }
    return data;
  }

  static HomeWizardData fromJson(const JsonDocument& doc) {
    HomeWizardData data{};

    data.energyImportT1Kwh = doc["energy_import_t1_kwh"];
    data.energyImportT2Kwh = doc["energy_import_t2_kwh"];
    data.energyExportT1Kwh = doc["energy_export_t1_kwh"];
    data.energyExportT2Kwh = doc["energy_export_t2_kwh"];
    data.powerW = doc["power_w"];
    data.timestampSeconds = timestampSecondsFromString(doc["timestamp"]);

    for (const JsonVariantConst ext : doc["external"].as<JsonArrayConst>()) {
      if (ext["type"] == "gas_meter") {
        data.gasM3 = ext["value"];
        data.gasTimestampSeconds = timestampSecondsFromString(ext["timestamp"]);
        break;
      }
    }

    return data;
  }

  Point electricityPoint() const {
    Point point("electricity");
    point.addField("energy_import_t1_kwh", energyImportT1Kwh, 3);
    point.addField("energy_import_t2_kwh", energyImportT2Kwh, 3);
    point.addField("energy_export_t1_kwh", energyExportT1Kwh, 3);
    point.addField("energy_export_t2_kwh", energyExportT2Kwh, 3);
    point.addField("power_w", powerW, 1);
    point.setTime((unsigned long long)timestampSeconds);
    return point;
  }

  // nullopt when there is no gas reading
  std::optional<Point> gasPoint() const {
    if (gasTimestampSeconds == 0) return std::nullopt;
    Point point("gas");
    point.addField("total_m3", gasM3, 3);
    point.setTime((unsigned long long)gasTimestampSeconds);
    return point;
  }

  std::optional<String> writeToInfluxDB(InfluxDBClient& client) const {
    auto electricity = electricityPoint();
    bool ok = client.writePoint(electricity);
    if (auto gas = gasPoint()) {
      ok = client.writePoint(*gas) && ok;
    }
    if (client.flushBuffer() && ok) return std::nullopt;
    return client.getLastErrorMessage();
  }

private:
  // Convert the dongle's ISO 8601 timestamp ("2026-10-18T17:41:17",
  // Amsterdam local time) to Unix epoch seconds. Returns 0 if missing,
  // malformed, or containing invalid components.
  static int64_t timestampSecondsFromString(const char* ts) {
    if (!ts) {
        Serial.println("ERROR: HomeWizard no timestamp found");
        return 0;
    }

    int year, month, day, hour, minute, second;
    const auto scanned = sscanf(ts, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second);
    if (scanned != 6) {
        Serial.println("ERROR: HomeWizard failed to parse timestamp");
        return 0;
    }

    const auto zdt = ace_time::ZonedDateTime::forComponents(year, month, day, hour, minute, second, amsterdamTz());
    if (zdt.isError()) {
        Serial.println("ERROR: HomeWizard failed to convert timestamp to UTC");
        return 0;
    }

    return zdt.toUnixSeconds64();
  }
};
