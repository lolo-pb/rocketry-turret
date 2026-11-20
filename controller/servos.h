#ifndef SERVOS_H
#define SERVOS_H

struct Servo {
    int dir, min_us, max_us, last_angle;
    int started;
};

struct ServoOutput {
    struct Servo azimuth, elevation;
};

int servos_open(struct ServoOutput *output);
void servos_write(struct ServoOutput *output, int azimuth, int elevation);
void servos_close(struct ServoOutput *output);

#endif
