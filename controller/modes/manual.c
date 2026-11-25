#include "main.h"
#include "servos.h"

#include <math.h>
#include <time.h>

enum { CENTER_X = 504, CENTER_Y = 498, DEAD_ZONE = 30, INPUT_TIMEOUT_MS = 250 };

static int stick_x, stick_y;
static int64_t received_ms;
static double azimuth_fraction, elevation_fraction;

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void manual_input(int x, int y)
{
    stick_x = x;
    stick_y = y;
    received_ms = monotonic_ms();
}

static int advance_axis(int raw, int center, int current, double *fraction,
                        int min, int max, int zero, int direction)
{
    double delta = raw - center;
    if (fabs(delta) <= DEAD_ZONE) {
        *fraction = 0;
        return current;
    }
    double travel = delta < 0 ? center : 1023 - center;
    double step = STEP_DEG * (fabs(delta) - DEAD_ZONE) / (travel - DEAD_ZONE);
    double next = current + *fraction + copysign(step, delta);
    int first = (min - zero) * direction;
    int last = (max - zero) * direction;
    next = fmin(fmax(next, fmin(first, last)), fmax(first, last));
    int angle = (int)lround(next);
    *fraction = next - angle;
    return angle;
}

Event manual_update(struct Controller *controller, struct ServoOutput *servos)
{
    struct Turret *turret = &controller->turret;
    int64_t now = monotonic_ms();
    if (!received_ms || !now || now - received_ms >= INPUT_TIMEOUT_MS) {
        azimuth_fraction = elevation_fraction = 0;
        return NO_EVENT;
    }
    if (now < turret->next_step_ms)
        return NO_EVENT;
    turret->next_step_ms = now + STEP_INTERVAL_MS;

    int azimuth = advance_axis(stick_x, CENTER_X, turret->azimuth_deg, &azimuth_fraction,
                              gs_state.azimuth_min_deg, gs_state.azimuth_max_deg,
                              gs_state.azimuth_zero_deg, gs_state.azimuth_direction);
    int elevation = advance_axis(stick_y, CENTER_Y, turret->elevation_deg, &elevation_fraction,
                                gs_state.elevation_min_deg, gs_state.elevation_max_deg,
                                gs_state.elevation_zero_deg, gs_state.elevation_direction);
    if (azimuth != turret->azimuth_deg || elevation != turret->elevation_deg) {
        servos_write(servos, azimuth, elevation);
        turret->azimuth_deg = turret->target_azimuth_deg = azimuth;
        turret->elevation_deg = turret->target_elevation_deg = elevation;
    }
    return NO_EVENT;
}
