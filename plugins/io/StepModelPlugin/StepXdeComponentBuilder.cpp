#include "StepXdeComponentBuilder.h"
#include "GeometryData.h"

#include <TCollection_AsciiString.hxx>
#include <NCollection_Sequence.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TCollection_ExtendedString.hxx>
#include <cstring>

#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <TopoDS_Shape.hxx>
#include <TopLoc_Location.hxx>
#include <spdlog/spdlog.h>

namespace systems::io {

static std::string labelName(const TDF_Label& label)
{
    Handle(TDataStd_Name) nameAttr;
    if (!label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        return {};
    }

    const TCollection_ExtendedString& ext = nameAttr->Get();

    std::string buf(ext.Length() * 4 + 1, '\0');
    char* cstr = buf.data();
    const Standard_Integer n = ext.ToUTF8CString(cstr);
    if (n < 0) {
        return {};
    }

    buf.resize(static_cast<std::string::size_type>(n));
    return buf;
}

/**
 * @brief 展开装配实例并收集位于世界坐标系的叶形状。
 * @param shapeTool XDE 形状工具。
 * @param label 当前实例或自由形状的标签。
 * @param parent_location 父装配到世界坐标系的变换。
 * @param outLeaves 输出的零件定义标签及其已定位的形状。
 */
static void collectLeafShapes(
    const Handle(XCAFDoc_ShapeTool) & shapeTool,
    const TDF_Label& label,
    const TopLoc_Location& parent_location,
    std::vector<std::pair<TDF_Label, TopoDS_Shape>>& outLeaves)
{
    TDF_Label refLabel = label;

    if (shapeTool->IsReference(label)) {
        TDF_Label referred;
        if (shapeTool->GetReferredShape(label, referred)) {
            refLabel = referred;
        }
    }

    // 从实例标签取形状，保留本级实例变换及定义形状自身的位置。
    const TopoDS_Shape shape = shapeTool->GetShape(label);
    if (shapeTool->IsAssembly(refLabel)) {
        const TopLoc_Location assembly_location = parent_location * shape.Location();
        NCollection_Sequence<TDF_Label> children;
        shapeTool->GetComponents(refLabel, children);

        for (Standard_Integer i = 1; i <= children.Length(); ++i) {
            collectLeafShapes(shapeTool, children.Value(i), assembly_location, outLeaves);
        }
        return;
    }

    if (!shape.IsNull()) {
        // 本级位置已在 shape 中，仅前乘父级累计变换以得到世界坐标。
        outLeaves.emplace_back(refLabel, shape.Moved(parent_location));
    }
}

std::optional<ModelPayload> StepXdeComponentBuilder::buildModelData(
    TDocStd_Document& doc,
    const std::u8string& modelName)
{
    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc.Main());
    if (shapeTool.IsNull()) {
        spdlog::error("Failed to get XCAF shape tool.");
        return std::nullopt;
    }

    ComponentDatas comps;

    NCollection_Sequence<TDF_Label> freeShapes;
    shapeTool->GetFreeShapes(freeShapes);

    std::vector<std::pair<TDF_Label, TopoDS_Shape>> leaves;
    for (Standard_Integer i = 1; i <= freeShapes.Length(); ++i) {
        collectLeafShapes(shapeTool, freeShapes.Value(i), TopLoc_Location {}, leaves);
    }

    if (leaves.empty()) {
        spdlog::warn("[STEP-XDE] no leaf found, fallback to first free shape");

        int freeIndex = 0;
        for (Standard_Integer i = 1; i <= freeShapes.Length(); ++i) {
            TopoDS_Shape s = shapeTool->GetShape(freeShapes.Value(i));
            if (!s.IsNull()) {
                auto geometry_data = std::make_unique<GeometryData>();
                geometry_data->setRootShape(s);

                auto c = std::make_unique<ComponentData>();
                c->id = -1;
                c->name = "Comp_" + std::to_string(freeIndex);
                c->geometry = std::move(geometry_data);

                comps.push_back(std::move(c));
                ++freeIndex;
            }
        }

        if (!comps.empty()) {
            return ModelPayload { modelName, std::move(comps) };
        }

        spdlog::error("[STEP-XDE] no valid shape found");
        return std::nullopt;
    }

    int leafIndex = 0;
    for (const auto& [label, shape] : leaves) {
        auto geometry_data = std::make_unique<GeometryData>();
        geometry_data->setRootShape(shape);

        std::string compName = labelName(label);
        if (compName.empty()) {
            compName = "Comp_" + std::to_string(leafIndex);
        }

        auto c = std::make_unique<ComponentData>();
        c->id = -1;
        c->name = compName;
        c->geometry = std::move(geometry_data);
        c->source_xde_leaf_id = leafIndex;

        comps.push_back(std::move(c));

        spdlog::info("[STEP-XDE] create component: index={}, name='{}', shapeType={}",
            leafIndex, compName, static_cast<int>(shape.ShapeType()));

        ++leafIndex;
    }

    spdlog::info("[STEP-XDE] model '{}' created {} components",
        u8Narrow(modelName), comps.size());

    return ModelPayload { modelName, std::move(comps) };
}
}
