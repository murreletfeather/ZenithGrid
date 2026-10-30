#include "GeometryBuilder.h"
#include "GeometryData.h"
#include "GeometryPointOps.h"
#include "GeometryRegistry.h"
#include "GeometryTopologyEditor.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Curve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Surface.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
/** @brief 保留面上的 pcurve，将原三维边偏离平面以模拟合法的历史 CAD 表示误差。 */
struct TolerantPointTestBoundary {
    TopoDS_Face face = TopoDS::Face(GeometryBuilder::makeRectangleFace(0, 0, 0, 10, 10, CoordinatePlane::XY));
    TopoDS_Edge edge;

    /** @brief 固定原边与面之间的合法表示误差，用于验证后续拓扑拆分。 */
    TolerantPointTestBoundary()
    {
        for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next()) {
            const auto candidate = TopoDS::Edge(explorer.Current());
            double first = 0.0, last = 0.0;
            const auto curve = BRep_Tool::Curve(candidate, first, last);
            if (std::abs(curve->Value((first + last) * 0.5).Y()) < 1.0e-8) {
                edge = candidate;
                break;
            }
        }
        REQUIRE_FALSE(edge.IsNull());
        double first = 0.0, last = 0.0, pc_first = 0.0, pc_last = 0.0;
        const auto curve = BRep_Tool::Curve(edge, first, last);
        const auto pcurve = BRep_Tool::CurveOnSurface(edge, face, pc_first, pc_last);
        BRep_Builder builder;
        builder.UpdateEdge(edge, pcurve, face, 0.03);
        builder.UpdateEdge(edge, occ::down_cast<Geom_Curve>(curve->Translated(gp_Vec(0, 0, -0.02))), 0.03);
        for (TopExp_Explorer explorer(edge, TopAbs_VERTEX); explorer.More(); explorer.Next())
            builder.UpdateVertex(TopoDS::Vertex(explorer.Current()), 0.03);
    }
};

/** @brief 按组件根约束包装测试形状，不改变子形状拓扑身份。 */
TopoDS_Shape makePointTestRoot(const TopoDS_Shape& shape)
{
    GeometryData geometry;
    geometry.setRootShape(shape);
    return *geometry.rootShape;
}

/** @brief 统计唯一子形状，避免共享边和点被重复计数。 */
int countPointTestShapes(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> shapes;
    TopExp::MapShapes(shape, type, shapes);
    return shapes.Extent();
}

/** @brief 序列化操作前像，检测曲线、pcurve、点参数或容差的原地污染。 */
std::string serializePointTestShape(const TopoDS_Shape& shape)
{
    std::ostringstream output;
    BRepTools::Write(shape, output);
    return output.str();
}

/** @brief 根据精确 UV 创建有效面点载荷，模拟渲染层完成约束后的结果。 */
GeometryPoint makePointTestFacePoint(const TopoDS_Face& face, Index face_id, double u, double v)
{
    const auto position = BRep_Tool::Surface(face)->Value(u, v);
    GeometryPoint point;
    point.component_id = 7;
    point.support_kind = GeometryPointKind::Face;
    point.support_id = face_id;
    point.face_uv = { u, v };
    point.world_pos = { position.X(), position.Y(), position.Z() };
    point.valid = true;
    return point;
}

/** @brief 从箱体中取得全部顶点均位于 z=0 的底面。 */
TopoDS_Face findPointTestBottomFace(const TopoDS_Shape& root)
{
    for (TopExp_Explorer face(root, TopAbs_FACE); face.More(); face.Next()) {
        bool bottom = true;
        for (TopExp_Explorer vertex(face.Current(), TopAbs_VERTEX); vertex.More(); vertex.Next())
            bottom = bottom && std::abs(BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())).Z()) < 1.0e-7;
        if (bottom)
            return TopoDS::Face(face.Current());
    }
    return { };
}

/** @brief 用三维点创建面参数载荷，检查投影器接受真实的面内或边界点。 */
GeometryPoint projectPointTestFacePoint(const TopoDS_Face& face, Index face_id, const std::array<double, 3>& position)
{
    const auto point = GeometryPointOps::projectOnFace(face, 7, face_id, position);
    if (!point)
        throw std::runtime_error("Test point does not project onto the face");
    return *point;
}

/** @brief 找到贴合给定点的目标面边，添加精确边吸附载荷。 */
void snapPointTestToBoundary(GeometryPoint& point, const TopoDS_Face& face, GeometryRegistry& registry)
{
    for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto edge = TopoDS::Edge(explorer.Current());
        const auto projected = GeometryPointOps::projectOnEdge(edge, 7, -1, point.world_pos);
        if (!projected)
            continue;
        if (gp_Pnt(projected->world_pos[0], projected->world_pos[1], projected->world_pos[2])
                .Distance(gp_Pnt(point.world_pos[0], point.world_pos[1], point.world_pos[2]))
            < 1.0e-7) {
            point.snap_kind = GeometryPointKind::Edge;
            point.snap_id = registry.allocEdge(edge);
            point.snap_edge_parameter = projected->edge_parameter;
            return;
        }
    }
    throw std::runtime_error("Test boundary point has no supporting edge");
}

/** @brief 对结果的每个面核验所有边的二维和三维曲线表示。 */
void validatePointTestPCurves(const TopoDS_Shape& root)
{
    for (TopExp_Explorer face(root, TopAbs_FACE); face.More(); face.Next()) {
        for (TopExp_Explorer edge(face.Current(), TopAbs_EDGE); edge.More(); edge.Next()) {
            const auto current = TopoDS::Edge(edge.Current());
            if (BRep_Tool::Degenerated(current))
                continue;
            REQUIRE(BRep_Tool::SameParameter(current));
            REQUIRE(BRep_Tool::SameRange(current));
            BRepLib_CheckCurveOnSurface check(current, TopoDS::Face(face.Current()));
            check.Perform();
            REQUIRE(check.IsDone());
            REQUIRE(check.MaxDistance() <= BRep_Tool::Tolerance(current) * 2.0 + 1.0e-7);
        }
    }
}
}

TEST_CASE("GeometryPointOps queries preserve support identity and reject misses", "[GeometryPointOps]")
{
    const auto face = TopoDS::Face(GeometryBuilder::makeRectangleFace(0, 0, 0, 10, 10, CoordinatePlane::XY));
    const auto before = serializePointTestShape(face);
    const auto hit = GeometryPointOps::intersectFace(face, 7, 12, { 5, 5, 10 }, { 0, 0, -1 });
    REQUIRE(hit.has_value());
    CHECK(hit->valid);
    CHECK(hit->component_id == 7);
    CHECK(hit->support_id == 12);
    CHECK(hit->support_kind == GeometryPointKind::Face);
    CHECK(hit->world_pos[2] == Catch::Approx(0).margin(1.0e-7));
    CHECK_FALSE(GeometryPointOps::intersectFace(face, 7, 12, { 5, 5, 10 }, { 0, 0, 1 }));
    CHECK_FALSE(GeometryPointOps::intersectFace(face, 7, 12, { 20, 5, 10 }, { 0, 0, -1 }));
    CHECK_FALSE(GeometryPointOps::intersectFace(face, 7, 12, { 5, 5, 10 }, { 0, 0, 0 }));
    const auto projected = GeometryPointOps::projectOnFace(face, 7, 12, { 2, 3, 5 });
    REQUIRE(projected.has_value());
    const auto position = BRep_Tool::Surface(face)->Value(projected->face_uv[0], projected->face_uv[1]);
    CHECK(position.Distance(gp_Pnt(2, 3, 0)) < 1.0e-7);
    CHECK_FALSE(GeometryPointOps::projectOnFace(face, 7, 12, { 20, 3, 5 }));
    const auto edge = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0)).Edge();
    const auto endpoint = GeometryPointOps::projectOnEdge(edge, 7, 13, { 15, 2, 0 });
    REQUIRE(endpoint.has_value());
    CHECK(endpoint->support_kind == GeometryPointKind::Edge);
    CHECK(endpoint->support_id == 13);
    CHECK(endpoint->world_pos[0] == Catch::Approx(10));
    CHECK(endpoint->edge_parameter == Catch::Approx(10));
    CHECK(serializePointTestShape(face) == before);
}

TEST_CASE("GeometryTopologyEditor reuses a high tolerance boundary endpoint when vertex snapping is disabled", "[GeometryPointOps]")
{
    TolerantPointTestBoundary boundary;
    double first = 0.0, last = 0.0;
    const auto curve = BRep_Tool::Curve(boundary.edge, first, last);
    TopoDS_Vertex endpoint;
    for (TopExp_Explorer explorer(boundary.edge, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const auto vertex = TopoDS::Vertex(explorer.Current());
        if (BRep_Tool::Pnt(vertex).Distance(curve->Value(first)) <= 0.03)
            endpoint = vertex;
    }
    REQUIRE_FALSE(endpoint.IsNull());
    BRep_Builder builder;
    builder.UpdateVertex(endpoint, curve->Value(first), 0.03);
    const auto root = makePointTestRoot(boundary.face);
    REQUIRE(BRepCheck_Analyzer(root).IsValid());
    GeometryRegistry registry;
    const auto face_id = registry.allocFace(boundary.face);
    const auto before = serializePointTestShape(root);
    const auto start = GeometryPointOps::projectEdgePointOnFace(boundary.face, boundary.edge, 7, face_id, registry.allocEdge(boundary.edge), first);
    REQUIRE(start.has_value());
    auto end = projectPointTestFacePoint(boundary.face, face_id, { 5, 10, 0 });
    snapPointTestToBoundary(end, boundary.face, registry);
    const auto result = GeometryTopologyEditor::splitFaceByPoints(root, boundary.face, { *start, end }, registry);
    REQUIRE(BRepCheck_Analyzer(result).IsValid());
    CHECK(countPointTestShapes(result, TopAbs_FACE) == 2);
    CHECK(countPointTestShapes(result, TopAbs_VERTEX) == 5);
    CHECK(serializePointTestShape(root) == before);
}

TEST_CASE("GeometryTopologyEditor freezes the original continuous boundary error before trimming", "[GeometryPointOps]")
{
    TolerantPointTestBoundary boundary;
    double first = 0.0, last = 0.0;
    const auto original = BRep_Tool::Curve(boundary.edge, first, last);
    TColgp_Array1OfPnt poles(1, 4);
    TColStd_Array1OfReal knots(1, 4);
    TColStd_Array1OfInteger multiplicities(1, 4);
    const std::array<double, 4> fractions { 0.0, 0.37, 0.4, 1.0 };
    const std::array<double, 4> gaps { 0.0, -0.03000001, -0.02, 0.0 };
    for (int index = 1; index <= 4; ++index) {
        knots(index) = first + (last - first) * fractions[index - 1];
        auto position = original->Value(knots(index));
        position.SetZ(gaps[index - 1]);
        poles(index) = position;
        multiplicities(index) = index == 1 || index == 4 ? 2 : 1;
    }
    BRep_Builder builder;
    builder.UpdateEdge(boundary.edge, new Geom_BSplineCurve(poles, knots, multiplicities, 1), 0.03);
    const auto root = makePointTestRoot(boundary.face);
    REQUIRE(BRepCheck_Analyzer(root).IsValid());
    GeometryRegistry registry;
    const auto face_id = registry.allocFace(boundary.face);
    const auto start = GeometryPointOps::projectEdgePointOnFace(boundary.face, boundary.edge, 7, face_id, registry.allocEdge(boundary.edge), (first + last) * 0.5);
    REQUIRE(start.has_value());
    auto end = projectPointTestFacePoint(boundary.face, face_id, { 5, 10, 0 });
    snapPointTestToBoundary(end, boundary.face, registry);
    const auto before = serializePointTestShape(root);
    const auto result = GeometryTopologyEditor::splitFaceByPoints(root, boundary.face, { *start, end }, registry);
    REQUIRE(BRepCheck_Analyzer(result).IsValid());
    CHECK(countPointTestShapes(result, TopAbs_FACE) == 2);
    double boundary_tolerance = 0.0;
    for (TopExp_Explorer explorer(result, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto edge = TopoDS::Edge(explorer.Current());
        double edge_first = 0.0, edge_last = 0.0;
        const auto curve = BRep_Tool::Curve(edge, edge_first, edge_last);
        if (!curve.IsNull() && std::abs(curve->Value((edge_first + edge_last) * 0.5).Y()) < 1.0e-8)
            boundary_tolerance = std::max(boundary_tolerance, BRep_Tool::Tolerance(edge));
    }
    CHECK(boundary_tolerance >= 0.03000001);
    CHECK(boundary_tolerance < 0.030001);
    // 继承仅作用于新拆分点，结果中的原角点也不能被扩大容差。
    for (TopExp_Explorer original_vertex(root, TopAbs_VERTEX); original_vertex.More(); original_vertex.Next()) {
        const auto vertex = TopoDS::Vertex(original_vertex.Current());
        bool found = false;
        for (TopExp_Explorer result_vertex(result, TopAbs_VERTEX); result_vertex.More(); result_vertex.Next()) {
            const auto candidate = TopoDS::Vertex(result_vertex.Current());
            if (BRep_Tool::Pnt(vertex).Distance(BRep_Tool::Pnt(candidate)) < 1.0e-8) {
                CHECK(BRep_Tool::Tolerance(candidate) == BRep_Tool::Tolerance(vertex));
                found = true;
            }
        }
        CHECK(found);
    }
    // 相同面点伪造为另一个三维边参数，仍必须拒绝且不能污染输入。
    auto inconsistent = *start;
    inconsistent.snap_edge_parameter = first + (last - first) * 0.75;
    CHECK_THROWS_AS(GeometryTopologyEditor::splitFaceByPoints(root, boundary.face, { inconsistent, end }, registry), std::invalid_argument);
    CHECK(serializePointTestShape(root) == before);
}

TEST_CASE("GeometryTopologyEditor splits shared cylinder edges and preserves all pcurves", "[GeometryPointOps]")
{
    bool circular = true;
    SECTION("circular rim shared by cap and side") { }
    SECTION("periodic seam with two pcurves on one face") { circular = false; }
    const auto root = makePointTestRoot(BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape());
    TopoDS_Edge target;
    for (TopExp_Explorer explorer(root, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto edge = TopoDS::Edge(explorer.Current());
        const BRepAdaptor_Curve curve(edge);
        if ((circular && curve.GetType() == GeomAbs_Circle && std::abs(curve.Value(curve.FirstParameter()).Z() - 10.0) < 1e-7)
            || (!circular && curve.GetType() == GeomAbs_Line)) {
            target = edge;
            break;
        }
    }
    REQUIRE_FALSE(target.IsNull());
    double first = 0.0, last = 0.0;
    BRep_Tool::Range(target, first, last);
    const auto before = serializePointTestShape(root);
    const auto result = GeometryTopologyEditor::splitEdgeAtParameter(root, target, first + (last - first) * 0.3);
    REQUIRE(BRepCheck_Analyzer(result).IsValid());
    CHECK(countPointTestShapes(result, TopAbs_SOLID) == 1);
    CHECK(countPointTestShapes(result, TopAbs_FACE) == 3);
    CHECK(countPointTestShapes(result, TopAbs_EDGE) == countPointTestShapes(root, TopAbs_EDGE) + 1);
    validatePointTestPCurves(result);
    CHECK(serializePointTestShape(root) == before);
    GProp_GProps volume;
    BRepGProp::VolumeProperties(result, volume);
    CHECK(volume.Mass() == Catch::Approx(std::acos(-1.0) * 250).epsilon(1.0e-7));
}

TEST_CASE("GeometryTopologyEditor cuts a solid face using boundary points and an internal point", "[GeometryPointOps]")
{
    const auto root = makePointTestRoot(GeometryBuilder::makeBox(0, 0, 0, 10, 20, 30));
    const auto face = findPointTestBottomFace(root);
    REQUIRE_FALSE(face.IsNull());
    GeometryRegistry registry;
    const auto face_id = registry.allocFace(face);
    auto start = projectPointTestFacePoint(face, face_id, { 5, 0, 0 });
    const auto middle = projectPointTestFacePoint(face, face_id, { 3, 10, 0 });
    auto end = projectPointTestFacePoint(face, face_id, { 5, 20, 0 });
    snapPointTestToBoundary(start, face, registry);
    snapPointTestToBoundary(end, face, registry);
    const auto before = serializePointTestShape(root);
    const auto result = GeometryTopologyEditor::splitFaceByPoints(root, face, { start, middle, end }, registry);
    REQUIRE(BRepCheck_Analyzer(result).IsValid());
    CHECK(countPointTestShapes(result, TopAbs_FACE) == 7);
    CHECK(countPointTestShapes(result, TopAbs_SOLID) == 1);
    validatePointTestPCurves(result);
    CHECK(serializePointTestShape(root) == before);
    GProp_GProps volume;
    BRepGProp::VolumeProperties(result, volume);
    CHECK(volume.Mass() == Catch::Approx(6000.0).epsilon(1.0e-7));
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> owners;
    TopExp::MapShapesAndUniqueAncestors(result, TopAbs_VERTEX, TopAbs_FACE, owners);
    int connected_boundary_points = 0;
    for (int index = 1; index <= owners.Extent(); ++index) {
        const auto point = BRep_Tool::Pnt(TopoDS::Vertex(owners.FindKey(index)));
        if (std::abs(point.X() - 5) < 1.0e-7 && std::abs(point.Z()) < 1.0e-7
            && (std::abs(point.Y()) < 1.0e-7 || std::abs(point.Y() - 20) < 1.0e-7)) {
            CHECK(owners.FindFromIndex(index).Extent() == 3);
            ++connected_boundary_points;
        }
    }
    CHECK(connected_boundary_points == 2);
}

TEST_CASE("GeometryTopologyEditor preserves shared vertices for point paths and existing polylines", "[GeometryPointOps]")
{
    const auto face = TopoDS::Face(GeometryBuilder::makeRectangleFace(0, 0, 0, 10, 10, CoordinatePlane::XY));
    const auto root = makePointTestRoot(face);
    GeometryRegistry registry;
    const auto face_id = registry.allocFace(face);
    auto start = projectPointTestFacePoint(face, face_id, { 0, 0, 0 });
    auto end = projectPointTestFacePoint(face, face_id, { 10, 10, 0 });
    for (TopExp_Explorer explorer(face, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        const auto vertex = TopoDS::Vertex(explorer.Current());
        const auto position = BRep_Tool::Pnt(vertex);
        for (auto* point : { &start, &end }) {
            if (position.Distance(gp_Pnt(point->world_pos[0], point->world_pos[1], point->world_pos[2])) < 1.0e-7) {
                point->snap_kind = GeometryPointKind::Vertex;
                point->snap_id = registry.allocVertex(vertex);
            }
        }
    }
    REQUIRE(start.snap_kind == GeometryPointKind::Vertex);
    REQUIRE(end.snap_kind == GeometryPointKind::Vertex);
    const auto before = serializePointTestShape(root);
    TopoDS_Shape result;
    SECTION("ordered point path")
    {
        result = GeometryTopologyEditor::splitFaceByPoints(root, face,
            { start, projectPointTestFacePoint(face, face_id, { 4, 6, 0 }), end }, registry);
    }
    SECTION("existing polyline with shared middle vertex")
    {
        const auto* first_vertex = registry.getVertex(start.snap_id);
        const auto* last_vertex = registry.getVertex(end.snap_id);
        REQUIRE(first_vertex);
        REQUIRE(last_vertex);
        const TopoDS_Vertex middle = BRepBuilderAPI_MakeVertex(gp_Pnt(4, 6, 0));
        const auto first = TopoDS::Edge(GeometryBuilder::makeLine(TopoDS::Vertex(*first_vertex), middle));
        const auto second = TopoDS::Edge(GeometryBuilder::makeLine(middle, TopoDS::Vertex(*last_vertex)));
        result = GeometryTopologyEditor::splitFace(root, face, { first, second });
    }
    REQUIRE(BRepCheck_Analyzer(result).IsValid());
    CHECK(countPointTestShapes(result, TopAbs_FACE) == 2);
    CHECK(countPointTestShapes(result, TopAbs_VERTEX) == 5);
    CHECK(serializePointTestShape(root) == before);
}

TEST_CASE("GeometryTopologyEditor splits the full cylinder face across its periodic seam", "[GeometryPointOps]")
{
    const double pi = std::acos(-1.0);
    const occ::handle<Geom_CylindricalSurface> surface = new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 5.0);
    const auto face = BRepBuilderAPI_MakeFace(surface, 0.0, 2 * pi, 0.0, 10.0, 1.0e-7).Face();
    const auto root = makePointTestRoot(face);
    GeometryRegistry registry;
    const auto face_id = registry.allocFace(face);
    auto start = makePointTestFacePoint(face, face_id, 2 * pi - 0.2, 0.0);
    const auto middle = makePointTestFacePoint(face, face_id, 0.1, 5.0);
    auto end = makePointTestFacePoint(face, face_id, 0.3, 10.0);
    // 同一周期路径同时核验预览落在真实曲面上，并沿短分支跨过周期缝。
    const auto sampled = GeometryPointOps::sampleSurfacePath(face, { start, middle });
    REQUIRE(sampled.size() > 2);
    for (const auto& point : sampled) {
        CHECK(std::hypot(point[0], point[1]) == Catch::Approx(5.0).margin(1.0e-7));
        CHECK(point[0] > 4.8);
    }
    CHECK(GeometryPointOps::sampleSurfacePath(face, { start }).size() == 1);
    snapPointTestToBoundary(start, face, registry);
    snapPointTestToBoundary(end, face, registry);
    SECTION("canonical periodic coordinates") { }
    SECTION("equivalent coordinates outside the face parameter interval")
    {
        // 周期等价坐标仍表示同一条边界到边界路径，分类必须先归回修剪域。
        start.face_uv[0] += 2 * pi;
        end.face_uv[0] -= 2 * pi;
        start.snap_kind = end.snap_kind = GeometryPointKind::None;
        start.snap_id = end.snap_id = -1;
    }
    const auto before = serializePointTestShape(root);
    const auto result = GeometryTopologyEditor::splitFaceByPoints(root, face, { start, middle, end }, registry);
    REQUIRE(BRepCheck_Analyzer(result).IsValid());
    // 切线穿过已有周期缝，参数域两侧的小块分别保留，因此实际形成三块拓扑面。
    CHECK(countPointTestShapes(result, TopAbs_FACE) == 3);
    validatePointTestPCurves(result);
    CHECK(serializePointTestShape(root) == before);
    GProp_GProps area;
    BRepGProp::SurfaceProperties(result, area);
    CHECK(area.Mass() == Catch::Approx(100 * pi).epsilon(1.0e-6));
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> owners;
    TopExp::MapShapesAndUniqueAncestors(result, TopAbs_VERTEX, TopAbs_FACE, owners);
    int seam_crossings = 0;
    for (int index = 1; index <= owners.Extent(); ++index) {
        const auto position = BRep_Tool::Pnt(TopoDS::Vertex(owners.FindKey(index)));
        if (position.Distance(gp_Pnt(5, 0, 10.0 / 3.0)) < 1.0e-7) {
            CHECK(owners.FindFromIndex(index).Extent() == 3);
            ++seam_crossings;
        }
    }
    CHECK(seam_crossings == 1);
}

TEST_CASE("GeometryTopologyEditor rejects invalid point paths without changing geometry", "[GeometryPointOps]")
{
    const auto face = TopoDS::Face(GeometryBuilder::makeRectangleFace(0, 0, 0, 10, 10, CoordinatePlane::XY));
    const auto root = makePointTestRoot(face);
    GeometryRegistry registry;
    const auto face_id = registry.allocFace(face);
    const auto point = [&](double x, double y) { return projectPointTestFacePoint(face, face_id, { x, y, 0 }); };
    const auto before = serializePointTestShape(root);
    REQUIRE_THROWS_AS(GeometryTopologyEditor::splitFaceByPoints(root, face, { point(2, 2), point(8, 8) }, registry), std::invalid_argument);
    REQUIRE_THROWS_AS(GeometryTopologyEditor::splitFaceByPoints(root, face, { point(0, 5), point(5, 5), point(5, 5), point(10, 5) }, registry), std::invalid_argument);
    REQUIRE_THROWS_AS(GeometryTopologyEditor::splitFaceByPoints(root, face, { point(0, 2), point(8, 8), point(2, 8), point(10, 2) }, registry), std::invalid_argument);
    auto stale = point(0, 5);
    stale.snap_kind = GeometryPointKind::Vertex;
    stale.snap_id = 1000;
    REQUIRE_THROWS_AS(GeometryTopologyEditor::splitFaceByPoints(root, face, { stale, point(10, 5) }, registry), std::invalid_argument);
    CHECK(serializePointTestShape(root) == before);
}
