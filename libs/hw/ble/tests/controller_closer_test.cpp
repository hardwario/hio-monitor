// ControllerCloser (HIL finding H2): a closed controller is deleted only once it is unconnected,
// at the latest after the bound; a new connect to the device waits for that. A QTimer stands in
// for the controller (its timeout() is the "now unconnected" notification).

#include "controller_closer.h"
#include "mock_ble_api.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace {

using hio::ble::ControllerCloser;
using hio::ble::test::runFor;
using hio::ble::test::runUntil;
using namespace std::chrono_literals;

// A stand-in controller owned by `parent` (Qt parent ownership, as QtBleApi's controllers).
QTimer* makeController(QObject& parent) {
    auto controller = std::make_unique<QTimer>();
    controller->setSingleShot(true);
    controller->setParent(&parent);
    return controller.release(); // NOLINT(cppcoreguidelines-owning-memory): owned by `parent`
}

ControllerCloser::Watch watchOf(QTimer* controller) {
    return [controller](QObject* context, std::function<void()> onGone) {
        return QObject::connect(controller, &QTimer::timeout, context,
                                [onGone = std::move(onGone)] { onGone(); });
    };
}

TEST(ControllerCloser, NotDeletedBeforeItIsUnconnected) {
    QObject owner;
    ControllerCloser closer(5s);
    QPointer<QTimer> controller = makeController(owner);
    closer.close(controller, "F1:22:33:44:55:66", false, watchOf(controller));
    EXPECT_TRUE(closer.isClosing("f1:22:33:44:55:66"));
    runFor(100ms);
    EXPECT_FALSE(controller.isNull()) << "deleted while still closing";
    controller->start(0); // "disconnected"
    ASSERT_TRUE(runUntil([&controller] { return controller.isNull(); }));
    EXPECT_FALSE(closer.isClosing("F1:22:33:44:55:66"));
}

TEST(ControllerCloser, BoundedWaitExpires) {
    QObject owner;
    ControllerCloser closer(50ms);
    QPointer<QTimer> controller = makeController(owner);
    closer.close(controller, "F1:22:33:44:55:66", false, watchOf(controller));
    ASSERT_TRUE(runUntil([&controller] { return controller.isNull(); }, 5s)); // never signalled
    EXPECT_EQ(closer.closingCount(), 0U);
}

TEST(ControllerCloser, UnconnectedIsDeletedAtOnce) {
    QObject owner;
    ControllerCloser closer(5s);
    QPointer<QTimer> controller = makeController(owner);
    closer.close(controller, "F1:22:33:44:55:66", true, watchOf(controller));
    EXPECT_FALSE(closer.isClosing("F1:22:33:44:55:66"));
    // Called outside an event loop here: deleteLater() is processed explicitly.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(controller.isNull());
}

TEST(ControllerCloser, NewConnectWaitsForTheOldController) {
    QObject owner;
    ControllerCloser closer(5s);
    QPointer<QTimer> controller = makeController(owner);
    closer.close(controller, "F1:22:33:44:55:66", false, watchOf(controller));
    bool started = false;
    bool other = false;
    closer.whenClosed("F1:22:33:44:55:66", [&started] { started = true; });
    closer.whenClosed("C0:11:22:33:44:55", [&other] { other = true; }); // another device: at once
    EXPECT_TRUE(other);
    runFor(50ms);
    EXPECT_FALSE(started);
    controller->start(0);
    ASSERT_TRUE(runUntil([&started] { return started; }));
}

TEST(ControllerCloser, WaitAllEndsWhenEverythingIsGoneOrAtTheBound) {
    QObject owner;
    ControllerCloser closer(100ms);
    QPointer<QTimer> quick = makeController(owner);
    QPointer<QTimer> stuck = makeController(owner);
    closer.close(quick, "A", false, watchOf(quick));
    closer.close(stuck, "B", false, watchOf(stuck));
    quick->start(10);
    const auto started = std::chrono::steady_clock::now();
    closer.waitAll();
    EXPECT_EQ(closer.closingCount(), 0U);
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
}

} // namespace
