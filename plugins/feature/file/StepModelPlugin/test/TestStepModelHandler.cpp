/**
 * @file TestStepModelHandler.cpp
 * @brief STEP 模型文件处理器单元测试
 *
 * StepModelHandler 基于 OpenCASCADE 的 STEPControl_Reader/Writer，
 * 当前以组件化 GeometryData（封装 TopoDS_Shape）作为 Geometry 几何读写对象。
 * 测试模式参考 IGES 的测试：用 OCCT 在内存中构造 box/sphere，写出 STEP 再读回，
 * 验证读取到有效组件化几何；同时覆盖中文路径、重复实例与嵌套装配的世界坐标放置。
 */
#include "StepModelHandler.h"
#include "StepXdeComponentBuilder.h"
#include "ComponentData.h"
#include "GeometryData.h"
#include "ModelLayer.h"
#include "ModelPayload.h"
#include "TempFile.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static ComponentDatas makeGeometryModel(
    const TopoDS_Shape& shape,
    const std::string& componentName)
{
    ComponentDatas comps;
    auto component = std::make_unique<ComponentData>();
    component->id = -1;
    component->name = componentName;

    auto geometry = std::make_unique<GeometryData>();
    geometry->rootShape = std::make_unique<TopoDS_Shape>(shape);
    component->geometry = std::move(geometry);

    comps.push_back(std::move(component));
    return comps;
}

static std::vector<Index> addModelAndGetComponentIds(
    ModelLayer& layer,
    ComponentDatas comps)
{
    const Index modelId = layer.addModel("model", std::move(comps));
    REQUIRE(modelId >= 0);

    std::vector<Index> componentIds = layer.modelById(modelId)->componentIds();
    REQUIRE(!componentIds.empty());

    return componentIds;
}

static void requireReadableGeometryModel(const ModelPayload& payload)
{
    REQUIRE(!payload.components.empty());

    bool hasValidGeometry = false;
    for (const auto& component : payload.components) {
        if (component
            && component->geometry
            && component->geometry->rootShape
            && !component->geometry->rootShape->IsNull()) {
            REQUIRE(component->geometry->rootShape->ShapeType() == TopAbs_COMPOUND);
            for (TopoDS_Iterator it(*component->geometry->rootShape); it.More(); it.Next())
                REQUIRE(it.Value().ShapeType() != TopAbs_COMPOUND);
            hasValidGeometry = true;
            break;
        }
    }

    REQUIRE(hasValidGeometry);
}

/**
 * @brief 构造先绕原点旋转、再平移的实例变换。
 * @param axis 旋转轴方向。
 * @param angle 旋转角度，单位为弧度。
 * @param translation 旋转后的平移量。
 * @return 实例变换。
 */
static gp_Trsf makePlacement(const gp_Dir& axis, double angle, const gp_Vec& translation)
{
    gp_Trsf placement;
    placement.SetRotation(gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), axis), angle);
    placement.SetTranslationPart(translation);
    return placement;
}

/**
 * @brief 创建可注册装配和零件定义的 XDE 文档。
 * @return 已在 XCAF 应用中打开的文档。
 */
static Handle(TDocStd_Document) makeAssemblyDocument()
{
    Handle(TDocStd_Document) doc;
    XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", doc);
    return doc;
}

/**
 * @brief 将 XDE 装配写为临时 STEP 文件，再经插件的导入入口读回。
 * @param doc 待导出的 XDE 文档，写出后由此函数关闭。
 * @return 导入后的模型载荷。
 */
static ModelPayload readAssemblyDocument(const Handle(TDocStd_Document) & doc)
{
    const auto shape_tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    shape_tool->UpdateAssemblies();

    fs::path out = core::TempFile::instance().path();
    out.replace_extension(".stp");
    {
        STEPCAFControl_Writer writer;
        REQUIRE(writer.Transfer(doc, STEPControl_AsIs));
        std::ofstream stream(out);
        REQUIRE(stream.is_open());
        REQUIRE(writer.WriteStream(stream) == IFSelect_RetDone);
    }
    XCAFApp_Application::GetApplication()->Close(doc);

    systems::io::StepModelHandler io;
    auto payload = io.readModel(out);
    REQUIRE(payload.has_value());
    return std::move(*payload);
}

/**
 * @brief 逐层变换测试长方体顶点，独立计算预期世界坐标。
 * @param placements 按从零件到外层装配的顺序排列的变换。
 * @return 变换后的八个顶点。
 */
static std::vector<gp_Pnt> makePlacedBoxVertices(std::initializer_list<gp_Trsf> placements)
{
    std::vector<gp_Pnt> vertices;
    for (double x : { 0.0, 10.0 }) {
        for (double y : { 0.0, 20.0 }) {
            for (double z : { 0.0, 30.0 }) {
                gp_Pnt point(x, y, z);
                // 按内到外逐次变换，独立验证导入遍历的组合顺序。
                for (const auto& placement : placements) {
                    point.Transform(placement);
                }
                vertices.push_back(point);
            }
        }
    }
    return vertices;
}

/**
 * @brief 按 1e-6 的距离容差匹配两组顶点，不依赖顶点顺序。
 * @param actual 导入后的世界坐标顶点。
 * @param expected 预期的世界坐标顶点。
 * @return 两组顶点是否能一一匹配。
 */
static bool isSameVertexSet(const std::vector<gp_Pnt>& actual, const std::vector<gp_Pnt>& expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }

    std::vector<bool> matched(expected.size(), false);
    for (const auto& point : actual) {
        bool found = false;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!matched[i] && point.Distance(expected[i]) < 1e-6) {
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 验证每个导入组件都对应一个预期的长方体实例。
 * @param payload 导入后的模型载荷。
 * @param expected_vertices 各实例的预期世界坐标顶点集合。
 */
static void requirePlacedBoxes(const ModelPayload& payload,
    const std::vector<std::vector<gp_Pnt>>& expected_vertices)
{
    REQUIRE(payload.components.size() == expected_vertices.size());
    std::vector<bool> matched(expected_vertices.size(), false);
    for (const auto& component : payload.components) {
        REQUIRE(component);
        REQUIRE(component->geometry);
        REQUIRE(component->geometry->rootShape);
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> vertex_map;
        TopExp::MapShapes(*component->geometry->rootShape, TopAbs_VERTEX, vertex_map);
        std::vector<gp_Pnt> actual_vertices;
        for (int i = 1; i <= vertex_map.Extent(); ++i) {
            actual_vertices.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertex_map(i))));
        }

        // 不依赖 STEP 的组件或拓扑顺序；非对称长方体的完整顶点集同时约束位置和方向。
        bool found = false;
        for (std::size_t i = 0; i < expected_vertices.size(); ++i) {
            if (!matched[i] && isSameVertexSet(actual_vertices, expected_vertices[i])) {
                matched[i] = true;
                found = true;
                break;
            }
        }
        INFO("component: " << component->name);
        REQUIRE(found);
    }
}

TEST_CASE("StepModelHandler::readModel() preserves placements of repeated part instances", "[step][assembly]")
{
    const auto doc = makeAssemblyDocument();
    const auto shape_tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto part = shape_tool->AddShape(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0), false);
    const auto assembly = shape_tool->NewShape();
    const auto first = makePlacement(gp_Dir(0.0, 0.0, 1.0), std::numbers::pi / 3.0,
        gp_Vec(100.0, -40.0, 20.0));
    const auto second = makePlacement(gp_Dir(0.0, 1.0, 0.0), -std::numbers::pi / 4.0,
        gp_Vec(-50.0, 80.0, -30.0));
    shape_tool->AddComponent(assembly, part, TopLoc_Location(first));
    shape_tool->AddComponent(assembly, part, TopLoc_Location(second));

    const auto payload = readAssemblyDocument(doc);
    requirePlacedBoxes(payload, { makePlacedBoxVertices({ first }), makePlacedBoxVertices({ second }) });
}

TEST_CASE("StepModelHandler::readModel() composes nested assembly placements", "[step][assembly]")
{
    const auto doc = makeAssemblyDocument();
    const auto shape_tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto part = shape_tool->AddShape(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0), false);
    const auto subassembly = shape_tool->NewShape();
    const auto local = makePlacement(gp_Dir(1.0, 0.0, 0.0), std::numbers::pi / 6.0,
        gp_Vec(15.0, 25.0, -10.0));
    shape_tool->AddComponent(subassembly, part, TopLoc_Location(local));

    const auto assembly = shape_tool->NewShape();
    const auto first_parent = makePlacement(gp_Dir(0.0, 0.0, 1.0), std::numbers::pi / 2.0,
        gp_Vec(120.0, -70.0, 40.0));
    const auto second_parent = makePlacement(gp_Dir(0.0, 1.0, 0.0), -std::numbers::pi / 3.0,
        gp_Vec(-90.0, 60.0, 100.0));
    shape_tool->AddComponent(assembly, subassembly, TopLoc_Location(first_parent));
    shape_tool->AddComponent(assembly, subassembly, TopLoc_Location(second_parent));

    const auto payload = readAssemblyDocument(doc);
    requirePlacedBoxes(payload, { makePlacedBoxVertices({ local, first_parent }), makePlacedBoxVertices({ local, second_parent }) });
}

TEST_CASE("StepXdeComponentBuilder preserves located assembly definitions", "[step][assembly]")
{
    const auto doc = makeAssemblyDocument();
    const auto shape_tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    const auto part = shape_tool->AddShape(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0), false);
    const auto subassembly = shape_tool->NewShape();
    const auto local = makePlacement(gp_Dir(1.0, 0.0, 0.0), std::numbers::pi / 6.0,
        gp_Vec(15.0, 25.0, -10.0));
    shape_tool->AddComponent(subassembly, part, TopLoc_Location(local));
    const auto assembly = shape_tool->NewShape();
    const auto instance = makePlacement(gp_Dir(0.0, 1.0, 0.0), -std::numbers::pi / 3.0,
        gp_Vec(-90.0, 60.0, 100.0));
    const auto reference = shape_tool->AddComponent(assembly, subassembly, TopLoc_Location(instance));
    shape_tool->UpdateAssemblies();

    // 定义本身也可带位置；直接构造 XDE，避免 STEP 写出时将其规范化到实例上。
    const auto definition = makePlacement(gp_Dir(0.0, 0.0, 1.0), std::numbers::pi / 4.0,
        gp_Vec(30.0, -20.0, 50.0));
    shape_tool->SetShape(subassembly,
        shape_tool->GetShape(subassembly).Moved(TopLoc_Location(definition)));
    REQUIRE(shape_tool->IsAssembly(subassembly));
    gp_Pnt expected_origin(0.0, 0.0, 0.0);
    expected_origin.Transform(definition);
    expected_origin.Transform(instance);
    const gp_Pnt actual_origin = gp_Pnt(0.0, 0.0, 0.0).Transformed(
        shape_tool->GetShape(reference).Location().Transformation());
    CHECK(actual_origin.Distance(expected_origin) < 1e-6);

    const auto payload = systems::io::StepXdeComponentBuilder::buildModelData(*doc, u8"located_assembly");
    REQUIRE(payload.has_value());
    requirePlacedBoxes(*payload, { makePlacedBoxVertices({ local, definition, instance }) });
    XCAFApp_Application::GetApplication()->Close(doc);
}

TEST_CASE("CAD export topology copies share geometry and isolate BRep discretization")
{
    const TopoDS_Shape source = BRepPrimAPI_MakeSphere(10.0);
    BRepMesh_IncrementalMesh mesh(source, 0.2);
    REQUIRE(mesh.IsDone());
    const TopoDS_Shape copy = BRepBuilderAPI_Copy(source, false, false).Shape();
    CHECK_FALSE(copy.IsSame(source));
    TopExp_Explorer source_faces(source, TopAbs_FACE), copy_faces(copy, TopAbs_FACE);
    REQUIRE(source_faces.More());
    REQUIRE(copy_faces.More());
    const auto source_face = TopoDS::Face(source_faces.Current());
    const auto copy_face = TopoDS::Face(copy_faces.Current());
    CHECK_FALSE(copy_face.IsSame(source_face));
    TopLoc_Location source_location, copy_location;
    CHECK(BRep_Tool::Surface(source_face, source_location) == BRep_Tool::Surface(copy_face, copy_location));
    const auto source_mesh = BRep_Tool::Triangulation(source_face, source_location);
    REQUIRE_FALSE(source_mesh.IsNull());
    CHECK(BRep_Tool::Triangulation(copy_face, copy_location).IsNull());
    TopExp_Explorer source_edges(source_face, TopAbs_EDGE), copy_edges(copy_face, TopAbs_EDGE);
    while (source_edges.More() && BRep_Tool::Degenerated(TopoDS::Edge(source_edges.Current()))) {
        REQUIRE(copy_edges.More());
        source_edges.Next();
        copy_edges.Next();
    }
    REQUIRE(source_edges.More());
    REQUIRE(copy_edges.More());
    const auto source_edge = TopoDS::Edge(source_edges.Current());
    const auto copy_edge = TopoDS::Edge(copy_edges.Current());
    CHECK_FALSE(copy_edge.IsSame(source_edge));
    double source_first, source_last, copy_first, copy_last;
    REQUIRE_FALSE(BRep_Tool::Curve(source_edge, source_location, source_first, source_last).IsNull());
    CHECK(BRep_Tool::Curve(source_edge, source_location, source_first, source_last)
        == BRep_Tool::Curve(copy_edge, copy_location, copy_first, copy_last));
    CHECK(BRep_Tool::CurveOnSurface(source_edge, source_face, source_first, source_last)
        == BRep_Tool::CurveOnSurface(copy_edge, copy_face, copy_first, copy_last));
    CHECK(source_first == copy_first);
    CHECK(source_last == copy_last);
    BRepMesh_IncrementalMesh copy_mesh(copy, 0.5);
    REQUIRE(copy_mesh.IsDone());
    CHECK_FALSE(BRep_Tool::Triangulation(copy_face, copy_location).IsNull());
    CHECK(BRep_Tool::Triangulation(copy_face, copy_location) != source_mesh);
    BRepTools::Clean(copy);
    CHECK(BRep_Tool::Triangulation(copy_face, copy_location).IsNull());
    CHECK(BRep_Tool::Triangulation(source_face, source_location) == source_mesh);
}

TEST_CASE("StepModelHandler::writeComponents()/readModel() - English path (box)")
{
    systems::io::StepModelHandler io;
    ModelLayer layer;
    fs::path out = core::TempFile::instance().path().string() + ".stp";

    TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0);
    REQUIRE(!box.IsNull());

    std::vector<Index> componentIds = addModelAndGetComponentIds(
        layer,
        makeGeometryModel(box, "Box"));

    REQUIRE_NOTHROW(io.writeComponents(layer, componentIds, out));
    REQUIRE(fs::exists(out));

    std::optional<ModelPayload> payload;
    REQUIRE_NOTHROW(payload = io.readModel(out));
    REQUIRE(payload.has_value());

    requireReadableGeometryModel(*payload);

    // model_name 应被设置为文件名
    REQUIRE(payload->model_name == out.filename().u8string());
}

TEST_CASE("StepModelHandler::writeComponents()/readModel() - Chinese filename")
{
    // 验证中文路径支持（pathUtf8()）
    systems::io::StepModelHandler io;
    ModelLayer layer;

    fs::path out = core::TempFile::instance().path();
    out.replace_filename("中文_测试_" + out.stem().string() + ".stp");

    TopoDS_Shape box = BRepPrimAPI_MakeBox(15.0, 25.0, 35.0);
    REQUIRE(!box.IsNull());

    std::vector<Index> componentIds = addModelAndGetComponentIds(
        layer,
        makeGeometryModel(box, "中文立方体"));

    REQUIRE_NOTHROW(io.writeComponents(layer, componentIds, out));
    REQUIRE(fs::exists(out));

    std::optional<ModelPayload> payload;
    REQUIRE_NOTHROW(payload = io.readModel(out));
    REQUIRE(payload.has_value());

    requireReadableGeometryModel(*payload);
}

TEST_CASE("StepModelHandler::writeComponents()/readModel() - Chinese full path")
{
    systems::io::StepModelHandler io;
    ModelLayer layer;

    fs::path out = core::TempFile::instance().path();
    out = out.parent_path() / "测试数据目录" / "子目录_模型"
        / ("中文_" + out.stem().string() + ".stp");
    fs::create_directories(out.parent_path());

    TopoDS_Shape box = BRepPrimAPI_MakeBox(12.0, 22.0, 32.0);
    REQUIRE(!box.IsNull());

    std::vector<Index> componentIds = addModelAndGetComponentIds(
        layer,
        makeGeometryModel(box, "完整路径立方体"));

    REQUIRE_NOTHROW(io.writeComponents(layer, componentIds, out));
    REQUIRE(fs::exists(out));

    std::optional<ModelPayload> payload;
    REQUIRE_NOTHROW(payload = io.readModel(out));
    REQUIRE(payload.has_value());

    requireReadableGeometryModel(*payload);
}

TEST_CASE("StepModelHandler::writeComponents()/readModel() - sphere")
{
    systems::io::StepModelHandler io;
    ModelLayer layer;
    fs::path out = core::TempFile::instance().path().string() + "_sphere.stp";

    TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(50.0);
    REQUIRE(!sphere.IsNull());
    GProp_GProps expected;
    BRepGProp::VolumeProperties(sphere, expected);
    BRepMesh_IncrementalMesh mesh(sphere, 0.5);
    const auto face = TopoDS::Face(TopExp_Explorer(sphere, TopAbs_FACE).Current());
    TopLoc_Location location;
    const auto triangulation = BRep_Tool::Triangulation(face, location);
    REQUIRE_FALSE(triangulation.IsNull());

    std::vector<Index> componentIds = addModelAndGetComponentIds(
        layer,
        makeGeometryModel(sphere, "Sphere"));

    REQUIRE_NOTHROW(io.writeComponents(layer, componentIds, out));
    REQUIRE(fs::exists(out));

    CHECK(BRep_Tool::Triangulation(face, location) == triangulation);
    GProp_GProps source_after;
    BRepGProp::VolumeProperties(sphere, source_after);
    CHECK(source_after.Mass() == Catch::Approx(expected.Mass()).epsilon(1e-12));

    std::optional<ModelPayload> payload;
    REQUIRE_NOTHROW(payload = io.readModel(out));
    REQUIRE(payload.has_value());

    requireReadableGeometryModel(*payload);

    double volume = 0.0;
    for (const auto& component : payload->components) {
        GProp_GProps actual;
        BRepGProp::VolumeProperties(*component->geometry->rootShape, actual);
        volume += actual.Mass();
    }
    CHECK(volume == Catch::Approx(expected.Mass()).epsilon(1e-6));
}

TEST_CASE("StepModelHandler::readModel() - non-existent file")
{
    systems::io::StepModelHandler io;
    fs::path bad = core::TempFile::instance().path().string() + "_not_exist.stp";

    // 文件不存在，readModel 应返回 nullopt（而不是崩溃）
    std::optional<ModelPayload> model;
    REQUIRE_NOTHROW(model = io.readModel(bad));
    REQUIRE(!model.has_value());
}
