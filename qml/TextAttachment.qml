import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    property bool caption: false
    readonly property var info: caption ? controller.currentCaption : controller.currentText
    readonly property string summary: info.attached ? "Follows clip" : "Project time"
    spacing: 6
    Label { text: panel.summary; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { text: panel.info.attached ? "Moves and trims with its clip." : "Stays at its project time when clips change."; color: UI.Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { text: "Clip" }
    UI.ComboBox {
        id: host
        Layout.fillWidth: true
        model: panel.controller.clips; textRole: "attachmentLabel"
        Accessible.name: panel.caption ? "Caption attachment host clip" : "Text attachment host clip"
        Accessible.description: panel.info.attached ? "Attached at source frame " + panel.info.hostSourceFrame : "Choose a clip for the text to follow."
        hoverEnabled: true
        ToolTip.visible: !!panel.info.attached && (hovered || activeFocus)
        ToolTip.text: "Clip source position: frame " + panel.info.hostSourceFrame
        currentIndex: {
            var clips = panel.controller.clips
            for (var i = 0; i < clips.length; ++i) {
                if (panel.info.attached && clips[i].id === panel.info.hostId) return i
                if (!panel.info.attached && panel.info.startFrame >= clips[i].startFrame && panel.info.startFrame < clips[i].startFrame + clips[i].frames) return i
            }
            return clips.length ? 0 : -1
        }
    }
    Flow {
        Layout.fillWidth: true; spacing: 6
        UI.Button { text: "Attach to clip"; iconName: "link"; enabled: host.currentIndex >= 0; onClicked: panel.controller.attachTimedText(panel.caption,host.currentIndex) }
        UI.Button { text: "Detach"; iconName: "unlink"; enabled: !!panel.info.attached; onClicked: panel.controller.detachTimedText(panel.caption) }
    }
}
