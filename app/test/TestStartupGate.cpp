/**
 * @file TestStartupGate.cpp
 * @brief 启动门控顺序、早期帧、重复通知及终态回归测试。
 */

#include "QStartupGate.h"
#include "QStartupProgress.h"

#include <QCoreApplication>
#include <QEvent>
#include <QString>
#include <QStringList>
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

TEST_CASE("Startup gate waits for a new frame after either prerequisite order")
{
    (void)ensureApplication();
    GateProbe probe;

    SECTION("viewport becomes ready before startup files")
    {
        probe.gate.markViewportReady();
        probe.gate.markFrameReady();
        CHECK(probe.ready_count == 0);
        probe.gate.markStartupFilesReady();
    }
    SECTION("startup files become ready before viewport")
    {
        probe.gate.markStartupFilesReady();
        probe.gate.markFrameReady();
        CHECK(probe.ready_count == 0);
        probe.gate.markViewportReady();
    }

    CHECK(probe.update_count == 2);
    CHECK(probe.ready_count == 0);
    CHECK(probe.failed_count == 0);
    probe.gate.markFrameReady();
    CHECK(probe.ready_count == 1);
}

TEST_CASE("Startup gate discards frames observed before both prerequisites")
{
    (void)ensureApplication();
    GateProbe probe;
    probe.gate.markFrameReady();
    probe.gate.markFrameReady();
    CHECK(probe.update_count == 0);
    CHECK(probe.ready_count == 0);

    probe.gate.markViewportReady();
    probe.gate.markFrameReady();
    probe.gate.markFrameReady();
    CHECK(probe.ready_count == 0);
    probe.gate.markStartupFilesReady();
    // 旧帧不能在最后一个条件到达时直接触发切换。
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
    probe.gate.markStartupFilesReady();
    probe.gate.markStartupFilesReady();
    CHECK(probe.update_count == 4);
    CHECK(probe.ready_count == 0);

    probe.gate.markFrameReady();
    probe.gate.markFrameReady();
    probe.gate.markViewportReady();
    probe.gate.markStartupFilesReady();
    CHECK(probe.ready_count == 1);
    CHECK(probe.failed_count == 0);
    CHECK(probe.update_count == 4);
}

TEST_CASE("Startup gate failure is terminal regardless of prerequisite progress")
{
    (void)ensureApplication();
    GateProbe probe;
    SECTION("failure before any readiness") { }
    SECTION("failure after viewport readiness")
    {
        probe.gate.markViewportReady();
    }
    SECTION("failure after both prerequisites but before their new frame")
    {
        probe.gate.markStartupFilesReady();
        probe.gate.markViewportReady();
    }
    const int update_count = probe.update_count;
    probe.gate.fail(QStringLiteral("视口初始化失败"));
    probe.gate.fail(QStringLiteral("后续错误不能覆盖首个失败"));
    probe.gate.markViewportReady();
    probe.gate.markStartupFilesReady();
    probe.gate.markFrameReady();
    CHECK(probe.failed_count == 1);
    CHECK(probe.failure == QStringLiteral("视口初始化失败"));
    CHECK(probe.ready_count == 0);
    CHECK(probe.update_count == update_count);
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
        probe.gate.markStartupFilesReady();
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
            probe.gate.markStartupFilesReady();
            probe.gate.markFrameReady();
        });
        probe.gate.fail(QStringLiteral("首次失败"));
        CHECK(probe.failed_count == 1);
        CHECK(probe.failure == QStringLiteral("首次失败"));
        CHECK(probe.ready_count == 0);
        CHECK(probe.update_count == 0);
    }
}

TEST_CASE("Startup progress preserves stage text and queues worker notifications to GUI")
{
    auto& app = ensureApplication();
    auto& progress = QStartupProgress::instance();
    REQUIRE(progress.thread() == app.thread());
    QObject receiver;
    QStringList stages;
    QThread* stage_thread = nullptr;
    QThread* viewport_thread = nullptr;
    int viewport_count = 0;
    QObject::connect(&progress, &QStartupProgress::onStageChange, &receiver, [&](const QString& stage) {
        stages.append(stage);
        stage_thread = QThread::currentThread(); }, Qt::QueuedConnection);
    QObject::connect(&progress, &QStartupProgress::onViewportReady, &receiver, [&] {
        ++viewport_count;
        viewport_thread = QThread::currentThread(); }, Qt::QueuedConnection);

    const QString first_stage = QStringLiteral("正在加载插件：网格修复 <A&B>.dll");
    const QString second_stage = QStringLiteral("正在初始化三维视口…");
    std::thread worker([&] {
        progress.reportStage(first_stage);
        progress.reportStage(second_stage);
        progress.reportViewportReady();
    });
    worker.join();
    // 工作线程发通知不应直接进入 GUI 接收者，也不应把文案当成 HTML 改写。
    CHECK(stages.isEmpty());
    CHECK(viewport_count == 0);
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    REQUIRE(stages.size() == 2);
    CHECK(stages[0] == first_stage);
    CHECK(stages[1] == second_stage);
    CHECK(viewport_count == 1);
    CHECK(stage_thread == app.thread());
    CHECK(viewport_thread == app.thread());
}
