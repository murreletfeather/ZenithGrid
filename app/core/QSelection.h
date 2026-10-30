#ifndef Q_SELECTION_H
#define Q_SELECTION_H
#include "QElement.h"
#include "Selection.h"
#include <QJSEngine>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlintegration.h>

/**
 * @brief Selection类的QML交互接口。Selection可以存于QSelection中，就可以在QML代码中进行交互。C++中使用new来创建对象，QML中为只读对象
 */
class QSelection : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    QSelection() { }
    QSelection(std::unique_ptr<Selection> data)
        : data_(std::move(data))
    {
    }

    /**
     * 存入
     * @param data 待存入的数据
     */
    void set(std::shared_ptr<Selection> data) { data_ = data ? std::make_shared<Selection>(*data) : nullptr; }
    /**
     * 取出
     * @return 存储的Selection对象
     */
    std::shared_ptr<Selection> get() { return data_; }

    /**
     * @brief 获取该选择的数组大小
     * @return 存储数组大小
     */
    Q_INVOKABLE size_t size()
    {
        if (data_) {
            return data_->size();
        }
        return 0;
    }
    /**
     * @brief 获取选中数据的类型枚举值
     * @return 类型枚举值
     */
    Q_INVOKABLE Element::Type type() { return static_cast<Element::Type>(data_ ? data_->type() : ElementEnum::None); }
    /**
     * @brief 初始化一个Selection数据
     */
    Q_INVOKABLE void initialize()
    {
        std::unique_ptr<Selection> temp = std::make_unique<Selection>();
        temp->setType(ElementEnum::Face);
        for (int i = 1; i < 2; i++) {
            temp->ids().push_back(i);
        }
        data_ = std::move(temp);
    }
    /**
     * @brief ids 返回选择对象的id值表的第i个元素
     * @param i   想要返回第几个元素
     * @return    int类
     */
    Q_INVOKABLE int ids(int i)
    {
        return data_ && data_->type() != ElementEnum::GeometryPoint && i >= 0 && static_cast<size_t>(i) < data_->size() ? data_->ids()[i] : -1;
    }
    /**
     * @brief 返回选中对象的模型 ID
     * @return 模型id
     */
    Q_INVOKABLE Index getComponentId()
    {
        return data_ ? data_->component_id : -1;
    }
    Q_INVOKABLE QVariantList getAsComponentIds()
    {
        QVariantList list;
        if (!data_)
            return list;
        if (data_->type() == ElementEnum::Component) {
            for (auto id : data_->ids())
                list.append(id);
        }
        return list;
    }

    /** @brief 按顺序查询一个点的支撑和吸附来源，越界时返回空映射。 */
    Q_INVOKABLE QVariantMap point(int index) const
    {
        if (!data_ || index < 0 || static_cast<std::size_t>(index) >= data_->size())
            return { };
        const auto* points = data_->get<ElementEnum::GeometryPoint>();
        if (!points)
            return { };
        const auto& point = (*points)[index];
        return {
            { "componentId", point.component_id },
            { "supportKind", static_cast<int>(point.support_kind) },
            { "supportId", point.support_id },
            { "worldPosition", QVariantList { point.world_pos[0], point.world_pos[1], point.world_pos[2] } },
            { "edgeParameter", point.edge_parameter },
            { "faceUV", QVariantList { point.face_uv[0], point.face_uv[1] } },
            { "snapKind", static_cast<int>(point.snap_kind) },
            { "snapId", point.snap_id },
            { "snapEdgeParameter", point.snap_edge_parameter },
            { "valid", point.valid },
        };
    }

private:
    std::shared_ptr<Selection> data_;
};

#endif //  Q_SELECTION_H
