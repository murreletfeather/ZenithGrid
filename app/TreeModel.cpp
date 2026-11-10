#include "TreeModel.h"
#include "QModelQuery.h"

#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace {
/** @brief 把 VTK 单元类型号转换为界面名称，未知类型仍保留独立入口。 */
QString cellTypeName(int type)
{
    switch (type) {
    case 5:
        return TreeModel::tr("三角形");
    case 9:
        return TreeModel::tr("四边形");
    case 7:
        return TreeModel::tr("多边形");
    case 10:
        return TreeModel::tr("四面体");
    case 11:
        return TreeModel::tr("体素");
    case 12:
        return TreeModel::tr("六面体");
    case 13:
        return TreeModel::tr("三棱柱");
    case 14:
        return TreeModel::tr("金字塔");
    case 15:
        return TreeModel::tr("五棱柱");
    case 16:
        return TreeModel::tr("六棱柱");
    case 42:
        return TreeModel::tr("多面体");
    default:
        return TreeModel::tr("单元类型 %1").arg(type);
    }
}
}

TreeModel::TreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    rootNode = new TreeNode("root");
    rootNode->nodeId = -1;
}

TreeModel::~TreeModel()
{
    delete rootNode;
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeNode* parentNode = getNode(parent);

    if (row < 0 || row >= parentNode->children.size())
        return QModelIndex();

    return createIndex(row, column, parentNode->children[row]);
}

QModelIndex TreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return QModelIndex();

    TreeNode* childNode = static_cast<TreeNode*>(child.internalPointer());
    TreeNode* parentNode = childNode->parent;

    if (!parentNode || parentNode == rootNode)
        return QModelIndex();

    TreeNode* grandParent = parentNode->parent;
    int row = grandParent->children.indexOf(parentNode);
    return createIndex(row, 0, parentNode);
}

int TreeModel::rowCount(const QModelIndex& parent) const
{
    TreeNode* parentNode = getNode(parent);
    return parentNode->children.size();
}

int TreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant TreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    TreeNode* node = static_cast<TreeNode*>(index.internalPointer());

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return node->name;

    case NumberRole:
        return node->number;

    case NodeIdRole:
        return node->nodeId;

    case IsVisibleRole:
        return visibilityState(node) != Qt::Unchecked;
    case VisibilityStateRole:
        return visibilityState(node);
    case CanToggleVisibilityRole:
        return node->nodeType == TreeNode::NodeType::Model
            || node->nodeType == TreeNode::NodeType::Component
            || node->nodeType == TreeNode::NodeType::Mesh
            || node->nodeType == TreeNode::NodeType::Geometry
            || node->nodeType == TreeNode::NodeType::D2
            || node->nodeType == TreeNode::NodeType::D3
            || node->nodeType == TreeNode::NodeType::CellType;
    case StableKeyRole:
        return stableKey(node);

    case ComponentIdRole: {
        TreeNode* cur = node;
        while (cur && cur->parent && cur->parent != rootNode) {
            if (cur->parent->parent == rootNode)
                return cur->nodeId;
            cur = cur->parent;
        }
        return -1;
    }

    case NodeTypeRole:
        return static_cast<int>(node->nodeType);

    default:
        return QVariant();
    }
}

QHash<int, QByteArray> TreeModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole]     = "name";
    roles[NumberRole]   = "number";
    roles[NodeIdRole]   = "nodeId";
    roles[IsVisibleRole] = "isVisible";
    roles[ComponentIdRole] = "componentId";
    roles[NodeTypeRole] = "nodeType";
    roles[VisibilityStateRole] = "visibilityState";
    roles[CanToggleVisibilityRole] = "canToggleVisibility";
    roles[StableKeyRole] = "stableKey";
    return roles;
}

TreeNode* TreeModel::getNode(const QModelIndex& index) const
{
    if (index.isValid())
        return static_cast<TreeNode*>(index.internalPointer());
    return rootNode;
}

bool TreeModel::refresh()
{
    if (!modelQuery_)
        return false;

    // 先通知视图保存旧节点的展开状态，再更换内部节点树。
    beginResetModel();
    auto oldRoot = rootNode;
    rootNode = new TreeNode("root");
    rootNode->nodeId = -1;

    QVariantList models = modelQuery_->listModels();
    int fakeId = -2;
    for (const QVariant& mv : models) {
        QVariantMap m = mv.toMap();
        int mid = m["model_id"].toInt();
        QString mname = m["name"].toString();
        int ccount = m["component_count"].toInt();

        TreeNode* mNode = new TreeNode(mname, QString::number(ccount), rootNode);
        mNode->nodeId = mid;
        mNode->nodeType = TreeNode::NodeType::Model;

        QVariantList comps = modelQuery_->getComponentsSummary(mid);
        for (const QVariant& cv : comps) {
            QVariantMap c = cv.toMap();
            int cid = c["component_id"].toInt();
            QString cname = c["name"].toString();
            bool hasMesh = c["has_mesh"].toBool();
            bool hasGeometry = c["has_geometry"].toBool();

            TreeNode* cNode = new TreeNode(cname, "", mNode);
            cNode->nodeId = cid;
            cNode->nodeType = TreeNode::NodeType::Component;

            if (hasMesh) {
                const QVariantMap ms = modelQuery_->getMeshSummary(cid);
                if (!mesh_cell_type_counts_.contains(cid))
                    mesh_cell_type_counts_[cid] = modelQuery_->getMeshCellTypeCounts(cid);
                const QVariantMap& type_counts = mesh_cell_type_counts_.at(cid);
                int fc = ms["face_count"].toInt();
                int sc = ms["solid_count"].toInt();

                TreeNode* meshN = new TreeNode("Mesh", QString::number(fc + sc), cNode);
                meshN->nodeId = fakeId--;
                meshN->nodeType = TreeNode::NodeType::Mesh;
                meshN->hasIndependentEdges = ms["edge_count"].toLongLong() > 0;

                // 固定维度与类型顺序；即使只有一种类型也保留相同的层级。
                for (int dimension : { 2, 3 }) {
                    const int count = dimension == 2 ? fc : sc;
                    if (count <= 0)
                        continue;
                    auto* group = new TreeNode(dimension == 2 ? "2D" : "3D", QString::number(count), meshN);
                    group->nodeId = fakeId--;
                    group->nodeType = dimension == 2 ? TreeNode::NodeType::D2 : TreeNode::NodeType::D3;
                    group->dimension = dimension;
                    for (const auto& entry : type_counts[dimension == 2 ? "face_types" : "solid_types"].toList()) {
                        const auto item = entry.toMap();
                        const int type = item["cell_type"].toInt();
                        auto* leaf = new TreeNode(cellTypeName(type), item["count"].toString(), group);
                        leaf->nodeId = fakeId--;
                        leaf->nodeType = TreeNode::NodeType::CellType;
                        leaf->dimension = dimension;
                        leaf->cellType = type;
                    }
                }
            }

            if (hasGeometry) {
                QVariantMap gs = modelQuery_->getGeometrySummary(cid);
                int vc = gs["vertex_count"].toInt();
                int ec = gs["edge_count"].toInt();
                int fc = gs["face_count"].toInt();
                int sc = gs["solid_count"].toInt();

                TreeNode* geoN = new TreeNode("Geometry", QString::number(vc + ec + fc + sc), cNode);
                geoN->nodeId = fakeId--;
                geoN->nodeType = TreeNode::NodeType::Geometry;

                if (vc > 0) {
                    TreeNode* vn = new TreeNode("Vertex", QString::number(vc), geoN);
                    vn->nodeId = fakeId--;
                    vn->nodeType = TreeNode::NodeType::Vertex;
                }
                if (ec > 0) {
                    TreeNode* en = new TreeNode("Edge", QString::number(ec), geoN);
                    en->nodeId = fakeId--;
                    en->nodeType = TreeNode::NodeType::Edge;
                }
                if (fc > 0) {
                    TreeNode* fn = new TreeNode("Face", QString::number(fc), geoN);
                    fn->nodeId = fakeId--;
                    fn->nodeType = TreeNode::NodeType::Face;
                }
                if (sc > 0) {
                    TreeNode* sn = new TreeNode("Solid", QString::number(sc), geoN);
                    sn->nodeId = fakeId--;
                    sn->nodeType = TreeNode::NodeType::Solid;
                }
            }
        }
    }

    // 只保留仍存在组件的管线开关，组件和模型的显示状态由子节点聚合。
    std::unordered_map<int, std::pair<bool, bool>> freshSub;
    for (TreeNode* mNode : rootNode->children) {
        for (TreeNode* cNode : mNode->children) {
            int ckey = cNode->nodeId;
            auto sit = sub_visibility_.find(ckey);
            const auto visibility = sit != sub_visibility_.end() ? sit->second : std::pair { true, true };
            freshSub[ckey] = visibility;
            // 无子节点的组件仍使用回传开关；有子节点时由 visibilityState 统一聚合。
            cNode->isVisible = visibility.first || visibility.second;
        }
    }
    sub_visibility_ = std::move(freshSub);
    // 删除组件时同时释放其派生统计与显示状态，避免组件身份复用后继承旧数据。
    std::erase_if(mesh_cell_type_counts_, [this](const auto& entry) { return !sub_visibility_.contains(entry.first); });
    std::erase_if(hidden_cell_types_, [this](const auto& entry) { return !sub_visibility_.contains(entry.first); });

    for (TreeNode* mNode : rootNode->children) {
        for (TreeNode* cNode : mNode->children) {
            syncSubNodes(cNode);
        }
    }

    endResetModel();
    
    delete oldRoot;
    return true;
}

bool TreeModel::requestVisibility(const QModelIndex& idx, bool visible)
{
    if (!idx.isValid() || !data(idx, CanToggleVisibilityRole).toBool())
        return false;
    const auto* target = getNode(idx);
    const int component_id = data(idx, ComponentIdRole).toInt();
    // 类型与整体请求都不修改展示副本，拒绝或延迟处理不会留下乐观状态。
    if (target->dimension != 0)
        emit meshCellVisibilityRequested(component_id, target->dimension, target->cellType, visible);
    else
        emit visibilityRequested(static_cast<int>(target->nodeType),
            target->nodeType == TreeNode::NodeType::Model ? target->nodeId : component_id, visible);
    return true;
}

void TreeModel::requestAllVisibility(bool visible)
{
    if (!modelQuery_)
        return;

    // 全局操作以模型查询为准，尚未进入展示树的模型也必须收到请求。
    const auto models = modelQuery_->listModels();
    for (const auto& model : models)
        emit visibilityRequested(static_cast<int>(TreeNode::NodeType::Model), model.toMap()["model_id"].toInt(), visible);
}

void TreeModel::syncSubNodes(TreeNode* node)
{
    for (TreeNode* child : node->children) {
        if (child->nodeType == TreeNode::NodeType::Mesh) {
            child->isVisible = sub_visibility_.at(node->nodeId).first;
        } else if (child->nodeType == TreeNode::NodeType::Geometry) {
            child->isVisible = sub_visibility_.at(node->nodeId).second;
        } else if (child->nodeType == TreeNode::NodeType::CellType) {
            auto* component = node;
            while (component->nodeType != TreeNode::NodeType::Component)
                component = component->parent;
            const auto it = hidden_cell_types_.find(component->nodeId);
            const bool hidden = it != hidden_cell_types_.end()
                && (child->dimension == 2 ? it->second.first : it->second.second).contains(child->cellType);
            child->isVisible = node->isVisible && !hidden;
        } else {
            child->isVisible = node->isVisible;
        }
        syncSubNodes(child);
    }
}

void TreeModel::emitDescendantDataChanged(const QModelIndex& parentIndex)
{
    int count = rowCount(parentIndex);
    for (int i = 0; i < count; ++i) {
        QModelIndex child = index(i, 0, parentIndex);
        emit dataChanged(child, child, { IsVisibleRole, VisibilityStateRole });
        emitDescendantDataChanged(child);
    }
}

QModelIndex TreeModel::findIndexByNodeId(int nodeId, int depth) const
{
    std::function<QModelIndex(TreeNode*, const QModelIndex&, int)> find =
        [&](TreeNode* parent, const QModelIndex& parentIdx, int currentDepth) -> QModelIndex {
            for (int i = 0; i < parent->children.size(); ++i) {
                TreeNode* child = parent->children[i];
                QModelIndex childIdx = createIndex(i, 0, child);
                if (child->nodeId == nodeId && currentDepth == depth)
                    return childIdx;
                QModelIndex found = find(child, childIdx, currentDepth + 1);
                if (found.isValid())
                    return found;
            }
            return QModelIndex();
        };
    return find(rootNode, QModelIndex(), 0);
}

void TreeModel::setModelQuery(QObject* query)
{
    auto* next = qobject_cast<QModelQuery*>(query);
    if (next == modelQuery_)
        return;

    // 组件身份仅在所属查询内有效，更换查询不能继承旧组件的回传和统计。
    modelQuery_ = next;
    sub_visibility_.clear();
    hidden_cell_types_.clear();
    mesh_cell_type_counts_.clear();
}

int TreeModel::visibilityState(const TreeNode* node) const
{
    if (node->children.isEmpty())
        return node->isVisible ? Qt::Checked : Qt::Unchecked;
    bool any_visible = false;
    bool all_visible = true;
    for (const auto* child : node->children) {
        const int state = visibilityState(child);
        any_visible |= state != Qt::Unchecked;
        all_visible &= state == Qt::Checked;
    }
    // 独立线单元不属于 2D/3D，继续由 Mesh 整体开关控制。
    if (node->hasIndependentEdges) {
        any_visible |= node->isVisible;
        all_visible &= node->isVisible;
    }
    return all_visible ? Qt::Checked : any_visible ? Qt::PartiallyChecked
                                                   : Qt::Unchecked;
}

QString TreeModel::stableKey(const TreeNode* node) const
{
    if (!node || node == rootNode)
        return { };
    const int identity = node->nodeType == TreeNode::NodeType::CellType                                  ? node->cellType
        : node->nodeType == TreeNode::NodeType::Model || node->nodeType == TreeNode::NodeType::Component ? node->nodeId
                                                                                                         : 0;
    return stableKey(node->parent) + "/" + QString::number(static_cast<int>(node->nodeType)) + ":" + QString::number(identity);
}

void TreeModel::notifyVisibility(const QModelIndex& idx)
{
    emitDescendantDataChanged(idx);
    for (auto current = idx; current.isValid(); current = parent(current))
        emit dataChanged(current, current, { IsVisibleRole, VisibilityStateRole });
}

void TreeModel::invalidateMeshCellTypeCounts(int component_id)
{
    if (component_id < 0)
        mesh_cell_type_counts_.clear();
    else
        mesh_cell_type_counts_.erase(component_id);
}

void TreeModel::syncComponentVisibility(int component_id, bool mesh_visible, bool geometry_visible,
    const QVariantList& hidden_faces, const QVariantList& hidden_solids)
{
    // 完整回传可能早于首次建树，先保存总开关和类型过滤，再一次更新展示。
    sub_visibility_[component_id] = { mesh_visible, geometry_visible };
    auto& hidden = hidden_cell_types_[component_id];
    hidden = { };
    for (const auto& type : hidden_faces)
        hidden.first.insert(type.toInt());
    for (const auto& type : hidden_solids)
        hidden.second.insert(type.toInt());
    const auto idx = findIndexByNodeId(component_id, 1);
    if (idx.isValid()) {
        auto* component = getNode(idx);
        component->isVisible = mesh_visible || geometry_visible;
        syncSubNodes(component);
        notifyVisibility(idx);
    }
}
