/**
 * @file TestSession.cpp
 * @brief Session 组合根测试（子系统装配、观察者转发与 ModelEvent 桥接、有序拆解）
 */
#include "ModelScope.h"
#include "Session.h"

#include "ComponentData.h"
#include "EventBus.h"
#include "FeatureEvents.h"
#include "FeatureSystem.h"
#include "GeometryBuilder.h"
#include "GeometryData.h"
#include "ModelIOHandler.h"
#include "ModelIOSystem.h"
#include "ModelObserver.h"
#include "ModelOperator.h"
#include "UndoStack.h"

#include <any>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace session;

namespace {
//! @brief 通过外部计数验证借用 IO 系统不会提前销毁其处理器。
class BorrowedIOHandler final : public systems::io::ModelIOHandler {
public:
    explicit BorrowedIOHandler(int& destroyed_count)
        : destroyed_count_(destroyed_count)
    {
    }

    ~BorrowedIOHandler() override { ++destroyed_count_; }

    std::optional<ModelPayload> read_model(const std::filesystem::path&, const std::vector<std::any>&) override
    {
        return ModelPayload { u8"借用 IO 导入模型", { } };
    }

    void write_components(const ModelLayer&, const std::vector<Index>&,
        const std::filesystem::path&, const std::vector<std::any>&) override
    {
    }

    std::vector<core::ArgType> read_args_type() const override { return { }; }
    std::vector<core::ArgType> write_args_type() const override { return { }; }

private:
    int& destroyed_count_;
};

// 记录各类模型层通知次数的观察者桩
class RecordingObserver : public ModelObserver {
public:
    int model_added { 0 };
    int model_removed { 0 };
    int model_changed { 0 };
    int model_name_changed { 0 };
    int component_changed { 0 };
    int component_removed { 0 };
    int geometry_load_failed { 0 };

    void notifyModelAdded(Index) override { ++model_added; }
    void notifyModelRemoved(Index) override { ++model_removed; }
    void notifyModelChanged(Index) override { ++model_changed; }
    void notifyModelNameChanged(Index, const std::string&) override { ++model_name_changed; }
    void notifyComponentChanged(Index) override { ++component_changed; }
    void notifyComponentRemoved(Index) override { ++component_removed; }
    void notifyGeometryLoadFailed(const std::string&) override { ++geometry_load_failed; }
};

// 统计 ModelEvent 各类事件次数
struct EventCounter {
    int added { 0 };
    int removed { 0 };
    int component_removed { 0 };
    core::EventBus::Subscription sub_; //> 订阅句柄，析构自动退订

    void subscribe(core::EventBus& bus)
    {
        sub_ = bus.subscribe<systems::feature::ModelEvent>([this](const systems::feature::ModelEvent& e) {
            using Kind = systems::feature::ModelEvent::Kind;
            if (e.kind == Kind::ModelAdded)
                ++added;
            else if (e.kind == Kind::ModelRemoved)
                ++removed;
            else if (e.kind == Kind::ComponentRemoved)
                ++component_removed;
        });
    }
};
}

namespace {
Index addTestModel(Session& session)
{
    ModelScope command(session.model(), &session.undoStack(), "添加模型");
    return session.model().addModel("A", { });
}
}

TEST_CASE("Session assembles subsystems and bridges model events", "[session]")
{
    RecordingObserver observer;
    Session s(&observer);
    EventCounter counter;
    counter.subscribe(s.events());

    SECTION("加模型：宿主观察者与 ModelEvent 同时收到通知")
    {
        const Index model_id = addTestModel(s);
        CHECK(observer.model_added == 1);
        CHECK(counter.added == 1);
        CHECK(s.query().hasModel(model_id));
    }

    SECTION("结构操作通过边界记录，undo 恢复并通知")
    {
        addTestModel(s);
        REQUIRE(s.undoStack().canUndo());
        s.undoStack().undo();
        CHECK(s.query().listModels().empty());
        CHECK(observer.model_removed == 1);
        CHECK(counter.removed == 1);
        CHECK(s.undoStack().canRedo());
    }

    SECTION("removeModel 经会话入口删除并通知")
    {
        const Index model_id = addTestModel(s);
        s.removeModel(model_id);
        CHECK(s.query().listModels().empty());
        CHECK(observer.model_removed == 1);
        CHECK(counter.removed == 1);
        REQUIRE(s.undoStack().undo());
        CHECK(s.query().hasModel(model_id));
        REQUIRE(s.undoStack().redo());
        CHECK_FALSE(s.query().hasModel(model_id));
    }

    SECTION("removeGeometry 为操作边界：undo 记录 + 通知 flush")
    {
        const Index model_id = addTestModel(s);
        auto geometry = std::make_unique<GeometryData>();
        geometry->setRootShape(GeometryBuilder::makeBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0));
        auto component = std::make_unique<ComponentData>();
        component->name = "Box";
        component->geometry = std::move(geometry);
        auto op = s.model().getModelOperator(model_id);
        Index comp_id;
        {
            ModelScope command(s.model(), &s.undoStack(), "添加组件");
            comp_id = op->addGeometryComponent(std::move(component));
        }
        REQUIRE(s.query().hasComponent(comp_id));
        const int component_changed_before = observer.component_changed;

        s.removeGeometry(comp_id);
        CHECK(s.query().geometrySummary(comp_id).has_geometry == false);
        // removeGeometry 的边界 flush 发一次组件变更通知（addGeometryComponent 已即时通知过一次）
        CHECK(observer.component_changed == component_changed_before + 1);

        s.undoStack().undo();
        CHECK(s.query().geometrySummary(comp_id).has_geometry == true);
    }

    SECTION("teardown 幂等：重复调用与析构安全")
    {
        addTestModel(s);
        s.teardown();
        s.teardown();
    }
}

TEST_CASE("Session works without host observer", "[session]")
{
    Session s;
    EventCounter counter;
    counter.subscribe(s.events());

    addTestModel(s);
    CHECK(counter.added == 1);
    CHECK(s.query().listModels().size() == 1);
    CHECK(s.undoStack().canUndo());
}

TEST_CASE("Session deletes components from multiple models in one operation", "[session][undo]")
{
    RecordingObserver observer;
    Session session(&observer);
    auto& model = session.model();
    auto& history = session.undoStack();

    Index first_model;
    Index second_model;
    {
        ModelScope scope(model, &history, "添加测试模型");
        ComponentDatas first_components;
        first_components.push_back(std::make_unique<ComponentData>());
        first_components.push_back(std::make_unique<ComponentData>());
        ComponentDatas second_components;
        second_components.push_back(std::make_unique<ComponentData>());
        first_model = model.addModel("first", std::move(first_components));
        second_model = model.addModel("second", std::move(second_components));
    }
    const auto first_components = session.query().componentIds(first_model);
    const auto second_components = session.query().componentIds(second_model);
    REQUIRE(first_components.size() == 2);
    REQUIRE(second_components.size() == 1);
    const Index first_removed = first_components.back();
    const Index second_removed = second_components.front();
    history.clear();

    SECTION("valid IDs across models are removed once and share an undo record")
    {
        session.removeComponents({ first_removed, -1, second_removed, first_removed });
        CHECK(session.query().componentIds(first_model) == std::vector<Index> { first_components.front() });
        CHECK(session.query().componentIds(second_model).empty());
        CHECK(observer.component_removed == 2);

        REQUIRE(history.undo());
        CHECK(session.query().componentIds(first_model) == first_components);
        CHECK(session.query().componentIds(second_model) == second_components);
        CHECK_FALSE(history.canUndo());

        REQUIRE(history.redo());
        CHECK_FALSE(session.query().hasComponent(first_removed));
        CHECK_FALSE(session.query().hasComponent(second_removed));
        CHECK(observer.component_removed == 4);
    }

    SECTION("empty and stale inputs leave preview and history unchanged")
    {
        REQUIRE(history.beginScope("预览"));
        session.removeComponents({});
        session.removeComponents({ -1 });
        CHECK(history.scopeActive());
        CHECK(observer.component_removed == 0);
        history.cancelScope();
        CHECK_FALSE(history.canUndo());
    }

    SECTION("a pending write operation refuses the whole batch")
    {
        const auto operation = model.beginWriteOperation();
        REQUIRE(operation);
        REQUIRE_THROWS_AS(session.removeComponents({ first_removed, second_removed }), ModelOperationBusy);
        CHECK(session.query().hasComponent(first_removed));
        CHECK(session.query().hasComponent(second_removed));
        CHECK(observer.component_removed == 0);
        CHECK_FALSE(history.canUndo());
    }
}

TEST_CASE("FeatureSystem borrows the session IO system without taking ownership", "[session][feature][io]")
{
    int destroyed_count = 0;
    {
        Session session;
        auto* io_system = session.featureSystem().ioSystem();
        REQUIRE(io_system == &session.ioSystem());

        auto handler = std::make_unique<BorrowedIOHandler>(destroyed_count);
        REQUIRE(io_system->registerHandler({ "Session IO test", { "test" } },
            systems::io::ModelIOSystem::SystemHandlerPtr { handler.release() }));
        EventCounter counter;
        counter.subscribe(session.events());

        // 借用入口仍使用同一模型、undo 栈与通知链。
        REQUIRE(io_system->read("model.test", "Session IO test", { }));
        REQUIRE(session.query().listModels().size() == 1);
        CHECK(counter.added == 1);
        REQUIRE(session.undoStack().undo());
        CHECK(session.query().listModels().empty());
        CHECK(counter.removed == 1);

        // teardown 先销毁 FeatureSystem，IO 系统仍由 Session 持有。
        session.teardown();
        CHECK(destroyed_count == 0);
        CHECK(session.ioSystem().registeredFileTypeInfos().size() == 1);
    }
    CHECK(destroyed_count == 1);
}
