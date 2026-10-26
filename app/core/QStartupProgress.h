/**
 * @file QStartupProgress.h
 * @brief 启动阶段通知桥：仅转发真实阶段和视口初始化完成通知。
 */

#pragma once

#include <QObject>
#include <QString>

/**
 * @brief 应用层启动通知入口，不保存阶段或就绪状态。
 *
 * main 须在 GUI 线程先取得实例；渲染线程发出的通知应排队连接到 GUI 接收者。
 */
class QStartupProgress final : public QObject {
    Q_OBJECT

public:
    //! @brief 获取由 main 在 GUI 线程首次创建的通知入口。
    static QStartupProgress& instance();

    //! @brief 通知当前即将执行的真实初始化阶段。
    Q_INVOKABLE void reportStage(const QString& stage);

    //! @brief 通知中央视口已完成必要的渲染初始化。
    void reportViewportReady();

signals:
    //! @brief 启动阶段发生变化；内容为普通文本。
    void onStageChange(const QString& stage);
    //! @brief 中央视口初始化完成，接收方负责判断何时切换界面。
    void onViewportReady();

private:
    explicit QStartupProgress(QObject* parent = nullptr);
};
