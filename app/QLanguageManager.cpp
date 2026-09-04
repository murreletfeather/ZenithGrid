#include "QLanguageManager.h"

#include "DisplayTranslate.h"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QVariantMap>

namespace {
const QString kSourceLanguage = QStringLiteral("zh_CN"); //> 源语言：界面字符串本身的语言，无需翻译文件
const QStringList kSupportedLanguages = { QStringLiteral("zh_CN"), QStringLiteral("en_US") };
const QString kSettingsKey = QStringLiteral("language");
const QString kTranslationPrefix = QStringLiteral(":/i18n/app_");
const QString kTranslationSuffix = QStringLiteral(".qm");
}

QLanguageManager::QLanguageManager(QQmlEngine* engine, QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , current_language_(kSourceLanguage)
{
    // 同步模型树自动命名的显示语言（默认为源语言 zh_CN）
    systems::setDisplayLanguage(current_language_);
}

QVariantList QLanguageManager::languageOptions() const
{
    QVariantList options;
    for (const QString& code : kSupportedLanguages) {
        QVariantMap option;
        option.insert(QStringLiteral("code"), code);
        option.insert(QStringLiteral("name"), displayName(code));
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

    installTranslator(target);

    current_language_ = target;
    // 同步模型树自动命名的显示语言（zh_CN → 中文映射，其余语言用英文存储原名）
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

void QLanguageManager::installTranslator(const QString& code)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (translator_loaded_ && app)
        app->removeTranslator(&translator_);
    translator_loaded_ = false;

    // 源语言不需要翻译文件
    if (code == kSourceLanguage)
        return;

    const QString path = kTranslationPrefix + code + kTranslationSuffix;
    if (translator_.load(path)) {
        translator_loaded_ = true;
        if (app)
            app->installTranslator(&translator_);
    } else {
        qWarning() << "QLanguageManager: 翻译文件装载失败，界面保持源语言：" << path;
    }
}

QString QLanguageManager::detectSystemLanguage()
{
    return QLocale::system().language() == QLocale::Chinese ? kSourceLanguage : QStringLiteral("en_US");
}

void QLanguageManager::persistLanguage(const QString& code)
{
    QSettings().setValue(kSettingsKey, code);
}
