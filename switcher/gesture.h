#pragma once
/* Pure gesture-detection state machine: N fingers held for HOLD_MS fires once,
 * then requires all fingers lifted plus a cooldown before it can fire again.
 * No I/O here so it can be unit-tested on the host.
 */
#include <linux/input.h>
#include <stdint.h>

#define GESTURE_MAX_SLOTS 32

struct gesture {
    /* configuration */
    int min_fingers;        /* fingers required, e.g. 4 */
    int64_t hold_ms;        /* how long they must stay down */
    int64_t cooldown_ms;    /* re-arm delay after all fingers lift */

    /* state */
    int slot;                          /* current MT slot selected by driver */
    int tracking[GESTURE_MAX_SLOTS];   /* -1 = empty, else contact id */
    int64_t held_since_ms;             /* 0 = not currently >= min_fingers */
    int fired;                         /* fired for the current touch group */
    int64_t rearm_at_ms;               /* earliest next fire time */
};

void gesture_init(struct gesture *g, int min_fingers, int64_t hold_ms,
                  int64_t cooldown_ms);

/* Feed one evdev event. now_ms is CLOCK_MONOTONIC in milliseconds.
 * Returns 1 exactly once per successful gesture, else 0. */
int gesture_feed(struct gesture *g, const struct input_event *ev, int64_t now_ms);

/* Call periodically (poll timeout) so the hold can complete without a new
 * event arriving. Same return convention as gesture_feed. */
int gesture_tick(struct gesture *g, int64_t now_ms);

/* Active contact count (for logging). */
int gesture_fingers(const struct gesture *g);
