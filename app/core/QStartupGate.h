/**
 * @file QStartupGate.h
 * @brief 在视口初始化后的新帧到达时，单次通知主界面可用。
 */

#pragma once

#include <QObject>
#include <QString>

/**
 * @brief 汇合视口初始化和后续呈现帧通知的 GUI 线程门控。
 *
 * 所有槽均由 GUI 线程调用；渲染线程的帧通知须排队投递。
 * 早于视口初始化的帧不保留，成功或失败均为不可逆终态。
 */
class QStartupGate final : public QObject {
    Q_OBJECT

public:
    explicit QStartupGate(QObject* parent = nullptr);
    //! @brief 允许订阅后续同步/呈现帧；终态返回 false。
    bool isPrepared() const;

public slots:
    //! @brief 记录视口初始化完成，并请求刷新主窗口。
    void markViewportReady();
    //! @brief 仅在视口初始化完成后接受本次新帧。
    void markFrameReady();
    //! @brief 未到终态时单次发布失败，随后忽略所有启动通知。
    void fail(const QString& message);

signals:
    //! @brief 视口初始化后已有新帧，允许关闭启动页。
    void onReady();
    //! @brief 启动失败，消息为普通文本。
    void onFailed(const QString& message);
    //! @brief 视口就绪通知到达，请求主窗口产生一帧。
    void onUpdateRequested();

private:
    bool viewport_ready_ { false };
    bool finished_ { false };
};
