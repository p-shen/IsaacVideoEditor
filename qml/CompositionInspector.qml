import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI
ColumnLayout {
    id: panel
    required property var controller
    property var t: controller.currentTransform
    property bool canvasPreview: false
    Connections { target: controller; function onCompositionChanged() { if (!controller.visualPreviewActive) panel.canvasPreview = false } }
    Keys.onEscapePressed: { controller.endVisualGesture(true); panel.canvasPreview = false }
    spacing: 8
    function focusFirst() { canvasChoice.forceActiveFocus() }
    Label { text: "Canvas and composition"; font.weight: Font.DemiBold; Layout.margins: 12 }
    UI.ComboBox {
        id: canvasChoice
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
        model: ["Auto resolution", "Landscape 1920 × 1080", "Portrait 1080 × 1920", "Square 1080 × 1080", "Custom"]
        currentIndex: controller.canvas.width === 0 ? 0 : controller.canvas.width === 1920 && controller.canvas.height === 1080 ? 1 : controller.canvas.width === 1080 && controller.canvas.height === 1920 ? 2 : controller.canvas.width === 1080 && controller.canvas.height === 1080 ? 3 : 4
        Accessible.name: "Project canvas preset"
        onActivated: index => {
            if (index === 4) return
            controller.beginVisualGesture(); panel.canvasPreview = true
            controller.setCanvas(index === 0 ? 0 : index === 1 ? 1920 : 1080,index === 0 ? 0 : index === 2 ? 1920 : 1080)
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: 12
        UI.TextField { id: canvasW; objectName: "canvasWidthField"; Layout.fillWidth: true; text: controller.canvas.width; Accessible.name: "Canvas width in pixels"; validator: IntValidator { bottom: 2; top: 8192 } Keys.onEscapePressed: event => { text = Qt.binding(function(){ return String(controller.canvas.width) }); focus = false; event.accepted = true } }
        Label { text: "×" }
        UI.TextField { id: canvasH; objectName: "canvasHeightField"; Layout.fillWidth: true; text: controller.canvas.height; Accessible.name: "Canvas height in pixels"; validator: IntValidator { bottom: 2; top: 8192 } Keys.onEscapePressed: event => { text = Qt.binding(function(){ return String(controller.canvas.height) }); focus = false; event.accepted = true } }
    }
    Flow {
        Layout.fillWidth: true; Layout.leftMargin: 12; spacing: 4
        UI.Button { text: "Preview canvas"; onClicked: { controller.beginVisualGesture(); panel.canvasPreview = true; controller.setCanvas(Number(canvasW.text),Number(canvasH.text)) } }
        UI.Button { text: "Apply"; visible: panel.canvasPreview; onClicked: { controller.endVisualGesture(); panel.canvasPreview = false } }
        UI.Button { text: "Cancel"; visible: panel.canvasPreview; onClicked: { controller.endVisualGesture(true); panel.canvasPreview = false } }
    }
    Label { text: "Canvas changes keep each clip’s Fit, Fill, or Crop choice. Dimensions must be even."; Layout.fillWidth: true; Layout.margins: 12; wrapMode: Text.Wrap; color: UI.Theme.muted }
    CheckBox { text: "Transform handles in viewer"; checked: controller.transformMode; enabled: Object.keys(panel.t).length > 0; onToggled: controller.transformMode = checked; Layout.leftMargin: 12 }
    UI.ComboBox {
        Layout.fillWidth: true; Layout.margins: 12; enabled: Object.keys(panel.t).length > 0
        model: ["Fit", "Fill", "Crop"]; currentIndex: Math.max(0,["fit","fill","crop"].indexOf(panel.t.framing))
        Accessible.name: "Framing mode"
        onActivated: index => controller.setTransform({framing:["fit","fill","crop"][index]})
    }
    Repeater {
        model: [{key:"x",label:"Position X (canvas fraction)"},{key:"y",label:"Position Y (canvas fraction)"},{key:"scale",label:"Uniform scale"},{key:"rotation",label:"Rotation (degrees)"},{key:"opacity",label:"Opacity (0–1)"}]
        delegate: ColumnLayout {
            id: propertyRow
            required property var modelData
            property string animationProperty: modelData.key === "x" || modelData.key === "y" ? "position" : modelData.key
            Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
            enabled: Object.keys(panel.t).length > 0
            Label { text: modelData.label; Layout.fillWidth: true; wrapMode: Text.Wrap }
            UI.TextField {
                Layout.fillWidth: true; text: Number(panel.t[modelData.key] || 0).toFixed(3); Accessible.name: modelData.label
                validator: DoubleValidator { bottom: -36000; top: 36000; decimals: 6; locale: "C" }
                onEditingFinished: if (acceptableInput) { var values = {}; values[modelData.key] = Number(text); controller.setTransform(values) }
                Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(panel.t[modelData.key] || 0).toFixed(3) }); focus = false; event.accepted = true }
            }
            Flow {
                visible: propertyRow.modelData.key !== "y"; Layout.fillWidth: true; spacing: 3
                UI.Button { text: panel.t[propertyRow.modelData.key + "Key"] ? "◆ Update" : "◇ Keyframe"; Accessible.name: "Add or update " + propertyRow.animationProperty + " keyframe"; onClicked: controller.editKeyframe(propertyRow.animationProperty,"add") }
                UI.Button { text: "Previous"; onClicked: controller.editKeyframe(propertyRow.animationProperty,"previous") }
                UI.Button { text: "Next"; onClicked: controller.editKeyframe(propertyRow.animationProperty,"next") }
                UI.Button { text: "Delete key"; enabled: panel.t[propertyRow.modelData.key + "Key"] || false; onClicked: controller.editKeyframe(propertyRow.animationProperty,"delete") }
                UI.Button { text: "Linear"; enabled: panel.t[propertyRow.modelData.key + "Key"] || false; onClicked: controller.editKeyframe(propertyRow.animationProperty,"linear") }
                UI.Button { text: "Ease"; enabled: panel.t[propertyRow.modelData.key + "Key"] || false; onClicked: controller.editKeyframe(propertyRow.animationProperty,"ease") }
            }
            Label { visible: propertyRow.modelData.key !== "y"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted; text: panel.t[propertyRow.modelData.key + "Animated"] ? "Source keys: " + panel.t[propertyRow.modelData.key + "Keys"].join(", ") + (panel.t[propertyRow.modelData.key + "Ease"] ? " · Ease" : " · Linear") : "Static · first keyframe enables animation" }
            UI.TextField {
                visible: propertyRow.modelData.key !== "y" && (panel.t[propertyRow.modelData.key + "Key"] || false)
                objectName: "move" + propertyRow.animationProperty + "KeyframeField"
                Layout.fillWidth: true; text: panel.t.sourceFrame || 0; Accessible.name: "Move " + propertyRow.animationProperty + " keyframe to source frame"
                validator: IntValidator { bottom: 0; top: 5184000 }
                Keys.onEscapePressed: event => { text = Qt.binding(function(){ return String(panel.t.sourceFrame || 0) }); focus = false; event.accepted = true }
                onEditingFinished: if (acceptableInput) controller.editKeyframe(propertyRow.animationProperty,"move",Number(text))
            }
        }
    }
    Repeater {
        model: panel.t.framing === "crop" ? ["cropLeft","cropTop","cropRight","cropBottom"] : []
        delegate: ColumnLayout {
            required property string modelData
            Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
            Label { text: modelData + " (0–1)" }
            UI.TextField { objectName: "visual" + modelData + "Field"; Layout.fillWidth: true; text: panel.t[modelData]; Accessible.name: modelData; validator: DoubleValidator { bottom: 0; top: 0.98; decimals: 4; locale: "C" } Keys.onEscapePressed: event => { text = Qt.binding(function(){ return String(panel.t[modelData] || 0) }); focus = false; event.accepted = true } onEditingFinished: if (acceptableInput) { var values={}; values[modelData]=Number(text); controller.setTransform(values) } }
        }
    }
    UI.Button { text: "Reset transform and keyframes"; enabled: Object.keys(panel.t).length > 0; Layout.margins: 12; onClicked: controller.resetTransform() }
}
