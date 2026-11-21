import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Isaac 1.0
import "." as UI

ColumnLayout {
    id: sourceControls
    required property var controller
    readonly property var insertion: {
        controller.currentSource; controller.position; controller.duration
        return controller.sourceInsertionPreview(controller.position)
    }
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
        implicitHeight: Math.max(52, font.pixelSize * 2.2)
        Accessible.name: "Source frame"
        Accessible.description: (waveformStatus.visible ? waveformStatus.text + ". " : "Audio waveform. ")
                                + "Selected range " + ((controller.currentSource.inFrame || 0)/60).toFixed(3)
                                + " to " + ((controller.currentSource.outFrame || 0)/60).toFixed(3)
                                + " seconds. Arrow keys seek one frame; Escape cancels a scrub."
        background: Rectangle {
            x: sourceSlider.leftPadding; y: sourceSlider.topPadding
            width: sourceSlider.availableWidth; height: sourceSlider.availableHeight
            color: UI.Theme.field; radius: 4
            border.color: sourceSlider.visualFocus ? UI.Theme.accent : UI.Theme.controlBorder
            border.width: sourceSlider.visualFocus ? 2 : 1
            Rectangle {
                objectName: "sourceWaveformRange"
                x: Math.min(1, (controller.currentSource.inFrame || 0)/Math.max(1,sourceSlider.to)) * parent.width
                width: Math.max(0, Math.min(parent.width, (controller.currentSource.outFrame || 0)/Math.max(1,sourceSlider.to) * parent.width) - x)
                height: parent.height
                color: Qt.rgba(UI.Theme.accent.r, UI.Theme.accent.g, UI.Theme.accent.b, .12)
                Rectangle { anchors.left: parent.left; width: 2; height: parent.height; color: UI.Theme.accent }
                Rectangle { anchors.right: parent.right; width: 2; height: parent.height; color: UI.Theme.accent }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 2; color: UI.Theme.accent }
            }
            CachedWaveform {
                id: waveform
                objectName: "sourceWaveform"
                anchors.fill: parent
                cache: derivedMedia
                source: sourceControls.visible ? (controller.currentSource.path || "") : ""
                duration: Math.max(1,sourceSlider.to)/60
                color: UI.Theme.accent
                Accessible.ignored: true
            }
            Label {
                id: waveformStatus
                objectName: "sourceWaveformStatus"
                anchors.fill: parent; anchors.margins: 6
                visible: waveform.state !== "ready"
                text: waveform.state === "unavailable" ? "Audio waveform unavailable" : "Loading audio waveform…"
                color: UI.Theme.text
                verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
        handle: Rectangle {
            objectName: "sourceWaveformCursor"
            x: sourceSlider.leftPadding + sourceSlider.visualPosition * sourceSlider.availableWidth - width/2
            y: sourceSlider.topPadding
            implicitWidth: 2; height: sourceSlider.availableHeight
            color: UI.Theme.text
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 10; height: 5; color: UI.Theme.text }
        }
        onMoved: if (!sourcePointer.pressed) controller.seekSource(value)
        Keys.onEscapePressed: sourceControls.cancelScrub()
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
            onCanceled: sourceControls.cancelScrub()
        }
    }
    Flow {
        id: trimTools
        objectName: "sourceTrimTools"
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
        spacing: 6
        Row {
            spacing: 4
            UI.Button {
                id: markIn
                objectName: "markSourceInButton"
                text: "In"; helpText: "Mark in at the current frame (I)"
                Accessible.name: helpText
                implicitWidth: Math.max(controlSize,contentItem.implicitWidth + leftPadding + rightPadding)
                onClicked: controller.markSourceIn()
            }
            UI.TextField {
                objectName: "sourceInField"
                width: Math.min(Math.max(100,font.pixelSize * 7),Math.max(100,sourceControls.width - markIn.width - 4))
                suffix: "s"
                text: ((controller.currentSource.inFrame || 0)/60).toFixed(3)
                function restore() { text = Qt.binding(function(){ return ((controller.currentSource.inFrame || 0)/60).toFixed(3) }) }
                Accessible.name: "Source in point in seconds"
                validator: DoubleValidator { bottom: 0; top: Math.max(0,(controller.currentSource.durationFrames || 1) - 1)/60 + .0005; decimals: 3; locale: "C"; notation: DoubleValidator.StandardNotation }
                onEditingFinished: { if (acceptableInput) controller.setSourceRange(Math.round(Number(text)*60),controller.currentSource.outFrame); restore() }
                Keys.onEscapePressed: { restore(); focus = false }
            }
        }
        Row {
            spacing: 4
            UI.Button {
                id: markOut
                objectName: "markSourceOutButton"
                text: "Out"; helpText: "Mark out after the current frame (O)"
                Accessible.name: helpText
                implicitWidth: Math.max(controlSize,contentItem.implicitWidth + leftPadding + rightPadding)
                onClicked: controller.markSourceOut()
            }
            UI.TextField {
                objectName: "sourceOutField"
                width: Math.min(Math.max(100,font.pixelSize * 7),Math.max(100,sourceControls.width - markOut.width - 4))
                suffix: "s"
                text: ((controller.currentSource.outFrame || 1)/60).toFixed(3)
                function restore() { text = Qt.binding(function(){ return ((controller.currentSource.outFrame || 1)/60).toFixed(3) }) }
                Accessible.name: "Source out point in seconds"
                validator: DoubleValidator { bottom: 1/60 - .0005; top: (controller.currentSource.durationFrames || 1)/60 + .0005; decimals: 3; locale: "C"; notation: DoubleValidator.StandardNotation }
                onEditingFinished: { if (acceptableInput) controller.setSourceRange(controller.currentSource.inFrame,Math.round(Number(text)*60)); restore() }
                Keys.onEscapePressed: { restore(); focus = false }
            }
        }
        UI.Button { text: "Clear range"; iconName: "reset"; iconOnly: true; flat: true; helpText: "Clear the marked range"; onClicked: controller.clearSourceRange() }
        Label {
            text: ((controller.currentSource.rangeFrames || 0)/60).toFixed(3) + " s selected"
            height: Math.max(36,Math.ceil(font.pixelSize * 2),implicitHeight); verticalAlignment: Text.AlignVCenter
            color: UI.Theme.muted; Accessible.name: text
        }
        UI.Button { objectName: "appendSourceButton"; text: "Append"; highlighted: true; visible: controller.currentSource.kind === "video"; enabled: controller.currentSource.validRange === true; helpText: "Append the marked range to the project"; onClicked: controller.appendSource() }
        UI.Button { objectName: "insertSourceButton"; text: "Insert"; visible: controller.currentSource.kind === "video"; enabled: sourceControls.insertion.valid === true; helpText: "Insert the marked range at the Project playhead\n" + (sourceControls.insertion.reason || ""); onClicked: controller.insertSource() }
        UI.Button { text: "Use as narration"; visible: controller.currentSource.kind === "audio"; onClicked: controller.setVoicePathAsync(controller.currentSource.path) }
        UI.Button { text: "Use as music"; visible: controller.currentSource.kind === "audio"; onClicked: controller.setMusicPathAsync(controller.currentSource.path) }
    }
    Label {
        visible: controller.currentSource.kind === "video" && sourceControls.insertion.valid !== true
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted
        text: sourceControls.insertion.reason || ""
    }
}
