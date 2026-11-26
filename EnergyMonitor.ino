#include <ESP8266WiFi.h>
#include <InfluxDbClient.h>
#include <optional>

#include "HomeWizardData.h"
#include "APsystemsData.h"
#include "secrets.h"

constexpr unsigned long HOMEWIZARD_FETCH_INTERVAL_MS = 60000; // 60s
constexpr unsigned long APSYSTEMS_FETCH_INTERVAL_MS = 60UL * 60UL * 1000UL; // 1h
constexpr unsigned long DAILY_RESTART_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL; // 24h

// InfluxDB Cloud now serves a Let's Encrypt cert, which the client library's
// bundled InfluxDbCloud2CACert no longer matches; skip server verification,
// same as fetch() in HttpRequest.h.
InfluxDBClient influxClient(
    String("https://") + INFLUXDB_HOST,
    INFLUXDB_ORG,
    INFLUXDB_BUCKET,
    INFLUXDB_TOKEN
);

// Initialized to -INTERVAL so the first loop() runs immediately: unsigned
// wraparound makes now - last >= INTERVAL true on the first iteration.
unsigned long lastHomeWizardFetch = -HOMEWIZARD_FETCH_INTERVAL_MS;
unsigned long lastAPsystemsFetch = -APSYSTEMS_FETCH_INTERVAL_MS;

void setup() {
  Serial.begin(115200);
  delay(100);
  WiFi.mode(WIFI_STA);
  influxClient.setInsecure();
  Serial.println("\n=== HomeWizard + APsystems to InfluxDB Bridge ===");

  // Timestamps are Unix epoch seconds. Batch size 64 keeps the hourly solar
  // push (up to ~290 samples) to a handful of requests while not pinning a
  // large ring buffer in RAM; P1 cycles flush explicitly in writeToInfluxDB.
  if (!influxClient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S).batchSize(64))) {
    Serial.println("ERROR: InfluxDB client setup error: " + influxClient.getLastErrorMessage());
  }
}

void loop() {
  // Daily maintenance restart: clears the heap fragmentation that BearSSL/TLS
  // accumulates over long uptimes on ESP8266
  if (millis() >= DAILY_RESTART_INTERVAL_MS) {
    Serial.println("Performing daily maintenance restart");
    delay(100);
    ESP.restart();
  }

  if (!ensureWiFi()) return;

  const auto now = millis();
  if (now - lastHomeWizardFetch < HOMEWIZARD_FETCH_INTERVAL_MS) {
    delay(1000);
    return;
  }
  lastHomeWizardFetch = now;

  const auto homewizard = HomeWizardData::fetch();
  if (!homewizard) return;
  if (const auto err = homewizard->writeToInfluxDB(influxClient)) {
    Serial.println("ERROR: InfluxDB " + *err);
    return;
  }
  Serial.println("SUCCESS: HomeWizard data sent");

  if (now - lastAPsystemsFetch < APSYSTEMS_FETCH_INTERVAL_MS) return;
  lastAPsystemsFetch = now;

  if (const auto err = APsystemsData::fetchAndWriteToInfluxDB(homewizard->timestampSeconds, influxClient)) {
    Serial.println(*err);
    return;
  }
  Serial.println("SUCCESS: APsystems power data sent");
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("ERROR: WiFi down, reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (int attempts = 0; attempts < 20 && WiFi.status() != WL_CONNECTED; attempts++) {
    ESP.wdtFeed(); // keep the soft watchdog happy during the reconnect wait
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi reconnect failed, will retry next cycle");
    return false;
  }
  Serial.println("SUCCESS: WiFi reconnected, IP: " + WiFi.localIP().toString());
  return true;
}
