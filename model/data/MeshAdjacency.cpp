#include "MeshAdjacency.h"

#include "MeshData.h"
#include "MeshIDMap.h"

#include <spdlog/spdlog.h>

namespace {
//! @brief 端点对打包为无序键：小端点占低 32 位，大端点占高 32 位
std::uint64_t packEndpoints(Index p0, Index p1)
{
    const std::uint32_t lo = static_cast<std::uint32_t>(p0 < p1 ? p0 : p1);
    const std::uint32_t hi = static_cast<std::uint32_t>(p0 < p1 ? p1 : p0);
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

// VTK 单元类型码（取自 vtkCellType.h）。本文件刻意不引入 VTK 依赖以保持模型层依赖边界，
// 故在此用整数常量（前缀 CELL_ 避免与可能的 VTK 宏重名）。
constexpr unsigned char CELL_VERTEX = 1;
constexpr unsigned char CELL_POLY_VERTEX = 2;
constexpr unsigned char CELL_LINE = 3;
constexpr unsigned char CELL_POLY_LINE = 4;
constexpr unsigned char CELL_TRIANGLE = 5;
constexpr unsigned char CELL_TRIANGLE_STRIP = 6;
constexpr unsigned char CELL_POLYGON = 7;
constexpr unsigned char CELL_PIXEL = 8;
constexpr unsigned char CELL_QUAD = 9;
constexpr unsigned char CELL_TETRA = 10;
constexpr unsigned char CELL_HEXAHEDRON = 12;
constexpr unsigned char CELL_WEDGE = 13;
constexpr unsigned char CELL_PYRAMID = 14;
constexpr unsigned char CELL_PENTAGONAL_PRISM = 15;
constexpr unsigned char CELL_HEXAGONAL_PRISM = 16;
constexpr unsigned char CELL_POLYHEDRON = 42;

//! @brief 枚举单个体单元的全部边，以端点对追加到 out
//!
//! 端点采用 solid_vertices_ 中的全局局部点 id（与 MeshData 连通性及源一/二同一键空间）；
//! 单元内局部点下标经 begin 偏移映射回全局（体单元在 solid_vertices_ 中按 VTK 单元格点序连续存放）。
//! 多面体（CELL_POLYHEDRON）的面顶点为单元内局部点 id，需经 begin 偏移还原。
//! 不支持的单元类型 out 不变（该行体边维持原端点对兜底行为，不引入回归）。
void appendSolidCellEdges(unsigned char cell_type,
    const std::vector<Index>& solid_vertices, Index begin, Index npts,
    const std::vector<Index>& solid_faces, Index face_begin, Index face_end,
    const std::vector<Index>& solid_faces_vertices,
    const std::vector<Index>& solid_faces_vertices_offset,
    std::vector<std::array<Index, 2>>& out)
{
    auto add = [&](Index la, Index lb) {
        if (la < 0 || lb < 0 || la >= npts || lb >= npts)
            return;
        const Index pa = solid_vertices[static_cast<size_t>(begin + la)];
        const Index pb = solid_vertices[static_cast<size_t>(begin + lb)];
        if (pa < 0 || pb < 0)
            return;
        out.push_back({ pa, pb });
    };

    switch (cell_type) {
    case CELL_LINE: // 3
        add(0, 1);
        break;
    case CELL_POLY_LINE: // 4
        for (Index i = 0; i + 1 < npts; ++i)
            add(i, i + 1);
        break;
    case CELL_TRIANGLE: // 5
        add(0, 1); add(1, 2); add(2, 0);
        break;
    case CELL_TRIANGLE_STRIP: // 6
        for (Index i = 0; i + 1 < npts; ++i) {
            add(i, i + 1);
            if (i + 2 < npts)
                add(i + 1, i + 2);
        }
        break;
    case CELL_QUAD: // 9（含 CELL_PIXEL 近似）
    case CELL_PIXEL: // 8
    case CELL_POLYGON: // 7
        if (npts >= 3)
            for (Index i = 0; i < npts; ++i)
                add(i, (i + 1) % npts);
        break;
    case CELL_TETRA: // 10
        add(0, 1); add(1, 2); add(2, 0);
        add(0, 3); add(1, 3); add(2, 3);
        break;
    case CELL_HEXAHEDRON: // 12
        add(0, 1); add(1, 2); add(2, 3); add(3, 0);
        add(4, 5); add(5, 6); add(6, 7); add(7, 4);
        add(0, 4); add(1, 5); add(2, 6); add(3, 7);
        break;
    case CELL_WEDGE: // 13
        add(0, 1); add(1, 2); add(2, 0);
        add(3, 4); add(4, 5); add(5, 3);
        add(0, 3); add(1, 4); add(2, 5);
        break;
    case CELL_PYRAMID: // 14
        add(0, 1); add(1, 2); add(2, 3); add(3, 0);
        add(0, 4); add(1, 4); add(2, 4); add(3, 4);
        break;
    case CELL_PENTAGONAL_PRISM: // 15
        for (Index i = 0; i < 5; ++i) add(i, (i + 1) % 5);
        for (Index i = 5; i < 10; ++i) add(i, 5 + (i + 1) % 5);
        for (Index i = 0; i < 5; ++i) add(i, i + 5);
        break;
    case CELL_HEXAGONAL_PRISM: // 16
        for (Index i = 0; i < 6; ++i) add(i, (i + 1) % 6);
        for (Index i = 6; i < 12; ++i) add(i, 6 + (i + 1) % 6);
        for (Index i = 0; i < 6; ++i) add(i, i + 6);
        break;
    case CELL_POLYHEDRON: // 42
        for (Index fi = face_begin; fi < face_end; ++fi) {
            // solid_faces[fi] 为该多面体的一个面在 solid_faces_vertices_offset_ 中的索引
            const Index face_idx = solid_faces[static_cast<size_t>(fi)];
            if (face_idx < 0 || face_idx + 1 >= static_cast<Index>(solid_faces_vertices_offset.size()))
                continue;
            const Index vb = solid_faces_vertices_offset[static_cast<size_t>(face_idx)];
            const Index ve = solid_faces_vertices_offset[static_cast<size_t>(face_idx + 1)];
            const Index fn = ve - vb;
            if (vb < 0 || ve < 0 || fn < 2)
                continue;
            for (Index j = 0; j < fn; ++j) {
                const Index a = solid_faces_vertices[static_cast<size_t>(vb + j)];
                const Index b = solid_faces_vertices[static_cast<size_t>(vb + (j + 1) % fn)];
                add(a, b);
            }
        }
        break;
    default:
        break; // 不支持的单元类型：不贡献边
    }
}
}

MeshAdjacency::MeshAdjacency(const MeshAdjacency& other)
    : stable_id_by_endpoints_(other.stable_id_by_endpoints_)
    , gid_by_stable_id_(other.gid_by_stable_id_)
{
    // 懒重建部分保持默认初始状态（空表 + dirty），下次查询自动重建
}

MeshAdjacency& MeshAdjacency::operator=(const MeshAdjacency& other)
{
    if (this == &other)
        return *this;

    // 只拷持久身份层
    stable_id_by_endpoints_ = other.stable_id_by_endpoints_;
    gid_by_stable_id_ = other.gid_by_stable_id_;

    // 懒重建部分清零并置 dirty
    built_mesh_ = nullptr;
    dirty_ = true;
    generation_ = 0;
    rows_.clear();
    row_by_endpoints_.clear();
    face_edge_stable_ids_.clear();
    row_by_stable_id_.clear();
    return *this;
}

std::optional<Index> MeshAdjacency::edgeStableId(const MeshData& mesh, EdgeHandle edge)
{
    ensureBuilt(mesh);

    if (!isCurrent(edge))
        return std::nullopt;
    return rows_[edge.row_].stable_id;
}

std::optional<Index> MeshAdjacency::edgeGlobalId(const MeshData& mesh, Index stable_id)
{
    ensureBuilt(mesh);

    if (stable_id < 0 || stable_id >= static_cast<Index>(gid_by_stable_id_.size()))
        return std::nullopt;
    const Index gid = gid_by_stable_id_[stable_id];
    if (gid < 0) // 尚未经 ensureEdgeGlobalIds 分配
        return std::nullopt;
    return gid;
}

const std::vector<Index>& MeshAdjacency::faceEdgeStableIds(const MeshData& mesh)
{
    ensureBuilt(mesh);
    return face_edge_stable_ids_;
}

std::optional<std::array<Index, 2>> MeshAdjacency::edgeEndpoints(const MeshData& mesh, Index stable_id)
{
    const auto edge = findEdgeByStableId(mesh, stable_id);
    if (!edge)
        return std::nullopt;
    return rows_[edge->row_].endpoints;
}

std::optional<EdgeHandle> MeshAdjacency::findEdgeByEndpoints(const MeshData& mesh, Index p0, Index p1)
{
    if (p0 < 0 || p1 < 0)
        return std::nullopt;

    ensureBuilt(mesh);

    auto it = row_by_endpoints_.find(packEndpoints(p0, p1));
    if (it == row_by_endpoints_.end())
        return std::nullopt;
    return EdgeHandle(it->second, generation_);
}

std::optional<EdgeHandle> MeshAdjacency::findEdgeByStableId(const MeshData& mesh, Index stable_id)
{
    ensureBuilt(mesh);

    if (stable_id < 0 || stable_id >= static_cast<Index>(row_by_stable_id_.size()))
        return std::nullopt;
    const Index row = row_by_stable_id_[stable_id];
    if (row < 0) // 该边在当前拓扑中已消亡
        return std::nullopt;
    return EdgeHandle(row, generation_);
}

Index MeshAdjacency::edgeCellIndex(const MeshData& mesh, EdgeHandle edge)
{
    ensureBuilt(mesh);

    if (!isCurrent(edge))
        return -1;
    return rows_[edge.row_].cell_index;
}

Index MeshAdjacency::edgeCount(const MeshData& mesh)
{
    ensureBuilt(mesh);
    return static_cast<Index>(rows_.size());
}

void MeshAdjacency::ensureEdgeGlobalIds(MeshIDMap& map, Index component_id, const MeshData& mesh)
{
    ensureBuilt(mesh);

    // 幂等补缺：只为尚未分配的稳定 id 申请 gid
    for (Index sid = 0; sid < static_cast<Index>(gid_by_stable_id_.size()); ++sid) {
        if (gid_by_stable_id_[sid] < 0)
            gid_by_stable_id_[sid] = map.insert(component_id, sid);
    }
}

void MeshAdjacency::releaseEdgeGlobalIds(MeshIDMap& map)
{
    for (Index gid : gid_by_stable_id_) {
        if (gid >= 0)
            map.remove(gid);
    }
    gid_by_stable_id_.clear();
    stable_id_by_endpoints_.clear();
    invalidate();
}

void MeshAdjacency::reclaimEdgeGlobalIds(MeshIDMap& map, Index component_id)
{
    // 仅作用持久层：按原值定向回收 gid，不触发边表重建
    for (Index sid = 0; sid < static_cast<Index>(gid_by_stable_id_.size()); ++sid) {
        const Index gid = gid_by_stable_id_[static_cast<size_t>(sid)];
        if (gid >= 0)
            map.reclaim(gid, component_id, sid);
    }
}

void MeshAdjacency::stripEdgeGlobalIds()
{
    // 稳定 id 结构保留，仅把 gid 层重置为待分配（-1 = 见 ensureEdgeGlobalIds 补发）
    for (Index& gid : gid_by_stable_id_)
        gid = -1;
}

void MeshAdjacency::invalidate() noexcept
{
    dirty_ = true;
}

bool MeshAdjacency::isCurrent(EdgeHandle edge) const noexcept
{
    return edge.row_ >= 0 && edge.generation_ == generation_
        && edge.row_ < static_cast<Index>(rows_.size());
}

void MeshAdjacency::ensureBuilt(const MeshData& mesh)
{
    if (!dirty_ && built_mesh_ == &mesh)
        return;

    rows_.clear();
    row_by_endpoints_.clear();
    face_edge_stable_ids_.clear();
    built_mesh_ = &mesh;
    dirty_ = false;

    // 端点对归并建行：持久稳定 id + 当轮行号。返回行号。
    auto resolve_row = [&](Index p0, Index p1) -> Index {
        const std::uint64_t key = packEndpoints(p0, p1);

        // 持久身份：首次见到的端点对单调分配稳定 id（不复用、不回收，见文件头说明）
        Index sid;
        if (auto sid_it = stable_id_by_endpoints_.find(key); sid_it != stable_id_by_endpoints_.end()) {
            sid = sid_it->second;
        } else {
            sid = static_cast<Index>(gid_by_stable_id_.size());
            stable_id_by_endpoints_.emplace(key, sid);
            gid_by_stable_id_.push_back(-1);
        }

        // 当轮边表行
        auto [it, inserted] = row_by_endpoints_.try_emplace(key, static_cast<Index>(rows_.size()));
        if (inserted) {
            EdgeRow row;
            row.endpoints = { p0 < p1 ? p0 : p1, p0 < p1 ? p1 : p0 };
            row.stable_id = sid;
            rows_.push_back(row);
        }
        return it->second;
    };

    // 源一：边数组（物化边）。先灌入以保 cell 顺序，重复边合并到先见行
    const auto& edge_vertices = mesh.edge_vertices_;
    if (edge_vertices.size() % 2 != 0) {
        // 异常数据跳过该源（按无有效边处理）
        spdlog::error("MeshAdjacency::ensureBuilt: edge_vertices_ size is odd ({})", edge_vertices.size());
    } else {
        const Index cell_count = static_cast<Index>(edge_vertices.size() / 2);
        for (Index cell = 0; cell < cell_count; ++cell) {
            const Index p0 = edge_vertices[2 * cell];
            const Index p1 = edge_vertices[2 * cell + 1];
            if (p0 < 0 || p1 < 0)
                continue;

            const Index row_id = resolve_row(p0, p1);
            auto& row = rows_[row_id];
            if (row.cell_index < 0) {
                row.cell_index = cell;
            } else {
                spdlog::warn("MeshAdjacency::ensureBuilt: duplicated edge cell {} on endpoints ({}, {}), first cell {} kept",
                    cell, p0, p1, row.cell_index);
            }
        }
    }

    // 源二：面单元的边（含与物化边重合者，经 resolve_row 自动归并）
    const auto& face_vertices = mesh.face_vertices_;
    const auto& offsets = mesh.face_vertices_offset_;
    if (offsets.size() >= 2) {
        face_edge_stable_ids_.assign(face_vertices.size(), -1);
        const Index face_count = static_cast<Index>(offsets.size() - 1);
        for (Index f = 0; f < face_count; ++f) {
            const Index begin = offsets[f];
            const Index n = offsets[f + 1] - begin;
            if (n < 2) // 无法成边的退化面
                continue;

            // 面顶点环绕展开边：(v0,v1), (v1,v2), ..., (v_{n-1},v0)
            for (Index j = 0; j < n; ++j) {
                const Index p0 = face_vertices[begin + j];
                const Index p1 = face_vertices[begin + (j + 1) % n];
                if (p0 < 0 || p1 < 0)
                    continue;
                const Index row_id = resolve_row(p0, p1);
                face_edge_stable_ids_[begin + j] = rows_[row_id].stable_id;
            }
        }
    }

    // 源三：体单元（含体网格内部边）的全部边。端点对与源一/二同一键空间，经 resolve_row 自动归并去重；
    const auto& solid_offsets = mesh.solid_vertices_offset_;
    const Index solid_count = solid_offsets.empty() ? 0 : static_cast<Index>(solid_offsets.size() - 1);
    if (solid_count > 0 && !mesh.solid_types_.empty()) {
        const auto& sfaces_off = mesh.solid_faces_offset_;
        const bool has_poly_faces = sfaces_off.size() >= solid_count + 1;
        for (Index c = 0; c < solid_count; ++c) {
            const Index begin = solid_offsets[c];
            const Index end = solid_offsets[c + 1];
            if (begin < 0 || end <= begin || end > static_cast<Index>(mesh.solid_vertices_.size()))
                continue;
            const unsigned char cell_type = static_cast<unsigned char>(mesh.solid_types_[c]);
            const Index npts = end - begin;
            Index face_begin = 0, face_end = 0;
            if (cell_type == CELL_POLYHEDRON && has_poly_faces) {
                face_begin = sfaces_off[c];
                face_end = sfaces_off[c + 1];
            }
            std::vector<std::array<Index, 2>> cell_edges;
            appendSolidCellEdges(cell_type, mesh.solid_vertices_, begin, npts,
                mesh.solid_faces_, face_begin, face_end,
                mesh.solid_faces_vertices_, mesh.solid_faces_vertices_offset_, cell_edges);
            for (const auto& e : cell_edges)
                resolve_row(e[0], e[1]);
        }
    }

    // 回填稳定 id -> 行号（消亡边留 -1 空洞）
    row_by_stable_id_.assign(gid_by_stable_id_.size(), -1);
    for (Index r = 0; r < static_cast<Index>(rows_.size()); ++r)
        row_by_stable_id_[rows_[r].stable_id] = r;

    // 世代递增：此前签发的句柄全部失效
    ++generation_;
}
