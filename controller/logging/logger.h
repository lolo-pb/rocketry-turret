#ifndef LOGGER2_H
#define LOGGER2_H

#include <stddef.h>

int logger_start(const char *directory);
void logger_raw(const char *source, const void *data, size_t size);
void logger_parsed(const char *source, const char *data);
void logger_stop(void);
int initialize_logging(void);

#endif
