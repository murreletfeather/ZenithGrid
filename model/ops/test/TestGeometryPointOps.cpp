#include "GeometryData.h"
#include "GeometryTopologyEditor.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <sstream>

namespace {
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
    CHECK_THROWS(GeometryTopologyEditor::splitEdgeAtParameter(root, target, first));
    CHECK_THROWS(GeometryTopologyEditor::splitEdgeAtParameter(root, target, last));
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
