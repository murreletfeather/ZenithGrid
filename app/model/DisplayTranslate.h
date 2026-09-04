/**
 * @file DisplayTranslate.h
 * @brief 显示层翻译工具：动态注册字符串以源语言（中文）存储，UI 读取时经 QTranslator 按当前语言翻译
 *
 * 翻译统一挂载在 "PreCess" 上下文（与 app_en_US.ts 中插件/系统动态字符串条目一致），
 * 未命中翻译条目时返回源串。切换语言后各系统经 notifyLanguageChanged() 重发信息
 * 变更回调，UI 重建并重新读取，本工具在新一轮读取时返回新语言文本。
 *
 * 另提供模型树自动命名的显示翻译（displayTranslateTreeNodeName）：模型数据以英文
 * 存储（Point_1、component_3、Mesh 等），仅按已知模式翻译为界面语言，用户自定义
 * 名称一律透传；显示语言经 setDisplayLanguage 由 QLanguageManager 同步。
 */
#pragma once

#include "ArgType.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QString>

#include <string>

namespace systems {

namespace detail {
    // 模型树自动命名的显示语言状态（zh_CN 为真），由 QLanguageManager 切换时经 setDisplayLanguage 更新
    inline bool& displayLanguageChinese()
    {
        static bool chinese = false;
        return chinese;
    }
}

/// @brief 同步模型树显示语言（QLanguageManager 切换时调用，zh_CN 以外视为英文）
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
    // 模型树自动命名词表（英文存储）：zh 时为中文，en 时保留原文
    inline const QHash<QString, QString>& treeKeywordZhMap()
    {
        static const QHash<QString, QString> map = {
            // 固定层级标签（总览汇总行，无编号）
            { QStringLiteral("Mesh"), QStringLiteral("网格") },
            { QStringLiteral("Geometry"), QStringLiteral("几何") },
            { QStringLiteral("2D"), QStringLiteral("二维") },
            { QStringLiteral("3D"), QStringLiteral("三维") },
            { QStringLiteral("Vertex"), QStringLiteral("点") },
            { QStringLiteral("Edge"), QStringLiteral("边") },
            { QStringLiteral("Face"), QStringLiteral("面") },
            { QStringLiteral("Solid"), QStringLiteral("体") },
            // 组件级自动命名关键词（Point_1 / component_3 等模式，下划线保留）
            { QStringLiteral("Point"), QStringLiteral("点") },
            { QStringLiteral("Line"), QStringLiteral("直线") },
            { QStringLiteral("RectangleFace"), QStringLiteral("矩形面") },
            { QStringLiteral("DiskFace"), QStringLiteral("圆盘面") },
            { QStringLiteral("Box"), QStringLiteral("长方体") },
            { QStringLiteral("Cylinder"), QStringLiteral("圆柱体") },
            { QStringLiteral("Cone"), QStringLiteral("圆锥体") },
            { QStringLiteral("Sphere"), QStringLiteral("球体") },
            { QStringLiteral("component"), QStringLiteral("组件") },
            // 无编号裸词自动命名（几何功能新建组件）
            { QStringLiteral("FaceFromEdges"), QStringLiteral("闭合边面") },
            { QStringLiteral("Extrude"), QStringLiteral("拉伸体") },
        };
        return map;
    }

    // 关键词_数字 模式（如 Point_1 / component_3）
    inline const QRegularExpression& numberedNamePattern()
    {
        static const QRegularExpression pattern(QStringLiteral("^([A-Za-z]+)_(\\d+)$"));
        return pattern;
    }
}

/// @brief 模型树节点名的显示翻译：只识别已知的自动命名模式（固定层级标签与
/// 关键词_数字 组合名），用户自定义名称（中文名、temp_ 前缀、未知关键词等）一律透传。
/// 编号分隔符保留下划线（zh：点_1；en：原名）。
inline QString displayTranslateTreeNodeName(const QString& name)
{
    if (!detail::displayLanguageChinese())
        return name;
    if (name.isEmpty())
        return name;

    // 裸关键词（固定层级标签，无编号）
    const auto& map = detail::treeKeywordZhMap();
    if (const auto it = map.constFind(name); it != map.cend())
        return it.value();

    // 关键词_数字 模式（组件级自动命名）
    const QRegularExpressionMatch match = detail::numberedNamePattern().match(name);
    if (match.hasMatch()) {
        const QString keyword = match.captured(1);
        if (const auto it = map.constFind(keyword); it != map.cend())
            return it.value() + QLatin1Char('_') + match.captured(2);
    }

    return name;
}
}
