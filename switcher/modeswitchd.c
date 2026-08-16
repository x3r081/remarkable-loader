/* modeswitchd - watch the reMarkable 2 touch panel for a 4-finger hold and
 * run a toggle script when it happens.
 *
 * Runs alongside xochitl or any Qt e-paper app: evdev delivers events to every
 * reader and (verified on-device) nobody holds an exclusive grab. Reading is
 * passive - this daemon never grabs, never injects, never writes anywhere
 * except its stdout (the journal).
 *
 *   modeswitchd [--device /dev/input/eventN] [--fingers 4] [--hold-ms 1200]
 *               [--exec /home/root/apps/mode-toggle.sh]
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
static int opt_fingers = 4;
static long opt_hold_ms = 1200;
static long opt_cooldown_ms = 3000;

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
        else if (!strcmp(argv[i], "--fingers")) opt_fingers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hold-ms")) opt_hold_ms = atol(argv[++i]);
    }
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

        gesture_init(&g, opt_fingers, opt_hold_ms, opt_cooldown_ms);
        fprintf(stderr, "modeswitchd: armed (%d fingers, %ld ms hold)\n",
                opt_fingers, opt_hold_ms);

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
            for (size_t i = 0; i < n / sizeof evbuf[0]; i++)
                if (gesture_feed(&g, &evbuf[i], t))
                    run_toggle();
        }

        close(fd);
        sleep(2);   /* device went away (e.g. resume glitch) - rescan */
    }
}
