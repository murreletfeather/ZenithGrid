/**
 * @file QStartupSplashWindow.cpp
 * @brief 原生栅格启动窗口，沿用 ZenithGrid 已确认设计稿
 */
#include "QStartupSplashWindow.h"

#include <QCloseEvent>
#include <QCursor>
#include <QEvent>
#include <QExposeEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>

#include <algorithm>
#include <stdexcept>

QStartupSplashWindow::QStartupSplashWindow(const QString& version)
    : backing_store_(this)
    , background_(QStringLiteral(":/images/splash/ZenithGrid-splash-background@2x.png"))
    , version_(version)
    , stage_(tr("正在启动…"))
{
    if (background_.isNull())
        throw std::runtime_error("ZenithGrid startup background is missing");

    setObjectName(QStringLiteral("startupSplash"));
    setTitle(tr("ZenithGrid — 正在启动"));
    setSurfaceType(QSurface::RasterSurface);
    setFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setModality(Qt::ApplicationModal);

    auto* target_screen = QGuiApplication::screenAt(QCursor::pos());
    if (!target_screen)
        target_screen = QGuiApplication::primaryScreen();
    if (target_screen) {
        setScreen(target_screen);
        const QRect available = target_screen->availableGeometry();
        const qreal scale = std::min({ 1.0, available.width() * 0.9 / 960.0, available.height() * 0.9 / 600.0 });
        const QSize splash_size(std::max(1, qRound(960 * scale)), std::max(1, qRound(600 * scale)));
        setGeometry(QRect(available.center() - QPoint(splash_size.width() / 2, splash_size.height() / 2), splash_size));
    } else {
        resize(960, 600);
    }

    activity_timer_.setInterval(40);
    connect(&activity_timer_, &QTimer::timeout, this, [this]() {
        activity_phase_ = (activity_phase_ + 1) % 120;
        renderNow();
    });
    connect(this, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible)
            activity_timer_.start();
        else
            activity_timer_.stop();
    });
}

void QStartupSplashWindow::setStage(const QString& message)
{
    stage_ = message;
    // QBackingStore 直接 flush，不在构造未完成的 QML/插件中调用 processEvents。
    renderNow();
}

bool QStartupSplashWindow::event(QEvent* event)
{
    if (event->type() == QEvent::UpdateRequest)
        renderNow();
    return QWindow::event(event);
}

void QStartupSplashWindow::exposeEvent(QExposeEvent* event)
{
    QWindow::exposeEvent(event);
    renderNow();
}

void QStartupSplashWindow::resizeEvent(QResizeEvent* event)
{
    QWindow::resizeEvent(event);
    renderNow();
}

void QStartupSplashWindow::closeEvent(QCloseEvent* event)
{
    QWindow::closeEvent(event);
    emit onCancelled();
}

void QStartupSplashWindow::renderNow()
{
    if (!isExposed() || !isVisible() || size().isEmpty())
        return;
    backing_store_.resize(size());
    const QRegion dirty(QRect(QPoint(), size()));
    backing_store_.beginPaint(dirty);
    {
        QPainter painter(backing_store_.paintDevice());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.scale(width() / 960.0, height() / 600.0);
        paintContents(painter);
    }
    backing_store_.endPaint();
    backing_store_.flush(dirty);
    if (!first_frame_) {
        first_frame_ = true;
        emit onFirstFrame();
    }
}

void QStartupSplashWindow::paintContents(QPainter& painter) const
{
    painter.drawImage(QRectF(0, 0, 960, 600), background_);
    auto text = [&painter](const QString& value, qreal x, qreal y, int size, const QColor& color,
                    QFont::Weight weight = QFont::Normal, qreal spacing = 0.0, bool right = false) {
        QFont font = QGuiApplication::font();
        font.setFamilies({ QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI"), QStringLiteral("sans-serif") });
        font.setPixelSize(size);
        font.setWeight(weight);
        font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
        painter.setFont(font);
        painter.setPen(color);
        if (right)
            x -= QFontMetricsF(font).horizontalAdvance(value);
        painter.drawText(QPointF(x, y), value);
    };

    text(tr("几何 · 网格 · 工程"), 88, 54, 14, QColor("#B8CED5"), QFont::Normal, 1);
    text(QStringLiteral("GEOMETRY"), 50, 131, 44, QColor("#E1ECEF"), QFont::Normal, 2);
    text(QStringLiteral("& MESH"), 50, 185, 44, QColor("#B8CED5"), QFont::Light, 2);
    text(tr("几何与网格前处理"), 168, 485, 16, QColor("#536F7C"), QFont::Normal, 1);
    text(tr("版本"), 908, 403, 14, QColor("#536F7C"), QFont::Normal, 1, true);
    text(QStringLiteral("v%1").arg(version_), 910, 442, 30, QColor("#536F7C"), QFont::Normal, 0.5, true);
    text(tr("正在启动"), 908, 553, 14, QColor("#536F7C"), QFont::Normal, 0, true);

    QFont status_font = QGuiApplication::font();
    status_font.setPixelSize(16);
    painter.setFont(status_font);
    painter.setPen(QColor("#536F7C"));
    const QString elided = QFontMetricsF(status_font).elidedText(stage_, Qt::ElideMiddle, 700);
    painter.drawText(QPointF(80, 554), elided);
    painter.setPen(QPen(QColor("#E1ECEF"), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(59, 548), 6, 6);
    painter.setPen(QPen(QColor("#087C8B"), 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(53, 542, 12, 12), (90 - activity_phase_ * 3) * 16, -270 * 16);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#E1ECEF"));
    painter.drawRoundedRect(QRectF(52, 575, 856, 3), 1.5, 1.5);
    painter.setBrush(QColor("#159FAC"));
    // 中间短段往返，明确表示活动而非固定百分比。
    const int phase = activity_phase_ <= 60 ? activity_phase_ : 120 - activity_phase_;
    painter.drawRoundedRect(QRectF(52 + phase * (712.0 / 60), 575, 144, 3), 1.5, 1.5);
}
