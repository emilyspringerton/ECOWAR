/* packages/livingmap/living_map_events.c -- see living_map_events.h for the real design
 * rationale. Plain ring buffer: total_emitted tracks how many events have ever been written so
 * "log is full and wrapping" is distinguishable from "log has fewer than CAPACITY events so far"
 * without a separate has-wrapped flag. */
#include "living_map_events.h"

#include <string.h>

void living_map_event_log_reset(LivingMapEventLog *log) {
    memset(log, 0, sizeof(*log));
}

void living_map_emit(LivingMapEventLog *log, LivingMapEventKind kind, int subject_id, int a, int b) {
    int slot = log->total_emitted % LIVING_MAP_EVENT_LOG_CAPACITY;
    log->events[slot].kind = kind;
    log->events[slot].subject_id = subject_id;
    log->events[slot].a = a;
    log->events[slot].b = b;
    log->total_emitted++;
}

int living_map_event_log_size(const LivingMapEventLog *log) {
    return log->total_emitted < LIVING_MAP_EVENT_LOG_CAPACITY
        ? log->total_emitted
        : LIVING_MAP_EVENT_LOG_CAPACITY;
}

const LivingMapEvent *living_map_event_log_at(const LivingMapEventLog *log, int index) {
    int size = living_map_event_log_size(log);
    if (index < 0 || index >= size) return NULL;

    if (log->total_emitted <= LIVING_MAP_EVENT_LOG_CAPACITY) {
        return &log->events[index];
    }

    /* Wrapped: the oldest retained event is at slot (total_emitted % CAPACITY), i.e. the next
     * slot that will be overwritten. */
    int oldest_slot = log->total_emitted % LIVING_MAP_EVENT_LOG_CAPACITY;
    int slot = (oldest_slot + index) % LIVING_MAP_EVENT_LOG_CAPACITY;
    return &log->events[slot];
}
