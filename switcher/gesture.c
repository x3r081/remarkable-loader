#include "gesture.h"

#include <string.h>

void gesture_init(struct gesture *g, struct gesture_zone a,
                  struct gesture_zone b, int64_t hold_ms, int64_t cooldown_ms)
{
    memset(g, 0, sizeof *g);
    g->zone_a = a;
    g->zone_b = b;
    g->hold_ms = hold_ms;
    g->cooldown_ms = cooldown_ms;
    /* Sensible defaults for the rM2 panel; override with set_transform. */
    g->max_x = 1403;
    g->max_y = 1871;
    g->flip_x = 0;
    g->flip_y = 1;
    for (int i = 0; i < GESTURE_MAX_SLOTS; i++)
        g->tracking[i] = -1;
}

void gesture_set_transform(struct gesture *g, int max_x, int max_y,
                           int flip_x, int flip_y)
{
    if (max_x > 0)
        g->max_x = max_x;
    if (max_y > 0)
        g->max_y = max_y;
    g->flip_x = flip_x;
    g->flip_y = flip_y;
}

int gesture_fingers(const struct gesture *g)
{
    int n = 0;
    for (int i = 0; i < GESTURE_MAX_SLOTS; i++)
        if (g->tracking[i] != -1)
            n++;
    return n;
}

static int in_zone(const struct gesture_zone *z, int x, int y)
{
    return x >= z->x0 && x <= z->x1 && y >= z->y0 && y <= z->y1;
}

/* Raw axes -> screen coordinates (origin top-left). Applied at the point of
 * use so slots always hold unmodified raw values. */
static void to_screen(const struct gesture *g, int slot, int *x, int *y)
{
    *x = g->flip_x ? g->max_x - g->px[slot] : g->px[slot];
    *y = g->flip_y ? g->max_y - g->py[slot] : g->py[slot];
}

static int zone_held(const struct gesture *g, const struct gesture_zone *z)
{
    for (int i = 0; i < GESTURE_MAX_SLOTS; i++) {
        if (g->tracking[i] == -1)
            continue;
        int x, y;
        to_screen(g, i, &x, &y);
        if (in_zone(z, x, y))
            return 1;
    }
    return 0;
}

int gesture_zone_a_held(const struct gesture *g) { return zone_held(g, &g->zone_a); }
int gesture_zone_b_held(const struct gesture *g) { return zone_held(g, &g->zone_b); }

static int evaluate(struct gesture *g, int64_t now_ms)
{
    const int both = zone_held(g, &g->zone_a) && zone_held(g, &g->zone_b);

    if (both) {
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

    if (gesture_fingers(g) == 0 && g->fired) {
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
    case ABS_MT_POSITION_X:
        g->px[g->slot] = ev->value;      /* raw; transformed in to_screen() */
        break;
    case ABS_MT_POSITION_Y:
        g->py[g->slot] = ev->value;
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
