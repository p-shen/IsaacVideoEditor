import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: cuts
    required property var controller
    readonly property var cut: controller.currentTransition
    readonly property string selectionKey: {
        var index = controller.selectedTransition
        var clips = controller.clips
        return index > 0 && index < clips.length ? index + ":" + clips[index - 1].id + ":" + clips[index].id : ""
    }
    property real bulkSeconds: controller.crossfade
    signal cancelEdits()
    onSelectionKeyChanged: cancelEdits()
    Layout.fillWidth: true
    spacing: 10
    function focusFirst() { cutChooser.forceActiveFocus() }
    function setFrames(frames) {
        if (controller.selectedTransition > 0) controller.setTransition(controller.selectedTransition, frames > 0 ? "dissolve" : "cut", frames)
    }

    component TransitionField: UI.TextField {
        id: field
        property bool seconds: false
        property string editingCut: cuts.selectionKey
        Layout.fillWidth: true
        stepper: true
        suffix: seconds ? "s" : "frames"
        enabled: controller.selectedTransition > 0
        text: displayValue()
        function displayValue() { return seconds ? Number((cuts.cut.frames || 0) / 60).toFixed(3) : String(cuts.cut.frames || 0) }
        function restore() { text = Qt.binding(function() { return field.displayValue() }) }
        function cancelEdit() { editingCut = ""; restore(); focus = false }
        validator: DoubleValidator { bottom: 0; top: field.seconds ? 10 : 600; decimals: field.seconds ? 6 : 0; locale: "C" }
        onActiveFocusChanged: if (activeFocus) editingCut = cuts.selectionKey
        onEditingFinished: {
            if (acceptableInput && editingCut === cuts.selectionKey && controller.selectedTransition > 0) cuts.setFrames(seconds ? Math.round(Number(text) * 60) : Number(text))
            restore()
        }
        onStepRequested: direction => {
            if (editingCut !== cuts.selectionKey || controller.selectedTransition <= 0) return
            cuts.setFrames(Math.max(0, Math.min(600, (cuts.cut.frames || 0) + direction)))
            restore()
        }
        Keys.onEscapePressed: event => { cancelEdit(); event.accepted = true }
        Connections { target: cuts; function onCancelEdits() { field.cancelEdit() } }
    }

    UI.ComboBox {
        id: cutChooser
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 16
        model: controller.transitions; textRole: "name"
        currentIndex: controller.selectedTransition - 1
        displayText: currentIndex >= 0 ? currentText : "Choose a cut"
        enabled: controller.transitions.length > 0
        Accessible.name: "Choose a cut to edit"
        onActivated: controller.selectTransition(currentIndex + 1)
    }
    Label {
        visible: controller.selectedTransition <= 0
        text: controller.transitions.length > 0 ? "Choose a cut or select one on the timeline." : "Add a second clip to create a cut."
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        wrapMode: Text.Wrap; color: UI.Theme.muted
    }
    ColumnLayout {
        visible: controller.selectedTransition > 0
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        spacing: 8
        Label { text: "Transition"; Layout.fillWidth: true }
        UI.ComboBox {
            Layout.fillWidth: true
            model: ["Cut", "Dissolve"]
            currentIndex: cuts.cut.type === "dissolve" ? 1 : 0
            enabled: controller.selectedTransition > 0
            Accessible.name: "Transition type"
            onActivated: if (controller.selectedTransition > 0) cuts.setFrames(currentIndex === 0 ? 0 : Math.max(1, cuts.cut.frames || 30))
        }
        Label { text: "Duration"; Layout.fillWidth: true }
        TransitionField {
            objectName: "transitionDurationField"
            seconds: true
            Accessible.name: "Selected cut duration in seconds; arrow keys step one frame"
        }
        Label {
            objectName: "transitionShorteningNote"
            visible: cuts.cut.shortened === true
            text: "Requested " + Number((cuts.cut.requestedFrames || 0) / 60).toFixed(3) + " s; shortened to " + Number((cuts.cut.frames || 0) / 60).toFixed(3) + " s to fit nearby clips."
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted
        }
    }
    UI.InspectorDetails {
        objectName: "transitionPrecisionDetails"
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        title: "Precision and presets"
        summary: controller.selectedTransition > 0 ? (cuts.cut.frames || 0) + " frames" : "Choose a cut first"
        Label { text: "Overlap"; Layout.fillWidth: true }
        TransitionField {
            objectName: "localTransitionFrames"
            Accessible.name: "Selected cut overlap in frames"
        }
        UI.Button {
            objectName: "transitionPresetButton"
            text: "Dissolve · 0.5 s"
            Layout.maximumWidth: parent.width
            Accessible.name: "Apply half-second dissolve at selected cut; can also drag onto a timeline cut"
            enabled: controller.selectedTransition > 0
            onClicked: if (controller.selectedTransition > 0) controller.setTransition(controller.selectedTransition, "dissolve", 30)
            Drag.active: presetDrag.active
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction
            Drag.mimeData: ({"application/x-isaac-transition": "dissolve"})
            DragHandler { id: presetDrag; target: null }
        }
        Label { text: "Arrow keys and steppers adjust one frame. Drag a transition edge on the timeline to preview its exact overlap."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted }
    }
    UI.InspectorDetails {
        objectName: "transitionBulkDetails"
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.bottomMargin: 16
        title: "All existing cuts"
        summary: "Replace existing transitions"
        Label { text: "Crossfade duration"; Layout.fillWidth: true }
        UI.TextField {
            id: bulkDuration
            objectName: "transitionBulkDurationField"
            Layout.fillWidth: true
            suffix: "s"; stepper: true
            text: Number(cuts.bulkSeconds).toFixed(3)
            Accessible.name: "Crossfade duration for all existing cuts in seconds"
            validator: DoubleValidator { bottom: 0; top: 10; decimals: 6; locale: "C" }
            function restore() { text = Qt.binding(function() { return Number(cuts.bulkSeconds).toFixed(3) }) }
            onEditingFinished: { if (acceptableInput) cuts.bulkSeconds = Number(text); restore() }
            onStepRequested: direction => { cuts.bulkSeconds = Math.max(0, Math.min(600, Math.round(cuts.bulkSeconds * 60) + direction)) / 60; restore() }
            Keys.onEscapePressed: event => { restore(); focus = false; event.accepted = true }
        }
        Label { text: "Replaces every existing transition, including split points. Apply again after adding clips or splitting. Short clips shorten only nearby crossfades."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted }
        UI.Button {
            id: bulkApply
            objectName: "transitionBulkApplyButton"
            text: "Apply to all existing cuts"
            Layout.maximumWidth: parent.width
            contentItem: Label { text: bulkApply.text; font: bulkApply.font; color: bulkApply.foreground; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            enabled: controller.transitions.length > 0 && bulkDuration.acceptableInput
            onClicked: {
                if (!bulkDuration.acceptableInput || controller.transitions.length === 0) return
                cuts.bulkSeconds = Number(bulkDuration.text)
                controller.setCrossfade(cuts.bulkSeconds)
                bulkDuration.restore()
            }
        }
    }
}
