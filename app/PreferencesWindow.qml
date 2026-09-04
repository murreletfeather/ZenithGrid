import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import app.model

Item {
    id: root
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 120
                Layout.fillHeight: true
                color: "#f0f0f0"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2

                    ButtonGroup {
                        id: categoryGroup
                    }

                    ToolButton {
                        text: qsTr("主题")
                        Layout.fillWidth: true
                        checkable: true
                        checked: true
                        ButtonGroup.group: categoryGroup
                        onClicked: panelStack.currentIndex = 0
                    }

                    ToolButton {
                        text: qsTr("插件")
                        Layout.fillWidth: true
                        checkable: true
                        ButtonGroup.group: categoryGroup
                        onClicked: panelStack.currentIndex = 1
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            ToolSeparator {
                Layout.fillHeight: true
            }

            StackLayout {
                id: panelStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 8
                currentIndex: 0

                // 主题面板：语言选择器置顶左上锚定，主题其余设置待开发
                ColumnLayout {
                    RowLayout {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                        Layout.margins: 8
                        spacing: 6

                        Label {
                            text: qsTr("语言")
                        }

                        ComboBox {
                            id: languageComboBox
                            model: QLanguageManager.languageOptions
                            textRole: "name"
                            valueRole: "code"

                            // 绑定建立在自己的属性上，外部语言变化（启动恢复等）时同步选择框
                            property string sourceLanguage: QLanguageManager.currentLanguage
                            onSourceLanguageChanged: currentIndex = indexOfValue(sourceLanguage)
                            Component.onCompleted: currentIndex = indexOfValue(sourceLanguage)

                            onActivated: QLanguageManager.setLanguage(currentValue)
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("主题设置 - 待开发")
                            color: "#888"
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: pluginListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: QModelManager.systemPluginManager.pluginNames
                        currentIndex: -1

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 30
                            color: pluginListView.currentIndex === index ? "gray" : "white"
                            border.color: "#ccc"
                            border.width: 1

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                elide: Text.ElideRight
                                color: pluginListView.currentIndex === index ? "white" : "black"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (pluginListView.currentIndex === index) {
                                        pluginListView.currentIndex = -1
                                    } else {
                                        pluginListView.currentIndex = index
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ToolSeparator {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            visible: panelStack.currentIndex === 1

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("注册")
                implicitWidth: 70
                implicitHeight: 26
                onClicked: pluginFileDialog.open()
            }

            Button {
                text: qsTr("注销")
                implicitWidth: 70
                implicitHeight: 26
                enabled: pluginListView.currentIndex !== -1
                onClicked: {
                    var selectedPlugin = QModelManager.systemPluginManager.pluginNames[pluginListView.currentIndex]
                    var pluginPath = QModelManager.systemPluginManager.getPluginPath(selectedPlugin)
                    QModelManager.systemPluginManager.unregisterPlugin(pluginPath)
                    pluginListView.currentIndex = -1
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: panelStack.currentIndex === 0

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("恢复默认")
                implicitWidth: 70
                implicitHeight: 26
                // 默认语言为简体中文：恢复默认即切回源语言（卸载翻译器，界面文案回中文）
                onClicked: QLanguageManager.setLanguage("zh_CN")
            }

            Button {
                text: qsTr("应用")
                implicitWidth: 70
                implicitHeight: 26
            }
        }
    }

    FileDialog {
        id: pluginFileDialog
        title: qsTr("请选择插件文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Plugin files (*.dll *.so)"), qsTr("All files (*)")]

        onAccepted: {
            var result = QModelManager.systemPluginManager.registerPlugin(selectedFile)
            if (!result) {
                registerFailureDialog.open()
            }
        }
    }

    Dialog {
        id: registerFailureDialog
        title: qsTr("注册失败")
        standardButtons: Dialog.Ok
        modal: true
        anchors.centerIn: parent

        Label {
            text: qsTr("插件注册失败")
        }
    }
}
