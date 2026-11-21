#include "controller.h"
#include "logger.h"
#include "parsers.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>

int receiver_open(const char *path)
{
    struct stat info;
    if (stat(path, &info) < 0)
        return -1;
    // Keep a mock FIFO alive between writers.
    int mode = S_ISFIFO(info.st_mode) ? O_RDWR : O_RDONLY;
    int fd = open(path, mode | O_NONBLOCK | O_NOCTTY);
    if (fd < 0 || !isatty(fd))
        return fd;

    struct termios tty;
    if (tcgetattr(fd, &tty) == 0) {
        tty.c_iflag = 0;
        tty.c_oflag = 0;
        tty.c_lflag = 0;
        tty.c_cflag = CLOCAL | CREAD | CS8;
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 0;
        if (cfsetispeed(&tty, B115200) == 0 && cfsetospeed(&tty, B115200) == 0 &&
            tcsetattr(fd, TCSANOW, &tty) == 0)
            return fd;
    }
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

static bool receiver_read(struct Receiver *receiver, struct Telemetry *sample)
{
    char bytes[512];
    ssize_t count = read(receiver->fd, bytes, sizeof bytes);
    if (count <= 0) {
        if (count < 0 && (errno == EAGAIN || errno == EINTR))
            return false;
        if (count < 0)
            perror("receiver read");
        close(receiver->fd);
        receiver->fd = -1;
    }

    bool found = false;
    char source[32];
    snprintf(source, sizeof source, "%s-%u",
             receiver->type == TELEDONGLE ? "teledongle" : "featherweight", receiver->id);
    if (count > 0)
        logger_raw(source, bytes, count);
    for (ssize_t i = 0; i < count; i++) {
        bool complete = receiver->type == TELEDONGLE ? bytes[i] == '\n' : bytes[i] == '@';
        if (complete && receiver->used) {
            receiver->record[receiver->used] = '\0';
            char fields[1024];
            if (receiver->type == TELEDONGLE) {
                if (telemega_parse(receiver->record, fields, sizeof fields))
                    logger_parsed(source, fields);
            } else if (parse_record(receiver->record, &receiver->latest, fields, sizeof fields)) {
                logger_parsed(source, fields);
                if (receiver->latest.tracker_fix && sample) {
                    *sample = receiver->latest;
                    found = true;
                }
            }
            receiver->used = 0;
        }
        if (receiver->type == FEATHERWEIGHT) {
            if (bytes[i] == '@') {
                receiver->record[receiver->used++] = '@';
                continue;
            }
            if (!receiver->used)
                continue;
        } else if (complete) {
            continue;
        }
        if (receiver->used == sizeof receiver->record - 1 || !bytes[i]) {
            receiver->used = 0;
            continue;
        }
        receiver->record[receiver->used++] = bytes[i];
    }
    return found;
}

void receivers_read(struct Controller *controller, const struct pollfd *inputs)
{
    bool selected = false;
    for (size_t i = 0; i < controller->recieverCount; i++) {
        if (inputs[i].revents && receiver_read(&controller->receivers[i],
                selected || controller->receivers[i].type == TELEDONGLE ? NULL : &rocket_state.telemetry))
            selected = true;
    }
}
