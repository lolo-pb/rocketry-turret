#include "controller.h"
#include <assert.h>

int main(void)
{
    Event setups[] = {START_MANUAL, START_AUTOMATIC};
    for (size_t i = 0; i < 2; i++) {
        struct Controller c = {.state = DISARMED};
        assert(!transition(&c, ARM));
        assert(transition(&c, setups[i]));
        assert(!transition(&c, ARM));
        assert(transition(&c, TARGET_ACQUIRED) && c.state == AWAITING_ARM);
        assert(transition(&c, ARM) && c.state == TRACKING);
        assert(transition(&c, LANDING_DETECTED) && c.state == LANDED);
    }
}
