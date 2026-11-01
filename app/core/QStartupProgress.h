/**
 * @file QStartupProgress.h
 * @brief 启动视口通知桥：仅转发视口初始化完成通知。
 */

#pragma once

#include <QObject>

/**
 * @brief 应用层视口就绪通知入口，不保存就绪状态。
 *
 * main 须在 GUI 线程先取得实例；渲染线程发出的通知应排队连接到 GUI 接收者。
 */
class QStartupProgress final : public QObject {
    Q_OBJECT

public:
    //! @brief 获取由 main 在 GUI 线程首次创建的通知入口。
    static QStartupProgress& instance();

    //! @brief 通知中央视口已完成必要的渲染初始化。
    void reportViewportReady();

signals:
    //! @brief 中央视口初始化完成，接收方负责判断何时切换界面。
    void onViewportReady();

private:
    explicit QStartupProgress(QObject* parent = nullptr);
};
