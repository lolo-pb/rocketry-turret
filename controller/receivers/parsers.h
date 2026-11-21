#ifndef PARSERS_H
#define PARSERS_H

#include <stddef.h>
#include <stdbool.h>

struct Telemetry;
bool parse_record(const char *record, struct Telemetry *sample, char *fields, size_t size);
bool telemega_parse(const char *line, char *output, size_t size);

#endif
