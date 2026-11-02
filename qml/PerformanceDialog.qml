import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

Dialog {
    id: panel
    objectName: "performanceDialog"
    required property var controller
    property var stats: ({})
    title: "Performance"
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent.width - 40,540)
    height: Math.min(parent.height - 40,580)
    standardButtons: Dialog.Close
    onOpened: stats = controller.performanceDiagnostics()
    Timer { interval: 750; repeat: true; running: panel.visible; onTriggered: panel.stats = panel.controller.performanceDiagnostics() }
    contentItem: ScrollView {
        id: scroll
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12
            Label { text: "Preview quality"; font.weight: Font.DemiBold }
            UI.ComboBox {
                objectName: "previewQualityChoice"
                Layout.fillWidth: true
                model: ["Automatic", "540p", "1080p", "4K"]
                currentIndex: [0,540,1080,2160].indexOf(panel.controller.previewQuality)
                Accessible.name: "Preview quality"
                onActivated: index => panel.controller.setPreviewQuality([0,540,1080,2160][index])
            }
            Label { text: "Higher quality needs more memory and processing. Export resolution is unchanged."; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: "Preview storage folder"; font.weight: Font.DemiBold }
            UI.TextField { objectName: "previewStorageFolder"; text: panel.controller.cacheDirectory; readOnly: true; Accessible.name: "Preview storage folder"; Layout.fillWidth: true }
            UI.Button { text: "Choose folder…"; onClicked: panel.controller.chooseCacheDirectory() }
            RowLayout {
                Label { text: "Storage budget (GB)"; Layout.fillWidth: true }
                SpinBox { objectName: "previewStorageBudget"; from: 0; to: 1024; editable: true; value: panel.controller.cacheLimitGiB; Accessible.name: "Preview storage budget in GB"; onValueModified: panel.controller.setCacheLimitGiB(value) }
            }
            Label {
                text: "Unused copies are removed automatically to stay within the budget. Files currently in use are kept."
                Layout.fillWidth: true; wrapMode: Text.Wrap
            }
            Label {
                text: "Stored copies: " + (((panel.stats.proxy || {}).storageBytes || 0) / 1073741824).toFixed(2) + " GB"
                Layout.fillWidth: true; wrapMode: Text.Wrap
            }
            UI.InspectorDetails {
                title: "Diagnostics"
                Layout.fillWidth: true
                TextArea { text: JSON.stringify(panel.stats,null,2); readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; Accessible.name: "Performance diagnostics"; Layout.fillWidth: true }
            }
        }
    }
}
