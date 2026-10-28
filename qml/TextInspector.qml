import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    property bool appearanceOnly: false
    readonly property var placementNames: ["Top left", "Top center", "Top right", "Middle left", "Center", "Middle right", "Bottom left", "Bottom center", "Bottom right"]
    readonly property int placementIndex: {
        var t = controller.currentText
        for (var i = 0; i < 9; ++i)
            if (Math.abs(Number(t.x) - [.2,.5,.8][i % 3]) < .001 && Math.abs(Number(t.y) - [.2,.5,.8][Math.floor(i / 3)]) < .001) return i
        return -1
    }
    readonly property string placementSummary: placementIndex >= 0 ? placementNames[placementIndex] : "X " + (Number(controller.currentText.x || 0) * 100).toFixed(1) + "% · Y " + (Number(controller.currentText.y || 0) * 100).toFixed(1) + "%"
    spacing: 10
    function focusFirst() {
        if (controller.selectedText >= 0) {
            if (appearanceOnly) { fontSize.forceActiveFocus(); fontSize.selectAll() }
            else { content.forceActiveFocus(); content.selectAll() }
        } else if (!appearanceOnly) emptyAdd.forceActiveFocus()
    }
    RowLayout {
        visible: !panel.appearanceOnly && controller.textOverlays.length > 0
        Layout.fillWidth: true; Layout.margins: 12
        UI.ComboBox {
            Layout.fillWidth: true
            model: controller.textOverlays; textRole: "text"
            currentIndex: controller.selectedText
            displayText: controller.selectedText < 0 ? "Choose text" : (controller.currentText.text || "Empty text").split("\n")[0]
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
    ColumnLayout {
        visible: !panel.appearanceOnly && controller.selectedText < 0
        Layout.fillWidth: true; Layout.margins: 12
        Label { text: "Add a title or a note to your video."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted }
        UI.Button {
            id: emptyAdd
            objectName: "textEmptyAddButton"
            text: "Add text"; iconName: "plus"
            enabled: !controller.exporting && controller.duration > 0
            helpText: controller.duration > 0 ? "Add text at the playhead" : "Add a video clip before adding text"
            onClicked: { controller.addText(); panel.focusFirst() }
        }
    }
    ColumnLayout {
        visible: controller.selectedText >= 0
        Layout.fillWidth: true; Layout.leftMargin: panel.appearanceOnly ? 0 : 12; Layout.rightMargin: panel.appearanceOnly ? 0 : 12
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
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Style" }
            UI.ComboBox {
                id: styleChooser
                Layout.fillWidth: true; Layout.minimumWidth: 0
                model: ["Clean", "Playful", "Background box"]
                currentIndex: Math.max(0,["clean","playful","box"].indexOf(controller.currentText.style))
                Accessible.name: "Text style"
                delegate: ItemDelegate {
                    id: styleOption
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    text: modelData
                    highlighted: styleChooser.highlightedIndex === index
                    hoverEnabled: styleChooser.hoverEnabled
                    palette.text: UI.Theme.text
                    palette.highlightedText: UI.Theme.text
                    Accessible.name: modelData + " text style"
                    contentItem: RowLayout {
                        spacing: 8
                        Rectangle {
                            implicitWidth: sample.implicitWidth + 16
                            implicitHeight: sample.implicitHeight + 8
                            color: "#55555c"; radius: 3
                            Accessible.ignored: true
                            Rectangle { visible: styleOption.index === 2; anchors.fill: parent; anchors.margins: 3; color: "#202126"; radius: 2 }
                            Text {
                                id: sample
                                anchors.centerIn: parent
                                text: "Aa"
                                font.family: styleOption.index === 1 && controller.fontFamilies.indexOf("Arial Rounded MT Bold") >= 0 ? "Arial Rounded MT Bold" : "Arial"
                                font.pixelSize: styleChooser.font.pixelSize
                                font.bold: styleOption.index === 1
                                color: styleOption.index === 1 ? "#ffe395" : "#ffffff"
                                style: styleOption.index === 2 ? Text.Normal : Text.Raised
                                styleColor: "#000000"
                                Accessible.ignored: true
                            }
                        }
                        Label {
                            text: styleOption.modelData
                            color: UI.Theme.text
                            font.weight: styleChooser.currentIndex === styleOption.index ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                    background: Rectangle {
                        radius: UI.Theme.radius - 3
                        color: styleOption.down || styleOption.highlighted ? UI.Theme.selected : styleOption.hovered ? UI.Theme.hover : UI.Theme.panel
                        border.width: styleOption.highlighted || styleOption.visualFocus ? 1 : 0
                        border.color: UI.Theme.accent
                    }
                }
                onActivated: index => controller.updateText(controller.selectedText,{style: ["clean","playful","box"][index]})
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Font" }
            UI.ComboBox {
                Layout.fillWidth: true; Layout.minimumWidth: 0
                model: controller.fontFamilies
                currentIndex: Math.max(0,controller.fontFamilies.indexOf(controller.currentText.fontFamily || "Arial"))
                Accessible.name: "Text font"
                onActivated: controller.updateText(controller.selectedText,{fontFamily: currentText})
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Size" }
            UI.TextField {
                id: fontSize
                objectName: panel.appearanceOnly ? "captionSizeField" : "textSizeField"
                property int editingIndex: -1
                property string initialText: ""
                Layout.fillWidth: true; Layout.minimumWidth: 80
                text: String(controller.currentText.fontSize || 84)
                validator: IntValidator { bottom: 12; top: 200 }
                stepper: true
                Accessible.name: "Text size"
                onActiveFocusChanged: if (activeFocus) { editingIndex = controller.selectedText; initialText = text }
                onEditingFinished: if (acceptableInput && text !== initialText && editingIndex === controller.selectedText) controller.updateText(editingIndex,{fontSize: Number(text)})
                onStepRequested: direction => {
                    if (acceptableInput && editingIndex === controller.selectedText) {
                        controller.updateText(editingIndex,{fontSize: Math.max(12,Math.min(200,Number(text) + direction))})
                        text = Qt.binding(function() { return String(controller.currentText.fontSize || 84) })
                    }
                }
                Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return String(controller.currentText.fontSize || 84) }); focus = false; event.accepted = true }
            }
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
        }
        Label {
            visible: !panel.appearanceOnly && !!controller.currentText.clamped
            text: "Video ends before this text finishes. Its requested duration is retained."
            color: UI.Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
        }
        UI.InspectorDetails {
            objectName: panel.appearanceOnly ? "captionPlacementDetails" : "textPlacementDetails"
            title: "Placement"
            summary: panel.placementSummary + (panel.appearanceOnly ? "" : " · " + Number(controller.currentText.durationSeconds || 0).toFixed(2) + " s")
            Layout.fillWidth: true
            UI.ComboBox {
                Layout.fillWidth: true
                model: panel.placementNames
                currentIndex: panel.placementIndex
                displayText: panel.placementIndex < 0 ? "Custom position" : panel.placementNames[panel.placementIndex]
                Accessible.name: "Text placement"
                onActivated: index => controller.updateText(controller.selectedText,{x: [.2,.5,.8][index % 3],y: [.2,.5,.8][Math.floor(index / 3)]})
            }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "Horizontal"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    UI.TextField {
                        objectName: panel.appearanceOnly ? "captionPositionXField" : "textPositionXField"
                        property int editingIndex: -1
                        property string initialText: ""
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        text: (Number(controller.currentText.x || 0) * 100).toFixed(1)
                        suffix: "%"; stepper: true
                        validator: DoubleValidator { bottom: 0; top: 100; decimals: 4; locale: "C" }
                        Accessible.name: "Text horizontal position in percent"
                        onActiveFocusChanged: if (activeFocus) { editingIndex = controller.selectedText; initialText = text }
                        onEditingFinished: if (acceptableInput && text !== initialText && editingIndex === controller.selectedText) controller.updateText(editingIndex,{x: Number(text)/100})
                        onStepRequested: direction => {
                            if (acceptableInput && editingIndex === controller.selectedText) {
                                var value = text === initialText ? Number(controller.currentText.x || 0) * 100 : Number(text)
                                controller.updateText(editingIndex,{x: Math.max(0,Math.min(100,value + direction))/100})
                                text = Qt.binding(function() { return (Number(controller.currentText.x || 0) * 100).toFixed(1) })
                                initialText = text
                            }
                        }
                        Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return (Number(controller.currentText.x || 0) * 100).toFixed(1) }); focus = false; event.accepted = true }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: "Vertical"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    UI.TextField {
                        objectName: panel.appearanceOnly ? "captionPositionYField" : "textPositionYField"
                        property int editingIndex: -1
                        property string initialText: ""
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        text: (Number(controller.currentText.y || 0) * 100).toFixed(1)
                        suffix: "%"; stepper: true
                        validator: DoubleValidator { bottom: 0; top: 100; decimals: 4; locale: "C" }
                        Accessible.name: "Text vertical position in percent"
                        onActiveFocusChanged: if (activeFocus) { editingIndex = controller.selectedText; initialText = text }
                        onEditingFinished: if (acceptableInput && text !== initialText && editingIndex === controller.selectedText) controller.updateText(editingIndex,{y: Number(text)/100})
                        onStepRequested: direction => {
                            if (acceptableInput && editingIndex === controller.selectedText) {
                                var value = text === initialText ? Number(controller.currentText.y || 0) * 100 : Number(text)
                                controller.updateText(editingIndex,{y: Math.max(0,Math.min(100,value + direction))/100})
                                text = Qt.binding(function() { return (Number(controller.currentText.y || 0) * 100).toFixed(1) })
                                initialText = text
                            }
                        }
                        Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return (Number(controller.currentText.y || 0) * 100).toFixed(1) }); focus = false; event.accepted = true }
                    }
                }
            }
            Label { text: "Length"; visible: !panel.appearanceOnly }
            UI.TextField {
                objectName: "textDurationField"
                visible: !panel.appearanceOnly
                property int editingIndex: -1
                Layout.fillWidth: true
                text: Number(controller.currentText.durationSeconds || 0).toFixed(3)
                suffix: "s"; stepper: true
                validator: DoubleValidator { bottom: 1/60; top: 86400; decimals: 3; locale: "C" }
                Accessible.name: "Text duration in seconds. Arrow keys adjust one frame."
                onActiveFocusChanged: if (activeFocus) editingIndex = controller.selectedText
                onEditingFinished: if (acceptableInput && editingIndex === controller.selectedText) controller.updateText(editingIndex,{durationFrames: Math.round(Number(text)*60)})
                onStepRequested: direction => {
                    if (acceptableInput && editingIndex === controller.selectedText) {
                        controller.updateText(editingIndex,{durationFrames: Math.max(1,Math.min(5184000,Math.round(Number(text)*60) + direction))})
                        text = Qt.binding(function() { return Number(controller.currentText.durationSeconds || 0).toFixed(3) })
                    }
                }
                Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return Number(controller.currentText.durationSeconds || 0).toFixed(3) }); focus = false; event.accepted = true }
            }
        }
        UI.InspectorDetails {
            objectName: panel.appearanceOnly ? "captionEffectsDetails" : "textEffectsDetails"
            title: "Effects"
            summary: [controller.currentText.shadow ? "Shadow" : "", controller.currentText.fade ? "Fade in and out" : ""].filter(function(value) { return value.length > 0 }).join(" · ") || "No effects"
            Layout.fillWidth: true
            CheckBox { text: "Shadow"; checked: controller.currentText.shadow || false; onToggled: controller.updateText(controller.selectedText,{shadow: checked}) }
            CheckBox { text: "Fade in and out"; checked: controller.currentText.fade || false; onToggled: controller.updateText(controller.selectedText,{fade: checked}) }
        }
        Loader {
            Layout.fillWidth: true
            active: !panel.appearanceOnly
            sourceComponent: ColumnLayout {
                spacing: 10
                UI.InspectorDetails {
                    objectName: "textAttachmentDetails"
                    title: "Attachment"
                    summary: panel.controller.currentText.attached ? "Follows clip" : "Project time"
                    Layout.fillWidth: true
                    UI.TextAttachment { controller: panel.controller; Layout.fillWidth: true }
                }
                UI.InspectorDetails {
                    objectName: "textGuidesDetails"
                    title: "Guides"
                    summary: panel.controller.textGuides ? "Safe margins on" : "Off"
                    Layout.fillWidth: true
                    CheckBox { text: "Show safe-margin guides"; checked: panel.controller.textGuides; onToggled: panel.controller.textGuides = checked }
                }
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
