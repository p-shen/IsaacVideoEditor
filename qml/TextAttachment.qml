import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    property bool caption: false
    readonly property var info: caption ? controller.currentCaption : controller.currentText
    spacing: 6
    Label { text: panel.info.attached ? "Attached to clip · source frame " + panel.info.hostSourceFrame : "Project time"; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { visible: !!panel.info.clamped; text: "Video ends before this text finishes. Its requested duration is retained."; color: UI.Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
    UI.ComboBox {
        id: host
        Layout.fillWidth: true
        model: panel.controller.clips; textRole: "attachmentLabel"
        Accessible.name: "Text attachment host clip"
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
        UI.Button { text: "Attach to clip"; enabled: host.currentIndex >= 0; onClicked: panel.controller.attachTimedText(panel.caption,host.currentIndex) }
        UI.Button { text: "Detach"; enabled: !!panel.info.attached; onClicked: panel.controller.detachTimedText(panel.caption) }
    }
}
