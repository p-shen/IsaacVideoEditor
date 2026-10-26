import QtQuick
import QtQuick.Controls as Controls

Controls.Button {
    id: control
    implicitWidth: Math.max(76,contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(34,contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: 12; rightPadding: 12; topPadding: 8; bottomPadding: 8
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? control.palette.disabled.buttonText : control.highlighted ? control.palette.highlightedText : control.palette.buttonText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: control.highlighted ? (control.down ? Qt.darker(control.palette.highlight,1.15) : control.hovered ? Qt.lighter(control.palette.highlight,1.1) : control.palette.highlight) : (control.down ? Qt.darker(control.palette.button,1.12) : control.hovered ? Qt.lighter(control.palette.button,1.08) : control.palette.button)
        opacity: control.enabled ? 1 : 0.5
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? control.palette.highlight : Qt.rgba(control.palette.buttonText.r,control.palette.buttonText.g,control.palette.buttonText.b,0.6)
    }
}
