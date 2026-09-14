/**
 * @file TestRidgesUmbilicsHandler.cpp
 * @brief RidgesUmbilicsHandler 单元测试
 */
#include "RidgesUmbilicsHandler.h"

#include "ArgObject.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "MeshData.h"
#include "ModelData.h"
#include "ModelIOSystemBase.h"
#include "ModelLayer.h"
#include "Selection.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace systems::algo;
using namespace core;

namespace {

constexpr double kPi = 3.14159265358979323846;

/**
 * @brief ModelIOSystemBase 测试桩：脊线分析为纯内存计算，不触碰 IO 系统
 */
class StubModelIOSystem : public systems::io::ModelIOSystemBase {
public:
    bool read(const std::filesystem::path&, const std::string&,
        const std::vector<std::any>&) override
    {
        return false;
    }
    void write(Index, const std::filesystem::path&, const std::string&,
        const std::vector<std::any>&) override
    {
    }
    void writeComponents(const std::vector<Index>&, const std::filesystem::path&,
        const std::string&, const std::vector<std::any>&) override
    {
    }
};

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
 * @brief 在 ModelLayer 中添加一个网格组件，返回组件 id 与初始组件数
 */
struct MeshComponentSetup {
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
    return { model->componentIds().front(), model->componentIds().size() };
}

/**
 * @brief 构造只包含一个目标 Component 的选择器参数
 */
std::shared_ptr<Selection> makeComponentSelection(Index component_id)
{
    auto selection = std::make_shared<Selection>();
    selection->type = ElementEnum::Component;
    selection->ids = { component_id };
    return selection;
}

/**
 * @brief 按 args_type 顺序构造执行参数：选择器 + 脊线类型/阶数/环数/脐点
 */
std::vector<core::ArgObject> makeExecuteArgs(std::shared_ptr<Selection> selection,
    int ridge_kind, int ridge_order, long long ring_count, int umbilics)
{
    return {
        core::ArgObject::create<ArgTypeEnum::Selector>(std::move(selection)),
        core::ArgObject::create<ArgTypeEnum::Combo>(ridge_kind),
        core::ArgObject::create<ArgTypeEnum::Combo>(ridge_order),
        core::ArgObject::create<ArgTypeEnum::Int>(ring_count),
        core::ArgObject::create<ArgTypeEnum::Combo>(umbilics),
    };
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

TEST_CASE("RidgesUmbilicsHandler::args_type() returns correct parameter count and types")
{
    RidgesUmbilicsHandler handler;
    const auto args = handler.args_type();

    REQUIRE(args.size() == 5);

    SECTION("Parameter 0 (Selector): target component via component selection")
    {
        CHECK(args[0].type == ArgTypeEnum::Selector);
        CHECK(args[0].name == "目标 Component");
        CHECK(args[0].content == "Component");
    }

    SECTION("Parameter 1 (Combo): ridge kind with default all")
    {
        CHECK(args[1].type == ArgTypeEnum::Combo);
        CHECK(args[1].name == "脊线类型");
        CHECK(args[1].content == "最大,最小,crest,全部|3");
    }

    SECTION("Parameter 2 (Combo): ridge order 3rd/4th")
    {
        CHECK(args[2].type == ArgTypeEnum::Combo);
        CHECK(args[2].name == "脊线阶数");
        CHECK(args[2].content == "三阶,四阶");
    }

    SECTION("Parameter 3 (Int): fitting neighborhood rings")
    {
        CHECK(args[3].type == ArgTypeEnum::Int);
        CHECK(args[3].name == "拟合邻域环数（0=自动）");
        CHECK(args[3].content == "0");
    }

    SECTION("Parameter 4 (Combo): umbilics toggle")
    {
        CHECK(args[4].type == ArgTypeEnum::Combo);
        CHECK(args[4].name == "是否计算脐点");
        CHECK(args[4].content == "是,否");
    }
}

TEST_CASE("RidgesUmbilicsHandler::resolveComponentId resolves via selection first")
{
    ModelLayer model_layer;
    const auto comp_a = addMeshComponent(model_layer,
        makeEllipsoidMesh(2.0, 1.5, 1.0, 8, 6), "compA");
    const auto comp_b = addMeshComponent(model_layer,
        makeEllipsoidMesh(2.0, 1.5, 1.0, 8, 6), "compB");
    REQUIRE(comp_a.component_id != comp_b.component_id);

    RidgesUmbilicsHandler handler;

    SECTION("Single component selection takes precedence over fallback")
    {
        const auto args_a = makeExecuteArgs(makeComponentSelection(comp_a.component_id), 3, 0, 0, 0);
        REQUIRE(handler.resolveComponentId(model_layer, comp_b.component_id, args_a)
            == comp_a.component_id);

        const auto args_b = makeExecuteArgs(makeComponentSelection(comp_b.component_id), 3, 0, 0, 0);
        REQUIRE(handler.resolveComponentId(model_layer, comp_a.component_id, args_b)
            == comp_b.component_id);
    }

    SECTION("Multiple component selection is rejected")
    {
        auto selection = std::make_shared<Selection>();
        selection->type = ElementEnum::Component;
        selection->ids = { comp_a.component_id, comp_b.component_id };
        const auto args = makeExecuteArgs(selection, 3, 0, 0, 0);
        CHECK_FALSE(handler.resolveComponentId(model_layer, comp_a.component_id, args).has_value());
    }

    SECTION("Empty selection falls back to object tree component")
    {
        const auto args = makeExecuteArgs(std::make_shared<Selection>(), 3, 0, 0, 0);
        REQUIRE(handler.resolveComponentId(model_layer, comp_a.component_id, args)
            == comp_a.component_id);
        CHECK_FALSE(handler.resolveComponentId(model_layer, -1, args).has_value());
    }
}

TEST_CASE("RidgesUmbilicsHandler::execute extracts ridges and umbilics from ellipsoid")
{
    ModelLayer model_layer;
    const auto setup = addMeshComponent(model_layer,
        makeEllipsoidMesh(2.0, 1.5, 1.0, 32, 24), "ellipsoid");

    StubModelIOSystem io_system;
    auto component_operator = model_layer.getComponentOperator(setup.component_id);
    REQUIRE(component_operator.has_value());
    HandlerContext context { io_system, *component_operator };
    const ModelData* model = model_layer.modelById(component_operator->modelId());
    REQUIRE(model != nullptr);

    RidgesUmbilicsHandler handler;

    SECTION("All ridges and umbilics on a clean ellipsoid")
    {
        // 三阶拟合 + 自动邻域，全部脊线类型 + 脐点；椭球面必有脊线，且理论上有 4 个椭圆脐点
        const auto args = makeExecuteArgs(makeComponentSelection(setup.component_id), 3, 0, 0, 0);
        handler.execute(context, args);

        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_") >= 1);

        const std::size_t umbilic_components = countComponentsWithPrefix(model_layer, *model, "Umbilic_");
        REQUIRE(umbilic_components >= 1);
        CHECK(umbilic_components <= 3);

        // 纯新增结果组件，原组件仍在
        CHECK(model->componentIds().size() > setup.initial_component_count);
    }

    SECTION("Umbilics disabled produces ridge components only")
    {
        const auto args = makeExecuteArgs(makeComponentSelection(setup.component_id), 3, 0, 0, 1);
        handler.execute(context, args);

        CHECK(countComponentsWithPrefix(model_layer, *model, "Umbilic_") == 0);
        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_") >= 1);
    }

    SECTION("Max ridge kind creates no min ridge components")
    {
        const auto args = makeExecuteArgs(makeComponentSelection(setup.component_id), 0, 0, 0, 1);
        handler.execute(context, args);

        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_Min") == 0);
        CHECK(countComponentsWithPrefix(model_layer, *model, "Ridge_Max") >= 1);
    }
}

TEST_CASE("RidgesUmbilicsHandler::execute rejects non-triangular mesh")
{
    ModelLayer model_layer;
    const auto setup = addMeshComponent(model_layer, makeQuadFaceMesh(), "quad");

    StubModelIOSystem io_system;
    auto component_operator = model_layer.getComponentOperator(setup.component_id);
    REQUIRE(component_operator.has_value());
    HandlerContext context { io_system, *component_operator };
    const ModelData* model = model_layer.modelById(component_operator->modelId());
    REQUIRE(model != nullptr);

    RidgesUmbilicsHandler handler;
    const auto args = makeExecuteArgs(makeComponentSelection(setup.component_id), 3, 0, 0, 0);
    handler.execute(context, args);

    // 非三角面被预检拒绝：不抛异常、不生成任何结果组件
    CHECK(model->componentIds().size() == setup.initial_component_count);
}
