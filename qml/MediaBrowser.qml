import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI
import Isaac 1.0

ColumnLayout {
    id: browser
    required property var controller
    spacing: 6
    property var rows: {
        controller.assets
        return controller.filteredAssets(search.text,["all","video","audio"][kind.currentIndex],["all","used","unused"][usage.currentIndex])
    }
    function focusList() { mediaList.forceActiveFocus() }
    UI.TextField {
        id: search; objectName: "mediaSearch"
        Layout.fillWidth: true; Layout.margins: 6
        placeholderText: "Search Media"
        Accessible.name: "Search media by filename"
    }
    Flow {
        Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 6; spacing: 3
        UI.ComboBox { id: kind; objectName: "mediaTypeFilter"; model: ["All types","Video","Audio"]; Accessible.name: "Media type filter"; width: Math.min(browser.width - 12,Math.max(120,font.pixelSize * 9)) }
        UI.ComboBox { id: usage; objectName: "mediaUsageFilter"; model: ["All uses","Used","Unused"]; Accessible.name: "Media usage filter"; width: Math.min(browser.width - 12,Math.max(120,font.pixelSize * 9)) }
    }
    ListView {
        id: mediaList; objectName: "mediaList"
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 76
        model: browser.rows; clip: true; spacing: 4
        activeFocusOnTab: true
        Accessible.name: "Reusable Media sources"
        currentIndex: {
            for (var i = 0; i < browser.rows.length; ++i) if (browser.rows[i].index === controller.selectedSource) return i
            return -1
        }
        onCurrentIndexChanged: if(currentIndex >= 0) positionViewAtIndex(currentIndex,ListView.Contain)
        ScrollBar.vertical: ScrollBar {}
        delegate: ItemDelegate {
            id: sourceRow
            required property var modelData
            required property int index
            width: mediaList.width - 12; x: 6
            height: Math.max(76,font.pixelSize * 5)
            highlighted: modelData.index === controller.selectedSource
            Accessible.name: modelData.name + ", " + modelData.kind + ", " + modelData.durationText + ", " + modelData.uses + " uses" + (modelData.offline ? ", offline" : "")
            background: Rectangle {
                radius: UI.Theme.radius
                color: sourceRow.highlighted ? UI.Theme.selected : sourceRow.hovered ? UI.Theme.hover : UI.Theme.panel
                border.width: sourceRow.visualFocus || sourceRow.highlighted ? 2 : 0
                border.color: sourceRow.palette.highlight
            }
            contentItem: RowLayout {
                spacing: 8
                CachedFrame { objectName: "mediaSourceThumbnail" + sourceRow.index; visible: sourceRow.modelData.kind === "video"; cache: derivedMedia; source: sourceRow.modelData.path; seconds: sourceRow.modelData.inFrame / 60; Layout.preferredWidth: 60; Layout.preferredHeight: 42 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Label { text: sourceRow.modelData.name; color: UI.Theme.text; elide: Text.ElideMiddle; Layout.fillWidth: true; font.weight: Font.DemiBold }
                    Label { text: sourceRow.modelData.kind + " · " + sourceRow.modelData.uses + " uses" + (sourceRow.modelData.offline ? " · Offline" : ""); color: UI.Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
            MouseArea {
                anchors.fill: parent
                property point pressedAt
                property bool dragged: false
                onPressed: mouse => { pressedAt = Qt.point(mouse.x,mouse.y); dragged = false; mediaList.forceActiveFocus(); controller.selectSource(sourceRow.modelData.index) }
                onPositionChanged: mouse => {
                    if (pressed && !dragged && Math.abs(mouse.x - pressedAt.x) + Math.abs(mouse.y - pressedAt.y) > 12) {
                        dragged = true; controller.startSourceDrag(sourceRow.modelData.index)
                    }
                }
                onClicked: controller.selectSource(sourceRow.modelData.index)
            }
            onClicked: controller.selectSource(modelData.index)
            Keys.onReturnPressed: controller.selectSource(modelData.index)
        }
        Keys.onUpPressed: { var i = Math.max(0,currentIndex - 1); if (browser.rows.length) controller.selectSource(browser.rows[i].index) }
        Keys.onDownPressed: { var i = Math.min(browser.rows.length - 1,currentIndex + 1); if (i >= 0) controller.selectSource(browser.rows[i].index) }
        Label { anchors.centerIn: parent; width: parent.width - 24; text: browser.rows.length ? "" : controller.assets.length ? "No matching sources" : "Drop media here\nor click Import"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: UI.Theme.muted }
        DropArea { anchors.fill: parent; onDropped: drop => { if (drop.hasUrls) { controller.importMediaUrls(drop.urls); drop.acceptProposedAction() } } }
    }
    Flow {
        Layout.fillWidth: true; Layout.margins: 6; spacing: 3
        UI.Button { text: "Import"; onClicked: controller.importDialog() }
        UI.Button { text: "Remove source"; enabled: controller.selectedSource >= 0; onClicked: controller.confirmRemoveSource(controller.selectedSource) }
        UI.Button { text: "Import and append…"; onClicked: controller.importAndAppendDialog() }
    }
}
