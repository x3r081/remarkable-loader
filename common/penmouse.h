#pragma once

#include <QObject>
#include <QPointF>

/*!
 * Turns pen samples into synthesized mouse events, so every MouseArea in the
 * app responds to the pen exactly like it does to a finger. Without this the
 * pen is dead in our apps - and worse, holding the pen near the glass makes
 * the touch controller suppress finger touches (hardware palm rejection), so
 * the whole UI looks frozen.
 *
 * Connect PenReader::sample to PenMouse::sample (cross-thread, queued).
 * Hover motion is deliberately not forwarded - only contact does anything.
 */
class PenMouse : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void sample(qreal x, qreal y, bool down);

private:
    bool m_down = false;
};
