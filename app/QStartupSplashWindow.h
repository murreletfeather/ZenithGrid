/**
 * @file QStartupSplashWindow.h
 * @brief 不依赖主 QML 和三维场景的轻量启动窗口
 */
#pragma once

#include <QBackingStore>
#include <QImage>
#include <QTimer>
#include <QWindow>

class QPainter;

//! @brief 同步初始化期间直接刷新真实阶段，不在插件构造中重入事件循环
class QStartupSplashWindow final : public QWindow {
    Q_OBJECT

public:
    explicit QStartupSplashWindow(const QString& version);

    //! @brief 更新当前真实阶段；单个阶段内部不伪造百分比
    void setStage(const QString& message);

signals:
    //! @brief 启动页第一次绘制并提交后，允许排队开始主程序初始化
    void onFirstFrame();
    //! @brief 用户关闭启动窗口时取消本次启动
    void onCancelled();

protected:
    bool event(QEvent* event) override;
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void renderNow();
    void paintContents(QPainter& painter) const;

    QBackingStore backing_store_;
    QImage background_;
    QTimer activity_timer_;
    QString version_;
    QString stage_;
    int activity_phase_ = 26;
    bool first_frame_ = false;
};
