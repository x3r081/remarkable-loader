/* Test injector: creates a VIRTUAL multitouch device via /dev/uinput that
 * mimics the rM2 panel (pt_mt: 32 slots, 1404x1872), presses N fingers, holds,
 * and releases. Used to test modeswitchd end-to-end without touching the
 * physical panel. The virtual device is destroyed on exit.
 *
 *   uinject [fingers] [hold_ms] [pre_delay_ms]
 *
 * pre_delay_ms: keep the virtual device idle this long before pressing, so a
 * Qt app can be started in the meantime and discover the device at startup.
 *
 * NOTE: whatever owns the display may also see this virtual device's touches
 * (harmless taps); point modeswitchd at the virtual node with --device.
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int ufd;

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(ufd, &ev, sizeof ev) != sizeof ev)
        perror("write");
}

int main(int argc, char **argv)
{
    int fingers = argc > 1 ? atoi(argv[1]) : 4;
    int hold_ms = argc > 2 ? atoi(argv[2]) : 1500;
    int pre_delay_ms = argc > 3 ? atoi(argv[3]) : 0;

    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    ioctl(ufd, UI_SET_EVBIT, EV_ABS);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(ufd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);

    struct uinput_user_dev ud;
    memset(&ud, 0, sizeof ud);
    snprintf(ud.name, sizeof ud.name, "modeswitch-test-mt");
    ud.id.bustype = BUS_VIRTUAL;
    ud.absmin[ABS_MT_SLOT] = 0;         ud.absmax[ABS_MT_SLOT] = 31;
    ud.absmin[ABS_MT_TRACKING_ID] = 0;  ud.absmax[ABS_MT_TRACKING_ID] = 65535;
    ud.absmin[ABS_MT_POSITION_X] = 0;   ud.absmax[ABS_MT_POSITION_X] = 1403;
    ud.absmin[ABS_MT_POSITION_Y] = 0;   ud.absmax[ABS_MT_POSITION_Y] = 1871;

    if (write(ufd, &ud, sizeof ud) != sizeof ud) {
        perror("write dev");
        return 1;
    }
    if (ioctl(ufd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_CREATE");
        return 1;
    }

    /* Give udev / readers a moment to open the new node. */
    sleep(1);
    if (pre_delay_ms > 0) {
        fprintf(stderr, "uinject: device up, waiting %d ms before pressing\n",
                pre_delay_ms);
        usleep(pre_delay_ms * 1000);
    }

    fprintf(stderr, "uinject: pressing %d fingers for %d ms\n", fingers, hold_ms);
    for (int f = 0; f < fingers; f++) {
        emit(EV_ABS, ABS_MT_SLOT, f);
        emit(EV_ABS, ABS_MT_TRACKING_ID, 1000 + f);
        emit(EV_ABS, ABS_MT_POSITION_X, 300 + f * 120);
        emit(EV_ABS, ABS_MT_POSITION_Y, 900);
        emit(EV_SYN, SYN_REPORT, 0);
    }
    usleep(hold_ms * 1000);
    for (int f = 0; f < fingers; f++) {
        emit(EV_ABS, ABS_MT_SLOT, f);
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(EV_SYN, SYN_REPORT, 0);
    }
    fprintf(stderr, "uinject: released\n");
    sleep(1);

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    return 0;
}
