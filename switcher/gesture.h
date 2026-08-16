#pragma once
/* Pure gesture-detection state machine: a touch held in EACH of two opposite
 * corner zones at the same time, for HOLD_MS, fires once. It then requires all
 * contacts to lift plus a cooldown before it can fire again.
 *
 * Why two diagonal corners rather than "N fingers": a hand resting on the
 * panel while writing easily produces four or more contacts, which triggered
 * the old finger-count gesture by accident. A resting palm covers one
 * contiguous area and cannot span two opposite corners, so this cannot be
 * produced by writing posture alone.
 *
 * No I/O here, so it can be unit-tested on the host.
 */
#include <linux/input.h>
#include <stdint.h>

#define GESTURE_MAX_SLOTS 32

struct gesture_zone {
    int x0, y0, x1, y1;         /* inclusive, screen coordinates */
};

struct gesture {
    /* configuration */
    struct gesture_zone zone_a; /* e.g. top-left */
    struct gesture_zone zone_b; /* e.g. bottom-right */
    int64_t hold_ms;
    int64_t cooldown_ms;

    /* raw -> screen mapping (see gesture_set_transform) */
    int max_x, max_y;
    int flip_x, flip_y;

    /* state */
    int slot;
    int tracking[GESTURE_MAX_SLOTS];   /* -1 = empty, else contact id */
    int px[GESTURE_MAX_SLOTS];         /* RAW coords per slot */
    int py[GESTURE_MAX_SLOTS];
    int64_t held_since_ms;             /* 0 = both zones not currently held */
    int fired;
    int64_t rearm_at_ms;
};

void gesture_init(struct gesture *g, struct gesture_zone a,
                  struct gesture_zone b, int64_t hold_ms, int64_t cooldown_ms);

/*! Raw axis maxima and axis inversion. On the rM2 touch panel raw Y runs
 *  bottom-to-top, so flip_y = 1 maps it to a top-left screen origin. */
void gesture_set_transform(struct gesture *g, int max_x, int max_y,
                           int flip_x, int flip_y);

/*! Feed one evdev event. now_ms is CLOCK_MONOTONIC milliseconds.
 *  Returns 1 exactly once per completed gesture, else 0. */
int gesture_feed(struct gesture *g, const struct input_event *ev, int64_t now_ms);

/*! Call periodically (poll timeout) so a hold can complete without new
 *  events arriving. Same return convention as gesture_feed. */
int gesture_tick(struct gesture *g, int64_t now_ms);

/*! Active contact count (for logging). */
int gesture_fingers(const struct gesture *g);

/*! Which zones are currently occupied (for logging/diagnostics). */
int gesture_zone_a_held(const struct gesture *g);
int gesture_zone_b_held(const struct gesture *g);
