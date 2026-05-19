#include "ring_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static log_entry_t s_buffer[RING_LOG_CAPACITY];
static int s_head = 0;     // next write position
static int s_count = 0;    // total stored
static SemaphoreHandle_t s_mutex = NULL;

void ring_log_init(void) {
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);
    memset(s_buffer, 0, sizeof(s_buffer));
    s_head = 0;
    s_count = 0;
}

void ring_log_write(const char *fmt, ...) {
    if (!s_mutex) return;

    log_entry_t entry;
    entry.timestamp_us = esp_timer_get_time();

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, RING_LOG_ENTRY_LEN, fmt, args);
    va_end(args);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_buffer[s_head] = entry;
    s_head = (s_head + 1) % RING_LOG_CAPACITY;
    if (s_count < RING_LOG_CAPACITY) s_count++;
    xSemaphoreGive(s_mutex);
}

int ring_log_read(log_entry_t *out, int max_entries) {
    if (!s_mutex || !out) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int to_read = (max_entries < s_count) ? max_entries : s_count;
    int start = (s_head - s_count + RING_LOG_CAPACITY) % RING_LOG_CAPACITY;

    // Skip to only return the last `to_read` entries
    int skip = s_count - to_read;
    start = (start + skip) % RING_LOG_CAPACITY;

    for (int i = 0; i < to_read; i++) {
        out[i] = s_buffer[(start + i) % RING_LOG_CAPACITY];
    }
    xSemaphoreGive(s_mutex);
    return to_read;
}

int ring_log_count(void) {
    if (!s_mutex) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int c = s_count;
    xSemaphoreGive(s_mutex);
    return c;
}
