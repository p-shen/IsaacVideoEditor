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
    readonly property var range: controller.transitionRange
    readonly property bool rangeMode: controller.selectedTransition === 0 && range.count > 0
    readonly property var targets: {
        var choices = []
        if (range.count > 0) choices.push({name: "Selected range · " + range.count + (range.count === 1 ? " cut" : " cuts"), index: 0})
        var singles = controller.transitions
        for (var i = 0; i < singles.length; ++i) choices.push(singles[i])
        return choices
    }
    property string draftKey: ""
    property string draftType: ""
    property int draftFrames: -1
    signal cancelRangeEdits()
    onRangeChanged: resetRange()
    onRangeModeChanged: resetRange()
    function resetRange() {
        draftKey = ""
        cancelRangeEdits()
        var current = controller.transitionRange
        draftType = current.type || ""
        draftFrames = current.frames === undefined ? -1 : current.frames
        draftKey = current.key || ""
    }
    property real bulkSeconds: controller.crossfade
    signal cancelEdits()
    onSelectionKeyChanged: cancelEdits()
    Layout.fillWidth: true
    spacing: 10
    Keys.priority: Keys.AfterItem
    Keys.onEscapePressed: event => {
        if (rangeMode) { resetRange(); event.accepted = true }
        else event.accepted = false
    }
    function focusFirst() { cutChooser.forceActiveFocus() }
    function setFrames(frames) {
        var type = frames > 0 ? (cut.type && cut.type !== "cut" ? cut.type : "dissolve") : "cut"
        if (controller.selectedTransition > 0) controller.setTransition(controller.selectedTransition, type, frames)
    }

    component RangeField: UI.TextField {
        id: rangeField
        property bool seconds: true
        property string editingKey: ""
        Layout.fillWidth: true
        enabled: cuts.rangeMode && cuts.draftType !== "cut"
        stepper: true
        suffix: seconds ? "s" : "frames"
        placeholderText: "Mixed"
        text: displayValue()
        validator: DoubleValidator { bottom: rangeField.seconds ? 1 / 60 : 1; top: rangeField.seconds ? 10 : 600; decimals: rangeField.seconds ? 6 : 0; locale: "C" }
        function displayValue() { return cuts.draftFrames < 0 ? "" : seconds ? Number(cuts.draftFrames / 60).toFixed(3) : String(cuts.draftFrames) }
        function restore() { text = Qt.binding(function() { return rangeField.displayValue() }) }
        function stage() {
            if (editingKey && editingKey === cuts.draftKey && cuts.draftKey === cuts.range.key && acceptableInput)
                cuts.draftFrames = seconds ? Math.round(Number(text) * 60) : Number(text)
        }
        onActiveFocusChanged: if (activeFocus) editingKey = cuts.draftKey
        onEditingFinished: { stage(); restore() }
        onStepRequested: direction => {
            if (!editingKey || editingKey !== cuts.draftKey) return
            cuts.draftFrames = Math.max(1, Math.min(600, (cuts.draftFrames < 0 ? 30 : cuts.draftFrames) + direction))
            restore()
        }
        Keys.onEscapePressed: event => { cuts.resetRange(); event.accepted = true }
        Connections {
            target: cuts
            function onCancelRangeEdits() { rangeField.editingKey = ""; rangeField.restore(); rangeField.focus = false }
        }
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
        objectName: "transitionTargetChooser"
        model: cuts.targets; textRole: "name"
        currentIndex: {
            for (var i = 0; i < cuts.targets.length; ++i) if (cuts.targets[i].index === controller.selectedTransition) return i
            return -1
        }
        displayText: currentIndex >= 0 ? currentText : "Choose a cut"
        enabled: controller.transitions.length > 0
        Accessible.name: "Choose transitions to edit"
        onActivated: index => controller.selectTransition(cuts.targets[index].index)
    }
    Label {
        visible: controller.selectedTransition < 0
        text: controller.transitions.length > 0 ? "Choose a cut or select one on the timeline." : "Add a second clip to create a cut."
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        wrapMode: Text.Wrap; color: UI.Theme.muted
    }
    ColumnLayout {
        visible: cuts.rangeMode
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        spacing: 8
        Label {
            objectName: "transitionRangeSummary"
            text: "Clips " + ((cuts.range.first || 0) + 1) + "–" + ((cuts.range.last || 0) + 1) + " · " + (cuts.range.count || 0) + " cuts\n"
                + (cuts.range.firstName || "") + " → " + (cuts.range.lastName || "")
            Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.weight: Font.DemiBold
        }
        Label { text: "Replaces transitions inside this range, including clips between the selected endpoints."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted }
        Label { text: "Transition" }
        UI.ComboBox {
            id: rangeEffect
            objectName: "transitionRangeEffect"
            Layout.fillWidth: true
            model: controller.transitionPresets; textRole: "name"
            currentIndex: {
                var presets = controller.transitionPresets
                for (var i = 0; i < presets.length; ++i) if (presets[i].id === cuts.draftType) return i
                return -1
            }
            displayText: currentIndex < 0 ? "Mixed" : currentText
            Accessible.name: "Transition type for selected range"
            onActivated: index => {
                cuts.draftType = controller.transitionPresets[index].id
                if (cuts.draftType !== "cut" && cuts.draftFrames === 0) cuts.draftFrames = 30
            }
            Keys.onEscapePressed: event => { cuts.resetRange(); event.accepted = true }
        }
        Label { text: "Duration" }
        RangeField { id: rangeDuration; objectName: "transitionRangeDuration"; Accessible.name: "Transition duration for selected range in seconds" }
        UI.InspectorDetails {
            Layout.fillWidth: true
            title: "Frame precision"
            summary: cuts.draftFrames < 0 ? "Mixed durations" : cuts.draftFrames + " frames"
            RangeField { id: rangeFrames; objectName: "transitionRangeFrames"; seconds: false; Accessible.name: "Transition duration for selected range in frames" }
        }
        Label { text: "Changes take effect when you apply. Escape resets the configuration."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted }
        UI.Button {
            id: rangeApply
            objectName: "transitionRangeApply"
            text: "Apply to " + (cuts.range.count || 0) + (cuts.range.count === 1 ? " cut" : " cuts")
            Layout.fillWidth: true
            contentItem: Label { text: rangeApply.text; font: rangeApply.font; color: rangeApply.foreground; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            enabled: cuts.rangeMode && cuts.draftKey === cuts.range.key && cuts.draftType !== ""
                && (cuts.draftType === "cut" || (rangeDuration.acceptableInput && rangeFrames.acceptableInput && Number(rangeDuration.text) > 0))
            onClicked: {
                if (rangeFrames.activeFocus) rangeFrames.stage()
                else rangeDuration.stage()
                if (!enabled || cuts.draftKey !== cuts.range.key) return
                controller.setTransitionRange(cuts.draftKey, cuts.draftType, cuts.draftType === "cut" ? 0 : cuts.draftFrames)
            }
            Keys.onEscapePressed: event => { cuts.resetRange(); event.accepted = true }
        }
    }
    ColumnLayout {
        visible: controller.selectedTransition > 0
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        spacing: 8
        Label { text: "Transition"; Layout.fillWidth: true }
        UI.ComboBox {
            objectName: "transitionEffectChooser"
            Layout.fillWidth: true
            model: controller.transitionPresets
            textRole: "name"
            valueRole: "id"
            currentIndex: {
                var presets = controller.transitionPresets
                for (var i = 0; i < presets.length; ++i) if (presets[i].id === cuts.cut.type) return i
                return 0
            }
            enabled: controller.selectedTransition > 0
            Accessible.name: "Transition type"
            onActivated: index => {
                if (controller.selectedTransition <= 0) return
                var type = controller.transitionPresets[index].id
                controller.setTransition(controller.selectedTransition, type, type === "cut" ? 0 : Math.max(1, cuts.cut.frames || 30))
            }
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
        visible: !cuts.rangeMode
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
