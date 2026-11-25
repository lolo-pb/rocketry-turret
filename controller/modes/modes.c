#include "modes.h"

Event update_mode(struct Controller *controller, struct ServoOutput *servos)
{
    if (controller->state == MANUAL_ACQUISITION)
        return manual_update(controller, servos);
    if (controller->state == AUTOMATIC_ACQUISITION)
        return automatic_update(controller);
    if (controller->state == TRACKING)
        return tracker_update(controller, servos);
    return NO_EVENT;
}
