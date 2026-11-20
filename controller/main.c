#include "controller.h"
#include "servos.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
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

  if (argc < 2 || argc > MAX_RECEIVERS + 2) {
    fprintf(stderr, "usage: %s SOCKET [MOCK_RECEIVER ...] (maximum %d)\n",
            argv[0], MAX_RECEIVERS);
    return 2;
  }


  struct Receiver receivers[MAX_RECEIVERS] = {0};
  struct Controller controller = {.state = DISARMED, .receivers = receivers, .recieverCount = argc - 2};
  for (size_t i = 0; i < controller.recieverCount; i++) {
    receivers[i].id = i + 1;
    receivers[i].latest.fix = -1;
    receivers[i].fd = receiver_open(argv[i + 2]);
    if (receivers[i].fd < 0) {
      perror(argv[i + 2]);
      return 1;
    }
  }

  struct Dashboard dashboard = {0};
  if (dashboard_open(&dashboard, argv[1]) < 0) {
    perror(argv[1]);
    return 1;
  }

  struct pollfd polling_pool[FIRST_RECEIVER_SLOT + MAX_RECEIVERS];


  struct ServoOutput servos;
  if (servos_open(&servos) < 0) {
    perror("open servo PWM channels");
    unlink(argv[1]);
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

  servos_close(&servos);
  unlink(argv[1]);
  return result;
}
