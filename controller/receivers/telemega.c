#include "receivers.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t u16(const unsigned char *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static int16_t s16(const unsigned char *p)
{
    return (int16_t)u16(p);
}

static int32_t s32(const unsigned char *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 |
                     (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

bool telemega_parse(const char *line, char *output, size_t size)
{
    unsigned char bytes[64];
    if (strncmp(line, "TELEM ", 6))
        return false;
    const char *hex = line + 6;
    size_t length = strcspn(hex, "\r\n");
    if (length != 72)
        return false;
    for (size_t i = 0; i < length / 2; i++) {
        unsigned value;
        char pair[3] = {hex[2 * i], hex[2 * i + 1], 0};
        if (strspn(pair, "0123456789abcdefABCDEF") != 2 || sscanf(pair, "%x", &value) != 1)
            return false;
        bytes[i] = value;
    }
    if (bytes[0] != 34 || !(bytes[34] & 0x80))
        return false;
    unsigned checksum = 0x5a;
    for (size_t i = 1; i < 35; i++)
        checksum += bytes[i];
    if ((checksum & 0xff) != bytes[35])
        return false;

    const unsigned char *p = bytes + 1;
    int used = snprintf(output, size, "serial=%u tick_10ms=%u type=0x%02x rssi_raw=%u lqi=0x%02x",
                        u16(p), u16(p + 2), p[4], bytes[33], bytes[34]);
    if (used < 0 || (size_t)used >= size)
        return false;
    char *out = output + used;
    size_t left = size - used;
    switch (p[4]) {
    case 0x05:
        snprintf(out, left, " flags=%u altitude_m=%d latitude_deg=%.7f longitude_deg=%.7f utc=%04u-%02u-%02uT%02u:%02u:%02u pdop_raw=%u hdop_raw=%u vdop_raw=%u mode=%u speed_cm_s=%u climb_cm_s=%d course_raw=%u",
                 p[5], s16(p + 6), s32(p + 8) / 1e7, s32(p + 12) / 1e7,
                 2000 + p[16], p[17], p[18], p[19], p[20], p[21], p[22], p[23], p[24], p[25],
                 u16(p + 26), s16(p + 28), p[30]);
        break;
    case 0x08: case 0x12: case 0x13: case 0x14:
        snprintf(out, left, " tilt_deg=%u high_g_raw=%d pressure_pa=%.1f temperature_c=%.2f accel_raw=%d/%d/%d gyro_raw=%d/%d/%d mag_raw=%d/%d/%d",
                 p[5], s16(p + 6), s32(p + 8) / 10.0, s16(p + 12) / 100.0,
                 s16(p + 14), s16(p + 16), s16(p + 18), s16(p + 20), s16(p + 22),
                 s16(p + 24), s16(p + 26), s16(p + 28), s16(p + 30));
        break;
    case 0x09: case 0x15:
        snprintf(out, left, " state=%u battery_raw=%d pyro_battery_raw=%d continuity_raw=%d/%d/%d/%d/%d/%d ground_pressure_raw=%d ground_accel_raw=%d accel_plus_g=%d accel_minus_g=%d acceleration_m_s2=%.4f speed_m_s=%.4f height_m=%d",
                 p[5], s16(p + 6), s16(p + 8), (int8_t)p[10], (int8_t)p[11],
                 (int8_t)p[12], (int8_t)p[13], (int8_t)p[14], (int8_t)p[15],
                 s32(p + 16), s16(p + 20), s16(p + 22), s16(p + 24),
                 s16(p + 26) / 16.0, s16(p + 28) / 16.0, s16(p + 30));
        break;
    case 0x04:
        snprintf(out, left, " device_type=%u flight=%u config=%u.%u apogee_delay=%u main_altitude=%u max_log=%u callsign=%.8s firmware=%.8s",
                 p[5], u16(p + 6), p[8], p[9], u16(p + 10), u16(p + 12),
                 u16(p + 14), p + 16, p + 24);
        break;
    case 0x06:
        snprintf(out, left, " channels=%u satellites=%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u;%u/%u",
                 p[5], p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15],p[16],p[17],
                 p[18],p[19],p[20],p[21],p[22],p[23],p[24],p[25],p[26],p[27],p[28],p[29]);
        break;
    case 0x07:
        snprintf(out, left, " board_id=%u update_period_10ms=%u channels=%u data=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u",
                 p[5], p[6], p[7], u16(p + 8), u16(p + 10), u16(p + 12),
                 u16(p + 14), u16(p + 16), u16(p + 18), u16(p + 20),
                 u16(p + 22), u16(p + 24), u16(p + 26), u16(p + 28), u16(p + 30));
        break;
    default:
        return false;
    }
    return true;
}
