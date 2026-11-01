/**
 * @file QStartupProgress.cpp
 * @brief 启动视口就绪通知桥的无状态实现。
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

void QStartupProgress::reportViewportReady()
{
    emit onViewportReady();
}
