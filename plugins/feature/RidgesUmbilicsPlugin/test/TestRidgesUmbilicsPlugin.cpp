/**
 * @file TestRidgesUmbilicsPlugin.cpp
 * @brief RidgesUmbilics Feature 插件单元测试
 *
 * FeatureSystem 驱动模式：通过 Selector 参数显式传入目标 Component，验证
 *   1) 参数声明（Selector + Combo/Int）
 *   2) 未选 Component 时的兜底提示
 *   3) 椭球面脊线/脐点提取与结果组件生成
 *   4) 非三角面 / 体单元的入口预检拒绝
 *
 * 测试网格：参数化椭球面（半轴 a/b/c，经度 M 段、纬度 N 段），闭合且全三角，
 * 理论上有 4 个椭圆脐点，必有脊线，适合作为曲率特征分析的正例。
 */

#include "ArgObject.h"
#include "ArgType.h"
#include "ComponentData.h"
#include "Core.h"
#include "EventBus.h"
#include "FeatureInfo.h"
#include "FeatureSystem.h"
#include "MeshData.h"
#include "ModelLayer.h"
#include "RidgesUmbilicsHandler.h"
#include "Selection.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace systems;
using namespace systems::feature;

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr const char* kFeatureName = "RidgesUmbilics";

/**
 * @brief 构造参数化椭球面三角网格（半轴 a/b/c，经度 M 段、纬度 N 段，外法向一致绕向）
 */
std::unique_ptr<MeshData> makeEllipsoidMesh(double a, double b, double c,
    int longitude_count, int latitude_count)
{
    auto point = [&](double theta, double phi) -> std::array<double, 3> {
        return { a * std::sin(theta) * std::cos(phi),
            b * std::sin(theta) * std::sin(phi),
            c * std::cos(theta) };
    };

    auto mesh = std::make_unique<MeshData>();
    mesh->init();
    mesh->vertex_positions_.emplace_back(point(0.0, 0.0)); // 北极
    for (int j = 1; j < latitude_count; ++j) {
        const double theta = kPi * j / latitude_count;
        for (int i = 0; i < longitude_count; ++i) {
            const double phi = 2.0 * kPi * i / longitude_count;
            mesh->vertex_positions_.emplace_back(point(theta, phi));
        }
    }
    mesh->vertex_positions_.emplace_back(point(kPi, 0.0)); // 南极
    mesh->vertex_count_ = static_cast<Index>(mesh->vertex_positions_.size());

    // 局部点 id：北极 0，纬度行 j 列 i → 1 + (j-1)*M + i%M，南极收尾
    auto vertex_id = [&](int row, int i) {
        return static_cast<Index>(1 + (row - 1) * longitude_count + (i % longitude_count));
    };
    auto add_face = [&](Index p0, Index p1, Index p2) {
        mesh->face_vertices_.push_back(p0);
        mesh->face_vertices_.push_back(p1);
        mesh->face_vertices_.push_back(p2);
        mesh->face_vertices_offset_.push_back(static_cast<Index>(mesh->face_vertices_.size()));
    };

    for (int i = 0; i < longitude_count; ++i)
        add_face(0, vertex_id(1, i + 1), vertex_id(1, i)); // 北极盖
    for (int j = 1; j + 1 < latitude_count; ++j) {
        for (int i = 0; i < longitude_count; ++i) {
            add_face(vertex_id(j, i), vertex_id(j, i + 1), vertex_id(j + 1, i + 1));
            add_face(vertex_id(j, i), vertex_id(j + 1, i + 1), vertex_id(j + 1, i));
        }
    }
    const int last_row = latitude_count - 1;
    for (int i = 0; i < longitude_count; ++i)
        add_face(vertex_id(last_row, i), vertex_id(last_row, i + 1), mesh->vertex_count_ - 1); // 南极盖

    return mesh;
}

/**
 * @brief 构造含单四边形面的网格（用于非三角面拒绝路径）
 */
std::unique_ptr<MeshData> makeQuadFaceMesh()
{
    auto mesh = std::make_unique<MeshData>();
    mesh->init();
    mesh->vertex_positions_ = {
        { 0.0, 0.0, 0.0 },
        { 1.0, 0.0, 0.0 },
        { 1.0, 1.0, 0.0 },
        { 0.0, 1.0, 0.0 },
    };
    mesh->vertex_count_ = 4;
    mesh->face_vertices_ = { 0, 1, 2, 3 };
    mesh->face_vertices_offset_ = { 0, 4 };
    return mesh;
}

/**
 * @brief 在 ModelLayer 中添加一个网格组件，返回模型 id、组件 id 与初始组件数
 */
struct MeshComponentSetup {
    Index model_id;
    Index component_id;
    std::size_t initial_component_count;
};

MeshComponentSetup addMeshComponent(ModelLayer& model_layer,
    std::unique_ptr<MeshData> mesh, const std::string& name)
{
    auto component = std::make_unique<ComponentData>();
    component->name = name;
    component->mesh = std::move(mesh);

    ComponentDatas components;
    components.push_back(std::move(component));
    const Index model_id = model_layer.addModel(name + "_model", std::move(components));
    ModelData* model = model_layer.modelById(model_id);
    REQUIRE(model != nullptr);
    REQUIRE_FALSE(model->componentIds().empty());
    return { model_id, model->componentIds().front(), model->componentIds().size() };
}

/**
 * @brief 构造测试用的 RidgesUmbilics HandlerMetaData（name 与 json 声明一致）
 */
HandlerMetaData ridgesMetaData()
{
    HandlerMetaData metadata;
    metadata.name = kFeatureName;
    metadata.display_name = "脊线与脐点分析";
    return metadata;
}

/**
 * @brief 向功能系统注册 RidgesUmbilics 处理器
 */
void registerRidgesHandler(FeatureSystem& feature_system)
{
    FeatureSystem::SystemHandlerPtr handler { new RidgesUmbilicsHandler };
    REQUIRE(feature_system.registerHandler(ridgesMetaData(), std::move(handler)));
}

/**
 * @brief 创建指向目标 Component 的选择器
 */
std::shared_ptr<Selection> makeComponentSelection(Index component_id)
{
    auto selection = std::make_shared<Selection>();
    selection->type = ElementEnum::Component;
    selection->ids = { component_id };
    return selection;
}

/**
 * @brief 按注册顺序设置全部分析参数：选择器 + 脊线类型/阶数/环数/脐点
 */
void setAnalysisParams(FeatureSystem& feature_system, Index component_id,
    int ridge_kind, int ridge_order, long long ring_count, int umbilics)
{
    REQUIRE(feature_system.setParameter(kFeatureName, 0,
        core::ArgObject::create<ArgTypeEnum::Selector>(makeComponentSelection(component_id))));
    REQUIRE(feature_system.setParameter(kFeatureName, 1,
        core::ArgObject::create<ArgTypeEnum::Combo>(ridge_kind)));
    REQUIRE(feature_system.setParameter(kFeatureName, 2,
        core::ArgObject::create<ArgTypeEnum::Combo>(ridge_order)));
    REQUIRE(feature_system.setParameter(kFeatureName, 3,
        core::ArgObject::create<ArgTypeEnum::Int>(ring_count)));
    REQUIRE(feature_system.setParameter(kFeatureName, 4,
        core::ArgObject::create<ArgTypeEnum::Combo>(umbilics)));
}

/**
 * @brief 断言 invoke 结果为字符串并取出
 */
std::string resultText(const std::any& result)
{
    REQUIRE(result.type() == typeid(std::string));
    return std::any_cast<const std::string&>(result);
}

/**
 * @brief 统计模型中名字带前缀的结果组件个数
 */
std::size_t countComponentsWithPrefix(const ModelLayer& model_layer,
    const ModelData& model, const std::string& prefix)
{
    std::size_t count = 0;
    for (const Index component_id : model.componentIds()) {
        const ComponentData* component = model_layer.findComponent(component_id);
        if (component && component->name.rfind(prefix, 0) == 0)
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE("RidgesUmbilics exposes Selector and analysis parameters", "[RidgesUmbilicsPlugin]")
{
    core::EventBus bus;
    ModelLayer model_layer;
    FeatureSystem feature_system(model_layer, bus);
    registerRidgesHandler(feature_system);

    const auto infos = feature_system.getFeatureInfos();
    REQUIRE(infos.size() == 1);
    REQUIRE(infos.front()->arg_types.size() == 5);

    // 参数 0：Selector 限定为 Component
    CHECK(infos.front()->arg_types[0].type == ArgTypeEnum::Selector);
    CHECK(infos.front()->arg_types[0].name == "目标 Component");
    CHECK(infos.front()->arg_types[0].content == "Component");

    // 参数 1：脊线类型，默认全部
    CHECK(infos.front()->arg_types[1].type == ArgTypeEnum::Combo);
    CHECK(infos.front()->arg_types[1].name == "脊线类型");
    CHECK(infos.front()->arg_types[1].content == "最大,最小,crest,全部|3");

    // 参数 2：脊线阶数
    CHECK(infos.front()->arg_types[2].type == ArgTypeEnum::Combo);
    CHECK(infos.front()->arg_types[2].name == "脊线阶数");
    CHECK(infos.front()->arg_types[2].content == "三阶,四阶");

    // 参数 3：拟合邻域环数
    CHECK(infos.front()->arg_types[3].type == ArgTypeEnum::Int);
    CHECK(infos.front()->arg_types[3].name == "拟合邻域环数（0=自动）");
    CHECK(infos.front()->arg_types[3].content == "0");

    // 参数 4：脐点开关
    CHECK(infos.front()->arg_types[4].type == ArgTypeEnum::Combo);
    CHECK(infos.front()->arg_types[4].name == "是否计算脐点");
    CHECK(infos.front()->arg_types[4].content == "是,否|0");
}

TEST_CASE("RidgesUmbilics returns guidance when no Component is selected", "[RidgesUmbilicsPlugin]")
{
    core::EventBus bus;
    ModelLayer model_layer;
    FeatureSystem feature_system(model_layer, bus);
    registerRidgesHandler(feature_system);

    // 未设置任何 Selector：execute 应返回"请选择一个 Component"
    const std::any result = feature_system.invoke(kFeatureName);
    REQUIRE(resultText(result) == "请选择一个 Component");
}

TEST_CASE("RidgesUmbilics extracts ridges and umbilics from ellipsoid", "[RidgesUmbilicsPlugin]")
{
    core::EventBus bus;
    ModelLayer model_layer;
    FeatureSystem feature_system(model_layer, bus);
    const auto setup = addMeshComponent(model_layer,
        makeEllipsoidMesh(2.0, 1.5, 1.0, 32, 24), "ellipsoid");
    registerRidgesHandler(feature_system);
    const ModelData* model = model_layer.modelById(setup.model_id);
    REQUIRE(model != nullptr);

    SECTION("All ridges and umbilics on a clean ellipsoid")
    {
        // 三阶拟合 + 自动邻域，全部脊线类型 + 脐点；椭球面必有脊线，且理论上有 4 个椭圆脐点
        setAnalysisParams(feature_system, setup.component_id, 3, 0, 0, 0);
        const std::string text = resultText(feature_system.invoke(kFeatureName));
        REQUIRE(text.find("分析完成") != std::string::npos);

        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_") >= 1);

        const std::size_t umbilic_components = countComponentsWithPrefix(model_layer, *model, "Umbilic_");
        REQUIRE(umbilic_components >= 1);
        CHECK(umbilic_components <= 3);

        // 纯新增结果组件，原组件仍在
        CHECK(model->componentIds().size() > setup.initial_component_count);
    }

    SECTION("Umbilics disabled produces ridge components only")
    {
        setAnalysisParams(feature_system, setup.component_id, 3, 0, 0, 1);
        const std::string text = resultText(feature_system.invoke(kFeatureName));
        REQUIRE(text.find("分析完成") != std::string::npos);

        CHECK(countComponentsWithPrefix(model_layer, *model, "Umbilic_") == 0);
        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_") >= 1);
    }

    SECTION("Max ridge kind creates no min ridge components")
    {
        setAnalysisParams(feature_system, setup.component_id, 0, 0, 0, 1);
        const std::string text = resultText(feature_system.invoke(kFeatureName));
        REQUIRE(text.find("分析完成") != std::string::npos);

        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_Min") == 0);
        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_Max") >= 1);
    }
}

TEST_CASE("RidgesUmbilics rejects non-triangular mesh", "[RidgesUmbilicsPlugin]")
{
    core::EventBus bus;
    ModelLayer model_layer;
    FeatureSystem feature_system(model_layer, bus);
    const auto setup = addMeshComponent(model_layer, makeQuadFaceMesh(), "quad");
    registerRidgesHandler(feature_system);

    setAnalysisParams(feature_system, setup.component_id, 3, 0, 0, 0);
    const std::string text = resultText(feature_system.invoke(kFeatureName));

    // 非三角面被预检拒绝：温和文案、不抛异常、不生成任何结果组件
    REQUIRE(text.find("仅支持纯三角表面网格") != std::string::npos);
    const ModelData* model = model_layer.modelById(setup.model_id);
    REQUIRE(model != nullptr);
    CHECK(model->componentIds().size() == setup.initial_component_count);
}

TEST_CASE("RidgesUmbilics rejects meshes containing solid cells", "[RidgesUmbilicsPlugin]")
{
    core::EventBus bus;
    ModelLayer model_layer;
    FeatureSystem feature_system(model_layer, bus);
    auto setup = addMeshComponent(model_layer,
        makeEllipsoidMesh(2.0, 1.5, 1.0, 8, 6), "solid_ellipsoid");
    registerRidgesHandler(feature_system);

    // 表面网格上叠加 1 个 VTK_TETRA(=10) 体单元，触发"含体单元"预检
    ComponentData* component = model_layer.findComponent(setup.component_id);
    REQUIRE(component != nullptr);
    REQUIRE(component->mesh != nullptr);
    component->mesh->solid_types_.push_back(10);
    component->mesh->solid_vertices_ = { 0, 1, 2, 3 };
    component->mesh->solid_vertices_offset_ = { 0, 4 };

    setAnalysisParams(feature_system, setup.component_id, 3, 0, 0, 0);
    const std::string text = resultText(feature_system.invoke(kFeatureName));
    REQUIRE(text.find("含体单元") != std::string::npos);

    const ModelData* model = model_layer.modelById(setup.model_id);
    REQUIRE(model != nullptr);
    CHECK(model->componentIds().size() == setup.initial_component_count);
}
