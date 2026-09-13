/**
 * @file DisplayTranslate.h
 * @brief 显示层翻译工具：动态注册字符串以源语言（中文）存储，UI 读取时经 QTranslator 按当前语言翻译
 *
 * 翻译统一挂载在 "PreCess" 上下文（与 app_en_US.ts 中插件/系统动态字符串条目一致），
 * 未命中翻译条目时返回源串。切换语言后各系统经 notifyLanguageChanged() 重发信息
 * 变更回调，UI 重建并重新读取，本工具在新一轮读取时返回新语言文本。
 *
 * 另提供模型树层级标签的显示翻译（displayTranslateHierarchyLabel）：仅“几何/网格”
 * 层与“2D/3D/点/边/面/体”层这类界面标签按显示语言翻译；模型名与组件名（含自动
 * 命名 Point_1/component_3 等）一律透传——要翻译应从命名源头入手，展示层不代劳。
 * 显示语言经 setDisplayLanguage 由 QLanguageManager 同步。
 */
#pragma once

#include "ArgType.h"

#include <QCoreApplication>
#include <QHash>
#include <QString>

#include <string>

namespace systems {

namespace detail {
    // 模型树层级标签的显示语言状态（zh_CN 为真），由 QLanguageManager 切换时经 setDisplayLanguage 更新
    inline bool& displayLanguageChinese()
    {
        static bool chinese = false;
        return chinese;
    }
}

/// @brief 同步模型树层级标签显示语言（QLanguageManager 切换时调用，zh_CN 以外视为英文）
inline void setDisplayLanguage(const QString& code)
{
    detail::displayLanguageChinese() = (code == QStringLiteral("zh_CN"));
}

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

namespace detail {
    // 模型树层级标签词表（英文存储、界面标签）：zh 时为中文，en 时保留原文。
    // 仅收固定层级标签；模型名/组件名不在此列（展示层不翻译数据名）
    inline const QHash<QString, QString>& hierarchyLabelZhMap()
    {
        static const QHash<QString, QString> map = {
            { QStringLiteral("Mesh"), QStringLiteral("网格") },
            { QStringLiteral("Geometry"), QStringLiteral("几何") },
            { QStringLiteral("2D"), QStringLiteral("二维") },
            { QStringLiteral("3D"), QStringLiteral("三维") },
            { QStringLiteral("Vertex"), QStringLiteral("点") },
            { QStringLiteral("Edge"), QStringLiteral("边") },
            { QStringLiteral("Face"), QStringLiteral("面") },
            { QStringLiteral("Solid"), QStringLiteral("体") },
        };
        return map;
    }
}

/// @brief 模型树层级标签的显示翻译：仅精确匹配固定层级标签（几何/网格、
/// 2D/3D/点/边/面/体），其余名称（模型名、组件名与自定义名）一律透传。
inline QString displayTranslateHierarchyLabel(const QString& name)
{
    if (!detail::displayLanguageChinese() || name.isEmpty())
        return name;
    const auto& map = detail::hierarchyLabelZhMap();
    if (const auto it = map.constFind(name); it != map.cend())
        return it.value();
    return name;
}
}
