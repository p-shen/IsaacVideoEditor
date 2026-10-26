import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

UI.Pane {
    id: root
    required property var controller
    property bool expanded: controller.recoveryCandidates.length > 0
    padding: 8
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.controller.recoveryStatus; color: UI.Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
            UI.Button { text: "Retry recovery"; visible: root.controller.recoveryStatus.indexOf("failed") >= 0; onClicked: root.controller.retryRecovery() }
            UI.Button {
                text: root.controller.missingMedia.length ? root.controller.missingMedia.length + " offline · Locate" : "Recovery"
                visible: root.controller.missingMedia.length > 0 || root.controller.recoveryCandidates.length > 0
                onClicked: root.expanded = !root.expanded
            }
        }
        ColumnLayout {
            visible: root.expanded
            Layout.fillWidth: true
            Repeater {
                model: root.controller.recoveryCandidates
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: "Recover " + modelData.name + " · " + new Date(modelData.time).toLocaleString(); wrapMode: Text.Wrap; Layout.fillWidth: true }
                    UI.Button { text: "Recover"; onClicked: root.controller.recoverSnapshot(modelData.path) }
                    UI.Button { text: "Discard"; onClicked: root.controller.discardRecovery(modelData.path) }
                }
            }
            Label { visible: root.controller.missingMedia.length > 0; text: "Offline sources keep their timing and edits. Locate the originals to restore playback and export."; wrapMode: Text.Wrap; Layout.fillWidth: true }
            UI.Button { visible: root.controller.missingMedia.length > 0; text: "Search a folder…"; onClicked: root.controller.chooseRelinkFolder() }
            Repeater {
                model: root.controller.missingMedia
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: modelData.path; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    UI.Button { text: "Locate file…"; onClicked: root.controller.locateMissing(modelData.path) }
                }
            }
            Repeater {
                model: root.controller.relinkProposals
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label { text: (modelData.ambiguous ? "Choose between matching files: " : "Suggested match: ") + modelData.missing; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        UI.ComboBox { id: candidate; model: modelData.candidates; Layout.fillWidth: true; Accessible.name: "Replacement path" }
                        UI.Button { text: "Relink"; onClicked: root.controller.relinkMedia(modelData.missing,candidate.currentText) }
                    }
                }
            }
        }
    }
}
