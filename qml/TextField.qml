import QtQuick
import QtQuick.Controls as Controls

Controls.TextField {
    id: control
    property string suffix: ""
    property bool stepper: false
    signal stepRequested(int direction)
    implicitHeight: Math.max(36, contentHeight + topPadding + bottomPadding)
    leftPadding: 10; rightPadding: (suffix ? suffixLabel.implicitWidth + 18 : 10) + (stepper ? 28 : 0); topPadding: 8; bottomPadding: 8
    Keys.onUpPressed: event => { if (stepper) stepRequested(1); else event.accepted = false }
    Keys.onDownPressed: event => { if (stepper) stepRequested(-1); else event.accepted = false }
    color: enabled ? Theme.text : Theme.disabledText
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    Controls.Label {
        id: suffixLabel
        visible: control.suffix.length > 0
        text: control.suffix
        color: control.enabled ? Theme.muted : Theme.disabledText
        anchors.right: parent.right; anchors.rightMargin: 10 + (control.stepper ? 28 : 0)
        anchors.verticalCenter: parent.verticalCenter
        Accessible.ignored: true
    }
    Column {
        visible: control.stepper
        anchors.right: parent.right; anchors.rightMargin: 3
        anchors.top: parent.top; anchors.topMargin: 2
        anchors.bottom: parent.bottom; anchors.bottomMargin: 2
        width: 26
        Controls.ToolButton {
            width: parent.width; height: parent.height / 2
            padding: 0; text: "+"; focusPolicy: Qt.NoFocus
            Accessible.name: "Increase " + control.Accessible.name
            onClicked: { control.forceActiveFocus(Qt.MouseFocusReason); control.stepRequested(1) }
        }
        Controls.ToolButton {
            width: parent.width; height: parent.height / 2
            padding: 0; text: "−"; focusPolicy: Qt.NoFocus
            Accessible.name: "Decrease " + control.Accessible.name
            onClicked: { control.forceActiveFocus(Qt.MouseFocusReason); control.stepRequested(-1) }
        }
    }
    background: Rectangle {
        implicitWidth: 140
        radius: Theme.radius
        color: Theme.field
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : control.enabled ? Theme.controlBorder : Theme.border
    }
}
