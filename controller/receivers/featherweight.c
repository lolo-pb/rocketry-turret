#include "main.h"
#include "parsers.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

bool parse_record(const char *record, struct Telemetry *sample, char *fields, size_t size)
{
    char crc[16], unit[8], id[64], timestamp[32];
    unsigned length, year, month, day, satellites, sat24, sat32, sat40;
    int horizontal_speed, heading, vertical_speed;
    double altitude_ft;
    struct Telemetry parsed;
    int end = 0;
    int matched = sscanf(record,
        "@ GPS_STAT %u %u %u %u %31s %15s %7s %63s"
        " Alt %lf lt %lf ln %lf Vel %d %d %d Fix %1d"
        " # %u %u %u %u%n",
        &length, &year, &month, &day, timestamp, crc, unit, id, &altitude_ft,
        &parsed.latitude, &parsed.longitude, &horizontal_speed, &heading, &vertical_speed,
        &parsed.fix, &satellites, &sat24, &sat32, &sat40, &end);
    if (matched != 19 || !end || (strcasecmp(crc, "CRC_OK") && strcasecmp(crc, "CRC_ERR")))
        return false;
    if (strcasecmp(unit, "TRK") && strcasecmp(unit, "GS") && strcasecmp(unit, "FND"))
        return false;
    if (!isfinite(altitude_ft) || !isfinite(parsed.latitude) || !isfinite(parsed.longitude))
        return false;
    parsed.altitude_m = altitude_ft * 0.3048;
    parsed.tracker_fix = !strcasecmp(crc, "CRC_OK") && !strcasecmp(unit, "TRK")
        && parsed.fix == 3 && fabs(parsed.latitude) <= 90 && fabs(parsed.longitude) <= 180;
    snprintf(fields, size, "type=GPS_STAT id=%s utc=%04u-%02u-%02uT%s crc=%s unit=%s altitude_m=%.3f latitude_deg=%.7f longitude_deg=%.7f speed_ft_s=%d heading_deg=%d vertical_ft_s=%d fix=%d satellites=%u sat24=%u sat32=%u sat40=%u",
             id, year, month, day, timestamp, crc, unit, parsed.altitude_m,
             parsed.latitude, parsed.longitude, horizontal_speed, heading, vertical_speed,
             parsed.fix, satellites, sat24, sat32, sat40);
    *sample = parsed;
    return true;
}
