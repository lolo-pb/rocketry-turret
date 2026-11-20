#include "controller.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char *const state_names[] = {
    [DISARMED] = "DISARMED", [AUTOMATIC_ACQUISITION] = "AUTOMATIC_ACQUISITION",
    [MANUAL_ACQUISITION] = "MANUAL_ACQUISITION", [AWAITING_ARM] = "AWAITING_ARM",
    [TRACKING] = "TRACKING", [LANDED] = "LANDED"
};

int dashboard_open(struct Dashboard *dashboard, const char *path)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof address.sun_path) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(address.sun_path, path);
    dashboard->client = -1;
    dashboard->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (dashboard->listener < 0 ||
        bind(dashboard->listener, (struct sockaddr *)&address, sizeof address) < 0)
        return -1;
    if (listen(dashboard->listener, 4) < 0) {
        unlink(path);
        return -1;
    }
    return 0;
}

static void disconnect(struct Dashboard *dashboard)
{
    close(dashboard->client);
    dashboard->client = -1;
    dashboard->used = 0;
}

static void send_line(struct Dashboard *dashboard, const char *line)
{
    size_t size = strlen(line);
    if (send(dashboard->client, line, size, MSG_DONTWAIT | MSG_NOSIGNAL) != (ssize_t)size)
        disconnect(dashboard);
}

void dashboard_publish(struct Dashboard *dashboard, const struct Controller *controller)
{
    if (dashboard->client < 0)
        return;
    char *json = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&json, &size);
    if (!out) {
        disconnect(dashboard);
        return;
    }
    fprintf(out, "{\"type\":\"snapshot\",\"state\":\"%s\",\"receivers\":[", state_names[controller->state]);
    for (size_t i = 0; i < controller->recieverCount; i++) {
        const struct Receiver *receiver = &controller->receivers[i];
        fprintf(out, "%s{\"id\":%u,\"connected\":%s,\"telemetry\":", i ? "," : "", receiver->id,
                receiver->fd >= 0 ? "true" : "false");
        if (receiver->latest.fix >= 0) {
            const struct Telemetry *s = &receiver->latest;
            fprintf(out, "{\"latitude\":%.10g,\"longitude\":%.10g,\"altitude_m\":%.10g,\"fix\":%d}",
                    s->latitude, s->longitude, s->altitude_m, s->fix);
        } else {
            fputs("null", out);
        }
        fputc('}', out);
    }
    fprintf(out, "],\"rocket\":{\"latitude\":%.10g,\"longitude\":%.10g,\"altitude_m\":%.10g,\"fix\":%d},"
            "\"ground_station\":{\"latitude\":%.10g,\"longitude\":%.10g,\"altitude_m\":%.10g},"
            "\"turret\":{\"azimuth_deg\":%d,\"elevation_deg\":%d},"
            "\"landing_estimate\":null}\n",
            rocket_state.telemetry.latitude, rocket_state.telemetry.longitude,
            rocket_state.telemetry.altitude_m, rocket_state.telemetry.fix,
            gs_state.latitude, gs_state.longitude, gs_state.altitude_m,
            controller->turret.azimuth_deg, controller->turret.elevation_deg);
    if (fclose(out) == 0)
        send_line(dashboard, json);
    else
        disconnect(dashboard);
    free(json);
}

void dashboard_accept(struct Dashboard *dashboard, const struct Controller *controller)
{
    int fd = accept(dashboard->listener, NULL, NULL);
    if (fd < 0)
        return;
    if (dashboard->client >= 0) {
        close(fd);
        return;
    }
    dashboard->client = fd;
    dashboard_publish(dashboard, controller);
}

static void command(struct Dashboard *dashboard, struct Controller *controller)
{
    char *line = dashboard->line;
    while (dashboard->used && isspace((unsigned char)line[dashboard->used - 1]))
        dashboard->used--;
    line[dashboard->used] = '\0';
    while (isspace((unsigned char)*line))
        line++;

    Event event = NO_EVENT;
    if (!strcmp(line, "\"START_AUTOMATIC\""))
        event = START_AUTOMATIC;
    else if (!strcmp(line, "\"START_MANUAL\""))
        event = START_MANUAL;
    else if (!strcmp(line, "\"START_TRACKING\""))
        event = START_TRACKING;
    else if (!strcmp(line, "\"ARM\""))
        event = ARM;

    bool accepted = transition(controller, event);
    char reply[128];
    snprintf(reply, sizeof reply, "{\"type\":\"reply\",\"accepted\":%s,\"state\":\"%s\"}\n",
             accepted ? "true" : "false", state_names[controller->state]);
    send_line(dashboard, reply);
    if (accepted)
        dashboard_publish(dashboard, controller);
}

void dashboard_read(struct Dashboard *dashboard, struct Controller *controller)
{
    char bytes[512];
    ssize_t count = recv(dashboard->client, bytes, sizeof bytes, MSG_DONTWAIT);
    if (count <= 0) {
        if (!count || (errno != EAGAIN && errno != EINTR))
            disconnect(dashboard);
        return;
    }
    for (ssize_t i = 0; i < count && dashboard->client >= 0; i++) {
        if (bytes[i] == '\n') {
            command(dashboard, controller);
            dashboard->used = 0;
        } else if (!bytes[i] || dashboard->used == sizeof dashboard->line - 1) {
            disconnect(dashboard);
        } else {
            dashboard->line[dashboard->used++] = bytes[i];
        }
    }
}
