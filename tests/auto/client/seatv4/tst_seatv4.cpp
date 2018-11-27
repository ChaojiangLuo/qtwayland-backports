/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "mockcompositor.h"
#include <QtGui/QRasterWindow>
#include <QtGui/QOpenGLWindow>

using namespace MockCompositor;

// wl_seat version 5 was introduced in wayland 1.10, and although that's pretty old,
// there are still compositors that have yet to update their implementation to support
// the new version (most importantly our own QtWaylandCompositor).
// As long as that's the case, this test makes sure input events still works on version 4.
class SeatV4Compositor : public DefaultCompositor {
public:
    explicit SeatV4Compositor()
    {
        exec([this] {
            m_config.autoConfigure = true;

            removeAll<Seat>();

            uint capabilities = MockCompositor::Seat::capability_pointer;
            int version = 4;
            add<Seat>(capabilities, version);
        });
    }

    Pointer *pointer()
    {
        auto *seat = get<Seat>();
        Q_ASSERT(seat);
        return seat->m_pointer;
    }
};

class tst_seatv4 : public QObject, private SeatV4Compositor
{
    Q_OBJECT
private slots:
    void cleanup() { QTRY_VERIFY2(isClean(), qPrintable(dirtyMessage())); }
    void bindsToSeat();
    void createsPointer();
    void setsCursorOnEnter();
    void usesEnterSerial();
    void simpleAxis_data();
    void simpleAxis();
};

void tst_seatv4::bindsToSeat()
{
    QCOMPOSITOR_COMPARE(get<Seat>()->resourceMap().size(), 1);
    QCOMPOSITOR_COMPARE(get<Seat>()->resourceMap().first()->version(), 4);
}

void tst_seatv4::createsPointer()
{
    QCOMPOSITOR_TRY_COMPARE(pointer()->resourceMap().size(), 1);
    QCOMPOSITOR_TRY_COMPARE(pointer()->resourceMap().first()->version(), 4);
}

void tst_seatv4::setsCursorOnEnter()
{
    QRasterWindow window;
    window.resize(64, 64);
    window.show();
    QCOMPOSITOR_TRY_VERIFY(xdgSurface() && xdgSurface()->m_committedConfigureSerial);

    exec([=] { pointer()->sendEnter(xdgSurface()->m_surface, {0, 0}); });
    QCOMPOSITOR_TRY_VERIFY(pointer()->cursorSurface());
}

void tst_seatv4::usesEnterSerial()
{
    QSignalSpy setCursorSpy(exec([=] { return pointer(); }), &Pointer::setCursor);
    QRasterWindow window;
    window.resize(64, 64);
    window.show();
    QCOMPOSITOR_TRY_VERIFY(xdgSurface() && xdgSurface()->m_committedConfigureSerial);

    uint enterSerial = exec([=] {
        return pointer()->sendEnter(xdgSurface()->m_surface, {0, 0});
    });
    QCOMPOSITOR_TRY_VERIFY(pointer()->cursorSurface());

    QTRY_COMPARE(setCursorSpy.count(), 1);
    QCOMPARE(setCursorSpy.takeFirst().at(0).toUInt(), enterSerial);
}

void tst_seatv4::simpleAxis_data()
{
    QTest::addColumn<uint>("axis");
    QTest::addColumn<qreal>("value");
    QTest::addColumn<Qt::Orientation>("orientation");
    QTest::addColumn<QPoint>("angleDelta");

    // Directions in regular windows/linux terms (no "natural" scrolling)
    QTest::newRow("down") << uint(Pointer::axis_vertical_scroll) << 1.0 << Qt::Vertical << QPoint{0, -12};
    QTest::newRow("up") << uint(Pointer::axis_vertical_scroll) << -1.0 << Qt::Vertical << QPoint{0, 12};
    QTest::newRow("left") << uint(Pointer::axis_horizontal_scroll) << 1.0 << Qt::Horizontal << QPoint{-12, 0};
    QTest::newRow("right") << uint(Pointer::axis_horizontal_scroll) << -1.0 << Qt::Horizontal << QPoint{12, 0};
    QTest::newRow("up big") << uint(Pointer::axis_vertical_scroll) << -10.0 << Qt::Vertical << QPoint{0, 120};
}

void tst_seatv4::simpleAxis()
{
    QFETCH(uint, axis);
    QFETCH(qreal, value);
    QFETCH(Qt::Orientation, orientation);
    QFETCH(QPoint, angleDelta);

    class WheelWindow : QRasterWindow {
    public:
        WheelWindow()
        {
            resize(64, 64);
            show();
        }
        void wheelEvent(QWheelEvent *event) override
        {
            QRasterWindow::wheelEvent(event);
            // Angle delta should always be provided (says docs)
            QVERIFY(!event->angleDelta().isNull());

            // There are now scroll phases on Wayland prior to v5
            QCOMPARE(event->phase(), Qt::NoScrollPhase);

            // Pixel delta should only be set if we know it's a high-res input device (which we don't)
            QCOMPARE(event->pixelDelta(), QPoint(0, 0));

            // The axis vector of the event is already in surface space, so there is now way to tell
            // whether it is inverted or not.
            QCOMPARE(event->inverted(), false);

            // We didn't press any buttons
            QCOMPARE(event->buttons(), Qt::NoButton);

            if (event->orientation() == Qt::Horizontal)
                QCOMPARE(event->delta(), event->angleDelta().x());
            else
                QCOMPARE(event->delta(), event->angleDelta().y());

            // There has been no information about what created the event.
            // Documentation says not synthesized is appropriate in such cases
            QCOMPARE(event->source(), Qt::MouseEventNotSynthesized);

            m_events.append(Event{event->pixelDelta(), event->angleDelta(), event->orientation()});
        }
        struct Event // Because I didn't find a convenient way to copy it entirely
        {
            const QPoint pixelDelta;
            const QPoint angleDelta; // eights of a degree, positive is upwards, left
            const Qt::Orientation orientation{};
        };
        QVector<Event> m_events;
    };

    WheelWindow window;
    QCOMPOSITOR_TRY_VERIFY(xdgSurface() && xdgSurface()->m_committedConfigureSerial);

    exec([=] {
        Surface *surface = xdgSurface()->m_surface;
        pointer()->sendEnter(surface, {0, 0});
        wl_client *client = surface->resource()->client();
        // Length of vector in surface-local space. i.e. positive is downwards
        pointer()->sendAxis(
            client,
            Pointer::axis(axis),
            value // Length of vector in surface-local space. i.e. positive is downwards
        );
    });

    QTRY_COMPARE(window.m_events.size(), 1);
    auto event = window.m_events.takeFirst();
    QCOMPARE(event.angleDelta, angleDelta);
    QCOMPARE(event.orientation, orientation);
}

QCOMPOSITOR_TEST_MAIN(tst_seatv4)
#include "tst_seatv4.moc"
