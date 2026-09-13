#include "QLanguageManager.h"

#include <QCoreApplication>
#include <QSettings>
#include <QVariantMap>

#include <catch2/catch_test_macros.hpp>

namespace {
int kArgc = 1;
char kArgv0[] = "TestLanguageManager";
char* kArgv[] = { kArgv0, nullptr };

// 独立设置域，隔离并清理本测试写入的持久化数据，不污染用户真实设置
void setTestSettingsScope()
{
    QCoreApplication::setOrganizationName("PreCessTest");
    QCoreApplication::setApplicationName("TestLanguageManager");
    QSettings().clear();
}

QString readPersistedLanguage()
{
    return QSettings().value("language").toString();
}
}

TEST_CASE("language options and display names", "[app][language]")
{
    setTestSettingsScope();
    QCoreApplication app(kArgc, kArgv);
    QLanguageManager manager;

    const QVariantList options = manager.languageOptions();
    REQUIRE(options.size() == 2);

    const QVariantMap first = options.first().toMap();
    REQUIRE(first["code"].toString() == "zh_CN");
    REQUIRE(first["name"].toString() == "简体中文");

    const QVariantMap second = options.last().toMap();
    REQUIRE(second["code"].toString() == "en_US");
    REQUIRE(second["name"].toString() == "English");

    // 语言名使用该语言自身的名称，不随界面语言翻译
    REQUIRE(manager.displayName("zh_CN") == "简体中文");
    REQUIRE(manager.displayName("en_US") == "English");
    REQUIRE(manager.displayName("xx_XX") == "xx_XX");

    // 初始为源语言
    REQUIRE(manager.currentLanguage() == "zh_CN");
    REQUIRE_FALSE(manager.translatorLoaded());
}

TEST_CASE("language switch state machine and persistence", "[app][language]")
{
    setTestSettingsScope();
    QCoreApplication app(kArgc, kArgv);
    QLanguageManager manager;

    // 测试 exe 内嵌 /i18n/app_en_US.qm（见 app/test/CMakeLists.txt），可走真实装载路径
    manager.setLanguage("en_US");
    REQUIRE(manager.currentLanguage() == "en_US");
    REQUIRE(manager.translatorLoaded());
    REQUIRE(readPersistedLanguage() == "en_US");

    // 切回源语言：卸载翻译器
    manager.setLanguage("zh_CN");
    REQUIRE(manager.currentLanguage() == "zh_CN");
    REQUIRE_FALSE(manager.translatorLoaded());
    REQUIRE(readPersistedLanguage() == "zh_CN");

    // 非法语言代码回退到源语言
    manager.setLanguage("xx_XX");
    REQUIRE(manager.currentLanguage() == "zh_CN");
}

TEST_CASE("idempotent repeated language switch", "[app][language]")
{
    setTestSettingsScope();
    QCoreApplication app(kArgc, kArgv);
    QLanguageManager manager;

    manager.setLanguage("en_US");
    manager.setLanguage("en_US");
    REQUIRE(manager.currentLanguage() == "en_US");
    REQUIRE(manager.translatorLoaded());
    manager.setLanguage("zh_CN");
    REQUIRE(manager.currentLanguage() == "zh_CN");
}

TEST_CASE("switch back to source language uninstalls loaded translator", "[app][language]")
{
    setTestSettingsScope();
    QCoreApplication app(kArgc, kArgv);
    QLanguageManager manager;

    // 测试 exe 内嵌 /i18n/app_en_US.qm（见 app/test/CMakeLists.txt）：
    // 真实装载→卸载路径，防止"切回中文不卸载英文翻译器"回归
    manager.setLanguage("en_US");
    REQUIRE(manager.currentLanguage() == "en_US");
    REQUIRE(manager.translatorLoaded());

    manager.setLanguage("zh_CN");
    REQUIRE(manager.currentLanguage() == "zh_CN");
    REQUIRE_FALSE(manager.translatorLoaded());
    REQUIRE(readPersistedLanguage() == "zh_CN");
}

TEST_CASE("language choice persists across instances", "[app][language]")
{
    setTestSettingsScope();
    {
        QCoreApplication app(kArgc, kArgv);
        QLanguageManager manager;
        manager.setLanguage("zh_CN");
    }
    {
        QCoreApplication app(kArgc, kArgv);
        QLanguageManager manager;
        manager.applySavedLanguage();
        REQUIRE(manager.currentLanguage() == "zh_CN");
    }
}
