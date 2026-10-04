#include "events.h"

#include <string.h>

void event_queue_init(EventQueue *q) {
    memset(q, 0, sizeof(*q));
    q->epoch = 1;
}

void event_queue_commit_config(EventQueue *q) {
    q->epoch++;
    /* 在途事件一律失效；不主动清空，drain 时逐代际校验丢弃，便于统计/调试。 */
}

void event_queue_push_size(EventQueue *q, int w, int h, int display_index) {
    if (q->has_pending) q->coalesced++;
    q->has_pending = true;
    q->pending.width = w;
    q->pending.height = h;
    q->pending.display_index = display_index;
    q->pending.epoch = q->epoch;
    q->pending.seq = ++q->seq_gen;
}

bool event_queue_drain(EventQueue *q, SizeEvent *out) {
    while (q->has_pending) {
        SizeEvent e = q->pending;
        q->has_pending = false;
        if (e.epoch != q->epoch) {
            q->dropped_stale++;
            continue;
        }
        if (out) *out = e;
        return true;
    }
    return false;
}

uint64_t event_queue_current_epoch(const EventQueue *q) {
    return q->epoch;
}
