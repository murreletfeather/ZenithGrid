#pragma once

#include <QAbstractItemModel>
#include <QVariant>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <unordered_map>

#include "QModelQuery.h"

struct TreeNode {
    enum class NodeType {
        Model, Component, Mesh, Geometry,
        D2, D3,
        Vertex, Edge, Face, Solid
    };

    QString name;
    QString number;
    int nodeId = -1;
    NodeType nodeType = NodeType::Component;
    bool isVisible = true;
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
        NameRole    = Qt::UserRole + 1,
        NumberRole,
        NodeIdRole,
        IsVisibleRole,
        ComponentIdRole,
        NodeTypeRole
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
    Q_INVOKABLE bool setVisibility(const QModelIndex& idx, bool visible);
    Q_INVOKABLE QModelIndex findIndexByNodeId(int nodeId, int depth) const;
    Q_INVOKABLE void setAllVisibility(bool visible);

    /**
     * @brief 界面语言切换后通知各行名重新取值（轻量：不加锁展开/选中状态）
     *
     * 节点名按当前界面语言显示（见 DisplayTranslate.h），语言切换后经本方法
     * 递归重发 NameRole 的 dataChanged，QML 视图重新读取即可切换语言。
     */
    Q_INVOKABLE void notifyNamesChanged();

    QObject* getModelQuery() const { return modelQuery_; }
    void setModelQuery(QObject* query);

private:
    TreeNode* getNode(const QModelIndex& index) const;
    void emitDescendantDataChanged(const QModelIndex& parentIndex, const QVector<int>& roles = QVector<int> { IsVisibleRole });
    void setComponentVisibility(TreeNode* node, bool visible);
    void syncSubNodes(TreeNode* node);
    void propagateUp(TreeNode* node);

    TreeNode* rootNode = nullptr;
    QModelQuery* modelQuery_ = nullptr;
    std::unordered_map<int, bool> components_visibility_;
    std::unordered_map<int, std::pair<bool, bool>> sub_visibility_; // first=mesh visible, second=geometry visible
};
