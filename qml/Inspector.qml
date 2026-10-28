import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

UI.Pane {
    id: inspector
    required property var controller
    property int section: 0
    readonly property bool compactTabs: width < tabMetrics.width + 100
    TextMetrics { id: tabMetrics; font: inspector.font; text: "Clip Audio Transitions Text Transform Captions" }
    function focusFirst() {
        if (section === 5) { captionPanel.focusFirst(); return }
        if (section === 4) { compositionPanel.focusFirst(); return }
        if (section === 3) { textPanel.focusFirst(); return }
        if (section === 2) { cutsPanel.focusFirst(); return }
        if (section === 1) { audioPanel.focusFirst(); return }
        if (trimStartField.enabled && trimStartField.visible) trimStartField.forceActiveFocus()
        else if (compactTabs) sectionChoice.forceActiveFocus()
        else tabs.itemAt(section).forceActiveFocus()
    }
    function revealFocus() {
        var host = inspector.Window.window
        if (host && typeof host.revealFocusedControl === "function") Qt.callLater(host.revealFocusedControl)
    }
    padding: 0
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        UI.TabBar {
            id: tabs
            Layout.fillWidth: true
            visible: !inspector.compactTabs
            currentIndex: inspector.section
            onCurrentIndexChanged: inspector.section = currentIndex
            UI.TabButton { text: "Clip" }
            UI.TabButton { text: "Audio" }
            UI.TabButton { text: "Transitions" }
            UI.TabButton { text: "Text" }
            UI.TabButton { text: "Transform" }
            UI.TabButton { text: "Captions" }
        }
        UI.ComboBox {
            id: sectionChoice
            visible: inspector.compactTabs
            Layout.fillWidth: true; Layout.margins: 8
            model: ["Clip", "Audio", "Transitions", "Text", "Transform", "Captions"]
            currentIndex: inspector.section
            Accessible.name: "Inspector section"
            onActivated: index => inspector.section = index
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            currentIndex: inspector.section
            UI.InspectorPage {
                ColumnLayout {
                    id: clipPanel
                    spacing: 10
                    Layout.margins: 12
                    Label {
                        objectName: "clipInspectorEmpty"
                        visible: controller.selectedClip < 0
                        text: "Select a clip to edit"
                        wrapMode: Text.Wrap; color: UI.Theme.muted
                        Layout.fillWidth: true; Layout.margins: 12
                    }
                    ColumnLayout {
                        visible: controller.selectedClip >= 0
                        Layout.fillWidth: true; Layout.margins: 12
                        spacing: 10
                        Label { text: controller.currentClip.name || ""; elide: Text.ElideMiddle; Layout.fillWidth: true; font.weight: Font.DemiBold }
                        Label { visible: controller.selectedIndices.length > 1; text: controller.selectedIndices.length + " clips selected"; color: UI.Theme.muted }
                        Label { text: "Start" }
                        UI.TextField {
                            id: trimStartField
                            objectName: "trimStartField"
                            readonly property string targetId: controller.currentClip.id || ""
                            property string editingId: ""
                            function restore() { text = Qt.binding(function() { return Number(controller.currentClip.inSeconds || 0).toFixed(3) }) }
                            onTargetIdChanged: { editingId = ""; restore(); focus = false }
                            onActiveFocusChanged: if (activeFocus) editingId = targetId
                            Layout.fillWidth: true
                            enabled: controller.selectedClip >= 0
                            suffix: "s"; stepper: true
                            text: Number(controller.currentClip.inSeconds || 0).toFixed(3)
                            Keys.onEscapePressed: event => { editingId = ""; restore(); focus = false; event.accepted = true }
                            Accessible.name: "Selected clip source start in seconds"
                            validator: DoubleValidator { bottom: 0; decimals: 6; locale: "C" }
                            onEditingFinished: { if (acceptableInput && editingId && editingId === targetId) controller.setTrimStart(Number(text)); restore() }
                            onStepRequested: direction => { if (editingId && editingId === targetId) controller.nudgeTrim(0,direction); restore() }
                        }
                        Label { text: "Length" }
                        UI.TextField {
                            objectName: "trimLengthField"
                            readonly property string targetId: controller.currentClip.id || ""
                            property string editingId: ""
                            function restore() { text = Qt.binding(function() { return Number(controller.currentClip.lengthSeconds || 0).toFixed(3) }) }
                            onTargetIdChanged: { editingId = ""; restore(); focus = false }
                            onActiveFocusChanged: if (activeFocus) editingId = targetId
                            Layout.fillWidth: true
                            suffix: "s"; stepper: true
                            text: Number(controller.currentClip.lengthSeconds || 0).toFixed(3)
                            Keys.onEscapePressed: event => { editingId = ""; restore(); focus = false; event.accepted = true }
                            Accessible.name: "Selected clip length in seconds"
                            validator: DoubleValidator { bottom: 0.016; decimals: 6; locale: "C" }
                            onEditingFinished: { if (acceptableInput && editingId && editingId === targetId) controller.setTrimLength(Number(text)); restore() }
                            onStepRequested: direction => { if (editingId && editingId === targetId) controller.nudgeTrim(1,direction); restore() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Rotate"; Layout.fillWidth: true }
                            UI.Button { text: "Rotate left 90°"; iconName: "rotate-left"; iconOnly: true; onClicked: controller.rotate(-90) }
                            UI.Button { text: "Rotate right 90°"; iconName: "rotate-right"; iconOnly: true; onClicked: controller.rotate(90) }
                        }
                        UI.InspectorDetails {
                            objectName: "clipRotationDetails"
                            title: "Rotation options"
                            summary: (controller.currentClip.rotation || 0) + "°"
                            UI.Button { text: "Turn 180°"; onClicked: controller.rotate(180) }
                        }
                        UI.Button { text: "Clip audio"; iconName: "volume"; iconOnly: true; helpText: "Edit this clip’s audio"; onClicked: { controller.selectAudio(0); inspector.section = 1 } }
                    }
                }
            }
            UI.InspectorPage { UI.AudioMixer { id: audioPanel; controller: inspector.controller; Layout.fillWidth: true } }
            UI.InspectorPage { UI.CutInspector { id: cutsPanel; controller: inspector.controller; Layout.fillWidth: true } }
            UI.InspectorPage { UI.TextInspector { id: textPanel; controller: inspector.controller; Layout.fillWidth: true } }
            UI.InspectorPage { UI.CompositionInspector { id: compositionPanel; controller: inspector.controller; Layout.fillWidth: true } }
            UI.InspectorPage { UI.CaptionInspector { id: captionPanel; controller: inspector.controller; Layout.fillWidth: true } }
        }
    }
}
