#pragma once

#include <QAbstractItemModel>
#include <QVariant>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <map>
#include <set>
#include <unordered_map>

#include "QModelQuery.h"

/** @brief 对象树节点；维度和单元类型共同确定网格子节点的稳定身份。 */
struct TreeNode {
    enum class NodeType {
        Model,
        Component,
        Mesh,
        Geometry,
        D2,
        D3,
        Vertex,
        Edge,
        Face,
        Solid,
        CellType
    };

    QString name;
    QString number;
    int nodeId = -1;
    NodeType nodeType = NodeType::Component;
    bool isVisible = true;
    int dimension = 0;
    int cellType = -1;
    bool hasIndependentEdges = false;
    QVector<TreeNode*> children;
    TreeNode* parent = nullptr;

    TreeNode(const QString& n, const QString& num = "", TreeNode* p = nullptr)
        : name(n), number(num), parent(p)
    {
        if (p)
            p->children << this;
    }

    ~TreeNode() { qDeleteAll(children); }
};

class TreeModel : public QAbstractItemModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(TreeModel)
    Q_PROPERTY(QObject* modelQuery READ getModelQuery WRITE setModelQuery REQUIRED)

public:
    enum TreeRole {
        NameRole = Qt::UserRole + 1,
        NumberRole,
        NodeIdRole,
        IsVisibleRole,
        ComponentIdRole,
        NodeTypeRole,
        VisibilityStateRole,
        CanToggleVisibilityRole,
        StableKeyRole
    };
    Q_ENUM(TreeRole)
    explicit TreeModel(QObject* parent = nullptr);
    ~TreeModel();

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool refresh();
    /** @brief 只发送节点显隐意图；展示状态由管线回传后更新。 */
    Q_INVOKABLE bool requestVisibility(const QModelIndex& idx, bool visible);
    Q_INVOKABLE QModelIndex findIndexByNodeId(int nodeId, int depth) const;
    /** @brief 按模型发送全部节点的显隐请求，不提前修改展示副本。 */
    Q_INVOKABLE void requestAllVisibility(bool visible);
    /** @brief 模型通知使指定组件的规模缓存失效；负值使全部缓存失效。 */
    Q_INVOKABLE void invalidateMeshSummary(int component_id = -1);
    /** @brief 同步视口的整体开关，不触发新的渲染命令或重置类型过滤。 */
    Q_INVOKABLE void syncComponentVisibility(int component_id, bool mesh_visible, bool geometry_visible);
    /** @brief 同步 GUI 管线持有的类型过滤结果，重建父节点聚合状态。 */
    Q_INVOKABLE void syncMeshCellVisibility(int component_id, const QVariantList& hidden_faces, const QVariantList& hidden_solids);

    QObject* getModelQuery() const { return modelQuery_; }
    void setModelQuery(QObject* query);

signals:
    /** @brief 请求模型、组件、Mesh 或 Geometry 显隐；object_id 为真实模型或组件身份。 */
    void visibilityRequested(int node_type, int object_id, bool visible);
    /** @brief 请求视口调整某类型或整个维度（cell_type=-1），由管线回传最终状态。 */
    void meshCellVisibilityRequested(int component_id, int dimension, int cell_type, bool visible);

private:
    TreeNode* getNode(const QModelIndex& index) const;
    void emitDescendantDataChanged(const QModelIndex& parentIndex);
    void syncSubNodes(TreeNode* node);
    void propagateUp(TreeNode* node);
    /** @brief 聚合节点的全显、部分显示和全隐状态。 */
    int visibilityState(const TreeNode* node) const;
    /** @brief 返回刷新前后不变的语义路径，用于保留展开状态。 */
    QString stableKey(const TreeNode* node) const;
    /** @brief 通知整个组件子树及全部祖先，覆盖任意深度的类型节点。 */
    void notifyVisibility(const QModelIndex& index);

    TreeNode* rootNode = nullptr;
    QModelQuery* modelQuery_ = nullptr;
    std::unordered_map<int, bool> components_visibility_;
    std::unordered_map<int, std::pair<bool, bool>> sub_visibility_; // first=mesh visible, second=geometry visible
    std::map<int, std::pair<std::set<int>, std::set<int>>> hidden_cell_types_; //> 组件下隐藏的面/体类型
    std::map<int, QVariantMap> mesh_summaries_; //> 只在模型通知后重新统计
};
