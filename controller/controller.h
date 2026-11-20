#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>

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
    TARGET_ACQUIRED,
    ARM,
    LANDING_DETECTED
} Event;


struct Telemetry {
    double latitude, longitude, altitude_m;
    int fix;
};

struct Receiver {
    int fd;
    unsigned id;
    char record[1024];
    size_t used;
    bool has_sample;
    struct Telemetry latest;
};

struct Controller {
    State state;
    struct Receiver *receivers;
    size_t count;
};

bool transition(struct Controller *controller, Event event);
Event automatic_update(const struct Controller *controller);
Event manual_update(const struct Controller *controller);
Event tracker_update(const struct Controller *controller);
bool parse_record(const char *record, struct Telemetry *sample);
int receiver_open(const char *path);
bool receiver_read(struct Receiver *receiver);

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
