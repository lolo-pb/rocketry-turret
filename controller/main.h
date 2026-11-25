#ifndef MAIN2_H
#define MAIN2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STEP_INTERVAL_MS 40
#define STEP_DEG 1

typedef enum State {
    DISARMED,
    AUTOMATIC_ACQUISITION,
    MANUAL_ACQUISITION,
    AWAITING_ARM,
    TRACKING,
    LANDED
} State;

typedef enum Event {
    NO_EVENT,
    START_AUTOMATIC,
    START_MANUAL,
    START_TRACKING,
    TARGET_ACQUIRED,
    ARM,
    LANDING_DETECTED
} Event;


struct Telemetry {
    double latitude, longitude, altitude_m;
    int fix;
    bool tracker_fix;
};

struct RocketState {
    struct Telemetry telemetry;
};

struct GroundStationState {
    double latitude, longitude, altitude_m;
    int elevation_min_deg, elevation_max_deg;
    int azimuth_min_deg, azimuth_max_deg;
    int azimuth_zero_deg, elevation_zero_deg;
    int azimuth_direction, elevation_direction;
};

extern struct RocketState rocket_state;
extern struct GroundStationState gs_state;

struct Turret {
    int azimuth_deg, elevation_deg;  // Commanded turret angles relative to calibrated zero
    int target_azimuth_deg, target_elevation_deg;
    int64_t next_step_ms;
    unsigned ticks;
    bool calibrated;
    double zero_bearing_rad;
    double last_east_m, last_north_m, last_up_m;
};

struct Receiver;
struct Controller {
    struct Turret turret;
    State state;
    struct Receiver *receivers;
    size_t recieverCount;
};

int initialize_controller(struct Controller *controller, struct Receiver *receivers, int argc, char **argv, int *socket_arg);

#endif
