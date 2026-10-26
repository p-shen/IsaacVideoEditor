import QtQuick
import QtQuick.Controls as Controls

Controls.TabButton {
    id: control
    implicitWidth: Math.max(64, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(32, contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: 10; rightPadding: 10; topPadding: 7; bottomPadding: 7
    hoverEnabled: true
    font.weight: checked ? Font.DemiBold : Font.Normal
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.disabledText : control.checked ? Theme.text : Theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: control.checked ? Theme.selected : control.hovered ? Theme.hover : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
}
