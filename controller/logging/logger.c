#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_CAPACITY 256
#define RECORD_CAPACITY 2048

struct LogRecord {
    struct timespec received;
    char source[32];
    char data[RECORD_CAPACITY];
    size_t size;
    int raw;
};

static struct LogRecord queue[QUEUE_CAPACITY];
static size_t head, count;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static atomic_ulong dropped;
static int stopping;
static FILE *raw_file, *parsed_file;

static void *write_logs(void *unused)
{
    (void)unused;
    for (;;) {
        struct LogRecord record;
        pthread_mutex_lock(&mutex);
        while (!count && !stopping) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec++;
            if (pthread_cond_timedwait(&ready, &mutex, &deadline) == ETIMEDOUT) {
                pthread_mutex_unlock(&mutex);
                if (fflush(raw_file) || fflush(parsed_file))
                    perror("flush telemetry log");
                pthread_mutex_lock(&mutex);
            }
        }
        if (!count && stopping) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        record = queue[head];
        head = (head + 1) % QUEUE_CAPACITY;
        count--;
        pthread_mutex_unlock(&mutex);

        FILE *file = record.raw ? raw_file : parsed_file;
        int failed = fprintf(file, "%lld.%09ld,%s,", (long long)record.received.tv_sec,
                             record.received.tv_nsec, record.source) < 0;
        if (record.raw) {
            for (size_t i = 0; i < record.size && !failed; i++)
                failed = fprintf(file, "%02x", (unsigned char)record.data[i]) < 0;
        } else {
            failed = fputc('"', file) == EOF;
            for (size_t i = 0; i < record.size && !failed; i++) {
                if (record.data[i] == '"')
                    failed = fputc('"', file) == EOF;
                if (!failed)
                    failed = fputc(record.data[i], file) == EOF;
            }
            if (!failed)
                failed = fputc('"', file) == EOF;
        }
        if (failed || fputc('\n', file) == EOF) {
            perror("write telemetry log");
            clearerr(file);
        }
    }
    if (fflush(raw_file) || fflush(parsed_file))
        perror("flush telemetry log");
    return NULL;
}

int logger_start(const char *directory)
{
    char raw_path[512], parsed_path[512];
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0)
        return -1;
    snprintf(raw_path, sizeof raw_path, "%s/raw-%lld-%09ld-%ld.csv", directory,
             (long long)now.tv_sec, now.tv_nsec, (long)getpid());
    snprintf(parsed_path, sizeof parsed_path, "%s/parsed-%lld-%09ld-%ld.csv", directory,
             (long long)now.tv_sec, now.tv_nsec, (long)getpid());
    raw_file = fopen(raw_path, "wx");
    if (!raw_file)
        return -1;
    parsed_file = fopen(parsed_path, "wx");
    if (!parsed_file) {
        int error = errno;
        fclose(raw_file);
        errno = error;
        return -1;
    }
    fputs("received_unix_s,source,bytes_hex\n", raw_file);
    fputs("received_unix_s,source,fields\n", parsed_file);
    int error = pthread_create(&thread, NULL, write_logs, NULL);
    if (error) {
        fclose(raw_file);
        fclose(parsed_file);
        errno = error;
        return -1;
    }
    return 0;
}

static void enqueue(const char *source, const void *data, size_t size, int raw)
{
    if (size > RECORD_CAPACITY || pthread_mutex_trylock(&mutex)) {
        atomic_fetch_add(&dropped, 1);
        return;
    }
    if (count == QUEUE_CAPACITY) {
        atomic_fetch_add(&dropped, 1);
        pthread_mutex_unlock(&mutex);
        return;
    }
    struct LogRecord *record = &queue[(head + count) % QUEUE_CAPACITY];
    clock_gettime(CLOCK_REALTIME, &record->received);
    snprintf(record->source, sizeof record->source, "%s", source);
    memcpy(record->data, data, size);
    record->size = size;
    record->raw = raw;
    count++;
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&mutex);
}

void logger_raw(const char *source, const void *data, size_t size)
{
    enqueue(source, data, size, 1);
}

void logger_parsed(const char *source, const char *data)
{
    enqueue(source, data, strlen(data), 0);
}

void logger_stop(void)
{
    pthread_mutex_lock(&mutex);
    stopping = 1;
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&mutex);
    pthread_join(thread, NULL);
    if (fclose(raw_file))
        perror("close raw telemetry log");
    if (fclose(parsed_file))
        perror("close parsed telemetry log");
    fprintf(stderr, "Telemetry log dropped %lu records\n", atomic_load(&dropped));
}
