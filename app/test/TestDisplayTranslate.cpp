#include "DisplayTranslate.h"
#include "QArgType.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using systems::displayTranslateHierarchyLabel;
using systems::displayTranslatedArg;
using systems::setDisplayLanguage;

/**
 * @brief 模型树层级标签显示翻译回归：仅固定层级标签翻译；
 * 模型名与组件名（含自动命名）一律透传、不翻译
 */
TEST_CASE("hierarchy labels translate; model and component names pass through", "[app][language]")
{
    setDisplayLanguage(QStringLiteral("zh_CN"));

    SECTION("fixed hierarchy labels translate in zh")
    {
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Mesh")) == QStringLiteral("网格"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Geometry")) == QStringLiteral("几何"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("2D")) == QStringLiteral("二维"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("3D")) == QStringLiteral("三维"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Vertex")) == QStringLiteral("点"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Edge")) == QStringLiteral("边"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Face")) == QStringLiteral("面"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Solid")) == QStringLiteral("体"));
    }

    SECTION("component auto names pass through unchanged in zh")
    {
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Point_1")) == QStringLiteral("Point_1"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("component_3")) == QStringLiteral("component_3"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Box_1")) == QStringLiteral("Box_1"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("FaceFromEdges")) == QStringLiteral("FaceFromEdges"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Extrude")) == QStringLiteral("Extrude"));
    }

    SECTION("custom names pass through unchanged in zh")
    {
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("我的模型")) == QStringLiteral("我的模型"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Box_Final")) == QStringLiteral("Box_Final"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("temp_Point_1")) == QStringLiteral("temp_Point_1"));
        REQUIRE(displayTranslateHierarchyLabel(QString()) == QString());
    }

    SECTION("everything stays original in English")
    {
        setDisplayLanguage(QStringLiteral("en_US"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Mesh")) == QStringLiteral("Mesh"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("2D")) == QStringLiteral("2D"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("Point_1")) == QStringLiteral("Point_1"));
        REQUIRE(displayTranslateHierarchyLabel(QStringLiteral("我的模型")) == QStringLiteral("我的模型"));
    }
}

TEST_CASE("QArgType holds ArgType by value (no dangling reference)", "[app][language]")
{
    // 回归：QArgType 曾以 const 引用保存类型，构造自显示层临时副本
    //（displayTranslatedArg 返回值）后立即悬垂，QML 读取 type()/name() 时崩溃。
    std::unique_ptr<QArgType> arg_type;
    {
        core::ArgType local { ArgTypeEnum::Float, "尺寸", "1.5", "网格尺寸" };
        arg_type = std::make_unique<QArgType>(displayTranslatedArg(local));
        // local 在同一作用域块析构；值持有语义下 QArgType 不受影响
    }

    REQUIRE(arg_type->type() == QArgType::ArgTypeEnum::Float);
    REQUIRE(arg_type->name() == "尺寸");
    REQUIRE(arg_type->content() == "1.5");
    REQUIRE(arg_type->desc() == "网格尺寸");
}
