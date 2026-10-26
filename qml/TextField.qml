import QtQuick
import QtQuick.Controls as Controls

Controls.TextField {
    id: control
    property string suffix: ""
    implicitHeight: Math.max(36, contentHeight + topPadding + bottomPadding)
    leftPadding: 10; rightPadding: suffix ? suffixLabel.implicitWidth + 18 : 10; topPadding: 8; bottomPadding: 8
    color: enabled ? Theme.text : Theme.disabledText
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    Controls.Label {
        id: suffixLabel
        visible: control.suffix.length > 0
        text: control.suffix
        color: control.enabled ? Theme.muted : Theme.disabledText
        anchors.right: parent.right; anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        Accessible.ignored: true
    }
    background: Rectangle {
        implicitWidth: 140
        radius: Theme.radius
        color: Theme.field
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : control.enabled ? Theme.controlBorder : Theme.border
    }
}
