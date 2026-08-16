#include "gesture.h"

#include <string.h>

void gesture_init(struct gesture *g, int min_fingers, int64_t hold_ms,
                  int64_t cooldown_ms)
{
    memset(g, 0, sizeof *g);
    g->min_fingers = min_fingers;
    g->hold_ms = hold_ms;
    g->cooldown_ms = cooldown_ms;
    for (int i = 0; i < GESTURE_MAX_SLOTS; i++)
        g->tracking[i] = -1;
}

int gesture_fingers(const struct gesture *g)
{
    int n = 0;
    for (int i = 0; i < GESTURE_MAX_SLOTS; i++)
        if (g->tracking[i] != -1)
            n++;
    return n;
}

static int evaluate(struct gesture *g, int64_t now_ms)
{
    int fingers = gesture_fingers(g);

    if (fingers >= g->min_fingers) {
        if (g->held_since_ms == 0)
            g->held_since_ms = now_ms;
        if (!g->fired && now_ms >= g->rearm_at_ms &&
            now_ms - g->held_since_ms >= g->hold_ms) {
            g->fired = 1;
            return 1;
        }
    } else {
        g->held_since_ms = 0;
    }

    if (fingers == 0 && g->fired) {
        /* touch group over; re-arm after the cooldown */
        g->fired = 0;
        g->rearm_at_ms = now_ms + g->cooldown_ms;
    }
    return 0;
}

int gesture_feed(struct gesture *g, const struct input_event *ev, int64_t now_ms)
{
    if (ev->type == EV_SYN && ev->code == SYN_DROPPED) {
        /* Kernel buffer overflowed; our slot picture may be stale. Reset to
         * empty - worst case the user repeats the gesture. */
        for (int i = 0; i < GESTURE_MAX_SLOTS; i++)
            g->tracking[i] = -1;
        g->held_since_ms = 0;
        return 0;
    }

    if (ev->type != EV_ABS)
        return evaluate(g, now_ms);

    switch (ev->code) {
    case ABS_MT_SLOT:
        if (ev->value >= 0 && ev->value < GESTURE_MAX_SLOTS)
            g->slot = ev->value;
        break;
    case ABS_MT_TRACKING_ID:
        g->tracking[g->slot] = ev->value;   /* -1 = contact lifted */
        break;
    default:
        break;
    }
    return evaluate(g, now_ms);
}

int gesture_tick(struct gesture *g, int64_t now_ms)
{
    return evaluate(g, now_ms);
}
