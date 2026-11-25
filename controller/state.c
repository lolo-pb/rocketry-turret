#include "controller.h"

bool transition(struct Controller *controller, Event event)
{
    static const struct {
        State from;
        Event event;
        State to;
    } transitions[] = {
        { DISARMED, START_AUTOMATIC, AUTOMATIC_ACQUISITION },
        { DISARMED, START_MANUAL, MANUAL_ACQUISITION },
        { DISARMED, START_TRACKING, TRACKING },
        { AUTOMATIC_ACQUISITION, TARGET_ACQUIRED, AWAITING_ARM },
        { MANUAL_ACQUISITION, TARGET_ACQUIRED, AWAITING_ARM },
        { MANUAL_ACQUISITION, ARM, TRACKING },
        { TRACKING, START_MANUAL, MANUAL_ACQUISITION },
        { AWAITING_ARM, ARM, TRACKING },
        { TRACKING, LANDING_DETECTED, LANDED },
    };
    for (size_t i = 0; i < sizeof transitions / sizeof transitions[0]; i++) {
        if (transitions[i].from == controller->state && transitions[i].event == event) {
            controller->state = transitions[i].to;
            return true;
        }
    }
    return false;
}
