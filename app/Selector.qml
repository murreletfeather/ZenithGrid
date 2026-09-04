/**
 * @file Selector.qml
 * @brief 选择器，位于模型界面左上角，选择项类型与清空选择的交互界面
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import app.core

RowLayout {
    id:root
    signal clearButtonClicked
    signal confirmButtonClicked
    property QSelection selection

    // 当前角度扩散参数，由渲染区域传入。
    property bool faceSelectByAngle
    property real faceSelectAngle

    // 将用户编辑后的参数通知给渲染区域。
    signal faceSelectionSpreadEdited(bool enabled, real angle)

    ComboBox{
        id: selectModeComboBox
        model: [
            { text: "...", value: "None" },
            { text: qsTr("组件"), value: "Component" },
            { text: qsTr("点"), value: "Vertex" },
            { text: qsTr("边"), value: "Edge" },
            { text: qsTr("面"), value: "Face" },
            { text: qsTr("体"), value: "Solid" },
            { text: qsTr("几何点"), value: "GeometryVertex" },
            { text: qsTr("几何边"), value: "GeometryEdge" },
            { text: qsTr("几何面"), value: "GeometryFace" },
            { text: qsTr("几何体"), value: "GeometrySolid" },
        ]
        textRole: "text"
        valueRole: "value"
        // 宽度为隐式宽度与最大项宽度的较大值：保证弹窗项完整显示（如 Geometry Solid）
        Layout.preferredWidth: Math.max(implicitWidth, 140)
        Layout.minimumWidth: 0

        // 绑定建立在自己的属性上——ComboBox 内部不会碰它，绑定不会被用户交互破坏
        property string sourceMode: App.selection.selectMode

        // 外部值变化 → 更新显示（初始化时也会触发一次，不用 Component.onCompleted）
        onSourceModeChanged: {
            const idx = indexOfValue(sourceMode)
            if (idx >= 0)
                currentIndex = idx
        }

        // 用户选择 → 写回数据源
        onActivated: App.selection.selectMode = currentValue

        opacity: enabled ? 1.0 : 0.6
    }
    Button{
        id: selectClearButton
        text: qsTr("清除选择")
        onClicked: root.clearButtonClicked()
        opacity: enabled ? 1.0 : 0.6
    }
    CheckBox {
        text: qsTr("按角度扩散")
        checked: root.faceSelectByAngle
        visible: App.selection.selectMode === "Face"
        onClicked: root.faceSelectionSpreadEdited(checked, root.faceSelectAngle)
    }
    Label {
        text: qsTr("角度")
        visible: App.selection.selectMode === "Face" && root.faceSelectByAngle
    }
    TextField {
        text: root.faceSelectAngle.toFixed(1)
        visible: App.selection.selectMode === "Face" && root.faceSelectByAngle
        Layout.preferredWidth: 56
        validator: DoubleValidator {
            bottom: 0.0
            top: 180.0
            decimals: 2
        }
        onEditingFinished: {
            var value = Number(text)
            if (!isNaN(value))
                root.faceSelectionSpreadEdited(root.faceSelectByAngle,Math.max(0.0, Math.min(180.0, value)))
        }
    }
    Button{
        text: qsTr("确认")
        onClicked: {
            root.confirmButtonClicked()
            App.selection.confirmed(root.selection)
        }
        enabled: App.selection.listeningSelectorIndex >= 0
        opacity: enabled ? 1.0 : 0.6
    }
    /** type:string 选择框中当前文本 */
    property alias comboBoxSelectedString: selectModeComboBox.currentText
}
