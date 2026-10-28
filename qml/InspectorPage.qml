import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page
    default property alias content: layout.data
    Layout.minimumWidth: 0
    Layout.minimumHeight: 0
    contentWidth: availableWidth
    contentHeight: layout.implicitHeight
    clip: true
    function revealFocus() {
        var host = page.Window.window
        if (host && typeof host.revealFocusedControl === "function") Qt.callLater(host.revealFocusedControl)
    }
    onContentHeightChanged: revealFocus()
    onHeightChanged: revealFocus()
    Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds
    ColumnLayout { id: layout; width: page.availableWidth; spacing: 0 }
}
