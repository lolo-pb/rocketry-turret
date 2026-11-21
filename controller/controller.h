#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STEP_INTERVAL_MS 40
#define STEP_DEG 1
#define AZIMUTH_DIRECTION 1
#define ELEVATION_DIRECTION 1

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
};

extern struct RocketState rocket_state;
extern struct GroundStationState gs_state;

typedef enum ReceiverType { FEATHERWEIGHT, TELEDONGLE } ReceiverType;

struct Receiver {
    ReceiverType type;
    int fd;
    unsigned id;
    char record[1024];
    size_t used;
    struct Telemetry latest;
};

struct Turret {
    int azimuth_deg, elevation_deg;  // Commanded turret angles relative to calibrated zero
    int target_azimuth_deg, target_elevation_deg;
    int64_t next_step_ms;
    unsigned ticks;
    bool calibrated;
    double zero_bearing_rad;
    double last_east_m, last_north_m, last_up_m;
};

struct Controller {
    struct Turret turret;
    State state;
    struct Receiver *receivers;
    size_t recieverCount;
};

bool transition(struct Controller *controller, Event event);
Event automatic_update(const struct Controller *controller);
Event manual_update(const struct Controller *controller);
struct ServoOutput;
Event tracker_update(struct Controller *controller, struct ServoOutput *servos);
bool parse_record(const char *record, struct Telemetry *sample, char *fields, size_t size);
int receiver_open(const char *path);
struct pollfd;
void receivers_read(struct Controller *controller, const struct pollfd *inputs);

struct Dashboard {
    int listener, client;
    char line[256];
    size_t used;
};
int dashboard_open(struct Dashboard *dashboard, const char *path);
void dashboard_accept(struct Dashboard *dashboard, const struct Controller *controller);
void dashboard_read(struct Dashboard *dashboard, struct Controller *controller);
void dashboard_publish(struct Dashboard *dashboard, const struct Controller *controller);

#endif
