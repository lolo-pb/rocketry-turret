#include "controller.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int receiver_open(const char *path)
{
    struct stat info;
    if (stat(path, &info) < 0)
        return -1;
    // Keep a mock FIFO alive between writers.
    int mode = S_ISFIFO(info.st_mode) ? O_RDWR : O_RDONLY;
    return open(path, mode | O_NONBLOCK);
}

bool receiver_read(struct Receiver *receiver)
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

    bool updated = false;
    for (ssize_t i = 0; i < count; i++) {
        if (bytes[i] == '@') {
            if (receiver->used) {
                receiver->record[receiver->used] = '\0';
                if (parse_record(receiver->record, &receiver->latest)) {
                    receiver->has_sample = true;
                    updated = true;
                }
            }
            receiver->used = 0;
        } else if (!receiver->used) {
            continue;
        }
        if (receiver->used == sizeof receiver->record - 1 || !bytes[i]) {
            receiver->used = 0;
            continue;
        }
        receiver->record[receiver->used++] = bytes[i];
    }
    return updated;
}
