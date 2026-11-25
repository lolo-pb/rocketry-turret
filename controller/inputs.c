#include "inputs.h"
#include "dashboard/dashboard.h"

int poll_inputs(struct Inputs *inputs, const struct Controller *controller,
                const struct Dashboard *dashboard)
{
    inputs->pool[0] = (struct pollfd){.fd = dashboard->listener, .events = POLLIN};
    inputs->pool[1] = (struct pollfd){.fd = dashboard->client, .events = POLLIN};
    for (size_t i = 0; i < controller->recieverCount; i++)
        inputs->pool[FIRST_RECEIVER_SLOT + i] =
            (struct pollfd){.fd = controller->receivers[i].fd, .events = POLLIN};
    return poll(inputs->pool, FIRST_RECEIVER_SLOT + controller->recieverCount, 10);
}
