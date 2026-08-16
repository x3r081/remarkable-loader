/* Test injector: creates a VIRTUAL Wacom-like pen device via /dev/uinput
 * (same axes/ranges as the rM2 digitizer) and writes a big zigzag "scribble"
 * so the chat app's ink pipeline can be tested end-to-end without a hand.
 *
 *   peninject [pre_delay_ms]              zigzag scribble (ink test)
 *   peninject tap RAWX RAWY [pre_delay_ms]  single pen tap (button test)
 *
 * The scribble runs down the middle of the digitizer's coordinate space, so
 * after the rM2 transform it lands mid-screen - inside the chat app's
 * handwriting area.
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
    int tap_mode = argc > 1 && strcmp(argv[1], "tap") == 0;
    int tap_x = tap_mode && argc > 2 ? atoi(argv[2]) : 0;
    int tap_y = tap_mode && argc > 3 ? atoi(argv[3]) : 0;
    int pre_delay_ms = tap_mode ? (argc > 4 ? atoi(argv[4]) : 0)
                                : (argc > 1 ? atoi(argv[1]) : 0);

    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_ABS);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(ufd, UI_SET_KEYBIT, BTN_TOOL_PEN);
    ioctl(ufd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(ufd, UI_SET_ABSBIT, ABS_X);
    ioctl(ufd, UI_SET_ABSBIT, ABS_Y);
    ioctl(ufd, UI_SET_ABSBIT, ABS_PRESSURE);

    struct uinput_user_dev ud;
    memset(&ud, 0, sizeof ud);
    snprintf(ud.name, sizeof ud.name, "pen-test-digitizer");
    ud.id.bustype = BUS_VIRTUAL;
    ud.absmin[ABS_X] = 0;        ud.absmax[ABS_X] = 20966;
    ud.absmin[ABS_Y] = 0;        ud.absmax[ABS_Y] = 15725;
    ud.absmin[ABS_PRESSURE] = 0; ud.absmax[ABS_PRESSURE] = 4095;

    if (write(ufd, &ud, sizeof ud) != sizeof ud) {
        perror("write dev");
        return 1;
    }
    if (ioctl(ufd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_CREATE");
        return 1;
    }
    sleep(1);
    if (pre_delay_ms > 0) {
        fprintf(stderr, "peninject: device up, waiting %d ms\n", pre_delay_ms);
        usleep(pre_delay_ms * 1000);
    }

    if (tap_mode) {
        fprintf(stderr, "peninject: tapping raw (%d, %d)\n", tap_x, tap_y);
        emit(EV_KEY, BTN_TOOL_PEN, 1);
        emit(EV_ABS, ABS_X, tap_x);
        emit(EV_ABS, ABS_Y, tap_y);
        emit(EV_SYN, SYN_REPORT, 0);
        usleep(30000);
        emit(EV_ABS, ABS_PRESSURE, 2000);
        emit(EV_KEY, BTN_TOUCH, 1);
        emit(EV_SYN, SYN_REPORT, 0);
        usleep(120000);
        emit(EV_KEY, BTN_TOUCH, 0);
        emit(EV_ABS, ABS_PRESSURE, 0);
        emit(EV_SYN, SYN_REPORT, 0);
        usleep(50000);
        emit(EV_KEY, BTN_TOOL_PEN, 0);
        emit(EV_SYN, SYN_REPORT, 0);
        fprintf(stderr, "peninject: tap done\n");
        sleep(1);
        ioctl(ufd, UI_DEV_DESTROY);
        close(ufd);
        return 0;
    }

    /* Three zigzag strokes near the bottom of the screen (low ABS_X = bottom
     * after the rM2 transform), i.e. inside the chat app's writing area. */
    fprintf(stderr, "peninject: scribbling\n");
    emit(EV_KEY, BTN_TOOL_PEN, 1);
    emit(EV_SYN, SYN_REPORT, 0);

    for (int stroke = 0; stroke < 3; stroke++) {
        /* rM2 mapping: screenY = 1872 - rawX*1872/20966, screenX = rawY*1404/15725.
         * Writing area is roughly screenY 1160-1620 -> rawX ~ 2800-8000. */
        int base_x = 4200 - stroke * 600;          /* stroke rows, bottom-ish */
        int start_y = 2500 + stroke * 800;

        emit(EV_ABS, ABS_X, base_x);
        emit(EV_ABS, ABS_Y, start_y);
        emit(EV_ABS, ABS_PRESSURE, 2000);
        emit(EV_KEY, BTN_TOUCH, 1);
        emit(EV_SYN, SYN_REPORT, 0);
        usleep(20000);

        for (int i = 1; i <= 40; i++) {
            emit(EV_ABS, ABS_X, base_x + ((i % 4 < 2) ? 350 : -350));
            emit(EV_ABS, ABS_Y, start_y + i * 220);
            emit(EV_ABS, ABS_PRESSURE, 2000);
            emit(EV_SYN, SYN_REPORT, 0);
            usleep(12000);
        }

        emit(EV_KEY, BTN_TOUCH, 0);
        emit(EV_ABS, ABS_PRESSURE, 0);
        emit(EV_SYN, SYN_REPORT, 0);
        usleep(150000);
    }

    emit(EV_KEY, BTN_TOOL_PEN, 0);
    emit(EV_SYN, SYN_REPORT, 0);
    fprintf(stderr, "peninject: done\n");
    sleep(1);

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    return 0;
}
