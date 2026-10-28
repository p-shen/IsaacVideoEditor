import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: mixer
    required property var controller
    readonly property var audio: controller.currentAudio
    readonly property string sourcePath: controller.audioLane === 0 ? (controller.currentClip.path || "") : controller.audioLane === 1 ? controller.voicePath : controller.musicPath
    readonly property string sourceKey: controller.audioLane + ":" + (controller.audioLane === 0 ? (controller.currentClip.id || "") : sourcePath)
    spacing: 10
    Layout.fillWidth: true
    signal cancelEdits()
    onSourceKeyChanged: cancelEdits()
    function focusFirst() { sourceChooser.forceActiveFocus() }
    function chooseFile(lane) { if (lane === 1) controller.chooseVoice(); else if (lane === 2) controller.chooseMusic() }
    function setFile(lane, path) { if (lane === 1) controller.setVoicePath(path); else if (lane === 2) controller.setMusicPath(path) }

    component AudioField: UI.TextField {
        id: field
        required property string audioProperty
        property bool seconds: false
        property real minimum: 0
        property real maximum: 86400
        property string editingSource: mixer.sourceKey
        Layout.fillWidth: true
        enabled: mixer.audio.enabled
        stepper: true
        suffix: seconds ? "s" : "dB"
        text: displayValue()
        function displayValue() {
            var value = Number(mixer.audio[audioProperty] || 0)
            return seconds ? (value / 60).toFixed(3) : value.toFixed(1)
        }
        function restore() { text = Qt.binding(function() { return field.displayValue() }) }
        function cancelEdit() { editingSource = ""; restore(); focus = false }
        // Include the three-decimal display rounding; committed fades still clamp to exact playable frames.
        validator: DoubleValidator { bottom: field.minimum; top: field.maximum + (field.seconds ? .0005 : 0); decimals: field.seconds ? 6 : 2; locale: "C" }
        onActiveFocusChanged: if (activeFocus) editingSource = mixer.sourceKey
        onEditingFinished: {
            if (acceptableInput && editingSource === mixer.sourceKey && mixer.audio.enabled) {
                var values = {}
                values[audioProperty] = seconds ? Math.round(Number(text) * 60) : Number(text)
                controller.updateAudio(controller.audioLane, values)
            }
            restore()
        }
        onStepRequested: direction => {
            if (!mixer.audio.enabled || editingSource !== mixer.sourceKey) return
            var values = {}
            var step = Number(mixer.audio[audioProperty] || 0) + direction
            var lower = seconds ? Math.round(minimum * 60) : minimum
            var upper = seconds ? Math.round(maximum * 60) : maximum
            values[audioProperty] = Math.max(lower, Math.min(upper, step))
            controller.updateAudio(controller.audioLane, values)
            restore()
        }
        Keys.onEscapePressed: event => { cancelEdit(); event.accepted = true }
        Connections { target: mixer; function onCancelEdits() { field.cancelEdit() } }
    }

    UI.ComboBox {
        id: sourceChooser
        objectName: "audioSourceChoice"
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 16
        model: ["Selected clip audio", "Narration", "Music"]
        currentIndex: controller.audioLane
        Accessible.name: "Audio source to edit"
        onActivated: controller.selectAudio(currentIndex)
    }
    Label {
        visible: !mixer.audio.enabled
        text: controller.audioLane === 0 ? "Select a clip to edit its audio." : controller.audioLane === 1 ? "Add a narration file to your project." : "Add a music file to your project."
        wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        color: UI.Theme.muted
    }
    UI.Button {
        visible: !mixer.audio.enabled && controller.audioLane > 0
        text: controller.audioLane === 1 ? "Add narration" : "Add music"
        iconName: "folder"
        Layout.leftMargin: 16; Layout.rightMargin: 16
        Layout.maximumWidth: mixer.width - 32
        onClicked: mixer.chooseFile(controller.audioLane)
    }
    RowLayout {
        visible: mixer.audio.enabled || controller.audioLane > 0
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        Label {
            visible: mixer.audio.enabled
            text: mixer.sourcePath.split("/").pop()
            elide: Text.ElideMiddle; Layout.fillWidth: true; Layout.minimumWidth: 0
            Accessible.name: "Audio file: " + text
        }
        UI.Button {
            text: "File…"; iconName: "more"; iconOnly: true
            visible: controller.audioLane > 0
            Accessible.name: "Audio file actions"
            helpText: "Audio file actions"
            onClicked: { fileMenu.lane = controller.audioLane; recentDownloads.entries = controller.recentMusic(); fileMenu.popup() }
            Menu {
                id: fileMenu
                property int lane: 1
                MenuItem { text: mixer.audio.enabled ? "Replace file…" : "Choose file…"; onTriggered: mixer.chooseFile(fileMenu.lane) }
                MenuItem { text: "Clear file"; enabled: mixer.audio.enabled; onTriggered: mixer.setFile(fileMenu.lane, "") }
                MenuSeparator {}
                Menu {
                    id: recentDownloads
                    title: "Recent downloads"
                    property var entries: []
                    Instantiator {
                        model: recentDownloads.entries
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name
                            onTriggered: mixer.setFile(fileMenu.lane, modelData.path)
                        }
                        onObjectAdded: (index, object) => recentDownloads.insertItem(index, object)
                        onObjectRemoved: (index, object) => recentDownloads.removeItem(object)
                    }
                    MenuItem { text: "No recent audio files"; enabled: false; visible: recentDownloads.entries.length === 0 }
                }
                MenuItem { text: "Browse Mixkit…"; onTriggered: controller.browseMusic() }
            }
        }
    }
    ColumnLayout {
        visible: mixer.audio.enabled
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        spacing: 8
        Label { text: "Volume"; Layout.fillWidth: true }
        AudioField {
            objectName: "audioGainField"
            audioProperty: "gainDb"; minimum: -60; maximum: 12
            Accessible.name: "Audio gain in decibels"
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            UI.Button {
                text: mixer.audio.muted ? "Unmute" : "Mute"; iconName: mixer.audio.muted ? "volume-off" : "volume"; iconOnly: true
                checkable: true; checked: !!mixer.audio.muted
                onClicked: controller.updateAudio(controller.audioLane, {muted: !mixer.audio.muted})
            }
            CheckBox {
                visible: controller.audioLane === 2
                text: "Loop to end"; checked: !!mixer.audio.loopToEnd
                onClicked: controller.updateAudio(2, {loopToEnd: checked})
            }
        }
        UI.InspectorDetails {
            objectName: "audioTimingDetails"
            title: "Timing and fades"
            summary: "In " + Number((mixer.audio.fadeInFrames || 0) / 60).toFixed(2) + " s · Out " + Number((mixer.audio.fadeOutFrames || 0) / 60).toFixed(2) + " s"
            Label { text: "Fade in"; Layout.fillWidth: true }
            AudioField {
                objectName: "audioFadeInField"
                audioProperty: "fadeInFrames"; seconds: true
                maximum: Math.max(0, mixer.audio.playableFrames || 0) / 60
                Accessible.name: "Audio fade in in seconds; arrow keys step one frame"
            }
            Label { text: "Fade out"; Layout.fillWidth: true }
            AudioField {
                objectName: "audioFadeOutField"
                audioProperty: "fadeOutFrames"; seconds: true
                maximum: Math.max(0, mixer.audio.playableFrames || 0) / 60
                Accessible.name: "Audio fade out in seconds; arrow keys step one frame"
            }
            Label { visible: controller.audioLane === 1; text: "Start in project"; Layout.fillWidth: true }
            AudioField {
                objectName: "audioStartField"
                visible: controller.audioLane === 1
                audioProperty: "startFrame"; seconds: true
                Accessible.name: "Narration start in project seconds; arrow keys step one frame"
            }
            Label {
                text: "Arrow keys and steppers adjust one frame. Fades clamp to the playable duration."
                wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
            }
        }
    }
    Label {
        visible: controller.soloLane >= 0
        text: "SOLO PREVIEW · Export uses the full saved mix"
        wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        color: UI.Theme.accent
    }
    RowLayout {
        visible: controller.masterClipped
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        Label { text: "Master output clipped"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.weight: Font.DemiBold }
        UI.Button { text: "Reset"; iconName: "reset"; iconOnly: true; helpText: "Reset master clipping indicator"; onClicked: controller.resetClipping(); Accessible.name: "Reset persistent master clipping indicator" }
    }
    UI.InspectorDetails {
        objectName: "audioPreviewDetails"
        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.bottomMargin: 16
        title: "Preview and levels"
        summary: Number(controller.masterPeak).toFixed(1) + " dBFS"
        UI.Button {
            text: "Solo"; iconName: "headphones"; enabled: mixer.audio.enabled
            Accessible.name: "Solo preview"
            helpText: "Solo this source in preview; export keeps the full mix"
            checkable: true; checked: controller.soloLane === controller.audioLane
            onClicked: controller.setSolo(controller.audioLane)
        }
        UI.Button { visible: controller.soloLane >= 0; text: "Full mix"; iconName: "volume"; helpText: "End solo preview and restore the full mix"; Accessible.name: "End solo preview"; onClicked: controller.setSolo(-1) }
        Label { text: "Master output · " + Number(controller.masterPeak).toFixed(1) + " dBFS"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ProgressBar { Layout.fillWidth: true; from: -60; to: 6; value: controller.masterPeak; Accessible.name: "Master output level in decibels full scale" }
        Label {
            text: "Drag the timeline gain line vertically, fade handles horizontally, or the narration body to move its start."
            wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
        }
    }
}
