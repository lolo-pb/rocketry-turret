#ifndef RECEIVERS2_H
#define RECEIVERS2_H

#include "main.h"

#define MAX_RECEIVERS 5

typedef enum ReceiverType { FEATHERWEIGHT, TELEDONGLE } ReceiverType;

struct Receiver {
    ReceiverType type;
    int fd;
    unsigned id;
    char record[1024];
    size_t used;
    struct Telemetry latest;
};

struct pollfd;
int receiver_open(const char *path);
void receivers_read(struct Controller *controller, const struct pollfd *inputs);

struct Inputs;
int initialize_receivers(struct Controller *controller, int argc, char **argv, int socket_arg);
void read_receivers(struct Inputs *inputs, struct Controller *controller);

#endif
