import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Controls.impl

Controls.Button {
    id: control
    property string iconName: ""
    property bool iconOnly: false
    property string helpText: text
    readonly property color foreground: !enabled ? Theme.disabledText : highlighted ? Theme.accentText : Theme.text
    readonly property real controlSize: Math.max(34,Math.ceil(font.pixelSize * 2))
    implicitWidth: iconOnly ? Math.max(controlSize,contentItem.implicitWidth + leftPadding + rightPadding) : Math.max(76,contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(controlSize,contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: iconOnly ? 8 : 12; rightPadding: leftPadding
    topPadding: 8; bottomPadding: 8
    spacing: 6
    hoverEnabled: true
    font.weight: Font.Medium
    icon.source: iconName ? Qt.resolvedUrl("icons/" + iconName + ".svg") : ""
    icon.width: Math.max(18,Math.round(font.pixelSize * 1.25))
    icon.height: Math.max(18,Math.round(font.pixelSize * 1.25))
    icon.color: foreground
    display: iconOnly ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon
    Accessible.name: text
    Accessible.description: helpText !== text ? helpText : ""
    Controls.ToolTip.visible: helpText.length > 0 && (hovered || visualFocus)
    Controls.ToolTip.text: helpText
    Controls.ToolTip.delay: visualFocus ? 0 : 500
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.foreground
    }
    background: Rectangle {
        radius: Theme.radius
        color: !control.enabled ? (control.flat ? "transparent" : Theme.field) : control.highlighted ? (control.down ? Qt.darker(Theme.accent,1.08) : control.hovered ? Qt.lighter(Theme.accent,1.08) : Theme.accent) : control.checked || control.down ? Theme.selected : control.hovered ? Theme.hover : control.flat ? "transparent" : Theme.field
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus || control.checked ? Theme.accent : control.highlighted && control.enabled ? Theme.accent : control.flat ? "transparent" : Theme.border
        Rectangle {
            visible: control.checkable && control.checked
            anchors.bottom: parent.bottom; anchors.bottomMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.max(8,parent.width - 20); height: 2; radius: 1
            color: control.foreground
        }
    }
}
