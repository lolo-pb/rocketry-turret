#ifndef MODES2_H
#define MODES2_H

#include "main.h"

struct ServoOutput;
Event automatic_update(const struct Controller *controller);
void manual_input(int x, int y);
Event manual_update(struct Controller *controller, struct ServoOutput *servos);
Event tracker_update(struct Controller *controller, struct ServoOutput *servos);
Event update_mode(struct Controller *controller, struct ServoOutput *servos);

#endif
