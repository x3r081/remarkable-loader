/* Test injector: creates a VIRTUAL multitouch device via /dev/uinput that
 * mimics the rM2 panel (pt_mt: 32 slots, 1404x1872), presses N fingers, holds,
 * and releases. Used to test modeswitchd end-to-end without touching the
 * physical panel. The virtual device is destroyed on exit.
 *
 *   uinject [fingers] [hold_ms] [pre_delay_ms]     N contacts, centre-ish
 *   uinject corners [hold_ms] [pre_delay_ms]      top-left + bottom-right
 *   uinject palm [hold_ms] [pre_delay_ms]         5 clustered contacts, one
 *                                                 inside the bottom-right
 *                                                 corner (must NOT trigger) [pre_delay_ms]
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

#define PANEL_W 1404
#define PANEL_H 1872

/* Screen (top-left origin) -> raw axes: the panel reports Y bottom-to-top. */
static void press_screen(int slot, int sx, int sy)
{
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 2000 + slot);
    emit(EV_ABS, ABS_MT_POSITION_X, sx);
    emit(EV_ABS, ABS_MT_POSITION_Y, (PANEL_H - 1) - sy);
    emit(EV_SYN, SYN_REPORT, 0);
}

static void release(int slot)
{
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    emit(EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    int corners_mode = !strcmp(mode, "corners");
    int palm_mode = !strcmp(mode, "palm");
    int fingers = (corners_mode || palm_mode) ? 0 : (argc > 1 ? atoi(argv[1]) : 4);
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

    if (corners_mode) {
        fprintf(stderr, "uinject: holding top-left + bottom-right for %d ms\n",
                hold_ms);
        press_screen(0, 120, 120);
        press_screen(1, PANEL_W - 120, PANEL_H - 120);
        usleep(hold_ms * 1000);
        release(0);
        release(1);
        fprintf(stderr, "uinject: released\n");
        sleep(1);
        ioctl(ufd, UI_DEV_DESTROY);
        close(ufd);
        return 0;
    }

    if (palm_mode) {
        /* Writing posture: a cluster low-right, one contact inside the
         * bottom-right zone, nothing in top-left. Must NOT trigger. */
        fprintf(stderr, "uinject: simulating a resting palm for %d ms "
                        "(expected: NO trigger)\n", hold_ms);
        press_screen(0, 900, 1500);
        press_screen(1, 980, 1560);
        press_screen(2, 1050, 1620);
        press_screen(3, 1120, 1700);
        press_screen(4, PANEL_W - 60, PANEL_H - 60);
        usleep(hold_ms * 1000);
        for (int i = 0; i < 5; i++)
            release(i);
        fprintf(stderr, "uinject: released\n");
        sleep(1);
        ioctl(ufd, UI_DEV_DESTROY);
        close(ufd);
        return 0;
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
