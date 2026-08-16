#pragma once

#include <QObject>
#include <QThread>

/*!
 * Reads the Wacom pen digitizer directly from evdev on a worker thread and
 * emits samples in *screen* coordinates (1404x1872 portrait).
 *
 * The e-paper QPA's own pen handling is private to xochitl, so we go straight
 * to /dev/input. evdev fans events out to every reader, and reading is
 * passive - we never grab the device.
 */
class PenReader : public QObject
{
    Q_OBJECT

public:
    explicit PenReader(QString devicePath = QString(),
                       bool flipX = false, bool flipY = false,
                       QObject *parent = nullptr);
    ~PenReader() override;

    void start();

signals:
    /*! x/y in screen pixels; down = tip touching the surface. */
    void sample(qreal x, qreal y, bool down);

private:
    void runLoop();

    /* The reader object itself stays on the main thread (QML's Connections
     * refuses targets living on other threads); only runLoop() executes on
     * this worker. Signal emission from the worker is delivered queued. */
    QThread *m_thread = nullptr;
    QString m_devicePath;      // empty = autodetect (BTN_TOOL_PEN capability)
    bool m_flipX;
    bool m_flipY;
    volatile bool m_stop = false;
};
