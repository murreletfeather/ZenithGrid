/**
 * @file StartupFileImport.qml
 * @brief 按命令行顺序打开文件，并等待后台导入完成。
 */
import QtQuick
import app.core
import app.model

QtObject {
    id: root
    required property var featureSystem
    required property var ioSystem
    required property var taskStatus

    property var _urls: []
    property int _nextIndex: 0
    property bool _imported: false
    property bool _waitingForTask: false
    property bool _taskSucceeded: false

    signal finished(bool imported)

    function open(urls) {
        if (_urls.length || !urls.length)
            return;
        _urls = Array.from(urls);
        processNext();
    }

    function processNext() {
        if (!_urls.length || _waitingForTask || taskStatus.writePending)
            return;
        while (_nextIndex < _urls.length && !_waitingForTask && !taskStatus.writePending)
            openFile(_urls[_nextIndex++]);
        if (_waitingForTask || _nextIndex < _urls.length)
            return;

        const imported = _imported;
        _urls = [];
        _nextIndex = 0;
        _imported = false;
        finished(imported);
    }

    function openFile(url) {
        const path = App.localFilePath(url);
        const filename = path.split(/[\\/]/).pop();
        const dot = filename.lastIndexOf(".");
        const suffix = dot < 0 ? "" : filename.slice(dot + 1).toLowerCase();
        // 旧 IO 的桥接条目仍由原启动读取入口处理。
        const matches = featureSystem.importFileTypes.filter(type => !type.legacy_io && suffix
            && type.extensions.some(ext => ext.toLowerCase() === suffix));
        if (!matches.length) {
            if (ioSystem.read("All files", url, []))
                _imported = true;
            else
                taskStatus.reportFailure(qsTr("启动打开失败：%1").arg(path));
            return;
        }
        if (matches.length > 1) {
            taskStatus.reportFailure(qsTr("多个导入功能匹配 %1，请从文件菜单选择格式。").arg(path));
            return;
        }

        const name = matches[0].feature;
        const info = featureSystem.featuresInfo.find(item => item.name === name);
        const pathType = info?.arg_types[0]?.type;
        if ((pathType !== QArgType.Path && pathType !== QArgType.Paths)
                || !featureSystem.setParameter(name, 0, pathType === QArgType.Paths ? [path] : path)) {
            taskStatus.reportFailure(qsTr("无法设置启动文件路径：%1").arg(path));
            return;
        }
        const invocation = featureSystem.invokeFile(name);
        if (!invocation.succeeded)
            return;
        const result = invocation.result;
        if (result?.submitted === true) {
            _waitingForTask = true;
            _taskSucceeded = true;
        } else if (result?.submitted === false) {
            taskStatus.reportFailure(qsTr("无法启动导入任务：%1").arg(path));
        } else if (result === undefined || result === null) {
            _imported = true;
        } else if (typeof result === "string") {
            taskStatus.showMessage(result);
        }
    }

    readonly property Connections taskConnections: Connections {
        target: root.taskStatus

        function onTaskFailed() {
            if (root._waitingForTask && !root.taskStatus.writePending)
                root._taskSucceeded = false;
        }

        function onTaskCancelled() {
            if (root._waitingForTask) {
                root._taskSucceeded = false;
                root._nextIndex = root._urls.length;
            }
        }

        function onTaskFinished() {
            if (root._waitingForTask) {
                root._imported = root._imported || root._taskSucceeded;
                root._waitingForTask = false;
            }
            // Session 在终态信号返回后重放模型通知，再开始下一文件。
            if (root._urls.length)
                Qt.callLater(root.processNext);
        }
    }
}
