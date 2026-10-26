import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: mixer
    required property var controller
    property var audio: controller.currentAudio
    spacing: 8
    Label { text: "Mix and fades"; font.weight: Font.DemiBold; Layout.fillWidth: true }
    UI.ComboBox {
        Layout.fillWidth: true
        model: ["Selected clip audio", "Narration", "Music"]
        currentIndex: controller.audioLane
        Accessible.name: "Audio source to edit"
        onActivated: controller.selectAudio(currentIndex)
    }
    Label { visible: !mixer.audio.enabled; text: "Select a clip or choose an audio file to edit this source."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted }
    Label { text: "Gain (dB)" }
    UI.TextField {
        objectName: "audioGainField"
        Layout.fillWidth: true
        enabled: mixer.audio.enabled
        text: Number(mixer.audio.gainDb || 0).toFixed(1)
        Accessible.name: "Audio gain in decibels"
        validator: DoubleValidator { bottom: -60; top: 12; decimals: 2; locale: "C" }
        onEditingFinished: if (acceptableInput) controller.updateAudio(controller.audioLane, {gainDb: Number(text)})
        Keys.onEscapePressed: { text = Qt.binding(function() { return Number(mixer.audio.gainDb || 0).toFixed(1) }); focus = false }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        UI.Button { text: "−1 dB"; enabled: mixer.audio.enabled; Accessible.name: "Reduce audio gain by one decibel"; onClicked: controller.updateAudio(controller.audioLane, {gainDb: mixer.audio.gainDb - 1}) }
        UI.Button { text: "+1 dB"; enabled: mixer.audio.enabled; Accessible.name: "Increase audio gain by one decibel"; onClicked: controller.updateAudio(controller.audioLane, {gainDb: mixer.audio.gainDb + 1}) }
        UI.Button { text: mixer.audio.muted ? "Unmute" : "Mute"; enabled: mixer.audio.enabled; checkable: true; checked: !!mixer.audio.muted; onClicked: controller.updateAudio(controller.audioLane, {muted: !mixer.audio.muted}) }
        UI.Button { text: "Solo preview"; enabled: mixer.audio.enabled; checkable: true; checked: controller.soloLane === controller.audioLane; onClicked: controller.setSolo(controller.audioLane) }
    }
    Label { visible: controller.soloLane >= 0; text: "SOLO PREVIEW · Export uses the full saved mix"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.accent }
    Repeater {
        model: [{label: "Fade in (frames)", key: "fadeInFrames"}, {label: "Fade out (frames)", key: "fadeOutFrames"}]
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            Label { text: modelData.label; Layout.fillWidth: true; wrapMode: Text.Wrap }
            UI.TextField {
                Layout.fillWidth: true; enabled: mixer.audio.enabled
                text: String(mixer.audio[modelData.key] || 0)
                Accessible.name: modelData.label
                validator: IntValidator { bottom: 0; top: Math.max(0, mixer.audio.playableFrames || 0) }
                onEditingFinished: if (acceptableInput) { var fields = {}; fields[modelData.key] = Number(text); controller.updateAudio(controller.audioLane, fields) }
                Keys.onEscapePressed: { text = Qt.binding(function() { return String(mixer.audio[modelData.key] || 0) }); focus = false }
            }
            Flow {
                Layout.fillWidth: true; spacing: 4
                UI.Button { text: "−1 frame"; enabled: mixer.audio.enabled; Accessible.name: "Shorten " + modelData.label; onClicked: { var fields = {}; fields[modelData.key] = Math.max(0, mixer.audio[modelData.key] - 1); controller.updateAudio(controller.audioLane, fields) } }
                UI.Button { text: "+1 frame"; enabled: mixer.audio.enabled; Accessible.name: "Lengthen " + modelData.label; onClicked: { var fields = {}; fields[modelData.key] = mixer.audio[modelData.key] + 1; controller.updateAudio(controller.audioLane, fields) } }
            }
        }
    }
    Flow {
        visible: controller.audioLane === 1; Layout.fillWidth: true; spacing: 4
        UI.Button { text: "Start −1f"; enabled: mixer.audio.enabled; onClicked: controller.updateAudio(1, {startFrame: Math.max(0, mixer.audio.startFrame - 1)}) }
        UI.Button { text: "Start +1f"; enabled: mixer.audio.enabled; onClicked: controller.updateAudio(1, {startFrame: mixer.audio.startFrame + 1}) }
    }
    CheckBox { visible: controller.audioLane === 2; text: "Loop to end"; enabled: mixer.audio.enabled; checked: !!mixer.audio.loopToEnd; onClicked: controller.updateAudio(2, {loopToEnd: checked}) }
    Label { text: "Master output · " + Number(controller.masterPeak).toFixed(1) + " dBFS"; Layout.fillWidth: true; wrapMode: Text.Wrap }
    ProgressBar { Layout.fillWidth: true; from: -60; to: 6; value: controller.masterPeak; Accessible.name: "Master output level in decibels full scale" }
    UI.Button { text: controller.masterClipped ? "CLIP · Reset" : "Reset clip"; onClicked: controller.resetClipping(); Accessible.name: "Reset persistent master clipping indicator" }
    Label { text: "Drag the gain line vertically, square fade handles horizontally, or the narration body to move its start. Fades clamp to playable duration."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted }
}
