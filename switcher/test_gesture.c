/* Host-side unit tests for the gesture state machine. Build & run natively:
 *   gcc -O2 -Wall gesture.c test_gesture.c -o test_gesture && ./test_gesture
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gesture.h"

#define MAXX 1403
#define MAXY 1871
#define CORNER 450

static int fired;

static void feed(struct gesture *g, int type, int code, int value, int64_t t)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    fired += gesture_feed(g, &ev, t);
}

/* Press a contact at SCREEN coordinates, converting to the raw axes the
 * panel would report (raw Y runs bottom-to-top). */
static void touch_screen(struct gesture *g, int slot, int id,
                         int sx, int sy, int64_t t)
{
    feed(g, EV_ABS, ABS_MT_SLOT, slot, t);
    feed(g, EV_ABS, ABS_MT_TRACKING_ID, id, t);
    feed(g, EV_ABS, ABS_MT_POSITION_X, sx, t);
    feed(g, EV_ABS, ABS_MT_POSITION_Y, MAXY - sy, t);
    feed(g, EV_SYN, SYN_REPORT, 0, t);
}

static void lift(struct gesture *g, int slot, int64_t t)
{
    feed(g, EV_ABS, ABS_MT_SLOT, slot, t);
    feed(g, EV_ABS, ABS_MT_TRACKING_ID, -1, t);
    feed(g, EV_SYN, SYN_REPORT, 0, t);
}

static void setup(struct gesture *g)
{
    struct gesture_zone tl = { 0, 0, CORNER, CORNER };
    struct gesture_zone br = { MAXX - CORNER, MAXY - CORNER, MAXX, MAXY };
    gesture_init(g, tl, br, 1200, 3000);
    gesture_set_transform(g, MAXX, MAXY, 0, 1);
    fired = 0;
}

int main(void)
{
    struct gesture g;

    /* 1. both corners held long enough fires exactly once */
    setup(&g);
    touch_screen(&g, 0, 100, 120, 120, 1000);            /* top-left */
    touch_screen(&g, 1, 101, MAXX - 120, MAXY - 120, 1000); /* bottom-right */
    assert(fired == 0);
    fired += gesture_tick(&g, 2100);                     /* 1100ms - short */
    assert(fired == 0);
    fired += gesture_tick(&g, 2250);                     /* 1250ms - fire */
    assert(fired == 1);
    fired += gesture_tick(&g, 3000);                     /* still held */
    assert(fired == 1);
    printf("ok 1 - both corners fire once after the hold\n");

    /* 2. one corner alone never fires, however long */
    setup(&g);
    touch_screen(&g, 0, 200, 100, 100, 1000);
    fired += gesture_tick(&g, 60000);
    assert(fired == 0);
    printf("ok 2 - a single corner never fires\n");

    /* 3. THE REGRESSION: a hand resting while writing must not fire.
     *    Five contacts clustered in the lower-right writing area, one of
     *    which is inside the bottom-right zone - but nothing in top-left. */
    setup(&g);
    touch_screen(&g, 0, 300, 900, 1500, 1000);
    touch_screen(&g, 1, 301, 980, 1560, 1000);
    touch_screen(&g, 2, 302, 1050, 1620, 1000);
    touch_screen(&g, 3, 303, 1120, 1700, 1000);
    touch_screen(&g, 4, 304, MAXX - 60, MAXY - 60, 1000);  /* in the corner */
    fired += gesture_tick(&g, 60000);
    assert(fired == 0);
    printf("ok 3 - resting palm (5 contacts, one in a corner) does not fire\n");

    /* 4. wrong diagonal (top-right + bottom-left) does not fire */
    setup(&g);
    touch_screen(&g, 0, 400, MAXX - 100, 100, 1000);
    touch_screen(&g, 1, 401, 100, MAXY - 100, 1000);
    fired += gesture_tick(&g, 60000);
    assert(fired == 0);
    printf("ok 4 - the other diagonal does not fire\n");

    /* 5. lifting one corner mid-hold resets the timer */
    setup(&g);
    touch_screen(&g, 0, 500, 100, 100, 1000);
    touch_screen(&g, 1, 501, MAXX - 100, MAXY - 100, 1000);
    lift(&g, 1, 1800);                                   /* after 800ms */
    fired += gesture_tick(&g, 5000);
    assert(fired == 0);
    touch_screen(&g, 1, 502, MAXX - 100, MAXY - 100, 5100);
    fired += gesture_tick(&g, 6200);                     /* 1100ms - short */
    assert(fired == 0);
    fired += gesture_tick(&g, 6400);                     /* 1300ms - fire */
    assert(fired == 1);
    printf("ok 5 - releasing one corner resets the hold timer\n");

    /* 6. cooldown: an immediate repeat is ignored, a later one fires */
    setup(&g);
    touch_screen(&g, 0, 600, 100, 100, 1000);
    touch_screen(&g, 1, 601, MAXX - 100, MAXY - 100, 1000);
    fired += gesture_tick(&g, 2300);
    assert(fired == 1);
    lift(&g, 0, 2400); lift(&g, 1, 2400);
    touch_screen(&g, 0, 602, 100, 100, 2500);
    touch_screen(&g, 1, 603, MAXX - 100, MAXY - 100, 2500);
    fired += gesture_tick(&g, 3800);                     /* held, but cooling */
    assert(fired == 1);
    lift(&g, 0, 3900); lift(&g, 1, 3900);
    touch_screen(&g, 0, 604, 100, 100, 6000);            /* cooldown over */
    touch_screen(&g, 1, 605, MAXX - 100, MAXY - 100, 6000);
    fired += gesture_tick(&g, 7300);
    assert(fired == 2);
    printf("ok 6 - cooldown enforced, then re-arms\n");

    /* 7. just outside the zones does not fire; just inside does */
    setup(&g);
    touch_screen(&g, 0, 700, CORNER + 20, CORNER + 20, 1000);
    touch_screen(&g, 1, 701, MAXX - 100, MAXY - 100, 1000);
    fired += gesture_tick(&g, 3000);
    assert(fired == 0);
    touch_screen(&g, 0, 702, CORNER - 20, CORNER - 20, 3100);
    fired += gesture_tick(&g, 4500);
    assert(fired == 1);
    printf("ok 7 - zone boundaries respected\n");

    /* 8. SYN_DROPPED resets safely */
    setup(&g);
    touch_screen(&g, 0, 800, 100, 100, 1000);
    touch_screen(&g, 1, 801, MAXX - 100, MAXY - 100, 1000);
    feed(&g, EV_SYN, SYN_DROPPED, 0, 1100);
    fired += gesture_tick(&g, 9000);
    assert(fired == 0);
    printf("ok 8 - SYN_DROPPED resets state\n");

    printf("all gesture tests passed\n");
    return 0;
}
