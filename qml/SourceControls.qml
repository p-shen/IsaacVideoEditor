import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: source
    required property var controller
    function cancelScrub() {
        sourcePointer.canceled = true
        sourceSlider.pressed = false
        controller.finishSourceScrub(true)
    }
    spacing: 4
    Slider {
        id: sourceSlider
        objectName: "sourcePositionSlider"
        Layout.fillWidth: true; from: 0; to: Math.max(0,(controller.currentSource.durationFrames || 1) - 1)
        stepSize: 1; value: controller.sourcePosition
        Accessible.name: "Source frame"
        onMoved: if (!sourcePointer.pressed) controller.seekSource(value)
        Keys.onEscapePressed: source.cancelScrub()
        MouseArea {
            id: sourcePointer
            anchors.fill: parent
            preventStealing: true
            property bool canceled: false
            function seekAt(x) {
                var fraction = Math.max(0,Math.min(1,(x-sourceSlider.leftPadding)/Math.max(1,sourceSlider.availableWidth)))
                if (sourceSlider.mirrored) fraction = 1-fraction
                controller.seekSource(Math.round(sourceSlider.from+fraction*(sourceSlider.to-sourceSlider.from)))
            }
            onPressed: mouse => { canceled = false; sourceSlider.forceActiveFocus(); sourceSlider.pressed = true; controller.beginSourceScrub(); seekAt(mouse.x) }
            onPositionChanged: mouse => { if (pressed && !canceled) seekAt(mouse.x) }
            onReleased: mouse => { if (!canceled) seekAt(mouse.x); sourceSlider.pressed = false; controller.finishSourceScrub(canceled) }
            onCanceled: source.cancelScrub()
        }
    }
    Flow {
        Layout.fillWidth: true; spacing: 3
        UI.Button { text: "I · Mark In"; onClicked: controller.markSourceIn() }
        UI.Button { text: "O · Mark Out"; onClicked: controller.markSourceOut() }
        UI.Button { text: "Clear Range"; onClicked: controller.clearSourceRange() }
        UI.Button { text: "−1 frame"; onClicked: controller.stepSource(-1) }
        UI.Button { text: "+1 frame"; onClicked: controller.stepSource(1) }
    }
    Flow {
        Layout.fillWidth: true; spacing: 5
        Label { text: "In (frame)"; padding: 5 }
        UI.TextField {
            objectName: "sourceInField"; width: Math.max(70,font.pixelSize * 4)
            text: controller.currentSource.inFrame === undefined ? "0" : controller.currentSource.inFrame
            Accessible.name: "Source inclusive in frame"
            validator: IntValidator { bottom: 0; top: Math.max(0,(controller.currentSource.durationFrames || 1) - 1) }
            onEditingFinished: if (acceptableInput) controller.setSourceRange(Number(text),controller.currentSource.outFrame)
            Keys.onEscapePressed: { text = Qt.binding(function(){ return String(controller.currentSource.inFrame || 0) }); focus = false }
        }
        Label { text: "Out (exclusive)"; padding: 5 }
        UI.TextField {
            objectName: "sourceOutField"; width: Math.max(70,font.pixelSize * 4)
            text: controller.currentSource.outFrame || 1
            Accessible.name: "Source exclusive out frame"
            validator: IntValidator { bottom: 1; top: controller.currentSource.durationFrames || 1 }
            onEditingFinished: if (acceptableInput) controller.setSourceRange(controller.currentSource.inFrame,Number(text))
            Keys.onEscapePressed: { text = Qt.binding(function(){ return String(controller.currentSource.outFrame || 1) }); focus = false }
        }
    }
    Label { text: "Selected: " + (controller.currentSource.rangeFrames || 0) + " frames · " + ((controller.currentSource.rangeFrames || 0)/60).toFixed(3) + " s"; Layout.fillWidth: true; wrapMode: Text.Wrap; Accessible.name: text }
    Flow {
        Layout.fillWidth: true; spacing: 4
        UI.Button { objectName: "appendSourceButton"; text: "Append"; enabled: controller.currentSource.validRange === true && controller.currentSource.kind === "video"; onClicked: controller.appendSource() }
        UI.Button { objectName: "insertSourceButton"; text: "Insert at playhead"; enabled: { controller.currentSource; controller.duration; return controller.sourceInsertionPreview(controller.position).valid } onClicked: controller.insertSource() }
        UI.Button { text: "Use as narration"; visible: controller.currentSource.kind === "audio"; onClicked: controller.setVoicePath(controller.currentSource.path) }
        UI.Button { text: "Use as music"; visible: controller.currentSource.kind === "audio"; onClicked: controller.setMusicPath(controller.currentSource.path) }
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted
        text: { controller.currentSource; controller.position; controller.duration; return controller.sourceInsertionPreview(controller.position).reason || "" }
    }
}
