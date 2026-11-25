#ifndef INPUTS2_H
#define INPUTS2_H

#include "receivers.h"
#include <poll.h>

#define FIRST_RECEIVER_SLOT 2

struct Dashboard;
struct Inputs {
    struct pollfd pool[FIRST_RECEIVER_SLOT + MAX_RECEIVERS];
};

/* Check both dashboard and receiver connections for input. */
int poll_inputs(struct Inputs *inputs, const struct Controller *controller, const struct Dashboard *dashboard);

#endif
