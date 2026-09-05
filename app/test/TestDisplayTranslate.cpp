#include "DisplayTranslate.h"
#include "QArgType.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using systems::displayTranslateTreeNodeName;
using systems::displayTranslatedArg;
using systems::setDisplayLanguage;

/**
 * @brief 模型树自动命名显示翻译回归：仅翻译已知模式，用户自定义名称透传
 */
TEST_CASE("tree node names translate in Chinese and stay original in English", "[app][language]")
{
    setDisplayLanguage(QStringLiteral("zh_CN"));

    SECTION("fixed hierarchy labels translate in zh")
    {
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Mesh")) == QStringLiteral("网格"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Geometry")) == QStringLiteral("几何"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("2D")) == QStringLiteral("二维"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("3D")) == QStringLiteral("三维"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Vertex")) == QStringLiteral("点"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Edge")) == QStringLiteral("边"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Face")) == QStringLiteral("面"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Solid")) == QStringLiteral("体"));
    }

    SECTION("numbered component auto names keep underscore in zh")
    {
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Point_1")) == QStringLiteral("点_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Line_2")) == QStringLiteral("直线_2"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("RectangleFace_3")) == QStringLiteral("矩形面_3"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("DiskFace_1")) == QStringLiteral("圆盘面_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Box_1")) == QStringLiteral("长方体_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Cylinder_2")) == QStringLiteral("圆柱体_2"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Cone_1")) == QStringLiteral("圆锥体_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Sphere_1")) == QStringLiteral("球体_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("component_3")) == QStringLiteral("组件_3"));
    }

    SECTION("bare-word auto names translate in zh")
    {
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("FaceFromEdges")) == QStringLiteral("闭合边面"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Extrude")) == QStringLiteral("拉伸体"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Extrude_1")) == QStringLiteral("拉伸体_1"));
    }

    SECTION("custom names pass through unchanged in zh")
    {
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("我的模型")) == QStringLiteral("我的模型"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Box_Final")) == QStringLiteral("Box_Final"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("temp_Point_1")) == QStringLiteral("temp_Point_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Unknown_1")) == QStringLiteral("Unknown_1"));
        REQUIRE(displayTranslateTreeNodeName(QString()) == QString());
    }

    SECTION("everything stays original in English")
    {
        setDisplayLanguage(QStringLiteral("en_US"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Mesh")) == QStringLiteral("Mesh"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Point_1")) == QStringLiteral("Point_1"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("component_3")) == QStringLiteral("component_3"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("FaceFromEdges")) == QStringLiteral("FaceFromEdges"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("Extrude")) == QStringLiteral("Extrude"));
        REQUIRE(displayTranslateTreeNodeName(QStringLiteral("我的模型")) == QStringLiteral("我的模型"));
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
