#include "controller.h"
#include "servos.h"
#include "logger.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_RECEIVERS 5

#define DASHBOARD_LISTENER_SLOT 0
#define DASHBOARD_CLIENT_SLOT 1
#define FIRST_RECEIVER_SLOT 2

struct RocketState rocket_state;
struct GroundStationState gs_state = {
    .latitude = 31.95610,
    .longitude = -102.40344,
    .altitude_m = 915.0,
    .azimuth_min_deg = -90,
    .azimuth_max_deg = 90,
    .elevation_min_deg = 0,
    .elevation_max_deg = 90,
    .azimuth_zero_deg = 0,
    .elevation_zero_deg = 0,
};

static volatile sig_atomic_t stopping;

static void stop(int signal_number) {
  (void)signal_number;
  stopping = 1;
}

int main(int argc, char **argv) {
  signal(SIGINT, stop);
  signal(SIGTERM, stop);
  umask(0077);

  bool start_tracking = argc > 1 && strcmp(argv[1], "--tracking") == 0;
  int dashboard_socket_arg = start_tracking ? 2 : 1;
  if (argc < dashboard_socket_arg + 1 || argc > dashboard_socket_arg + MAX_RECEIVERS + 1) {
    fprintf(stderr, "usage: %s [--tracking] SOCKET [RECEIVER ...] (maximum %d)\n",
            argv[0], MAX_RECEIVERS);
    return 2;
  }


  struct Receiver receivers[MAX_RECEIVERS] = {0};
  struct Controller controller = {.state = start_tracking ? TRACKING : DISARMED, .receivers = receivers, .recieverCount = argc - dashboard_socket_arg - 1};
  for (size_t i = 0; i < controller.recieverCount; i++) {
    receivers[i].id = i + 1;
    receivers[i].latest.fix = -1;
    const char *path = argv[dashboard_socket_arg + 1 + i];
    if (strncmp(path, "teledongle:", 11) == 0) {
      receivers[i].type = TELEDONGLE;
      path += 11;
    } else if (strncmp(path, "featherweight:", 14) == 0) {
      path += 14;
    }
    receivers[i].fd = receiver_open(path);
    if (receivers[i].fd < 0) {
      perror(argv[dashboard_socket_arg + 1 + i]);
      return 1;
    }
  }

  struct Dashboard dashboard = {0};
  if (dashboard_open(&dashboard, argv[dashboard_socket_arg]) < 0) {
    perror(argv[dashboard_socket_arg]);
    return 1;
  }

  struct pollfd polling_pool[FIRST_RECEIVER_SLOT + MAX_RECEIVERS];


  struct ServoOutput servos;
  if (servos_open(&servos) < 0) {
    perror("open servo PWM channels");
    unlink(argv[dashboard_socket_arg]);
    return 1;
  }


  const char *log_dir = getenv("TURRET_LOG_DIR");
  if (!log_dir)
    log_dir = "logs";
  if (mkdir(log_dir, 0700) < 0 && errno != EEXIST) {
    perror("create log directory");
    servos_close(&servos);
    unlink(argv[dashboard_socket_arg]);
    return 1;
  }
  if (logger_start(log_dir) < 0) {
    perror("start telemetry logger");
    servos_close(&servos);
    unlink(argv[dashboard_socket_arg]);
    return 1;
  }

  int result = 0;

  while (!stopping) {
    polling_pool[DASHBOARD_LISTENER_SLOT] = (struct pollfd){.fd = dashboard.listener, .events = POLLIN};
    polling_pool[DASHBOARD_CLIENT_SLOT] = (struct pollfd){.fd = dashboard.client, .events = POLLIN};
    for (size_t i = 0; i < controller.recieverCount; i++)
      polling_pool[FIRST_RECEIVER_SLOT + i] = (struct pollfd){.fd = receivers[i].fd, .events = POLLIN};


    if (poll(polling_pool, FIRST_RECEIVER_SLOT + controller.recieverCount, 10) < 0) {
      if (errno == EINTR)
        continue;

      perror("poll");
      result = 1;
      break;
    }

    if (polling_pool[DASHBOARD_CLIENT_SLOT].revents)
      dashboard_read(&dashboard, &controller);

    if (polling_pool[DASHBOARD_LISTENER_SLOT].revents & POLLIN)
      dashboard_accept(&dashboard, &controller);

    receivers_read(&controller, &polling_pool[FIRST_RECEIVER_SLOT]);

    Event event = NO_EVENT;
    if (controller.state == MANUAL_ACQUISITION)
      event = manual_update(&controller);
    else if (controller.state == AUTOMATIC_ACQUISITION)
      event = automatic_update(&controller);
    else if (controller.state == TRACKING)
      event = tracker_update(&controller, &servos);


    transition(&controller, event);
    dashboard_publish(&dashboard, &controller);
  }

  logger_stop();
  servos_close(&servos);
  unlink(argv[dashboard_socket_arg]);
  return result;
}
