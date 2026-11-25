#include "main.h"
#include "receivers.h"
#include "dashboard/dashboard.h"
#include "modes/modes.h"
#include "inputs.h"
#include "state.h"
#include "servos.h"
#include "logger.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct RocketState rocket_state;
struct GroundStationState gs_state = {
    .latitude = 31.95610, .longitude = -102.40344, .altitude_m = 915.0,
    .azimuth_min_deg = -90, .azimuth_max_deg = 90,
    .elevation_min_deg = 0, .elevation_max_deg = 90,
    .azimuth_zero_deg = 0, .elevation_zero_deg = 0,
    .azimuth_direction = 1, .elevation_direction = 1,
};

static volatile sig_atomic_t stopping;

static void stop(int signal_number){
    (void)signal_number;
    stopping = 1;
}

int initialize_controller(struct Controller *controller, struct Receiver *receivers, int argc, char **argv, int *socket_arg){
    umask(0077);
    bool tracking = argc > 1 && strcmp(argv[1], "--tracking") == 0;
    *socket_arg = tracking ? 2 : 1;
    if (argc < *socket_arg + 1 || argc > *socket_arg + MAX_RECEIVERS + 1) {
        fprintf(stderr, "usage: %s [--tracking] SOCKET [RECEIVER ...] (maximum %d)\n",
                argv[0], MAX_RECEIVERS);
        return 2;
    }
    *controller = (struct Controller){.state = tracking ? TRACKING : DISARMED,
        .receivers = receivers, .recieverCount = argc - *socket_arg - 1};
    return 0;
}

int main(int argc, char **argv){
    struct Receiver receivers[MAX_RECEIVERS] = {0};
    struct Controller controller;
    struct Dashboard dashboard = {0};
    struct ServoOutput servos;
    struct Inputs inputs;
    int socket_arg;
    int result;

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    result = initialize_controller(&controller, receivers, argc, argv, &socket_arg);
    if (result)
        return result;
    if (initialize_receivers(&controller, argc, argv, socket_arg) < 0 ||
        initialize_dashboard(&dashboard, argv[socket_arg]) < 0)
        return 1;
    if (servos_open(&servos) < 0) {
        perror("open servo PWM channels");
        unlink(argv[socket_arg]);
        return 1;
    }
    if (initialize_logging() < 0) {
        servos_close(&servos);
        unlink(argv[socket_arg]);
        return 1;
    }

    while (!stopping) {
        if (poll_inputs(&inputs, &controller, &dashboard) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            result = 1;
            break;
        }
        read_dashboard(&inputs, &dashboard, &controller);
        read_receivers(&inputs, &controller);

        Event event = update_mode(&controller, &servos);
        transition(&controller, event);
        dashboard_publish(&dashboard, &controller);

    }

    logger_stop();
    servos_close(&servos);
    unlink(argv[socket_arg]);
    return result;
}
