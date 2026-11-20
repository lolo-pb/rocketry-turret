#include "controller.h"
#include "servos.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

static int reachable_angle(double angle, int zero, int direction) {
  int start = -zero * direction;
  int end = (SERVO_TRAVEL_DEG - zero) * direction;
  return (int)lround(fmin(fmax(angle, fmin(start, end)), fmax(start, end)));
}

static int step_toward(int current, int target) {
  if (target - current > STEP_DEG)
    return current + STEP_DEG;
  if (current - target > STEP_DEG)
    return current - STEP_DEG;
  return target;
}

// Same station and local east/north/up frame as the original ground tracker.
static void update_target(struct Turret *turret, const struct Telemetry *sample) {
  const double radians = acos(-1.0) / 180.0;
  const double earth_radius_m = 6378137.0;
  double east = (sample->longitude - gs_state.longitude) * radians * earth_radius_m * cos(gs_state.latitude * radians);
  double north = (sample->latitude - gs_state.latitude) * radians * earth_radius_m;
  double up = sample->altitude_m - gs_state.altitude_m;
  if (turret->calibrated && east == turret->last_east_m && north == turret->last_north_m && up == turret->last_up_m)
    return;

  double horizontal = hypot(east, north);
  double bearing = atan2(north, east);

  if (!turret->calibrated) {
    if (horizontal < 1e-6)
      return;
    turret->zero_bearing_rad = bearing;
    turret->calibrated = true;
  } else if (hypot(hypot(east - turret->last_east_m, north - turret->last_north_m),
                   up - turret->last_up_m) > 20000.0) {
    return;
  }

  turret->last_east_m = east;
  turret->last_north_m = north;
  turret->last_up_m = up;
  if (horizontal >= 1e-6) {
    double yaw = remainder((bearing - turret->zero_bearing_rad) / radians, 360.0);
    turret->target_azimuth_deg = reachable_angle(yaw, AZIMUTH_ZERO_DEG, AZIMUTH_DIRECTION);
  }
  double tilt = atan2(up, horizontal) / radians;
  turret->target_elevation_deg = reachable_angle(tilt, ELEVATION_ZERO_DEG, ELEVATION_DIRECTION);
}

Event tracker_update(struct Controller *controller, struct ServoOutput *servos) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return NO_EVENT;
  int64_t now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  
  struct Turret *turret = &controller->turret;

  if (now_ms < turret->next_step_ms) return NO_EVENT;

  if (rocket_state.tracker_fix) update_target(turret, &rocket_state);

  turret->next_step_ms = now_ms + STEP_INTERVAL_MS;
  
  int azimuth = step_toward(turret->azimuth_deg, turret->target_azimuth_deg);
  int elevation = step_toward(turret->elevation_deg, turret->target_elevation_deg);
  
  if (azimuth == turret->azimuth_deg && elevation == turret->elevation_deg) return NO_EVENT;
  servos_write(servos, azimuth, elevation);
  
  turret->azimuth_deg = azimuth;
  turret->elevation_deg = elevation;

  if (turret->ticks++ % 10 == 0)
    printf("Az: %d | El: %d\n", azimuth, elevation);
  return NO_EVENT;
}
