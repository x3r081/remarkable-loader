#include "penmouse.h"

#include <QGuiApplication>

#include <qpa/qwindowsysteminterface.h>

void PenMouse::sample(qreal x, qreal y, bool down)
{
    if (!down && !m_down)
        return;                             // hovering - nothing to do

    const QPointF pos(x, y);
    QEvent::Type type;
    Qt::MouseButton button;
    Qt::MouseButtons state;

    if (down && !m_down) {
        type = QEvent::MouseButtonPress;
        button = Qt::LeftButton;
        state = Qt::LeftButton;
    } else if (!down && m_down) {
        type = QEvent::MouseButtonRelease;
        button = Qt::LeftButton;
        state = Qt::NoButton;
    } else {
        type = QEvent::MouseMove;
        button = Qt::NoButton;
        state = Qt::LeftButton;
    }
    m_down = down;

    // nullptr window = deliver to whatever window is under the point (we are
    // always fullscreen, so: ours).
    QWindowSystemInterface::handleMouseEvent(nullptr, pos, pos, state, button,
                                             type, Qt::NoModifier,
                                             Qt::MouseEventSynthesizedByApplication);
}
