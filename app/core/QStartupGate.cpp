/**
 * @file QStartupGate.cpp
 * @brief 启动切换门控的 GUI 线程实现。
 */

#include "QStartupGate.h"

QStartupGate::QStartupGate(QObject* parent)
    : QObject(parent)
{
}

bool QStartupGate::isPrepared() const
{
    return !finished_ && viewport_ready_ && startup_files_ready_;
}

void QStartupGate::markViewportReady()
{
    if (finished_)
        return;
    viewport_ready_ = true;
    emit onUpdateRequested();
}

void QStartupGate::markStartupFilesReady()
{
    if (finished_)
        return;
    startup_files_ready_ = true;
    emit onUpdateRequested();
}

void QStartupGate::markFrameReady()
{
    if (finished_ || !viewport_ready_ || !startup_files_ready_)
        return;
    // 先进入终态，再发通知，避免接收者同步回调产生第二个终态。
    finished_ = true;
    emit onReady();
}

void QStartupGate::fail(const QString& message)
{
    if (finished_)
        return;
    finished_ = true;
    emit onFailed(message);
}
