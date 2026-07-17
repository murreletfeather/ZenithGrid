/**
 * @file TestMeasureHandler.cpp
 * @brief 测量处理器的单元测试
 */

#include "MeasureHandler.h"
#include "ArgObject.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "MakeMeshData.h"
#include "MeshData.h"
#include "ModelIOSystemBase.h"
#include "ModelLayer.h"
#include "Selection.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <string>

using namespace systems::algo;

namespace {

class MockModelIOSystem : public systems::io::ModelIOSystemBase {
public:
    void read(const std::filesystem::path&, const std::string&, const std::vector<std::any>&) override {}
    void write(Index, const std::filesystem::path&, const std::string&, const std::vector<std::any>&) override {}
    void writeComponents(const std::vector<Index>&, const std::filesystem::path&,
        const std::string&, const std::vector<std::any>&) override {}
};

std::shared_ptr<Selection> makeVertexSelection(const std::vector<Index>& ids)
{
    return std::make_shared<Selection>(Selection { ids, ElementEnum::Vertex, 0 });
}

} // namespace

TEST_CASE("MeasureHandler args_type is correct")
{
    MeasureHandler handler;
    auto args = handler.args_type();
    REQUIRE(args.size() == 2);
    CHECK(args[0].type == ArgTypeEnum::Combo);
    CHECK(args[1].type == ArgTypeEnum::Selector);
}

TEST_CASE("MeasureHandler: distance between two vertices")
{
    auto mesh = std::make_unique<MeshData>(MakeMeshData());
    auto c = std::make_unique<ComponentData>();
    c->id = -1;
    c->name = "Comp";
    c->mesh = std::move(mesh);

    ComponentDatas comps;
    comps.push_back(std::move(c));

    ModelLayer mgr;
    const Index model_id = mgr.addModel("test", std::move(comps));

    auto cids = mgr.modelById(model_id)->componentIds();
    REQUIRE(cids.size() == 1);

    ComponentData* comp = mgr.findComponent(cids[0]);
    REQUIRE(comp);
    ComponentOperator comp_op(cids[0], *comp, mgr, nullptr);

    MeshData* mesh_ptr = comp->asMeshData();
    REQUIRE(mesh_ptr);
    const Index base = mesh_ptr->local_to_global_[0];

    core::ArgObject type_arg = core::ArgObject::create<ArgTypeEnum::Combo>(0); // 距离
    auto selection = makeVertexSelection({ base + 0, base + 1 });
    selection->component_id = cids[0];
    core::ArgObject sel_arg = core::ArgObject::create<ArgTypeEnum::Selector>(selection);

    MockModelIOSystem io_sys;
    HandlerContext ctx { io_sys, comp_op };

    MeasureHandler handler;
    auto result_any = handler.execute(ctx, { type_arg, sel_arg });
    const std::string* result = std::any_cast<std::string>(&result_any);
    REQUIRE(result != nullptr);
    CHECK(result->find("Distance") != std::string::npos);
    CHECK(result->find("1.000000") != std::string::npos);
}
