#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTranslator>
#include <QVariantList>

class QQmlEngine;

/**
 * @brief 语言管理器：装载翻译文件、切换界面语言并持久化用户选择
 *
 * 语言策略：中文（zh_CN）为源语言，界面字符串以中文编写，切换到其他语言时
 * 装载对应 .qm 翻译文件（如 :/i18n/app_en_US.qm）。切换后经
 * QQmlEngine::retranslate() 触发全部翻译绑定重求值，界面即时刷新。
 * 用户选择经 QSettings 持久化，启动时经 applySavedLanguage() 恢复，
 * 无记录时按系统语言检测默认语言。
 */
class QLanguageManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList languageOptions READ languageOptions CONSTANT)
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY currentLanguageChanged)
    Q_PROPERTY(bool translatorLoaded READ translatorLoaded NOTIFY translatorLoadedChanged)

public:
    /**
     * @brief 构造语言管理器
     * @param engine 界面引擎，切换语言后触发 retranslate；测试等无界面场景可为空
     */
    explicit QLanguageManager(QQmlEngine* engine = nullptr, QObject* parent = nullptr);

    /// @brief 可供切换的语言列表，元素形如 {code, name}，name 使用该语言自身的名称（不翻译）
    QVariantList languageOptions() const;

    /// @brief 当前生效的语言代码（如 zh_CN / en_US）
    QString currentLanguage() const;

    /// @brief 当前语言对应的翻译文件是否已成功装载（zh_CN 为源语言，恒为 false）
    bool translatorLoaded() const;

    /// @brief 应用已保存的语言设置（程序启动时调用），无记录时按系统语言检测
    Q_INVOKABLE void applySavedLanguage();

    /// @brief 切换语言并持久化；未知语言代码回退到源语言 zh_CN
    Q_INVOKABLE void setLanguage(const QString& code);

    /// @brief 语言选项的显示名称，使用该语言自身的名称（不翻译）
    Q_INVOKABLE QString displayName(const QString& code) const;

signals:
    void currentLanguageChanged(const QString& code);
    void translatorLoadedChanged();

private:
    /// @brief 卸载旧翻译并按需装载目标语言的 .qm，装载失败仅告警不中断切换
    void installTranslator(const QString& code);

    /// @brief 按系统语言检测默认语言：中文环境用 zh_CN，其余用 en_US
    static QString detectSystemLanguage();

    /// @brief 将语言选择写入 QSettings
    static void persistLanguage(const QString& code);

    QPointer<QQmlEngine> engine_;
    QTranslator translator_;
    QString current_language_;
    bool translator_loaded_ = false;
};
