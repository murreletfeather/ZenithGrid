/**
 * @file QStartupProgress.cpp
 * @brief 启动阶段通知桥的无状态实现。
 */

#include "QStartupProgress.h"

QStartupProgress::QStartupProgress(QObject* parent)
    : QObject(parent)
{
}

QStartupProgress& QStartupProgress::instance()
{
    static QStartupProgress progress;
    return progress;
}

void QStartupProgress::reportStage(const QString& stage)
{
    emit onStageChange(stage);
}

void QStartupProgress::reportViewportReady()
{
    emit onViewportReady();
}
