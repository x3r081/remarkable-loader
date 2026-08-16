#include "penreader.h"

#include <QLoggingCategory>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcPen, "rmchat.pen")

namespace {
constexpr int kScreenW = 1404;
constexpr int kScreenH = 1872;

#define BITS_PER_LONG (8 * sizeof(long))
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

int testBit(const unsigned long *bits, int bit)
{
    return (bits[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1;
}

/* Find the first device with BTN_TOOL_PEN (the Wacom digitizer). */
int openPenDevice(QString *pathOut)
{
    char path[64];
    unsigned long keybits[NLONGS(KEY_MAX + 1)];

    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        memset(keybits, 0, sizeof keybits);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits);
        if (testBit(keybits, BTN_TOOL_PEN)) {
            *pathOut = QString::fromLatin1(path);
            return fd;
        }
        close(fd);
    }
    return -1;
}
} // namespace

PenReader::PenReader(QString devicePath, bool flipX, bool flipY, QObject *parent)
    : QObject(parent), m_devicePath(std::move(devicePath)),
      m_flipX(flipX), m_flipY(flipY)
{
    // Test hook used by the virtual-pen end-to-end tests: point any app's
    // pen reader at an explicit device without per-app plumbing.
    if (m_devicePath.isEmpty())
        m_devicePath = qEnvironmentVariable("RM_PEN_DEVICE");
}

PenReader::~PenReader()
{
    m_stop = true;
    if (m_thread) {
        m_thread->wait(4000);
        delete m_thread;
    }
}

void PenReader::start()
{
    // Only the loop runs on the worker; this object keeps main-thread
    // affinity so QML Connections can attach to it. Emissions from the
    // worker are delivered queued.
    m_thread = QThread::create([this] { runLoop(); });
    m_thread->start();
}

void PenReader::runLoop()
{
    while (!m_stop) {
        QString path = m_devicePath;
        int fd = -1;
        if (!path.isEmpty())
            fd = open(path.toLocal8Bit().constData(), O_RDONLY);
        else
            fd = openPenDevice(&path);

        if (fd < 0) {
            qCWarning(lcPen) << "cannot open pen device"
                             << (m_devicePath.isEmpty() ? "(autodetect)" : m_devicePath)
                             << strerror(errno) << "- retrying";
            QThread::sleep(3);
            continue;
        }

        struct input_absinfo absX = {}, absY = {};
        ioctl(fd, EVIOCGABS(ABS_X), &absX);
        ioctl(fd, EVIOCGABS(ABS_Y), &absY);
        const int maxX = absX.maximum > 0 ? absX.maximum : 20966;
        const int maxY = absY.maximum > 0 ? absY.maximum : 15725;
        qCInfo(lcPen) << "using" << path << "ranges" << maxX << "x" << maxY
                      << "flipX" << m_flipX << "flipY" << m_flipY;

        int rawX = 0, rawY = 0;
        bool touching = false, dirty = false;

        struct pollfd pfd = { fd, POLLIN, 0 };
        while (!m_stop) {
            int rc = poll(&pfd, 1, 1000);
            if (rc < 0 && errno != EINTR)
                break;
            if (rc <= 0)
                continue;

            struct input_event ev[64];
            ssize_t n = read(fd, ev, sizeof ev);
            if (n <= 0)
                break;

            for (size_t i = 0; i < n / sizeof ev[0]; i++) {
                const auto &e = ev[i];
                if (e.type == EV_ABS) {
                    if (e.code == ABS_X) { rawX = e.value; dirty = true; }
                    else if (e.code == ABS_Y) { rawY = e.value; dirty = true; }
                } else if (e.type == EV_KEY && e.code == BTN_TOUCH) {
                    touching = e.value != 0;
                    dirty = true;
                } else if (e.type == EV_SYN && e.code == SYN_REPORT && dirty) {
                    dirty = false;
                    /* rM2: the digitizer is mounted landscape relative to the
                     * portrait panel. Raw X runs along the screen's vertical
                     * axis (0 = bottom), raw Y along the horizontal.
                     *   screenX = rawY / maxY * W
                     *   screenY = H - rawX / maxX * H
                     * Config flips cover any orientation surprises. */
                    qreal sx = qreal(rawY) * kScreenW / maxY;
                    qreal sy = kScreenH - qreal(rawX) * kScreenH / maxX;
                    if (m_flipX)
                        sx = kScreenW - sx;
                    if (m_flipY)
                        sy = kScreenH - sy;
                    emit sample(sx, sy, touching);
                }
            }
        }

        close(fd);
        if (!m_stop) {
            qCWarning(lcPen) << "pen device lost, rescanning";
            QThread::sleep(2);
        }
    }
}
