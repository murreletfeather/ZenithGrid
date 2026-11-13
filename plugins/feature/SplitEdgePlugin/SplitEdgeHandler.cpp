#include "SplitEdgeHandler.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "FeatureContext.h"
#include "FeatureParams.h"
#include "FeatureRegistrar.h"
#include "GeometryData.h"
#include "GeometryTopologyEditor.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <algorithm>
#include <cmath>
#include <exception>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace systems::feature {
void SplitEdgeHandler::setup(FeatureRegistrar& reg, FeatureContext&)
{
    reg.addParameter({ ArgTypeEnum::Selector, "分割点", "GeometryPoint", "悬停任意几何边预览分割点，单击确认分割位置；点击新位置替换原分割点，点击执行才分割边。已有端点不能作为分割点。" });
    reg.addNavigationMenu({ "geometry", "几何", 50 });
    reg.addNavigationGroup({ "geometry", "topology", "拓扑", 20 });
    reg.navigation().addEntry({ "SplitEdge", "分割边", "qrc:/images/toolbar/Geometry/split-edge.svg", 0, "geometry", "topology" });
}

std::any SplitEdgeHandler::execute(FeatureContext& ctx)
{
    const auto* selection = ctx.params.value(0).get<ArgTypeEnum::Selector>();
    const auto* points = selection && *selection ? (*selection)->get<ElementEnum::GeometryPoint>() : nullptr;
    if (!points || points->size() != 1)
        return std::string("请在需要分割的几何边上选择一个分割点。");
    // 复制输入：拓扑写回的模型通知可能同步清空原参数。
    const GeometryPoint point = points->front();
    if (!point.valid || point.support_kind != GeometryPointKind::Edge)
        return std::string("分割点必须依附于一条几何边。");
    try {
        const Index edge_id = point.support_id;
        const auto component_id = ctx.model.findComponentIdByGeometryShapeId(TopAbs_EDGE, edge_id);
        const TopoDS_Shape* shape = ctx.model.geomRegistry().getEdge(edge_id);
        if (!component_id || !shape || *component_id != point.component_id)
            return std::string("所选几何边已失效。");
        // 原生调用者同样必须提供一致快照，不能用旧坐标配新的曲线参数切边。
        if (!std::isfinite(point.edge_parameter)
            || !std::all_of(point.world_pos.begin(), point.world_pos.end(), [](double value) { return std::isfinite(value); }))
            return std::string("分割点包含无效坐标或曲线参数，请重新选择。");
        const TopoDS_Edge edge = TopoDS::Edge(*shape);
        const BRepAdaptor_Curve curve(edge);
        if (point.edge_parameter < curve.FirstParameter() || point.edge_parameter > curve.LastParameter())
            return std::string("分割点的曲线参数已超出目标边，请重新选择。");
        const gp_Pnt expected = curve.Value(point.edge_parameter);
        const gp_Pnt position(point.world_pos[0], point.world_pos[1], point.world_pos[2]);
        const double tolerance = std::max(Precision::Confusion(), BRep_Tool::Tolerance(edge));
        if (expected.Distance(position) > tolerance)
            return std::string("分割点坐标与目标边参数不一致，请重新选择。");
        ComponentData* component = ctx.model.findComponent(*component_id);
        if (!component || !component->geometry || !component->geometry->rootShape)
            return std::string("目标组件没有几何。");
        if (component->mapping && !component->mapping->empty())
            return std::string("目标组件已经建立几何-网格映射，不能修改几何拓扑。");
        TopoDS_Shape result = GeometryTopologyEditor::splitEdgeAtParameter(
            *component->geometry->rootShape, edge, point.edge_parameter);
        auto op = ctx.componentOperator(*component_id);
        if (!op)
            return std::string("几何操作失败，详细原因请查看日志。");
        op->replaceGeometryRoot(std::move(result));
        return std::string("分割边成功");
    } catch (const std::exception& error) {
        spdlog::error("SplitEdge: {}", error.what());
        return std::string("几何边分割失败，详细原因请查看日志。");
    }
}
}
