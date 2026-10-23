pragma Singleton
import QtQuick

QtObject {
    property QtObject selection: QtObject {
        property int activeModelId: -1
        property int activeComponentId: -1
        property string selectMode: "None"
        // 框选是否穿透：true 走穿透拾取（可选中被遮挡的背面元素；网格模式下还包括体网格内部的体单元/节点/边），false 只选可见最前一层。
        // 对网格、几何、组件三种模式的框选均生效（默认非穿透），单选恒为非穿透。
        property bool penetrating: false
        property int listeningSelectorIndex: -1
        // 切换参数或结束选择时递增，丢弃旧渲染任务排队中的选择结果。
        property int selectionRevision: 0
        onListeningSelectorIndexChanged: ++selectionRevision
        signal selectionUpdated(var sel)
        signal selectionInvalidated()
    }

    property var activeOperation: null
    property bool importDragActive: false
    property var registry: ({})

    signal modelVisibilityUpdated(int modelId, bool visible)
    signal componentVisibilityUpdated(int componentId, bool meshVisible, bool geometryVisible)
}
