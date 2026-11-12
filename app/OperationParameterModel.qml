/**
 * @file OperationParameterModel.qml
 * @brief 操作参数的当前值、默认值和功能系统同步，不依赖参数控件生命周期。
 */
import QtQml
import app.core
import app.model

QtObject {
    id: root

    required property var featureSystem
    property bool _acceptingControlEdits: true
    property var values: []
    property var argumentTypes: []
    property string featureName: ""
    readonly property QSelection _emptySelection: QSelection {}
    property int contextIndex: -1
    property string _idleMode: "None"
    readonly property var renderWindow: App.registry.renderWindow || null

    // 统一结果只写入当前接收参数；控件销毁或滚出视区不会中断监听。
    readonly property Connections _selectionChanges: Connections {
        target: App.selection
        function onSelectionUpdated(value) {
            if (App.selection.listeningSelectorIndex >= 0)
                root.setValue(App.selection.listeningSelectorIndex, value);
        }
    }
    readonly property Connections _viewportChanges: Connections {
        target: root.renderWindow
        function onSelectionFinished(revision) {
            if (revision === App.selection.selectionRevision) {
                App.selection.listeningSelectorIndex = -1;
                App.selection.selectMode = root._idleMode;
            }
        }
        function onGeometryInvalidated(componentId) {
            for (let i = 0; i < root.values.length; ++i) {
                const value = root.values[i];
                if (root.argumentTypes[i].type === QArgType.Selector && value && value.isGeometryPoint()
                    && value.getComponentId() === componentId) {
                    if (App.selection.listeningSelectorIndex === i)
                        root.stopSelection();
                    root.setValue(i, null);
                }
            }
        }
    }

    function startSelection(index) {
        root.stopSelection();
        root.contextIndex = index;
        root._idleMode = App.selection.selectMode;
        App.selection.listeningSelectorIndex = index;
        const content = root.argumentTypes[index].content;
        let mode = content.length > 0 ? content.split(",")[0] : "";
        if (!mode) {
            const cid = App.selection.activeComponentId;
            const mesh = cid >= 0 ? QModelManager.query.getMeshSummary(cid) : ({});
            const geometry = cid >= 0 ? QModelManager.query.getGeometrySummary(cid) : ({});
            mode = !mesh.has_mesh && geometry.has_geometry ? "GeometryVertex" : "Face";
        }
        // 同类型重新开始也要配置；旧值保留到用户真正修改选择。
        if (App.selection.selectMode === mode) {
            if (root.renderWindow)
                root.renderWindow.setSelectMode(mode);
        } else {
            App.selection.selectMode = mode;
        }
    }

    function finishSelection() {
        if (root.renderWindow)
            root.renderWindow.finishSelection(root._idleMode);
        else
            root.stopSelection();
    }

    function stopSelection() {
        if (App.selection.listeningSelectorIndex < 0)
            return;
        App.selection.listeningSelectorIndex = -1;
        App.selection.selectMode = root._idleMode;
    }

    // 选择中由视口的空结果回写；结束后只清最近操作的参数，不重复提交。
    function clearSelection() {
        if (App.selection.listeningSelectorIndex < 0 && root.contextIndex >= 0)
            root.setValue(root.contextIndex, null);
    }

    Component.onDestruction: root.stopSelection()

    readonly property Connections _featureChanges: Connections {
        target: root.featureSystem
        function onParamValueChanged(feature, index, value) {
            if (feature === root.featureName)
                root._updateValue(index, value);
        }
    }

    function reset(operation) {
        root.stopSelection();
        root.contextIndex = -1;
        root.argumentTypes = operation && operation.info ? operation.info.arg_types : [];
        root.featureName = operation && operation.isFeature && operation.info ? operation.info.name : "";
        // 分类默认值只覆盖声明的参数，其他 Feature 参数保持其持久值。
        const presets = operation && operation.categoryId && operation.info && operation.info.category_defaults ? operation.info.category_defaults[operation.categoryId] : null;
        if (root.featureName && presets) {
            const defaults = root._createDefaultValues(operation);
            root.argumentTypes.forEach((arg, index) => {
                if (presets[arg.name] !== undefined)
                    root.featureSystem.setParameter(root.featureName, index, defaults[index]);
            });
        }
        // 功能系统持有长期参数；进入面板只读取快照，不用默认值覆盖或派发初始化事件。
        root.values = root.featureName ? Array.from(root.featureSystem.getParameterValues(root.featureName)) : root._createDefaultValues(operation);
    }

    function _createDefaultValues(operation) {
        if (!operation || !operation.info)
            return [];
        const presets = operation.categoryId && operation.info.category_defaults ? operation.info.category_defaults[operation.categoryId] : null;
        return root.argumentTypes.map((arg, index) => {
            const defaults = operation.defaultParameters;
            if (defaults && defaults[index] !== undefined)
                return defaults[index];
            const preset = presets ? presets[arg.name] : undefined;
            const content = preset !== undefined ? String(preset) : arg.content;
            switch (arg.type) {
            case QArgType.Combo:
                {
                    const parts = arg.content.split("|");
                    const count = parts[0].split(",").length;
                    const value = preset !== undefined ? Number(preset) : (parts.length > 1 ? parseInt(parts[1]) : 0);
                    return Number.isInteger(value) && value >= 0 && value < count ? value : 0;
                }
            case QArgType.Int:
                return parseInt(content);
            case QArgType.Float:
                return parseFloat(content);
            case QArgType.Bool:
                return content === "true";
            case QArgType.Selector:
                return root._emptySelection;
            case QArgType.Button:
                return 0;
            default:
                return content || "";
            }
        });
    }

    function _updateValue(index, value) {
        if (index < 0 || index >= root.values.length || Object.is(root.values[index], value))
            return false;
        const values = root.values.slice();
        values[index] = value;
        root.values = values;
        return true;
    }

    function setValue(index, value) {
        if (!root._acceptingControlEdits || index < 0 || index >= root.values.length)
            return;
        // 视口的 null 统一转为类型明确的空选择，控件无需持有模型内部的空对象。
        if (root.argumentTypes[index].type === QArgType.Selector && value === null)
            value = root._emptySelection;
        if (Object.is(root.values[index], value))
            return;
        if (root.featureName)
            root.featureSystem.setParameter(root.featureName, index, value);
        else
            root._updateValue(index, value);
    }

    function clearSelections() {
        for (let i = 0; i < root.argumentTypes.length; ++i) {
            if (root.argumentTypes[i].type === QArgType.Selector)
                root.setValue(i, null);
        }
    }
}
