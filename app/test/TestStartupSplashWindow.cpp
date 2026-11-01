/**
 * @file TestStartupSplashWindow.cpp
 * @brief 原生启动窗口的首帧、同步阶段刷新及关闭生命周期测试。
 */

#include "QStartupSplashWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QStringList>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

#include <functional>

namespace {

//! @brief 创建进程内唯一 GUI 应用；关闭最后一个测试窗口不退出测试进程。
QGuiApplication& ensureApplication()
{
    if (auto* existing = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        existing->setQuitOnLastWindowClosed(false);
        return *existing;
    }
    static int argc = 1;
    static char app_name[] = "TestStartupSplashWindow";
    static char* argv[] = { app_name, nullptr };
    static QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    return app;
}

//! @brief 无显示平台不承担窗口曝光契约；构造与门控测试仍可在无头 CI 执行。
void requireGraphicalPlatform()
{
    (void)ensureApplication();
    const QString platform = QGuiApplication::platformName();
    if (platform == QStringLiteral("minimal") || platform == QStringLiteral("offscreen")
        || platform == QStringLiteral("minimalegl")) {
        SKIP("Requires a graphical platform for native window exposure; current platform: " << platform.toStdString());
    }
}

//! @brief 等待实际窗口事件，避免依赖固定启动延时。
bool waitUntil(const std::function<bool()>& condition, int timeout_ms = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

//! @brief 仅测试使用：导出当前原生窗口中的阶段示例，供人工视觉检查。
void captureStages(QStartupSplashWindow& splash)
{
    const QString directory = qEnvironmentVariable("ZENITHGRID_SPLASH_CAPTURE_DIR");
    if (directory.isEmpty())
        return;
    QDir output(directory);
    REQUIRE(output.mkpath(QStringLiteral(".")));
    REQUIRE(splash.screen() != nullptr);
    const QStringList stages {
        QStringLiteral("正在初始化应用环境…"),
        QStringLiteral("正在加载插件：MeshRepairPlugin.dll"),
        QStringLiteral("正在初始化三维视口…"),
        QStringLiteral("正在打开文件：工程网格.vtk"),
    };
    for (qsizetype index = 0; index < stages.size(); ++index) {
        splash.setStage(stages[index]);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const QPixmap capture = splash.screen()->grabWindow(splash.winId());
        REQUIRE_FALSE(capture.isNull());
        const QString filename = QStringLiteral("startup-stage-%1.png").arg(index + 1, 2, 10, QLatin1Char('0'));
        REQUIRE(capture.save(output.filePath(filename)));
    }
}

} // namespace

TEST_CASE("Startup splash reports one first frame across redraw and visibility changes")
{
    requireGraphicalPlatform();
    QString background_path = QStringLiteral(":/images/splash/ZenithGrid-splash-background@2x.png");
    SECTION("bundled background") { }
    SECTION("missing background still presents a frame")
    {
        background_path = QStringLiteral(":/missing-startup-background.png");
    }
    QStartupSplashWindow splash(QStringLiteral("0.5.0"), background_path);
    int first_frame_count = 0;
    QObject::connect(&splash, &QStartupSplashWindow::onFirstFrame, &splash, [&first_frame_count] {
        ++first_frame_count;
    });
    CHECK(first_frame_count == 0);
    splash.show();
    REQUIRE(waitUntil([&first_frame_count] { return first_frame_count > 0; }));
    REQUIRE(splash.isExposed());
    splash.setStage(QStringLiteral("正在加载静态插件…"));
    splash.requestUpdate();
    captureStages(splash);
    splash.hide();
    splash.show();
    REQUIRE(waitUntil([&splash] { return splash.isExposed(); }));
    splash.setStage(QStringLiteral("正在初始化三维视口…"));
    CHECK(first_frame_count == 1);
    splash.hide();
}

TEST_CASE("Startup splash stage refresh does not dispatch queued application callbacks")
{
    requireGraphicalPlatform();
    QStartupSplashWindow splash(QStringLiteral("0.5.0"));
    bool first_frame = false;
    QObject::connect(&splash, &QStartupSplashWindow::onFirstFrame, &splash, [&first_frame] { first_frame = true; });
    splash.show();
    REQUIRE(waitUntil([&first_frame] { return first_frame; }));

    QObject callback_context;
    bool callback_ran = false;
    QTimer::singleShot(0, &callback_context, [&callback_ran] { callback_ran = true; });
    // 构造/插件注册可能在调用栈中；刷新阶段不得顺便执行它们排队的任意工作。
    splash.setStage(QStringLiteral("正在加载插件：MeshRepairPlugin.dll"));
    CHECK_FALSE(callback_ran);
    REQUIRE(waitUntil([&callback_ran] { return callback_ran; }));
    splash.hide();
}

TEST_CASE("Hidden startup splash does not produce a delayed first frame")
{
    requireGraphicalPlatform();
    QStartupSplashWindow splash(QStringLiteral("0.5.0"));
    int first_frame_count = 0;
    QObject::connect(&splash, &QStartupSplashWindow::onFirstFrame, &splash, [&first_frame_count] { ++first_frame_count; });
    splash.show();
    splash.hide();
    const int count_at_hide = first_frame_count;
    splash.setStage(QStringLiteral("窗口已隐藏"));
    QElapsedTimer elapsed;
    elapsed.start();
    // 覆盖多个活动定时器周期，验证隐藏窗口不会迟到绘制或重新发出首帧。
    REQUIRE(waitUntil([&elapsed] { return elapsed.elapsed() >= 140; }, 1000));
    CHECK_FALSE(splash.isVisible());
    CHECK(first_frame_count == count_at_hide);
}

TEST_CASE("Closing startup splash publishes startup cancellation")
{
    requireGraphicalPlatform();
    QStartupSplashWindow splash(QStringLiteral("0.5.0"));
    bool first_frame = false;
    int cancelled_count = 0;
    QObject::connect(&splash, &QStartupSplashWindow::onFirstFrame, &splash, [&first_frame] { first_frame = true; });
    QObject::connect(&splash, &QStartupSplashWindow::onCancelled, &splash, [&cancelled_count] { ++cancelled_count; });
    splash.show();
    REQUIRE(waitUntil([&first_frame] { return first_frame; }));
    REQUIRE(splash.close());
    CHECK(cancelled_count == 1);
    CHECK_FALSE(splash.isVisible());
    CHECK_FALSE(QGuiApplication::quitOnLastWindowClosed());
}

TEST_CASE("Startup splash tolerates missing and corrupt background images")
{
    (void)ensureApplication();
    QTemporaryFile corrupt_background;
    REQUIRE(corrupt_background.open());
    REQUIRE(corrupt_background.write("invalid image data") > 0);
    corrupt_background.close();
    QString background_path;
    SECTION("missing resource")
    {
        background_path = QStringLiteral(":/missing-startup-background.png");
    }
    SECTION("invalid image contents")
    {
        background_path = corrupt_background.fileName();
    }
    QStartupSplashWindow splash(QStringLiteral("0.5.0"), background_path);
    CHECK_FALSE(splash.isVisible());
    CHECK_NOTHROW(splash.setStage(QStringLiteral("正在加载插件…")));
    CHECK(splash.objectName() == QStringLiteral("startupSplash"));
}
