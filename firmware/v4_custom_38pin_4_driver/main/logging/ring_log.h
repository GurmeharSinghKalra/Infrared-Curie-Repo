#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RING_LOG_ENTRY_LEN 80
#define RING_LOG_CAPACITY  64

typedef struct {
    int64_t timestamp_us;
    char    message[RING_LOG_ENTRY_LEN];
} log_entry_t;

void ring_log_init(void);

// Add a log entry (thread-safe, non-blocking)
void ring_log_write(const char *fmt, ...);

// Get recent entries. Returns count written to `out`. Oldest first.
int ring_log_read(log_entry_t *out, int max_entries);

// Get total entries stored (capped at RING_LOG_CAPACITY)
int ring_log_count(void);

#ifdef __cplusplus
}
#endif
