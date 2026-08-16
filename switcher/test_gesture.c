/* Host-side unit test for the gesture state machine. Build & run natively:
 *   gcc -O2 -Wall gesture.c test_gesture.c -o test_gesture && ./test_gesture
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gesture.h"

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

static void touch(struct gesture *g, int slot, int id, int64_t t)
{
    feed(g, EV_ABS, ABS_MT_SLOT, slot, t);
    feed(g, EV_ABS, ABS_MT_TRACKING_ID, id, t);
    feed(g, EV_SYN, SYN_REPORT, 0, t);
}

int main(void)
{
    struct gesture g;

    /* 1. four fingers held long enough fires exactly once */
    gesture_init(&g, 4, 1200, 3000);
    fired = 0;
    for (int s = 0; s < 4; s++)
        touch(&g, s, 100 + s, 1000);
    assert(fired == 0);                          /* not yet - needs the hold */
    fired += gesture_tick(&g, 2100);             /* 1100ms - still short */
    assert(fired == 0);
    fired += gesture_tick(&g, 2250);             /* 1250ms - fire */
    assert(fired == 1);
    fired += gesture_tick(&g, 3000);             /* still held - no repeat */
    assert(fired == 1);
    printf("ok 1 - fires once after hold\n");

    /* 2. lifting below threshold before the hold completes resets the timer */
    gesture_init(&g, 4, 1200, 3000);
    fired = 0;
    for (int s = 0; s < 4; s++)
        touch(&g, s, 200 + s, 1000);
    touch(&g, 3, -1, 1800);                      /* one finger up at 800ms */
    fired += gesture_tick(&g, 5000);
    assert(fired == 0);
    touch(&g, 3, 300, 5100);                     /* 4th finger back down */
    fired += gesture_tick(&g, 6200);             /* 1100ms from re-press */
    assert(fired == 0);
    fired += gesture_tick(&g, 6400);             /* 1300ms - fire */
    assert(fired == 1);
    printf("ok 2 - partial lift resets the hold timer\n");

    /* 3. cooldown: an immediate second gesture is ignored, a later one fires */
    gesture_init(&g, 4, 1200, 3000);
    fired = 0;
    for (int s = 0; s < 4; s++)
        touch(&g, s, 400 + s, 1000);
    fired += gesture_tick(&g, 2300);
    assert(fired == 1);
    for (int s = 0; s < 4; s++)
        touch(&g, s, -1, 2400);                  /* all lifted at t=2400 */
    for (int s = 0; s < 4; s++)
        touch(&g, s, 500 + s, 2500);             /* right back down */
    fired += gesture_tick(&g, 3800);             /* held 1300ms but cooling */
    assert(fired == 1);
    for (int s = 0; s < 4; s++)
        touch(&g, s, -1, 3900);
    for (int s = 0; s < 4; s++)
        touch(&g, s, 600 + s, 6000);             /* cooldown (until 5400) over */
    fired += gesture_tick(&g, 7300);
    assert(fired == 2);
    printf("ok 3 - cooldown enforced, then re-arms\n");

    /* 4. three fingers never fire */
    gesture_init(&g, 4, 1200, 3000);
    fired = 0;
    for (int s = 0; s < 3; s++)
        touch(&g, s, 700 + s, 1000);
    fired += gesture_tick(&g, 60000);
    assert(fired == 0);
    printf("ok 4 - three fingers ignored\n");

    /* 5. SYN_DROPPED resets safely */
    gesture_init(&g, 4, 1200, 3000);
    fired = 0;
    for (int s = 0; s < 4; s++)
        touch(&g, s, 800 + s, 1000);
    feed(&g, EV_SYN, SYN_DROPPED, 0, 1100);
    fired += gesture_tick(&g, 9000);
    assert(fired == 0);
    printf("ok 5 - SYN_DROPPED resets state\n");

    printf("all gesture tests passed\n");
    return 0;
}
