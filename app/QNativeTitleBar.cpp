/**
 * @file QNativeTitleBar.cpp
 * @brief 原生标题栏颜色与系统高对比度回退。
 */
#include "QNativeTitleBar.h"

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QPlatformSurfaceEvent>
#include <QVariant>
#include <QWindow>
#include <spdlog/spdlog.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows 类型必须先于 DWM 头文件声明。
// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on
#endif

QNativeTitleBar::QNativeTitleBar(QWindow* window, QObject* parent)
    : QObject(parent)
    , window_(window)
    , surface_available_(window && window->handle())
{
    if (!window_)
        return;

    // 主窗口由 QML 创建，因此通过其属性变更信号同步原生颜色。
    connect(window_, SIGNAL(nativeTitleBarBackgroundChanged()), this, SLOT(refresh()));
    connect(window_, SIGNAL(nativeTitleBarTextColorChanged()), this, SLOT(refresh()));
    window_->installEventFilter(this);
#ifdef Q_OS_WIN
    if (auto* app = QCoreApplication::instance())
        app->installNativeEventFilter(this);
#endif
    refresh();
}

QNativeTitleBar::~QNativeTitleBar()
{
#ifdef Q_OS_WIN
    if (auto* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
#endif
    if (window_)
        window_->removeEventFilter(this);
}

void QNativeTitleBar::refresh()
{
#ifdef Q_OS_WIN
    // 不在窗口未创建或销毁期间调用 winId()，避免意外创建新的平台窗口。
    if (!window_ || !surface_available_ || !window_->handle())
        return;

    const auto hwnd = reinterpret_cast<HWND>(window_->winId());
    if (!hwnd)
        return;

    // 属性值从 Windows 11 22000 开始可用；数字常量使旧 Windows SDK 也可编译。
    constexpr DWORD caption_attribute = 35; // DWMWA_CAPTION_COLOR
    constexpr DWORD text_attribute = 36; // DWMWA_TEXT_COLOR
    constexpr COLORREF system_default = 0xffffffff; // DWMWA_COLOR_DEFAULT
    const auto restore_defaults = [hwnd]() {
        const COLORREF color = system_default;
        const auto caption_result = DwmSetWindowAttribute(hwnd, caption_attribute, &color, sizeof(color));
        const auto text_result = DwmSetWindowAttribute(hwnd, text_attribute, &color, sizeof(color));
        if (FAILED(caption_result) || FAILED(text_result)) {
            spdlog::debug("Failed to restore native title bar colors: caption HRESULT=0x{:08X}, text HRESULT=0x{:08X}",
                static_cast<unsigned long>(caption_result), static_cast<unsigned long>(text_result));
        }
    };

    HIGHCONTRASTW contrast {};
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) {
        const auto error = GetLastError();
        spdlog::warn("Failed to query high contrast state; using system title bar colors: Win32 error={}", error);
        restore_defaults();
        return;
    }
    if (contrast.dwFlags & HCF_HIGHCONTRASTON) {
        restore_defaults();
        return;
    }

    const QColor background = window_->property("nativeTitleBarBackground").value<QColor>();
    const QColor text = window_->property("nativeTitleBarTextColor").value<QColor>();
    if (!background.isValid() || !text.isValid()) {
        restore_defaults();
        return;
    }
    const COLORREF caption_color = RGB(background.red(), background.green(), background.blue());
    const COLORREF text_color = RGB(text.red(), text.green(), text.blue());
    const auto caption_result = DwmSetWindowAttribute(hwnd, caption_attribute, &caption_color, sizeof(caption_color));
    const auto text_result = DwmSetWindowAttribute(hwnd, text_attribute, &text_color, sizeof(text_color));
    // 旧系统可能不支持这些属性；避免只应用了一半的颜色。
    if (FAILED(caption_result) || FAILED(text_result)) {
        // 旧系统不支持标题栏配色属于预期回退，只在调试日志中记录。
        spdlog::debug("Failed to apply native title bar colors: caption HRESULT=0x{:08X}, text HRESULT=0x{:08X}",
            static_cast<unsigned long>(caption_result), static_cast<unsigned long>(text_result));
        restore_defaults();
    }
#endif
}

bool QNativeTitleBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event->type() == QEvent::PlatformSurface) {
        const auto* surface_event = static_cast<QPlatformSurfaceEvent*>(event);
        if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            surface_available_ = true;
            // 事件处理完毕后再取句柄，避免与平台窗口创建顺序竞争。
            QMetaObject::invokeMethod(this, &QNativeTitleBar::refresh, Qt::QueuedConnection);
        } else if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            surface_available_ = false;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool QNativeTitleBar::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result)
{
    Q_UNUSED(event_type);
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    const auto* msg = static_cast<const MSG*>(message);
    // 高对比度和系统主题是全局状态；不拦截通知，等待系统处理完后刷新。
    if (msg && (msg->message == WM_SETTINGCHANGE || msg->message == WM_THEMECHANGED))
        QMetaObject::invokeMethod(this, &QNativeTitleBar::refresh, Qt::QueuedConnection);
#else
    Q_UNUSED(message);
#endif
    return false;
}
