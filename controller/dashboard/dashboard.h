#ifndef DASHBOARD2_H
#define DASHBOARD2_H

#include "main.h"

struct Dashboard {
    int listener, client;
    char line[256];
    size_t used;
};

void dashboard_publish(struct Dashboard *dashboard, const struct Controller *controller);

struct Inputs;
int initialize_dashboard(struct Dashboard *dashboard, const char *path);
void read_dashboard(struct Inputs *inputs, struct Dashboard *dashboard, struct Controller *controller);

#endif
