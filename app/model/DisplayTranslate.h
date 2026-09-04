/**
 * @file DisplayTranslate.h
 * @brief 显示层翻译工具：动态注册字符串以源语言（中文）存储，UI 读取时经 QTranslator 按当前语言翻译
 *
 * 翻译统一挂载在 "PreCess" 上下文（与 app_en_US.ts 中插件/系统动态字符串条目一致），
 * 未命中翻译条目时返回源串。切换语言后各系统经 notifyLanguageChanged() 重发信息
 * 变更回调，UI 重建并重新读取，本工具在新一轮读取时返回新语言文本。
 */
#pragma once

#include "ArgType.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <string>

namespace systems {

/// @brief 动态注册字符串的显示翻译（未命中翻译条目时返回源串）
inline QString displayTranslate(const std::string& source)
{
    const QByteArray utf8 = QString::fromStdString(source).toUtf8();
    return QCoreApplication::translate("PreCess", utf8.constData());
}

/// @brief 参数类型的显示副本：name/desc 按当前语言翻译，content 保持原样（内容兼作数据语义）
inline core::ArgType displayTranslatedArg(const core::ArgType& arg)
{
    core::ArgType translated = arg;
    translated.name = displayTranslate(arg.name).toStdString();
    translated.desc = displayTranslate(arg.desc).toStdString();
    return translated;
}
}
