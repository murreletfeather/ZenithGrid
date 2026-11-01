/**
 * @file TestNativeTitleBar.cpp
 * @brief 原生标题栏颜色随窗口属性变化及平台窗口重建的回归测试。
 */
#include "QNativeTitleBar.h"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QWindow>
#include <catch2/catch_test_macros.hpp>

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

namespace {

QGuiApplication& testApplication()
{
#ifndef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    static int argc = 1;
    static char name[] = "TestNativeTitleBar";
    static char* argv[] = { name, nullptr };
    static QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    return app;
}

class TestColorWindow : public QWindow {
    Q_OBJECT
    Q_PROPERTY(QColor nativeTitleBarBackground READ background WRITE setBackground NOTIFY nativeTitleBarBackgroundChanged)
    Q_PROPERTY(QColor nativeTitleBarTextColor READ textColor WRITE setTextColor NOTIFY nativeTitleBarTextColorChanged)

public:
    QColor background() const { return background_; }
    QColor textColor() const { return text_color_; }

    void setBackground(const QColor& color)
    {
        if (background_ == color)
            return;
        background_ = color;
        emit nativeTitleBarBackgroundChanged();
    }

    void setTextColor(const QColor& color)
    {
        if (text_color_ == color)
            return;
        text_color_ = color;
        emit nativeTitleBarTextColorChanged();
    }

signals:
    void nativeTitleBarBackgroundChanged();
    void nativeTitleBarTextColorChanged();

private:
    QColor background_ { QStringLiteral("#102B33") };
    QColor text_color_ { QStringLiteral("#DDE8E9") };
};

#ifdef Q_OS_WIN
constexpr DWORD kCaptionAttribute = 35; // DWMWA_CAPTION_COLOR，Windows 11 起支持。
constexpr DWORD kTextAttribute = 36; // DWMWA_TEXT_COLOR。

COLORREF colorRef(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}
#endif

}

TEST_CASE("Native title bar updates do not create a platform surface", "[titlebar]")
{
    testApplication();
    TestColorWindow window;
    QNativeTitleBar title_bar(&window);

    REQUIRE(window.handle() == nullptr);
    window.setBackground(QColor("#F3F4F6"));
    window.setTextColor(QColor("#2B2F36"));
    title_bar.refresh();
    REQUIRE(window.handle() == nullptr);

    window.create();
    QCoreApplication::processEvents();
    REQUIRE(window.handle() != nullptr);
    window.destroy();
    REQUIRE(window.handle() == nullptr);
    window.setBackground(QColor("#102B33"));
    title_bar.refresh();
    REQUIRE(window.handle() == nullptr);
}

TEST_CASE("Native title bar follows colors after window recreation", "[titlebar][windows]")
{
    testApplication();
#ifndef Q_OS_WIN
    SKIP("原生标题栏颜色仅在 Windows 上设置");
#else
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
        SKIP("需要 Windows 原生窗口平台");

    HIGHCONTRASTW contrast {};
    contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0))
        SKIP("无法读取系统高对比度状态");
    if (contrast.dwFlags & HCF_HIGHCONTRASTON)
        SKIP("当前启用系统高对比度，标题栏应由系统配色");

    TestColorWindow window;
    QNativeTitleBar title_bar(&window);
    window.create();
    QCoreApplication::processEvents();
    REQUIRE(window.handle() != nullptr);
    auto hwnd = reinterpret_cast<HWND>(window.winId());
    REQUIRE(hwnd != nullptr);

    // 文档只承诺设置颜色；系统若不支持读取则跳过值断言，不误判应用回退。
    COLORREF actual_caption = 0;
    COLORREF actual_text = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, kCaptionAttribute, &actual_caption, sizeof(actual_caption)))
        || FAILED(DwmGetWindowAttribute(hwnd, kTextAttribute, &actual_text, sizeof(actual_text))))
        SKIP("当前系统不支持读取原生标题栏颜色属性");

    CHECK(actual_caption == colorRef(QColor("#102B33")));
    CHECK(actual_text == colorRef(QColor("#DDE8E9")));

    window.setBackground(QColor("#F3F4F6"));
    window.setTextColor(QColor("#2B2F36"));
    QCoreApplication::processEvents();
    REQUIRE(SUCCEEDED(DwmGetWindowAttribute(hwnd, kCaptionAttribute, &actual_caption, sizeof(actual_caption))));
    REQUIRE(SUCCEEDED(DwmGetWindowAttribute(hwnd, kTextAttribute, &actual_text, sizeof(actual_text))));
    CHECK(actual_caption == colorRef(QColor("#F3F4F6")));
    CHECK(actual_text == colorRef(QColor("#2B2F36")));

    // 重新创建平台窗口时必须重新应用当前主题，不依赖旧句柄值是否复用。
    window.destroy();
    REQUIRE(window.handle() == nullptr);
    window.setBackground(QColor("#102B33"));
    window.setTextColor(QColor("#DDE8E9"));
    window.create();
    QCoreApplication::processEvents();
    REQUIRE(window.handle() != nullptr);
    hwnd = reinterpret_cast<HWND>(window.winId());
    REQUIRE(SUCCEEDED(DwmGetWindowAttribute(hwnd, kCaptionAttribute, &actual_caption, sizeof(actual_caption))));
    REQUIRE(SUCCEEDED(DwmGetWindowAttribute(hwnd, kTextAttribute, &actual_text, sizeof(actual_text))));
    CHECK(actual_caption == colorRef(QColor("#102B33")));
    CHECK(actual_text == colorRef(QColor("#DDE8E9")));
#endif
}

#include "TestNativeTitleBar.moc"