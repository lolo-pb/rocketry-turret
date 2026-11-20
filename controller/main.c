#include "controller.h"

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
  struct Controller controller = {.state = DISARMED, .receivers = receivers, .count = argc - 2};
  for (size_t i = 0; i < controller.count; i++) {
    receivers[i].id = i + 1;
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


  int result = 0;

  while (!stopping) {
    polling_pool[DASHBOARD_LISTENER_SLOT] = (struct pollfd){.fd = dashboard.listener, .events = POLLIN};
    polling_pool[DASHBOARD_CLIENT_SLOT] = (struct pollfd){.fd = dashboard.client, .events = POLLIN};
    for (size_t i = 0; i < controller.count; i++)
      polling_pool[FIRST_RECEIVER_SLOT + i] = (struct pollfd){.fd = receivers[i].fd, .events = POLLIN};


    if (poll(polling_pool, FIRST_RECEIVER_SLOT + controller.count, 10) < 0) {
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

    bool changed = false;

    for (size_t i = 0; i < controller.count; i++)
      if (polling_pool[FIRST_RECEIVER_SLOT + i].revents && receiver_read(&receivers[i]))
        changed = true;


    Event event = NO_EVENT;
    if (controller.state == MANUAL_ACQUISITION)
      event = manual_update(&controller);
    else if (controller.state == AUTOMATIC_ACQUISITION)
      event = automatic_update(&controller);
    else if (controller.state == TRACKING)
      event = tracker_update(&controller);


    if (transition(&controller, event) || changed)
      dashboard_publish(&dashboard, &controller);
  }

  unlink(argv[1]);
  return result;
}
