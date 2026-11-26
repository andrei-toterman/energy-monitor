#pragma once

// Placeholder secrets, expanded by envsubst at build time (see Makefile).
// The expanded file with real values is only written to the gitignored
// build/ directory and must never be committed.

constexpr const char APSYSTEMS_APP_ID[] = "$APSYSTEMS_APP_ID";
constexpr const char APSYSTEMS_APP_SECRET[] = "$APSYSTEMS_APP_SECRET";
constexpr const char APSYSTEMS_SID[] = "$APSYSTEMS_SID";
constexpr const char APSYSTEMS_ECU_ID[] = "$APSYSTEMS_ECU_ID";
constexpr const char HOMEWIZARD_API_PATH[] = "$HOMEWIZARD_API_PATH";
constexpr const char HOMEWIZARD_AUTH_TOKEN[] = "$HOMEWIZARD_AUTH_TOKEN";
constexpr const char HOMEWIZARD_HOST[] = "$HOMEWIZARD_HOST";
constexpr const char INFLUXDB_BUCKET[] = "$INFLUXDB_BUCKET";
constexpr const char INFLUXDB_HOST[] = "$INFLUXDB_HOST";
constexpr const char INFLUXDB_ORG[] = "$INFLUXDB_ORG";
constexpr const char INFLUXDB_TOKEN[] = "$INFLUXDB_TOKEN";
constexpr const char WIFI_PASSWORD[] = "$WIFI_PASSWORD";
constexpr const char WIFI_SSID[] = "$WIFI_SSID";
