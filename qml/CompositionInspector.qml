import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    readonly property var t: controller.currentTransform
    readonly property bool hasTransform: Object.keys(t).length > 0
    readonly property string targetKey: controller.selectedOverlay >= 0 ? "overlay:" + controller.selectedOverlay : "clip:" + (controller.currentClip.id || "")
    readonly property bool animated: !!(t.xAnimated || t.yAnimated || t.scaleAnimated || t.rotationAnimated || t.opacityAnimated)
    property bool canvasPreview: false
    property bool customCanvas: false
    signal cancelEdits()
    onTargetKeyChanged: cancelEdits()
    Connections { target: controller; function onCompositionChanged() { if (!controller.visualPreviewActive) panel.canvasPreview = false } }
    Keys.onEscapePressed: event => { controller.endVisualGesture(true); panel.canvasPreview = false; event.accepted = true }
    spacing: 10
    function focusFirst() { if (hasTransform) framingChoice.forceActiveFocus(); else canvasDetails.focusHeader() }
    function validDimension(text) { var value = Number(text); return isFinite(value) && value >= 2 && value <= 8192 && value % 2 === 0 }

    component TransformField: UI.TextField {
        id: field
        required property string transformKey
        property real factor: 1
        property real fallback: 0
        property real minimum: -36000
        property real maximum: 36000
        property int places: 1
        property string editingTarget: ""
        property string initialText: ""
        Layout.fillWidth: true
        enabled: panel.hasTransform
        stepper: true
        text: displayValue()
        function value() { return Number(panel.t[transformKey] === undefined ? fallback : panel.t[transformKey]) * factor }
        function displayValue() { return value().toFixed(places) }
        function restore() { text = Qt.binding(function() { return field.displayValue() }) }
        function cancelEdit() { editingTarget = ""; restore(); focus = false }
        function applyValue(value) {
            if (!panel.hasTransform || editingTarget !== panel.targetKey) return
            var fields = {}; fields[transformKey] = Math.max(minimum,Math.min(maximum,value)) / factor
            panel.controller.setTransform(fields)
            restore(); initialText = text
        }
        validator: DoubleValidator { bottom: field.minimum; top: field.maximum; decimals: 4; locale: "C" }
        onActiveFocusChanged: if (activeFocus) { editingTarget = panel.targetKey; initialText = text }
        onEditingFinished: { if (acceptableInput && text !== initialText) applyValue(Number(text)); restore() }
        onStepRequested: direction => applyValue(value() + direction)
        Keys.onEscapePressed: event => { cancelEdit(); event.accepted = true }
        Connections { target: panel; function onCancelEdits() { field.cancelEdit() } }
    }

    ColumnLayout {
        Layout.fillWidth: true; Layout.margins: 12
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { text: panel.hasTransform ? (controller.currentClip.name || "Selected clip") : "Select a clip to transform"; elide: Text.ElideMiddle; Layout.fillWidth: true; Layout.minimumWidth: 0; font.weight: Font.DemiBold; color: panel.hasTransform ? UI.Theme.text : UI.Theme.muted }
            UI.Button { objectName: "resetTransformButton"; visible: panel.hasTransform; text: "Reset transform"; iconName: "reset"; iconOnly: true; flat: true; helpText: "Reset this clip’s transform and all its keyframes"; onClicked: controller.resetTransform() }
        }
        ColumnLayout {
            visible: panel.hasTransform
            Layout.fillWidth: true; spacing: 8
            Label { text: "Framing" }
            UI.ComboBox {
                id: framingChoice
                Layout.fillWidth: true
                model: ["Fit", "Fill", "Crop"]
                currentIndex: Math.max(0,["fit","fill","crop"].indexOf(panel.t.framing))
                Accessible.name: "Framing mode"
                onActivated: index => controller.setTransform({framing:["fit","fill","crop"][index]})
            }
            Label { text: "Scale" }
            TransformField { objectName: "transformScaleField"; transformKey: "scale"; factor: 100; fallback: 1; minimum: 1; maximum: 10000; suffix: "%"; Accessible.name: "Clip scale in percent" }
            Label { text: "Rotation" }
            TransformField { objectName: "transformRotationField"; transformKey: "rotation"; suffix: "°"; Accessible.name: "Clip rotation in degrees" }
            CheckBox { text: "Edit in preview"; checked: controller.transformMode; onClicked: controller.transformMode = checked; Accessible.description: "Drag to move, use corner handles to resize, or the rotation handle to turn" }
        }
        UI.InspectorDetails {
            objectName: "transformPrecisionDetails"
            visible: panel.hasTransform
            title: panel.t.framing === "crop" ? "Position, crop and opacity" : "Position and opacity"
            summary: Math.round(Number(panel.t.opacity === undefined ? 1 : panel.t.opacity) * 100) + "% opacity"
            Repeater {
                model: [{key:"x",label:"Horizontal position",min:-1000,max:1000},{key:"y",label:"Vertical position",min:-1000,max:1000},{key:"opacity",label:"Opacity",min:0,max:100}]
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.label }
                    TransformField { objectName: "transform" + modelData.key + "Field"; transformKey: modelData.key; factor: 100; fallback: modelData.key === "opacity" ? 1 : .5; minimum: modelData.min; maximum: modelData.max; suffix: "%"; Accessible.name: modelData.label + " in percent" }
                }
            }
            Repeater {
                model: panel.t.framing === "crop" ? [{key:"cropLeft",label:"Crop left"},{key:"cropTop",label:"Crop top"},{key:"cropRight",label:"Crop right"},{key:"cropBottom",label:"Crop bottom"}] : []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.label }
                    TransformField { objectName: "visual" + modelData.key + "Field"; transformKey: modelData.key; factor: 100; minimum: 0; maximum: 98; suffix: "%"; Accessible.name: modelData.label + " in percent" }
                }
            }
        }
        UI.InspectorDetails {
            objectName: "transformAnimationDetails"
            visible: panel.hasTransform
            title: "Animation"
            summary: panel.animated ? "Animated" : "Off"
            Repeater {
                model: [{key:"x",curve:"position",label:"Position"},{key:"scale",curve:"scale",label:"Scale"},{key:"rotation",curve:"rotation",label:"Rotation"},{key:"opacity",curve:"opacity",label:"Opacity"}]
                delegate: ColumnLayout {
                    id: animationRow
                    required property var modelData
                    Layout.fillWidth: true; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: animationRow.modelData.label; Layout.fillWidth: true; font.weight: Font.DemiBold }
                        UI.Button { text: panel.t[animationRow.modelData.key + "Key"] ? "Update key" : "Add key"; iconName: "keyframe"; iconOnly: true; Accessible.name: "Add or update " + animationRow.modelData.curve + " keyframe"; onClicked: controller.editKeyframe(animationRow.modelData.curve,"add") }
                    }
                    Flow {
                        visible: !!panel.t[animationRow.modelData.key + "Animated"]
                        Layout.fillWidth: true; spacing: 4
                        UI.Button { text: "Previous keyframe"; iconName: "step-back"; iconOnly: true; onClicked: controller.editKeyframe(animationRow.modelData.curve,"previous") }
                        UI.Button { text: "Next keyframe"; iconName: "step-forward"; iconOnly: true; onClicked: controller.editKeyframe(animationRow.modelData.curve,"next") }
                        UI.Button { text: "Delete keyframe"; iconName: "trash"; iconOnly: true; enabled: !!panel.t[animationRow.modelData.key + "Key"]; onClicked: controller.editKeyframe(animationRow.modelData.curve,"delete") }
                        UI.ComboBox {
                            model: ["Linear", "Ease"]
                            width: Math.max(100,font.pixelSize * 7)
                            enabled: !!panel.t[animationRow.modelData.key + "Key"]
                            currentIndex: panel.t[animationRow.modelData.key + "Ease"] ? 1 : 0
                            Accessible.name: animationRow.modelData.label + " interpolation"
                            onActivated: index => controller.editKeyframe(animationRow.modelData.curve,index ? "ease" : "linear")
                        }
                    }
                    Label { visible: !!panel.t[animationRow.modelData.key + "Animated"]; text: (panel.t[animationRow.modelData.key + "Keys"] || []).length + " keyframes"; color: UI.Theme.muted }
                    UI.TextField {
                        objectName: "move" + animationRow.modelData.curve + "KeyframeField"
                        visible: !!panel.t[animationRow.modelData.key + "Key"]
                        property string editingTarget: ""
                        property int editingFrame: -1
                        Layout.fillWidth: true; suffix: "frames"
                        text: String(panel.t.sourceFrame || 0)
                        Accessible.name: "Move " + animationRow.modelData.curve + " keyframe to source frame"
                        validator: IntValidator { bottom: 0; top: 5184000 }
                        onActiveFocusChanged: if (activeFocus) { editingTarget = panel.targetKey; editingFrame = panel.t.sourceFrame || 0 }
                        Keys.onEscapePressed: event => { editingTarget = ""; text = Qt.binding(function(){ return String(panel.t.sourceFrame || 0) }); focus = false; event.accepted = true }
                        onEditingFinished: if (acceptableInput && editingTarget === panel.targetKey && editingFrame === (panel.t.sourceFrame || 0)) controller.editKeyframe(animationRow.modelData.curve,"move",Number(text))
                    }
                }
            }
        }
        UI.InspectorDetails {
            id: canvasDetails
            objectName: "canvasDetails"
            title: "Project canvas"
            summary: canvasChoice.displayText
            UI.ComboBox {
                id: canvasChoice
                objectName: "canvasPresetChoice"
                Layout.fillWidth: true
                model: ["Auto resolution", "Landscape 1920 × 1080", "Portrait 1080 × 1920", "Square 1080 × 1080", "Custom"]
                currentIndex: panel.customCanvas ? 4 : controller.canvas.width === 0 ? 0 : controller.canvas.width === 1920 && controller.canvas.height === 1080 ? 1 : controller.canvas.width === 1080 && controller.canvas.height === 1920 ? 2 : controller.canvas.width === 1080 && controller.canvas.height === 1080 ? 3 : 4
                Accessible.name: "Project canvas preset"
                onActivated: index => {
                    panel.customCanvas = index === 4
                    if (index === 4) return
                    controller.beginVisualGesture(); panel.canvasPreview = true
                    controller.setCanvas(index === 0 ? 0 : index === 1 ? 1920 : 1080,index === 0 ? 0 : index === 2 ? 1920 : 1080)
                }
            }
            GridLayout {
                visible: canvasChoice.currentIndex === 4
                Layout.fillWidth: true
                columns: width < canvasW.font.pixelSize * 16 ? 1 : 2
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "Width" }
                    UI.TextField { id: canvasW; objectName: "canvasWidthField"; Layout.fillWidth: true; text: controller.canvas.width; Accessible.name: "Canvas width in pixels"; suffix: "px"; validator: IntValidator { bottom: 2; top: 8192 } Keys.onEscapePressed: event => { text = Qt.binding(function(){ return String(controller.canvas.width) }); focus = false; event.accepted = true } }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "Height" }
                    UI.TextField { id: canvasH; objectName: "canvasHeightField"; Layout.fillWidth: true; text: controller.canvas.height; Accessible.name: "Canvas height in pixels"; suffix: "px"; validator: IntValidator { bottom: 2; top: 8192 } Keys.onEscapePressed: event => { text = Qt.binding(function(){ return String(controller.canvas.height) }); focus = false; event.accepted = true } }
                }
            }
            UI.Button {
                objectName: "previewCanvasButton"
                visible: canvasChoice.currentIndex === 4
                text: "Preview canvas"
                enabled: panel.validDimension(canvasW.text) && panel.validDimension(canvasH.text)
                helpText: "Preview custom canvas. Width and height must be even."
                onClicked: { controller.beginVisualGesture(); panel.canvasPreview = true; controller.setCanvas(Number(canvasW.text),Number(canvasH.text)) }
            }
        }
        Flow {
            visible: panel.canvasPreview
            Layout.fillWidth: true; spacing: 6
            UI.Button { objectName: "applyCanvasButton"; text: "Apply canvas"; highlighted: true; onClicked: { controller.endVisualGesture(); panel.canvasPreview = false } }
            UI.Button { objectName: "cancelCanvasButton"; text: "Cancel"; iconName: "close"; iconOnly: true; helpText: "Cancel canvas preview"; Accessible.name: "Cancel canvas preview"; onClicked: { controller.endVisualGesture(true); panel.canvasPreview = false } }
        }
    }
}
