import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: details
    property string title: ""
    property string summary: ""
    property bool expanded: false
    default property alias content: body.data
    Layout.fillWidth: true
    spacing: 8
    function focusHeader() { toggle.forceActiveFocus(Qt.TabFocusReason) }
    UI.Button {
        id: toggle
        objectName: details.objectName ? details.objectName + "Toggle" : ""
        Layout.fillWidth: true
        flat: true
        checkable: true
        checked: details.expanded
        text: details.title
        iconName: details.expanded ? "chevron-down" : "chevron-right"
        helpText: details.summary ? details.title + ": " + details.summary : details.title
        Accessible.name: details.title
        Accessible.description: (details.expanded ? "Expanded" : "Collapsed") + (details.summary ? ". " + details.summary : "")
        onToggled: details.expanded = checked
        contentItem: RowLayout {
            spacing: 8
            Image { source: toggle.icon.source; sourceSize.width: 16; sourceSize.height: 16; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
            Label { text: details.title; color: UI.Theme.text; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: details.summary; visible: text.length > 0; color: UI.Theme.muted; elide: Text.ElideRight; Layout.maximumWidth: toggle.availableWidth * .42 }
        }
    }
    ColumnLayout {
        id: body
        visible: details.expanded
        Layout.fillWidth: true
        spacing: 8
    }
}
