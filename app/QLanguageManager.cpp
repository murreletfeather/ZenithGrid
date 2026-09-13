#include "QLanguageManager.h"

#include "DisplayTranslate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QVariantMap>

#include <spdlog/spdlog.h>

namespace {
const QString kSourceLanguage = QStringLiteral("zh_CN"); //> 源语言：界面字符串本身的语言，无需翻译文件
const QStringList kSupportedLanguages = { QStringLiteral("zh_CN"), QStringLiteral("en_US") };
const QString kSettingsKey = QStringLiteral("language");
const QString kTranslationPrefix = QStringLiteral(":/i18n/"); //> qm 资源目录（qt_add_translations RESOURCE_PREFIX /i18n）
//> 核心翻译文件基名（app/i18n/app_en_US.ts → app_en_US.qm）：先装载，失败则整体回退源语言
const QString kCoreTranslationBase = QStringLiteral("app");
}

QLanguageManager::QLanguageManager(QQmlEngine* engine, QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , current_language_(kSourceLanguage)
{
    // 同步模型树层级标签的显示语言（默认为源语言 zh_CN）
    systems::setDisplayLanguage(current_language_);
}

QVariantList QLanguageManager::languageOptions() const
{
    QVariantList options;
    for (const QString& code : kSupportedLanguages) {
        QVariantMap option;
        option.insert(QStringLiteral("code"), code);
        option.insert(QStringLiteral("name"), displayName(code));
        option.insert(QStringLiteral("available"), isLanguageAvailable(code));
        options << option;
    }
    return options;
}

QString QLanguageManager::currentLanguage() const
{
    return current_language_;
}

bool QLanguageManager::translatorLoaded() const
{
    return translator_loaded_;
}

void QLanguageManager::applySavedLanguage()
{
    const QString saved = QSettings().value(kSettingsKey).toString();
    setLanguage(!saved.isEmpty() ? saved : detectSystemLanguage());
}

void QLanguageManager::setLanguage(const QString& code)
{
    QString target = code;
    if (!kSupportedLanguages.contains(target))
        target = kSourceLanguage;

    // 幂等：重复设置相同语言不重复装载、不重复触发刷新
    if (target == current_language_)
        return;

    // 恒执行装载/卸载：源语言分支负责卸载已安装的翻译器（切回中文必须移除英文翻译器），
    // 核心翻译装载失败时回退源语言（插件翻译缺失仅跳过，不触发回退）：
    // 保证语言状态与界面实际文案一致，且不持久化装载失败的目标
    const bool loaded = loadTranslators(target);
    if (!loaded && target != kSourceLanguage) {
        target = kSourceLanguage;
        if (target == current_language_)
            return;
    }

    current_language_ = target;
    // 同步模型树层级标签的显示语言（仅几何/网格与 2D/3D/点/边/面/体；
    // 模型名与组件名不翻译）
    systems::setDisplayLanguage(target);
    persistLanguage(target);
    emit currentLanguageChanged(target);
    emit translatorLoadedChanged();

    // 翻译绑定是 translationLanguage 属性的观察者，retranslate 触发全部 qsTr 重求值
    if (engine_)
        engine_->retranslate();
}

QString QLanguageManager::displayName(const QString& code) const
{
    if (code == QStringLiteral("zh_CN"))
        return QStringLiteral("简体中文");
    if (code == QStringLiteral("en_US"))
        return QStringLiteral("English");
    return code;
}

bool QLanguageManager::isLanguageAvailable(const QString& code) const
{
    // 源语言无翻译文件需求，恒可用
    if (code == kSourceLanguage)
        return true;
    if (!kSupportedLanguages.contains(code))
        return false;
    return QFile::exists(kTranslationPrefix + kCoreTranslationBase + QLatin1Char('_') + code
        + QStringLiteral(".qm"));
}

bool QLanguageManager::loadTranslators(const QString& code)
{
    QCoreApplication* app = QCoreApplication::instance();

    // 卸载并清理当前语言的全部翻译器（切回源语言即空链）
    for (const auto& translator : translators_) {
        if (translator && app)
            app->removeTranslator(translator.get());
    }
    translators_.clear();
    translator_loaded_ = false;

    // 纵深防御：语言码必须落在白名单内（正常调用链已在上层过滤，这里兜底拦截非法输入）
    if (!kSupportedLanguages.contains(code)) {
        spdlog::warn("[i18n] QLanguageManager::loadTranslators: 非法语言码，已忽略: {}", code.toStdString());
        return false;
    }

    // 源语言不需要翻译文件
    if (code == kSourceLanguage)
        return true;

    // 装载单个 qm 并加入翻译器链；失败返回 false
    auto load_one = [this, app](const QString& base, const QString& lang_code) -> bool {
        auto translator = std::make_unique<QTranslator>();
        const QString path = kTranslationPrefix + base + QLatin1Char('_') + lang_code + QStringLiteral(".qm");
        if (!translator->load(path)) {
            spdlog::warn("[i18n] 翻译文件装载失败: {}", path.toStdString());
            return false;
        }
        if (app)
            app->installTranslator(translator.get());
        translators_.push_back(std::move(translator));
        return true;
    };

    // 核心翻译文件装载失败 → 整体回退源语言
    if (!load_one(kCoreTranslationBase, code)) {
        spdlog::warn("[i18n] 核心翻译文件装载失败，回退源语言");
        return false;
    }
    translator_loaded_ = true;

    // 插件翻译：枚举资源目录内同语言的 qm（由 app/CMakeLists.txt 逐插件 qt_add_translations
    // 生成并统一嵌入），无需 C++ 侧维护清单；缺失/损坏仅告警跳过，不阻断全局语言切换
    const QString suffix = QLatin1Char('_') + code + QStringLiteral(".qm");
    QStringList plugin_bases;
    const QDir i18n_dir(kTranslationPrefix);
    const QStringList qm_files = i18n_dir.entryList(QStringList { QStringLiteral("*") + suffix }, QDir::Files);
    for (const QString& qm_file : qm_files) {
        const QString base = qm_file.left(qm_file.size() - suffix.size());
        if (base != kCoreTranslationBase)
            plugin_bases << base;
    }
    plugin_bases.sort();
    for (const QString& base : plugin_bases) {
        load_one(base, code);
    }
    return true;
}

QString QLanguageManager::detectSystemLanguage()
{
    return QLocale::system().language() == QLocale::Chinese ? kSourceLanguage : QStringLiteral("en_US");
}

void QLanguageManager::persistLanguage(const QString& code)
{
    QSettings().setValue(kSettingsKey, code);
}
