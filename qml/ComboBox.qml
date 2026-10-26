import QtQuick
import QtQuick.Controls as Controls

Controls.ComboBox {
    id: control
    implicitHeight: Math.max(36, contentItem.implicitHeight + topPadding + bottomPadding)
    palette.dark: Theme.muted

    delegate: Controls.ItemDelegate {
        id: option
        required property var model
        required property int index
        width: ListView.view.width
        text: model[control.textRole]
        palette.text: enabled ? Theme.text : Theme.disabledText
        // Highlighted rows use a pale selection surface, not the accent fill.
        palette.highlightedText: enabled ? Theme.text : Theme.disabledText
        font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        background: Rectangle {
            radius: Theme.radius - 3
            color: option.down || option.highlighted ? Theme.selected : option.hovered ? Theme.hover : Theme.panel
            border.width: option.highlighted || option.visualFocus ? 1 : 0
            border.color: Theme.accent
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.selected : control.hovered ? Theme.hover : Theme.field
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
    }

    popup: Controls.Popup {
        y: control.height + 4
        width: control.width
        height: Math.min(implicitHeight, control.Window.height - topMargin - bottomMargin)
        topMargin: 8
        bottomMargin: 8
        padding: 4
        font: control.font
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollIndicator.vertical: Controls.ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.panel
            border.color: Theme.controlBorder
        }
    }
}
