/* modeswitchd - watch the reMarkable 2 touch panel for a two-corner hold and
 * run a toggle script when it happens.
 *
 * Touch the TOP-LEFT and BOTTOM-RIGHT corners at the same time and hold. A
 * hand resting on the panel while writing cannot span two opposite corners,
 * which is why this replaced the original "4 fingers" gesture: a resting palm
 * easily produces four or more contacts and triggered it by accident.
 *
 * Runs alongside xochitl or any Qt e-paper app: evdev delivers events to every
 * reader and (verified on-device) nobody holds an exclusive grab. Reading is
 * passive - this daemon never grabs, never injects, never writes anywhere
 * except its stdout (the journal).
 *
 *   modeswitchd [--device /dev/input/eventN] [--hold-ms 1200]
 *               [--corner-size 450] [--exec /home/root/apps/mode-toggle.sh]
 *               [--no-flip-y]
 *
 * --corner-size is the side of each square zone in screen pixels (the panel
 * is 1404x1872). --no-flip-y is an escape hatch if a future panel reports Y
 * with a top-left origin instead of bottom-left.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gesture.h"

#define BITS_PER_LONG (8 * sizeof(long))
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static const char *opt_device = NULL;
static const char *opt_exec = "/home/root/apps/mode-toggle.sh";
static long opt_hold_ms = 1200;
static long opt_cooldown_ms = 3000;
static int opt_corner = 450;      /* ~50 mm square: easy to hit unseen */
static int opt_flip_y = 1;        /* rM2 raw Y runs bottom-to-top */

static int test_bit(const unsigned long *bits, int bit)
{
    return (bits[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Find the multitouch panel: ABS_MT_SLOT present, BTN_TOOL_PEN absent. */
static int open_touch_device(char *path_out, size_t path_len)
{
    char path[64], name[128];
    unsigned long absbits[NLONGS(ABS_MAX + 1)];
    unsigned long keybits[NLONGS(KEY_MAX + 1)];

    if (opt_device) {
        int fd = open(opt_device, O_RDONLY);
        if (fd >= 0)
            snprintf(path_out, path_len, "%s", opt_device);
        return fd;
    }

    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        memset(absbits, 0, sizeof absbits);
        memset(keybits, 0, sizeof keybits);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits);

        if (test_bit(absbits, ABS_MT_SLOT) && !test_bit(keybits, BTN_TOOL_PEN)) {
            memset(name, 0, sizeof name);
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
            fprintf(stderr, "modeswitchd: using %s ('%s')\n", path, name);
            snprintf(path_out, path_len, "%s", path);
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void run_toggle(void)
{
    fprintf(stderr, "modeswitchd: gesture detected, running %s\n", opt_exec);
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", opt_exec, (char *)NULL);
        _exit(127);
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--device"))       opt_device = argv[++i];
        else if (!strcmp(argv[i], "--exec"))    opt_exec = argv[++i];
        else if (!strcmp(argv[i], "--hold-ms")) opt_hold_ms = atol(argv[++i]);
        else if (!strcmp(argv[i], "--corner-size")) opt_corner = atoi(argv[++i]);
    }
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--no-flip-y"))
            opt_flip_y = 0;
    setvbuf(stderr, NULL, _IOLBF, 0);
    signal(SIGCHLD, SIG_IGN);   /* no zombies from toggle scripts */

    struct gesture g;
    char devpath[64] = "";

    for (;;) {
        int fd = open_touch_device(devpath, sizeof devpath);
        if (fd < 0) {
            fprintf(stderr, "modeswitchd: no touch device found, retrying\n");
            sleep(5);
            continue;
        }

        /* Read the panel's real extents so the zones land in the right
         * physical corners regardless of panel size. */
        struct input_absinfo ax, ay;
        int max_x = 1403, max_y = 1871;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) == 0 && ax.maximum > 0)
            max_x = ax.maximum;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) == 0 && ay.maximum > 0)
            max_y = ay.maximum;

        int c = opt_corner;
        if (c > max_x / 2) c = max_x / 2;    /* never let the zones overlap */
        if (c > max_y / 2) c = max_y / 2;

        struct gesture_zone top_left = { 0, 0, c, c };
        struct gesture_zone bottom_right = { max_x - c, max_y - c, max_x, max_y };

        gesture_init(&g, top_left, bottom_right, opt_hold_ms, opt_cooldown_ms);
        gesture_set_transform(&g, max_x, max_y, 0, opt_flip_y);

        fprintf(stderr,
                "modeswitchd: armed - hold top-left (%d,%d)-(%d,%d) and "
                "bottom-right (%d,%d)-(%d,%d) together for %ld ms "
                "[panel %dx%d, flip_y=%d]\n",
                top_left.x0, top_left.y0, top_left.x1, top_left.y1,
                bottom_right.x0, bottom_right.y0, bottom_right.x1, bottom_right.y1,
                opt_hold_ms, max_x + 1, max_y + 1, opt_flip_y);

        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        for (;;) {
            /* Short timeout while a hold is being timed, long otherwise. */
            int timeout = g.held_since_ms ? 100 : 60000;
            int rc = poll(&pfd, 1, timeout);

            if (rc < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (rc == 0) {
                if (gesture_tick(&g, now_ms()))
                    run_toggle();
                continue;
            }

            struct input_event evbuf[64];
            ssize_t n = read(fd, evbuf, sizeof evbuf);
            if (n <= 0) {
                fprintf(stderr, "modeswitchd: read failed (%s), reopening\n",
                        strerror(errno));
                break;
            }
            int64_t t = now_ms();
            const int was_a = gesture_zone_a_held(&g);
            const int was_b = gesture_zone_b_held(&g);
            for (size_t i = 0; i < n / sizeof evbuf[0]; i++)
                if (gesture_feed(&g, &evbuf[i], t))
                    run_toggle();
            /* Log corner entry/exit: makes it obvious during setup whether
             * the zones are where the user thinks they are. */
            const int now_a = gesture_zone_a_held(&g);
            const int now_b = gesture_zone_b_held(&g);
            if (now_a != was_a || now_b != was_b)
                fprintf(stderr, "modeswitchd: corners top-left=%d bottom-right=%d "
                        "(%d contacts)\n", now_a, now_b, gesture_fingers(&g));
        }

        close(fd);
        sleep(2);   /* device went away (e.g. resume glitch) - rescan */
    }
}
