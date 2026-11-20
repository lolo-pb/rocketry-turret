#include "controller.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

bool parse_record(const char *record, struct Telemetry *sample)
{
    char crc[16], unit[8];
    double altitude_ft;
    struct Telemetry parsed;
    int end = 0;
    int fields = sscanf(record,
        "@ GPS_STAT %*[0-9] %*[0-9] %*[0-9] %*[0-9] %*[0-9:.] %15s %7s %*s"
        " Alt %lf lt %lf ln %lf Vel %*d %*d %*d Fix %1d"
        " # %*[0-9] %*[0-9] %*[0-9] %*[0-9]%n",
        crc, unit, &altitude_ft, &parsed.latitude, &parsed.longitude, &parsed.fix, &end);
    if (fields != 6 || !end || (strcasecmp(crc, "CRC_OK") && strcasecmp(crc, "CRC_ERR")))
        return false;
    if (strcasecmp(unit, "TRK") && strcasecmp(unit, "GS") && strcasecmp(unit, "FND"))
        return false;
    if (!isfinite(altitude_ft) || !isfinite(parsed.latitude) || !isfinite(parsed.longitude))
        return false;
    parsed.altitude_m = altitude_ft * 0.3048;
    *sample = parsed;
    return true;
}
