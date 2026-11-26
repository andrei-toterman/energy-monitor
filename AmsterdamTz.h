#pragma once

#include <AceTime.h>

// Shared Europe/Amsterdam timezone. The zone processor is static so AceTime
// can cache DST transition data between calls.
inline const ace_time::TimeZone& amsterdamTz() {
  static ace_time::BasicZoneProcessor processor;
  static const ace_time::TimeZone tz = ace_time::TimeZone::forZoneInfo(
      &ace_time::zonedb::kZoneEurope_Amsterdam,
      &processor
  );
  return tz;
}
