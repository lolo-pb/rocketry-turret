#include "servos.h"
#include "controller.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PERIOD_NS 20000000

static int attribute(int dir, const char *name, int value)
{
    char text[32];
    int length = snprintf(text, sizeof text, "%d\n", value);
    int fd = openat(dir, name, O_WRONLY);
    if (fd < 0)
        return -1;
    ssize_t written = write(fd, text, length);
    int error = written < 0 ? errno : EIO;
    if (close(fd) < 0)
        return -1;
    if (written != length) {
        errno = error;
        return -1;
    }
    return 0;
}

static int open_axis(struct Servo *servo, int chip, const char *channel, const char *setting)
{
    const char *range = getenv(setting);
    char extra;
    if (!range || sscanf(range, "%d:%d%c", &servo->min_us, &servo->max_us, &extra) != 2 ||
        servo->min_us <= 0 || servo->max_us <= servo->min_us || servo->max_us >= PERIOD_NS / 1000) {
        fprintf(stderr, "%s requires min_us:max_us for the servo's 0..%d degree range\n", setting, SERVO_TRAVEL_DEG);
        errno = EINVAL;
        return -1;
    }
    servo->dir = openat(chip, channel, O_RDONLY | O_DIRECTORY);
    servo->last_angle = -1;
    return servo->dir < 0 ? -1 : 0;
}

int servos_open(struct ServoOutput *output)
{
    *output = (struct ServoOutput){.azimuth.dir = -1, .elevation.dir = -1};
    const char *path = getenv("TURRET_PWM_CHIP");
    if (!path)
        return 0;
    int chip = open(path, O_RDONLY | O_DIRECTORY);
    if (chip < 0)
        return -1;
    int result = open_axis(&output->azimuth, chip, "pwm0", "TURRET_AZ_PULSE_US");
    if (result == 0)
        result = open_axis(&output->elevation, chip, "pwm1", "TURRET_EL_PULSE_US");
    int error = errno;
    close(chip);
    if (result < 0) {
        servos_close(output);
        errno = error;
    }
    return result;
}

static int write_angle(struct Servo *servo, int angle)
{
    if (servo->dir < 0 || angle == servo->last_angle)
        return 0;
    if (!servo->started) {
        if (attribute(servo->dir, "enable", 0) || attribute(servo->dir, "duty_cycle", 0) ||
            attribute(servo->dir, "period", PERIOD_NS))
            return -1;
    }
    int duty_ns = servo->min_us * 1000 +
        (int)((long long)(servo->max_us - servo->min_us) * 1000 * angle / SERVO_TRAVEL_DEG);
    if (attribute(servo->dir, "duty_cycle", duty_ns) ||
        (!servo->started && attribute(servo->dir, "enable", 1)))
        return -1;
    servo->started = 1;
    servo->last_angle = angle;
    return 0;
}

void servos_write(struct ServoOutput *output, int azimuth, int elevation)
{
    if (write_angle(&output->azimuth, AZIMUTH_ZERO_DEG + AZIMUTH_DIRECTION * azimuth) < 0)
        perror("write azimuth PWM");
    if (write_angle(&output->elevation, ELEVATION_ZERO_DEG + ELEVATION_DIRECTION * elevation) < 0)
        perror("write elevation PWM");
}

void servos_close(struct ServoOutput *output)
{
    struct Servo *axes[] = {&output->azimuth, &output->elevation};
    for (size_t i = 0; i < 2; i++) {
        if (axes[i]->started && attribute(axes[i]->dir, "enable", 0) < 0)
            perror("disable PWM");
        if (axes[i]->dir >= 0)
            close(axes[i]->dir);
    }
}
