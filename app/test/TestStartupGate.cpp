/**
 * @file TestStartupGate.cpp
 * @brief 视口后续帧、早期帧、重复通知及终态回归测试。
 */

#include "QStartupGate.h"

#include <QCoreApplication>
#include <QEvent>
#include <QString>
#include <QThread>

#include <catch2/catch_test_macros.hpp>

#include <thread>

namespace {

//! @brief 与现有 QtCore 测试保持相同的进程级应用生命周期。
QCoreApplication& ensureApplication()
{
    if (auto* existing = QCoreApplication::instance())
        return *existing;
    static int argc = 1;
    static char app_name[] = "TestStartupGate";
    static char* argv[] = { app_name, nullptr };
    static QCoreApplication app(argc, argv);
    return app;
}

//! @brief 以真实 Qt 信号观察门控输出，不访问实现私有状态。
struct GateProbe {
    QStartupGate gate;
    int ready_count {};
    int failed_count {};
    int update_count {};
    QString failure;

    GateProbe()
    {
        QObject::connect(&gate, &QStartupGate::onReady, &gate, [this] { ++ready_count; });
        QObject::connect(&gate, &QStartupGate::onFailed, &gate, [this](const QString& message) {
            ++failed_count;
            failure = message;
        });
        QObject::connect(&gate, &QStartupGate::onUpdateRequested, &gate, [this] { ++update_count; });
    }
};

} // namespace

TEST_CASE("Startup gate waits for a new frame after viewport readiness")
{
    (void)ensureApplication();
    GateProbe probe;
    CHECK_FALSE(probe.gate.isPrepared());

    probe.gate.markViewportReady();
    CHECK(probe.gate.isPrepared());
    CHECK(probe.update_count == 1);
    CHECK(probe.ready_count == 0);
    CHECK(probe.failed_count == 0);
    probe.gate.markFrameReady();
    CHECK(probe.ready_count == 1);
    CHECK_FALSE(probe.gate.isPrepared());
}

TEST_CASE("Startup gate discards frames observed before viewport readiness")
{
    (void)ensureApplication();
    GateProbe probe;
    probe.gate.markFrameReady();
    probe.gate.markFrameReady();
    CHECK(probe.update_count == 0);
    CHECK(probe.ready_count == 0);
    CHECK_FALSE(probe.gate.isPrepared());

    probe.gate.markViewportReady();
    // 旧帧不能在视口就绪时直接触发切换。
    CHECK(probe.gate.isPrepared());
    CHECK(probe.ready_count == 0);
    probe.gate.markFrameReady();
    CHECK(probe.ready_count == 1);
}

TEST_CASE("Startup gate tolerates repeated notifications and emits one success")
{
    (void)ensureApplication();
    GateProbe probe;
    probe.gate.markViewportReady();
    probe.gate.markViewportReady();
    CHECK(probe.update_count == 2);
    CHECK(probe.ready_count == 0);

    probe.gate.markFrameReady();
    probe.gate.markFrameReady();
    probe.gate.markViewportReady();
    CHECK(probe.ready_count == 1);
    CHECK(probe.failed_count == 0);
    CHECK(probe.update_count == 2);
    CHECK_FALSE(probe.gate.isPrepared());
}

TEST_CASE("Startup gate failure is terminal before or after viewport readiness")
{
    (void)ensureApplication();
    GateProbe probe;
    SECTION("failure before viewport readiness") { }
    SECTION("failure after viewport readiness but before its new frame")
    {
        probe.gate.markViewportReady();
    }
    const int update_count = probe.update_count;
    probe.gate.fail(QStringLiteral("视口初始化失败"));
    probe.gate.fail(QStringLiteral("后续错误不能覆盖首个失败"));
    probe.gate.markViewportReady();
    probe.gate.markFrameReady();
    CHECK(probe.failed_count == 1);
    CHECK(probe.failure == QStringLiteral("视口初始化失败"));
    CHECK(probe.ready_count == 0);
    CHECK(probe.update_count == update_count);
    CHECK_FALSE(probe.gate.isPrepared());
}

TEST_CASE("Startup gate ignores late failure and synchronous terminal reentry")
{
    (void)ensureApplication();
    GateProbe probe;
    SECTION("success cannot become failure even during its signal")
    {
        QObject::connect(&probe.gate, &QStartupGate::onReady, &probe.gate, [&probe] {
            probe.gate.fail(QStringLiteral("成功通知内的迟到失败"));
            probe.gate.markFrameReady();
        });
        probe.gate.markViewportReady();
        probe.gate.markFrameReady();
        probe.gate.fail(QStringLiteral("成功后的迟到失败"));
        CHECK(probe.ready_count == 1);
        CHECK(probe.failed_count == 0);
    }
    SECTION("failure is emitted once even during its signal")
    {
        QObject::connect(&probe.gate, &QStartupGate::onFailed, &probe.gate, [&probe](const QString&) {
            probe.gate.fail(QStringLiteral("失败通知内的重复失败"));
            probe.gate.markViewportReady();
            probe.gate.markFrameReady();
        });
        probe.gate.fail(QStringLiteral("首次失败"));
        CHECK(probe.failed_count == 1);
        CHECK(probe.failure == QStringLiteral("首次失败"));
        CHECK(probe.ready_count == 0);
        CHECK(probe.update_count == 0);
    }
}

TEST_CASE("Startup viewport readiness is queued from worker to GUI")
{
    auto& app = ensureApplication();
    GateProbe probe;
    REQUIRE(probe.gate.thread() == app.thread());
    QThread* viewport_thread = nullptr;
    QObject::connect(&probe.gate, &QStartupGate::onUpdateRequested, &probe.gate, [&] {
        viewport_thread = QThread::currentThread();
    });
    probe.gate.markFrameReady();
    viewport_thread = nullptr;

    bool notification_queued = false;
    std::thread worker([&] {
        notification_queued = QMetaObject::invokeMethod(&probe.gate, &QStartupGate::markViewportReady, Qt::QueuedConnection);
    });
    worker.join();
    // 工作线程只报告就绪，接收方在 GUI 线程消费通知。
    REQUIRE(notification_queued);
    CHECK_FALSE(probe.gate.isPrepared());
    CHECK(viewport_thread == nullptr);
    CHECK(probe.ready_count == 0);
    QCoreApplication::sendPostedEvents(&probe.gate, QEvent::MetaCall);
    CHECK(probe.gate.isPrepared());
    CHECK(probe.update_count == 1);
    CHECK(probe.ready_count == 0);
    CHECK(viewport_thread == app.thread());
    probe.gate.markFrameReady();
    CHECK(probe.ready_count == 1);
}
