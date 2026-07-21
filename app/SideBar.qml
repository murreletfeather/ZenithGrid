/**
 * @file SideBar.qml
 * @brief 侧边栏，执行复杂算法时提供参数的交互界面
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import app.core
import app.model
import app.model.systems.algo

Item{
    id: root
    property var parameters: []
    property var resultText: ""

    readonly property var activeOp: App.activeOperation
    readonly property bool isMeasureOp: !!(activeOp && activeOp.info && activeOp.info.name === "MeasurePlugin")
    readonly property bool hasInteractive: !!(activeOp && activeOp.info && activeOp.info.interactive)
    property int measureMode: 0 // 0=交互模式 1=参数执行

    onActiveOpChanged: {
        parameters = []
        resultText = ""
        measurePanel.measureResult = ""
        updateMeasureActive()
    }
    onMeasureModeChanged: updateMeasureActive()
    // activeOp 变化通知触发时 hasInteractive 绑定可能尚未刷新（链式绑定），
    // 直接监听才能保证首次打开交互插件就正确进入交互模式
    onHasInteractiveChanged: updateMeasureActive()

    // 视口交互与参数执行互斥：进出交互模式时按插件名启停渲染窗口的交互服务
    function updateMeasureActive() {
        var active = hasInteractive && measureMode === 0
        var rw = App.registry.renderWindow
        if (rw) {
            if (active)
                rw.startInteraction(activeOp.info.name)
            else
                rw.stopInteraction()
            measureConn.target = active ? rw : null
        }
        if (active) {
            App.selection.listeningSelectorIndex = -1
            App.selection.selectMode = "None"
        }
    }
    // 声明视口交互能力的插件专用：面板内切换交互模式/参数执行
    Row{
        id: measureModeSwitch
        visible: root.hasInteractive
        height: visible ? 34 : 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8
        RadioButton{
            text: "交互模式"
            // 关闭互斥自动写回，避免破坏 checked 绑定；互斥由 measureMode 驱动
            autoExclusive: false
            checked: root.measureMode === 0
            onClicked: root.measureMode = 0
        }
        RadioButton{
            text: root.isMeasureOp ? "尺寸标注" : "参数执行"
            autoExclusive: false
            checked: root.measureMode === 1
            onClicked: root.measureMode = 1
        }
    }

    // 写入参数值；功能的参数为持久参数，修改即时写回功能系统实时生效
    function setParam(index, value) {
        parameters[index] = value
        if (root.activeOp && root.activeOp.isFeature)
            QModelManager.featureSystem.setParameter(root.activeOp.info.name, index, value)
    }

    Button{
        id: commitButton
        text: "执行"
        enabled: !!(root.activeOp && root.activeOp.info && (root.activeOp.isFeature || App.selection.activeModelId >= 0))
        visible: !(root.hasInteractive && root.measureMode === 0)
        anchors.top: measureModeSwitch.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? 30 : 0
        onClicked:{
            if (root.activeOp && root.activeOp.execute) {
                var r = root.activeOp.execute(App.selection.activeComponentId, root.parameters)
                // 结果路由由插件声明的 result_display 决定：popup 走弹窗，其余走侧栏
                if (root.activeOp.info && root.activeOp.info.result_display === "popup")
                    App.showDialog(root.activeOp.info.display_name + "结果", r)
                else
                    root.resultText = r
            }
            if (App.registry.renderWindow)
                App.registry.renderWindow.clearSelection()
        }
    }
    TextArea {
        id: resultArea
        anchors.top: commitButton.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        // 不可见时不占锚定布局高度，避免留下空白
        height: visible ? 80 : 0
        readOnly: true
        text: root.resultText
        wrapMode: TextEdit.Wrap
        // 声明 popup 的插件结果走弹窗，侧栏结果区只服务其余算法
        visible: text.length > 0 && !(root.activeOp && root.activeOp.info && root.activeOp.info.result_display === "popup")
        background: Rectangle {
            color: "#f0f0f0"
            border.color: "#d0d0d0"
            border.width: 1
        }
    }
    Item{
        anchors.top: resultArea.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        visible: !(root.hasInteractive && root.measureMode === 0)
        ColumnLayout{
            anchors.fill: parent
            ListView{
                id:parameterList
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.margins: 3
                spacing: 5
                model: root.activeOp ? root.activeOp.info.arg_types : []
                delegate:Component{
                    Loader{
                        required property var model
                        required property int index
                        sourceComponent:{
                            if(model.type === QArgType.Path){           //文件
                                return fileComponent
                            }
                            if(model.type === QArgType.Combo){           //多选一
                                return componentComboBox
                            }
                            if(model.type === QArgType.Float){           //数字框
                                return oneNumberBox
                            }
                            if(model.type === QArgType.Selector){           //选择器
                                return selectorComponent
                            }
                            if(model.type === QArgType.Text){           //文字输入框
                                return textComponent
                            }
                            if(model.type === QArgType.Bool){           //布尔值
                                return boolComponent
                            }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    background: Rectangle {
                        implicitWidth: 8
                        color: "transparent"
                    }
                    contentItem: Rectangle {
                        implicitWidth: 8
                        radius: 4
                        color: parent.hovered ? "#c0c0c0" : "#e0e0e0"
                    }
                }
            }
        }
    }

    // 交互测量面板：说明、结果与清除
    Column{
        id: measurePanel
        visible: root.isMeasureOp && root.measureMode === 0
        anchors.top: measureModeSwitch.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 4
        spacing: 8
        property string measureResult: ""

        Text{
            width: parent.width
            wrapMode: Text.Wrap
            text: "依次点击拾取两点构成一条直线并显示长度；可连续画多条线，两线共端点时自动显示夹角；同一点连点两次取消当前起笔。"
        }
        TextArea{
            width: parent.width
            height: 80
            readOnly: true
            text: measurePanel.measureResult
            wrapMode: TextEdit.Wrap
            visible: text.length > 0
            background: Rectangle {
                color: "#f0f0f0"
                border.color: "#d0d0d0"
                border.width: 1
            }
        }
        Button{
            text: "清除"
            onClicked: {
                var rw = App.registry.renderWindow
                if (rw) rw.clearInteraction()
            }
        }
    }
    Connections{
        id: measureConn
        target: null
        function onInteractionUpdated(resultText) {
            measurePanel.measureResult = resultText
        }
    }

    Component{
        id:componentComboBox
        RowLayout{
            spacing: 5
            width: parameterList.width
            property var value: null
            ListModel{
                id: comboModel
            }
            Text{
                id:nametext
                text: model.name
            }
            Rectangle{
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "black"
            }
            ComboBox{
                id:parameterComboBox
                model: comboModel
                onCurrentIndexChanged: {
                    value = currentIndex
                    root.setParam(index, value)
                }
            }

            Component.onCompleted: {
                if(model && model.content){
                    let parts = model.content.split("|")
                    let items = parts[0].split(",")
                    comboModel.clear()
                    for(let i=0; i<items.length; i++){
                        comboModel.append({"text":items[i]})
                    }
                    let defaultIndex = parts.length > 1 ? parseInt(parts[1]) : 0
                    if (isNaN(defaultIndex) || defaultIndex < 0 || defaultIndex >= items.length) {
                        defaultIndex = 0
                    }
                    parameterComboBox.currentIndex = defaultIndex
                    value = defaultIndex
                    root.setParam(index, value)
                }
                value = parameterComboBox.currentIndex
                root.setParam(index, value)
            }
        }
    }
    Component{
        id:oneNumberBox
        RowLayout{
            spacing: 5
            width: parameterList.width
            Text{
                id:nametext
                text: model.name
            }
            Rectangle{
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "black"
            }
            TextField {
                id:parameterTextInput
                Layout.fillWidth: parent.width
                text: model.content
                onTextChanged:{
                    root.setParam(index, parseFloat(text))
                }
            }
        }
    }
    Component{
        id:fileComponent
        RowLayout{
            spacing: 5
            width: parameterList.width
            Text{
                id:nametext
                text: model.name
            }
            Rectangle{
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "black"
            }
            TextArea{
                id:fileText
                wrapMode: TextEdit.Wrap
                Layout.fillWidth: parent.width

                Component.onCompleted: {
                    fileText.text = model.content
                    root.setParam(index, fileText.text)
                }
                onEditingFinished: {
                    root.setParam(index, fileText.text)
                }
            }
            Button{
                text: "...."
                onClicked:{
                    parameterFileDialog.open()
                }
            }
            FileDialog{
                id:parameterFileDialog
                onAccepted:{
                    fileText.text = urlToPath(selectedFile)
                    root.setParam(index, fileText.text)
                }

                function urlToPath(url) {
                    var urlString = new String(url)
                    var s
                    if (urlString.startsWith("file:///")) {
                        var k = urlString.charAt(9) === ':' ? 8 : 7
                        s = urlString.substring(k)
                    } else {
                        s = urlString
                    }
                    return decodeURIComponent(s);
                }
            }
        }
    }
    Component{
        id:textComponent
        RowLayout{
            spacing: 5
            width: parameterList.width
            property var value: fileText.text
            Text{
                id:nametext
                text: model.name
                Layout.preferredWidth: 120
                elide: Text.ElideRight
                ToolTip.visible: nameHover.hovered && model.description.length > 0
                ToolTip.text: model.description
                HoverHandler {
                    id: nameHover
                }
            }
            Rectangle{
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "black"
            }
            TextArea{
                id:fileText
                wrapMode: TextEdit.Wrap
                Layout.fillWidth: true
                placeholderText: model.description
                ToolTip.visible: hovered && model.description.length > 0
                ToolTip.text: model.description

                Component.onCompleted: {
                    fileText.text = model.content
                    root.setParam(index, fileText.text)
                }
                onEditingFinished: {
                    root.setParam(index, fileText.text)
                }
            }
        }
    }
    Component{
        id: selectorComponent
        RowLayout{
            spacing: 5
            width: parameterList.width
            property var value: null

            Text{
                id:nametext
                text: model.name
            }
            Rectangle{
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "black"
            }
            Text{
                id:selectedItems
                text: value ? value.size():"无"
            }

            Button{
                id: selectStartButton
                text: "开始选择"
                checked: App.selection.listeningSelectorIndex === index
                onClicked: {
                    if (!checked) {
                        App.selection.listeningSelectorIndex = index
                        if (App.selection.selectMode === "None") {
                            // 活动组件为纯几何（无网格）时默认切到几何点模式，
                            // 网格组件则用参数给定的首选模式
                            const cid = App.selection.activeComponentId
                            const meshSummary = cid >= 0 ? QModelManager.query.getMeshSummary(cid) : ({})
                            const geomSummary = cid >= 0 ? QModelManager.query.getGeometrySummary(cid) : ({})
                            if (!meshSummary.has_mesh && geomSummary.has_geometry) {
                                App.selection.selectMode = "GeometryVertex"
                            } else if (model.content.length > 0) {
                                const modes = model.content.split(",")
                                if (modes.length > 0)
                                    App.selection.selectMode = modes[0]
                            } else {
                                App.selection.selectMode = "Face"
                            }
                        }
                    } else {
                        App.selection.listeningSelectorIndex = -1
                    }
                }
            }

            Connections {
                target: App.selection
                enabled: selectStartButton.checked
                function onConfirmed(selection) {
                    value = selection
                    root.setParam(index, value)
                    App.selection.listeningSelectorIndex = -1
                }
            }
        }
    }
    Component{
        id: boolComponent
        RowLayout{
            spacing: 5
            width: parameterList.width

            Text{
                id:nametext
                text: model.name
            }
            Rectangle{
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: "black"
            }
            CheckBox{
                id: parameterCheckBox

                Component.onCompleted: {
                    checked = (model.content === "true")
                    root.setParam(index, checked)
                }
                onCheckedChanged: {
                    root.setParam(index, checked)
                }
            }
        }
    }
}
