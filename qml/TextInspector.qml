import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    property bool appearanceOnly: false
    spacing: 10
    function focusFirst() { if (controller.selectedText >= 0) { content.forceActiveFocus(); content.selectAll() } }
    RowLayout {
        visible: !panel.appearanceOnly
        Layout.fillWidth: true; Layout.margins: 12
        UI.ComboBox {
            Layout.fillWidth: true
            model: controller.textOverlays; textRole: "text"
            currentIndex: controller.selectedText
            displayText: controller.selectedText < 0 ? "Choose text" : (controller.currentText.text || "Empty text").split("\n")[0]
            enabled: controller.textOverlays.length > 0
            Accessible.name: "Select text overlay"
            onActivated: index => controller.selectText(index)
        }
        UI.Button {
            objectName: "deleteTextButton"
            text: "Delete text"; iconName: "trash"; iconOnly: true; flat: true
            visible: controller.selectedText >= 0
            enabled: !controller.exporting
            onClicked: controller.deleteText()
        }
    }
    Label { visible: !panel.appearanceOnly && controller.selectedText < 0; text: "Select text or choose Add text"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: 12; color: UI.Theme.muted }
    ColumnLayout {
        visible: controller.selectedText >= 0
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
        spacing: 10
        Label { text: "Content"; visible: !panel.appearanceOnly }
        TextArea {
            id: content
            visible: !panel.appearanceOnly
            objectName: "textContentField"
            property int editingIndex: -1
            Layout.fillWidth: true
            Layout.minimumHeight: 90
            text: controller.currentText.text || ""
            textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; selectByMouse: true
            color: UI.Theme.text; selectionColor: UI.Theme.accent; selectedTextColor: UI.Theme.accentText
            padding: 10
            Accessible.name: "Overlay text"
            background: Rectangle { color: UI.Theme.field; radius: UI.Theme.radius; border.width: content.activeFocus ? 2 : 1; border.color: content.activeFocus ? UI.Theme.accent : UI.Theme.controlBorder }
            onActiveFocusChanged: {
                if (activeFocus) { editingIndex = controller.selectedText; controller.beginTextDrag() }
                else { controller.endTextDrag(false); editingIndex = -1 }
            }
            onTextChanged: if (activeFocus && editingIndex === controller.selectedText) {
                if (text.length > 2000) remove(2000,text.length)
                controller.previewTextContent(text)
            }
            Keys.onEscapePressed: event => { controller.endTextDrag(true); focus = false; text = Qt.binding(function() { return controller.currentText.text || "" }); event.accepted = true }
        }
        Label { text: "Style" }
        UI.ComboBox {
            Layout.fillWidth: true
            model: ["Clean", "Playful", "Background box"]
            currentIndex: Math.max(0,["clean","playful","box"].indexOf(controller.currentText.style))
            Accessible.name: "Text style"
            onActivated: index => controller.updateText(controller.selectedText,{style: ["clean","playful","box"][index]})
        }
        Label { text: "Font" }
        UI.ComboBox {
            Layout.fillWidth: true
            model: controller.fontFamilies
            currentIndex: Math.max(0,controller.fontFamilies.indexOf(controller.currentText.fontFamily || "Arial"))
            Accessible.name: "Text font"
            onActivated: controller.updateText(controller.selectedText,{fontFamily: currentText})
        }
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Size" }
                UI.TextField {
                    objectName: panel.appearanceOnly ? "captionSizeField" : "textSizeField"
                    property int editingIndex: -1
                    Layout.fillWidth: true
                    text: String(controller.currentText.fontSize || 84)
                    validator: IntValidator { bottom: 12; top: 200 }
                    Accessible.name: "Text size"
                    onActiveFocusChanged: if (activeFocus) editingIndex = controller.selectedText
                    onEditingFinished: if (acceptableInput && editingIndex === controller.selectedText) controller.updateText(editingIndex,{fontSize: Number(text)})
                    Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return String(controller.currentText.fontSize || 84) }); focus = false; event.accepted = true }
                }
            }
            ColumnLayout {
                visible: !panel.appearanceOnly
                Layout.fillWidth: true
                Label { text: "Duration (s)" }
                UI.TextField {
                    objectName: "textDurationField"
                    property int editingIndex: -1
                    Layout.fillWidth: true
                    text: Number(controller.currentText.durationSeconds || 0).toFixed(3)
                    validator: DoubleValidator { bottom: 1/60; top: 86400; decimals: 3; locale: "C" }
                    Accessible.name: "Text duration in seconds"
                    onActiveFocusChanged: if (activeFocus) editingIndex = controller.selectedText
                    onEditingFinished: if (acceptableInput && editingIndex === controller.selectedText) controller.updateText(editingIndex,{durationFrames: Math.round(Number(text)*60)})
                    Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return Number(controller.currentText.durationSeconds || 0).toFixed(3) }); focus = false; event.accepted = true }
                }
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            UI.Button {
                objectName: "textColorButton"
                text: "Choose text color"; iconOnly: true
                contentItem: Rectangle {
                    implicitWidth: 20; implicitHeight: 20
                    radius: 3; color: controller.currentText.color || "#ffffff"
                    border.color: UI.Theme.controlBorder
                }
                onClicked: controller.chooseTextColor()
            }
            UI.Button {
                objectName: "textBoldButton"
                text: "Bold"; iconName: "bold"; iconOnly: true; checkable: true
                checked: controller.currentText.bold || false
                onToggled: controller.updateText(controller.selectedText,{bold: checked})
            }
            CheckBox { text: "Shadow"; checked: controller.currentText.shadow || false; onToggled: controller.updateText(controller.selectedText,{shadow: checked}) }
        }
        Label { text: "Position" }
        GridLayout {
            columns: 3; Layout.fillWidth: true
            Repeater {
                model: ["↖","↑","↗","←","•","→","↙","↓","↘"]
                UI.Button {
                    required property string modelData
                    required property int index
                    text: modelData; Layout.fillWidth: true; Layout.minimumWidth: 0
                    Accessible.name: ["Top left","Top center","Top right","Middle left","Center","Middle right","Bottom left","Bottom center","Bottom right"][index]
                    onClicked: controller.updateText(controller.selectedText,{x: [.2,.5,.8][index%3],y: [.2,.5,.8][Math.floor(index/3)]})
                }
            }
        }
        CheckBox { text: "Fade in and out"; checked: controller.currentText.fade || false; onToggled: controller.updateText(controller.selectedText,{fade: checked}) }
        Loader { Layout.fillWidth: true; active: !panel.appearanceOnly; sourceComponent: ColumnLayout {
            UI.TextAttachment { controller: panel.controller; Layout.fillWidth: true }
            CheckBox { text: "Show safe-margin guides"; checked: panel.controller.textGuides; onToggled: panel.controller.textGuides = checked }
        } }
        Item { Layout.preferredHeight: 8 }
    }
}
