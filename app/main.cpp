#include "Docking.h"
#include "QFeatureSystemAdaptor.h"
#include "QLogManager.h"
#include "QModelManager.h"
#include "QStartupGate.h"
#include "QStartupProgress.h"
#include "WallpaperLibrary.h"
#ifndef __EMSCRIPTEN__
#include "QStartupSplashWindow.h"
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#endif
#include <QCoreApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QQuickStyle>
#include <QQuickVTKItem.h>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <exception>
#include <memory>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>
#ifdef __EMSCRIPTEN__
#include "QWasmBridge.h"
#include <QtGui/QFontDatabase>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#endif

#ifdef __EMSCRIPTEN__
// wasm 下将 Qt 相关对象提升为全局变量，避免在 main 函数返回后被销毁
#define WASM_GLOBAL static
#else
#define WASM_GLOBAL
#endif

namespace {

/**
 * @brief 全局按键事件转发器：把按键事件投递到功能系统
 *
 * 事件被功能消费（如按键绑定命中）时立即 accept，不再向焦点控件传递，
 * 避免同一按键被重复消费。
 */
class KeyEventForwarder : public QObject {
public:
    KeyEventForwarder(systems::feature::QFeatureSystemAdaptor& adaptor, QObject* parent = nullptr)
        : QObject(parent)
        , adaptor_(&adaptor)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            const auto* key_event = static_cast<QKeyEvent*>(event);
            if (!key_event->isAutoRepeat()
                && adaptor_->postKeyEvent(key_event->key(), static_cast<int>(key_event->modifiers()),
                    event->type() == QEvent::KeyPress)) {
                return true; // 功能已消费该按键事件，阻止继续传递
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    systems::feature::QFeatureSystemAdaptor* adaptor_;
};
}

int main(int argc, char* argv[])
{
    spdlog::cfg::load_env_levels();
    // WebGL/OpenGLES 不支持 OpenGL 3.2
#ifndef __EMSCRIPTEN__
    QQuickVTKItem::setGraphicsApi();
#endif
    QModelManager::argv0 = argv[0];

    WASM_GLOBAL QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/images/ZenithGrid.ico"));
    // 持久化身份（QSettings 存储位置）：布局快照等依赖明确的应用名/组织名
    QCoreApplication::setOrganizationName(QStringLiteral("PreCess"));
    QCoreApplication::setApplicationName(QStringLiteral("PreCess"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PRECESS_APP_VERSION));
    // 全平台统一 Fusion 运行时样式：QML 一律用运行时样式选择（裸 import
    // QtQuick.Controls），禁止文件内显式 import 特定样式，避免样式混用。
    // 控件调色板由 Main.qml 的 ApplicationWindow 绑定 Theme 令牌；Fusion 的
    // 默认控件调色板取自平台主题，应用调色板只覆盖 ToolTip 等弹窗。
    QQuickStyle::setStyle("Fusion");
#ifdef __EMSCRIPTEN__
    // wasm 平台需手动加载捆绑的字体，否则无法显示中文
    int font_id = QFontDatabase::addApplicationFont(":/fonts/appfont.bin");
    QStringList font_families = QFontDatabase::applicationFontFamilies(font_id);
    if (!font_families.isEmpty()) {
        app.setFont(QFont(font_families.first()));
    }
#endif
    // 先在 GUI 线程创建通知桥，三维渲染线程只发布无状态就绪通知。
    auto& startup_progress = QStartupProgress::instance();
    QStartupGate startup_gate;
#ifndef __EMSCRIPTEN__
    auto splash = std::make_unique<QStartupSplashWindow>(QCoreApplication::applicationVersion());
    QPointer<QQuickWindow> main_window;
    QMetaObject::Connection startup_error_connection;
    bool frame_armed = false;
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(&startup_progress, &QStartupProgress::onViewportReady,
        &startup_gate, &QStartupGate::markViewportReady, Qt::QueuedConnection);
    QObject::connect(splash.get(), &QStartupSplashWindow::onCancelled, &app, &QCoreApplication::quit);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, splash.get(), &QWindow::hide);
    QObject::connect(&startup_gate, &QStartupGate::onReady, &app, [&]() {
        // 启动门控进入终态后，恢复 Qt 对后续场景图初始化错误的默认处理。
        QObject::disconnect(startup_error_connection);
        spdlog::info("startup: main window ready after viewport, startup files and presented frame");
        // 先隐藏以停止动画，再释放背景、绘制缓冲与封面专属连接。
        splash->hide();
        splash.reset();
        app.setQuitOnLastWindowClosed(true);
        if (main_window)
            main_window->requestActivate();
    });
    QObject::connect(&startup_gate, &QStartupGate::onUpdateRequested, &app, [&]() {
        if (!main_window || !startup_gate.isPrepared() || frame_armed)
            return;
        frame_armed = true;
        // 两项前置条件完成后才订阅同步点；不接受早先已排队的 frameSwapped。
        QObject::connect(main_window, &QQuickWindow::afterSynchronizing, &startup_gate, [&]() {
            if (!main_window)
                return;
            QObject::connect(main_window, &QQuickWindow::frameSwapped,
                &startup_gate, &QStartupGate::markFrameReady,
                Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
            main_window->update(); }, Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
        main_window->update();
    });
#endif
    WASM_GLOBAL std::unique_ptr<QQmlApplicationEngine> engine;
    QObject::connect(&startup_gate, &QStartupGate::onFailed, &app, [&](const QString& message) {
        spdlog::error("startup: {}", message.toStdString());
#ifndef __EMSCRIPTEN__
        splash->setStage(message);
        splash->hide();
#endif
        app.exit(-1);
    });

    auto initialize = [&]() -> bool {
        try {
            QLogManager::initialize();
            engine = std::make_unique<QQmlApplicationEngine>();
            dock::init(engine.get());
            engine->addImageProvider(QStringLiteral("wallpaper"), new WallpaperProvider);

            QList<QUrl> arguments;
            for (const auto& argument : app.arguments().mid(1))
                arguments << QUrl::fromLocalFile(argument);
            engine->rootContext()->setContextProperty("QLogManager", QLogManager::instance());
            engine->rootContext()->setContextProperty("commandLineArgs", QVariant::fromValue(arguments));
#ifdef __EMSCRIPTEN__
            engine->rootContext()->setContextProperty("QWasmBridge", new QWasmBridge(&app));
#endif
            engine->loadFromModule("app", "Main");
            if (engine->rootObjects().isEmpty()) {
                startup_gate.fail(QCoreApplication::translate("Startup", "主界面加载失败"));
                return false;
            }

#ifndef __EMSCRIPTEN__
            main_window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
            if (!main_window) {
                startup_gate.fail(QCoreApplication::translate("Startup", "未创建主窗口"));
                return false;
            }
            // 停靠中央区同步创建；若失败，不把空主窗口当成启动成功。
            auto has_viewport = [](auto&& visit, QQuickItem* item) -> bool {
                if (qobject_cast<QQuickVTKItem*>(item))
                    return true;
                for (auto* child : item->childItems()) {
                    if (visit(visit, child))
                        return true;
                }
                return false;
            };
            if (!has_viewport(has_viewport, main_window->contentItem())) {
                startup_gate.fail(QCoreApplication::translate("Startup", "三维视口加载失败"));
                return false;
            }
            // startupFilesReady 由 QML 动态声明，没有可用于类型安全连接的 C++ 信号成员。
            if (!QObject::connect(main_window, SIGNAL(startupFilesReady()),
                    &startup_gate, SLOT(markStartupFilesReady()))) {
                startup_gate.fail(QCoreApplication::translate("Startup", "启动文件处理通知未连接"));
                return false;
            }
            startup_error_connection = QObject::connect(main_window, &QQuickWindow::sceneGraphError, &startup_gate, [&](QQuickWindow::SceneGraphError, const QString& message) { startup_gate.fail(message); }, Qt::QueuedConnection);
#endif
            // 安装全局按键事件过滤器，将按键事件转发给功能系统。
            if (auto* model_manager = engine->singletonInstance<QModelManager*>("app.model", "QModelManager")) {
                if (auto* feature_adaptor = model_manager->getFeatureSystemAdaptor()) {
#ifndef __EMSCRIPTEN__
                    QObject::connect(&startup_gate, &QStartupGate::onReady, &app, [&, feature_adaptor]() {
                        app.installEventFilter(new KeyEventForwarder(*feature_adaptor, &app));
                    });
#else
                    app.installEventFilter(new KeyEventForwarder(*feature_adaptor, &app));
#endif
                }
            }
            return true;
        } catch (const std::exception& error) {
            startup_gate.fail(QString::fromUtf8(error.what()));
            return false;
        }
    };

#ifdef __EMSCRIPTEN__
    // wasm 继续由网页壳与浏览器管理启动和事件循环。
    return initialize() ? 0 : -1;
#else
    // 第一张启动画面提交后再排队初始化，全程不在插件构造中泵事件。
    QObject::connect(splash.get(), &QStartupSplashWindow::onFirstFrame, &app, [&]() {
        QTimer::singleShot(0, &app, [&]() {
            if (splash && splash->isVisible())
                initialize();
        });
    });
    splash->show();
    return app.exec();
#endif
}
