pragma Singleton
import QtQuick

QtObject {
    property QtObject selection: QtObject {
        property int activeModelId: -1
        property int activeComponentId: -1
        property string selectMode: "None"
        property int listeningSelectorIndex: -1
        // 切换参数或结束选择时递增，丢弃旧渲染任务排队中的选择结果。
        property int selectionRevision: 0
        onListeningSelectorIndexChanged: ++selectionRevision
        signal selectionUpdated(var sel)
        // 用户主动清空；操作会话处理结束后的参数，视口仍走原有清空入口。
        signal clearSelectionRequested()
        signal selectionInvalidated()
    }

    property var activeOperation: null
    property bool importDragActive: false
    property var registry: ({})
}
