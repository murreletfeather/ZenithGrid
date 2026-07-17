/**
 * @file MeasureHandler.cpp
 * @brief 网格测量处理器，支持距离、角度、半径、长度、面积、体积、包围盒与重心
 */

#include "MeasureHandler.h"
#include "ArgObject.h"
#include "ComponentData.h"
#include "ComponentOperator.h"
#include "MeshData.h"
#include "ModelLayer.h"
#include "Selection.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>

namespace systems::algo {

namespace {
using Vec3 = std::array<double, 3>;

Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

Vec3 operator*(const Vec3& a, double s)
{
    return { a[0] * s, a[1] * s, a[2] * s };
}

Vec3 operator/(const Vec3& a, double s)
{
    return { a[0] / s, a[1] / s, a[2] / s };
}

double dot(const Vec3& a, const Vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double length(const Vec3& v)
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

enum class MeasureType {
    Distance,
    Angle,
    Radius,
    Length,
    Area,
    Volume,
    BoundingBox,
    Centroid
};

MeasureType toMeasureType(int index)
{
    switch (index) {
    case 0:
        return MeasureType::Distance;
    case 1:
        return MeasureType::Angle;
    case 2:
        return MeasureType::Radius;
    case 3:
        return MeasureType::Length;
    case 4:
        return MeasureType::Area;
    case 5:
        return MeasureType::Volume;
    case 6:
        return MeasureType::BoundingBox;
    case 7:
        return MeasureType::Centroid;
    default:
        return MeasureType::Distance;
    }
}

bool isValidVertexId(const MeshData& mesh, const ModelLayer& manager, Index id)
{
    if (id < 0)
        return false;
    if (mesh.local_to_global_.empty())
        return id < static_cast<Index>(mesh.vertex_positions_.size());
    return id < static_cast<Index>(manager.globalPoints().size());
}

const Vec3& getPosition(const MeshData& mesh, const ModelLayer& manager, Index id)
{
    if (!mesh.local_to_global_.empty() && !manager.globalPoints().empty())
        return manager.globalPoints()[id];
    return mesh.vertex_positions_[id];
}

std::string toString(double value, int precision = 6)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(precision);
    oss << value;
    return oss.str();
}

std::string vecString(const Vec3& v)
{
    return "(" + toString(v[0]) + ", " + toString(v[1]) + ", " + toString(v[2]) + ")";
}

double angleBetween(const Vec3& u, const Vec3& v)
{
    const double lu = length(u);
    const double lv = length(v);
    if (lu < std::numeric_limits<double>::epsilon() || lv < std::numeric_limits<double>::epsilon())
        return 0.0;

    double cos_theta = dot(u, v) / (lu * lv);
    cos_theta = std::max(-1.0, std::min(1.0, cos_theta));
    return std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
}

double triangleArea(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return 0.5 * length(cross(b - a, c - a));
}

double polygonArea(const std::vector<Vec3>& points)
{
    const size_t n = points.size();
    if (n < 3)
        return 0.0;

    double area = 0.0;
    for (size_t i = 2; i < n; ++i)
        area += triangleArea(points[0], points[i - 1], points[i]);
    return area;
}

double tetraVolume(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    return std::abs(dot(b - a, cross(c - a, d - a))) / 6.0;
}

double polyhedronVolume(const MeshData& mesh, const ModelLayer& manager, Index solid_id)
{
    if (solid_id + 1 >= static_cast<Index>(mesh.solid_faces_offset_.size()))
        return 0.0;

    const Index face_start = mesh.solid_faces_offset_[solid_id];
    const Index face_end = mesh.solid_faces_offset_[solid_id + 1];
    double volume = 0.0;

    for (Index f = face_start; f < face_end; ++f) {
        const Index face_id = mesh.solid_faces_[f];
        if (face_id + 1 >= static_cast<Index>(mesh.solid_faces_vertices_offset_.size()))
            continue;

        const Index vert_start = mesh.solid_faces_vertices_offset_[face_id];
        const Index vert_end = mesh.solid_faces_vertices_offset_[face_id + 1];
        if (vert_end - vert_start < 3)
            continue;

        const Vec3& v0 = getPosition(mesh, manager, mesh.solid_faces_vertices_[vert_start]);
        for (Index i = vert_start + 1; i + 1 < vert_end; ++i) {
            const Vec3& vi = getPosition(mesh, manager, mesh.solid_faces_vertices_[i]);
            const Vec3& vj = getPosition(mesh, manager, mesh.solid_faces_vertices_[i + 1]);
            volume += dot(v0, cross(vi, vj)) / 6.0;
        }
    }

    return std::abs(volume);
}

double solidVolume(const MeshData& mesh, const ModelLayer& manager, Index solid_id)
{
    if (solid_id < 0 || solid_id + 1 >= static_cast<Index>(mesh.solid_vertices_offset_.size()))
        return 0.0;
    if (solid_id >= static_cast<Index>(mesh.solid_types_.size()))
        return 0.0;

    const unsigned char cell_type = mesh.solid_types_[solid_id];
    const Index start = mesh.solid_vertices_offset_[solid_id];
    const Index end = mesh.solid_vertices_offset_[solid_id + 1];

    // VTK_TETRA = 10
    if (cell_type == 10) {
        if (end - start != 4)
            return 0.0;
        const Vec3& a = getPosition(mesh, manager, mesh.solid_vertices_[start]);
        const Vec3& b = getPosition(mesh, manager, mesh.solid_vertices_[start + 1]);
        const Vec3& c = getPosition(mesh, manager, mesh.solid_vertices_[start + 2]);
        const Vec3& d = getPosition(mesh, manager, mesh.solid_vertices_[start + 3]);
        return tetraVolume(a, b, c, d);
    }

    // VTK_POLYHEDRON = 42
    if (cell_type == 42)
        return polyhedronVolume(mesh, manager, solid_id);

    return 0.0;
}

std::vector<Vec3> collectPositions(const MeshData& mesh, const ModelLayer& manager,
    ElementEnum::Type type, const std::vector<Index>& ids)
{
    std::vector<Vec3> positions;
    positions.reserve(ids.size() * 4);

    switch (type) {
    case ElementEnum::Vertex:
        for (Index id : ids) {
            if (isValidVertexId(mesh, manager, id))
                positions.push_back(getPosition(mesh, manager, id));
        }
        break;
    case ElementEnum::Edge:
        for (Index id : ids) {
            const Index base = 2 * id;
            if (base + 1 >= static_cast<Index>(mesh.edge_vertices_.size()))
                continue;
            const Index v0 = mesh.edge_vertices_[base];
            const Index v1 = mesh.edge_vertices_[base + 1];
            if (isValidVertexId(mesh, manager, v0))
                positions.push_back(getPosition(mesh, manager, v0));
            if (isValidVertexId(mesh, manager, v1))
                positions.push_back(getPosition(mesh, manager, v1));
        }
        break;
    case ElementEnum::Face:
        for (Index id : ids) {
            if (id + 1 >= static_cast<Index>(mesh.face_vertices_offset_.size()))
                continue;
            for (Index i = mesh.face_vertices_offset_[id]; i < mesh.face_vertices_offset_[id + 1]; ++i) {
                const Index v = mesh.face_vertices_[i];
                if (isValidVertexId(mesh, manager, v))
                    positions.push_back(getPosition(mesh, manager, v));
            }
        }
        break;
    case ElementEnum::Solid:
        for (Index id : ids) {
            if (id + 1 >= static_cast<Index>(mesh.solid_vertices_offset_.size()))
                continue;
            for (Index i = mesh.solid_vertices_offset_[id]; i < mesh.solid_vertices_offset_[id + 1]; ++i) {
                const Index v = mesh.solid_vertices_[i];
                if (isValidVertexId(mesh, manager, v))
                    positions.push_back(getPosition(mesh, manager, v));
            }
        }
        break;
    default:
        break;
    }

    return positions;
}

std::string formatDistance(const Vec3& a, const Vec3& b)
{
    const Vec3 d = b - a;
    std::ostringstream oss;
    oss << "Distance: " << toString(length(d)) << "\n"
        << "Dx: " << toString(d[0]) << "\n"
        << "Dy: " << toString(d[1]) << "\n"
        << "Dz: " << toString(d[2]);
    return oss.str();
}

std::string formatAngle(const Vec3& a, const Vec3& b, const Vec3& c)
{
    std::ostringstream oss;
    oss << "Angle: " << toString(angleBetween(a - b, c - b)) << " deg";
    return oss.str();
}

std::string formatRadius(const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 n = cross(ab, ac);
    const double n_len2 = dot(n, n);

    if (n_len2 < std::numeric_limits<double>::epsilon())
        return "错误：三点共线或重合，无法计算半径";

    const Vec3 circumcenter = a + (cross(ac, n) * dot(ab, ab) - cross(ab, n) * dot(ac, ac)) / (2.0 * n_len2);
    const double radius = length(circumcenter - a);

    std::ostringstream oss;
    oss << "Radius: " << toString(radius) << "\n"
        << "Center: " << vecString(circumcenter);
    return oss.str();
}

std::string formatEdgeLength(const MeshData& mesh, const ModelLayer& manager, const std::vector<Index>& ids)
{
    double total = 0.0;
    std::ostringstream oss;

    for (size_t i = 0; i < ids.size(); ++i) {
        const Index id = ids[i];
        const Index base = 2 * id;
        if (base + 1 >= static_cast<Index>(mesh.edge_vertices_.size())) {
            oss << "边 " << id << ": 无效索引\n";
            continue;
        }

        const Vec3 a = getPosition(mesh, manager, mesh.edge_vertices_[base]);
        const Vec3 b = getPosition(mesh, manager, mesh.edge_vertices_[base + 1]);
        const double len = length(a - b);
        total += len;
        oss << "边 " << id << ": " << toString(len) << "\n";
    }

    oss << "累计长度: " << toString(total);
    return oss.str();
}

std::string formatFaceArea(const MeshData& mesh, const ModelLayer& manager, const std::vector<Index>& ids)
{
    double total = 0.0;
    std::ostringstream oss;

    for (Index id : ids) {
        if (id + 1 >= static_cast<Index>(mesh.face_vertices_offset_.size())) {
            oss << "面 " << id << ": 无效索引\n";
            continue;
        }

        std::vector<Vec3> points;
        for (Index i = mesh.face_vertices_offset_[id]; i < mesh.face_vertices_offset_[id + 1]; ++i)
            points.push_back(getPosition(mesh, manager, mesh.face_vertices_[i]));

        const double area = polygonArea(points);
        total += area;
        oss << "面 " << id << ": " << toString(area) << "\n";
    }

    oss << "总面积: " << toString(total);
    return oss.str();
}

std::string formatSolidVolume(const MeshData& mesh, const ModelLayer& manager, const std::vector<Index>& ids)
{
    double total = 0.0;
    std::ostringstream oss;

    for (Index id : ids) {
        const double volume = solidVolume(mesh, manager, id);
        if (volume <= 0.0) {
            oss << "体 " << id << ": 不支持的体类型或无体积\n";
            continue;
        }
        total += volume;
        oss << "体 " << id << ": " << toString(volume) << "\n";
    }

    oss << "总体积: " << toString(total);
    return oss.str();
}

std::string formatBoundingBox(const std::vector<Vec3>& positions)
{
    Vec3 min = positions[0];
    Vec3 max = positions[0];

    for (const auto& p : positions) {
        for (int i = 0; i < 3; ++i) {
            min[i] = std::min(min[i], p[i]);
            max[i] = std::max(max[i], p[i]);
        }
    }

    const Vec3 size = max - min;

    std::ostringstream oss;
    oss << "包围盒:\n"
        << "min: " << vecString(min) << "\n"
        << "max: " << vecString(max) << "\n"
        << "size: " << vecString(size);
    return oss.str();
}

std::string formatCentroid(const std::vector<Vec3>& positions)
{
    Vec3 sum = { 0.0, 0.0, 0.0 };
    for (const auto& p : positions)
        sum = sum + p;
    const Vec3 centroid = sum / static_cast<double>(positions.size());

    std::ostringstream oss;
    oss << "重心: " << vecString(centroid);
    return oss.str();
}
}

std::any MeasureHandler::execute(HandlerContext& context, const std::vector<core::ArgObject>& args)
{
    if (args.size() < 2) {
        spdlog::error("MeasureHandler::execute: 参数不足");
        return std::string("错误：参数不足");
    }

    const int* type_index = args[0].get<ArgTypeEnum::Combo>();
    if (!type_index) {
        spdlog::error("MeasureHandler::execute: 测量类型参数无效");
        return std::string("错误：测量类型参数无效");
    }

    const auto selection_ptr = args[1].get<ArgTypeEnum::Selector>();
    if (!selection_ptr || !*selection_ptr) {
        spdlog::error("MeasureHandler::execute: 选择对象参数无效");
        return std::string("错误：选择对象参数无效");
    }

    const auto& selection = **selection_ptr;

    const Index selected_component_id = selection.component_id >= 0 ? selection.component_id : context.cur_component.componentId();
    ComponentData* comp = context.cur_component.manager().findComponent(selected_component_id);
    if (!comp) {
        spdlog::error("MeasureHandler::execute: 找不到选择对象所在的组件");
        return std::string("错误：找不到选择对象所在的组件");
    }
    MeshData* mesh = comp->asMeshData();
    if (!mesh) {
        spdlog::error("MeasureHandler::execute: 选择对象所在组件没有网格数据");
        return std::string("错误：选择对象所在组件没有网格数据");
    }

    const ModelLayer& manager = context.cur_component.manager();
    const MeasureType measure_type = toMeasureType(*type_index);

    switch (measure_type) {
    case MeasureType::Distance: {
        if (selection.type != ElementEnum::Vertex || selection.ids.size() != 2) {
            spdlog::error("MeasureHandler::execute: 距离测量需要选择两个点");
            return std::string("错误：距离测量需要选择两个点");
        }
        const Vec3& a = getPosition(*mesh, manager, selection.ids[0]);
        const Vec3& b = getPosition(*mesh, manager, selection.ids[1]);
        return formatDistance(a, b);
    }
    case MeasureType::Angle: {
        if (selection.type == ElementEnum::Vertex && selection.ids.size() == 3) {
            const Vec3& a = getPosition(*mesh, manager, selection.ids[0]);
            const Vec3& b = getPosition(*mesh, manager, selection.ids[1]);
            const Vec3& c = getPosition(*mesh, manager, selection.ids[2]);
            return formatAngle(a, b, c);
        }
        if (selection.type == ElementEnum::Edge && selection.ids.size() == 2) {
            const Index base0 = 2 * selection.ids[0];
            const Index base1 = 2 * selection.ids[1];
            if (base0 + 1 < static_cast<Index>(mesh->edge_vertices_.size()) && base1 + 1 < static_cast<Index>(mesh->edge_vertices_.size())) {
                const Vec3 u = getPosition(*mesh, manager, mesh->edge_vertices_[base0 + 1]) - getPosition(*mesh, manager, mesh->edge_vertices_[base0]);
                const Vec3 v = getPosition(*mesh, manager, mesh->edge_vertices_[base1 + 1]) - getPosition(*mesh, manager, mesh->edge_vertices_[base1]);
                std::ostringstream oss;
                oss << "Angle: " << toString(angleBetween(u, v)) << " deg";
                return oss.str();
            }
        }
        spdlog::error("MeasureHandler::execute: 角度测量需要选择三个点或两条边");
        return std::string("错误：角度测量需要选择三个点或两条边");
    }
    case MeasureType::Radius: {
        if (selection.type != ElementEnum::Vertex || selection.ids.size() != 3) {
            spdlog::error("MeasureHandler::execute: 半径测量需要选择三个点");
            return std::string("错误：半径测量需要选择三个点");
        }
        const Vec3& a = getPosition(*mesh, manager, selection.ids[0]);
        const Vec3& b = getPosition(*mesh, manager, selection.ids[1]);
        const Vec3& c = getPosition(*mesh, manager, selection.ids[2]);
        return formatRadius(a, b, c);
    }
    case MeasureType::Length: {
        if (selection.type != ElementEnum::Edge) {
            spdlog::error("MeasureHandler::execute: 长度测量需要选择边");
            return std::string("错误：长度测量需要选择边");
        }
        return formatEdgeLength(*mesh, manager, selection.ids);
    }
    case MeasureType::Area: {
        if (selection.type != ElementEnum::Face) {
            spdlog::error("MeasureHandler::execute: 面积测量需要选择面");
            return std::string("错误：面积测量需要选择面");
        }
        return formatFaceArea(*mesh, manager, selection.ids);
    }
    case MeasureType::Volume: {
        if (selection.type != ElementEnum::Solid) {
            spdlog::error("MeasureHandler::execute: 体积测量需要选择体");
            return std::string("错误：体积测量需要选择体");
        }
        return formatSolidVolume(*mesh, manager, selection.ids);
    }
    case MeasureType::BoundingBox:
    case MeasureType::Centroid: {
        const auto positions = collectPositions(*mesh, manager, selection.type, selection.ids);
        if (positions.empty()) {
            spdlog::error("MeasureHandler::execute: 未找到可用顶点");
            return std::string("错误：未找到可用顶点");
        }
        if (measure_type == MeasureType::BoundingBox)
            return formatBoundingBox(positions);
        return formatCentroid(positions);
    }
    }

    return std::string("错误：未知测量类型");
}

std::vector<core::ArgType> MeasureHandler::args_type() const
{
    return {
        { ArgTypeEnum::Combo, "测量类型", "距离,角度,半径,长度,面积,体积,包围盒,重心|0" },
        { ArgTypeEnum::Selector, "选择对象", "Vertex,Edge,Face,Solid" }
    };
}

} // namespace systems::algo
