import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    property bool editing: false
    property int editingIndex: -1
    readonly property bool hasCaptions: controller.captions.length > 0
    readonly property bool hasSelection: controller.selectedCaption >= 0
    spacing: 10
    readonly property bool timingReviewNeeded: {
        var cues = controller.captions
        for (var i = 0; i < cues.length; ++i) if (cues[i].timingNeedsReview) return true
        return false
    }
    function seconds(frames) { return (Number(frames || 0) / 60).toFixed(3) }
    function focusFirst() { if (hasCaptions) captionList.forceActiveFocus(); else importCaptions.forceActiveFocus() }
    function beginEditing(index) {
        if (index < 0) return
        if (editing) finishEditing(false,false)
        controller.beginCaptionEdit(index)
        content.text = controller.currentCaption.text || ""
        editingIndex = index; editing = true
        Qt.callLater(function() { content.forceActiveFocus(); content.selectAll() })
    }
    function finishEditing(cancel, returnFocus = true) {
        if (!editing) return
        editing = false; controller.endTextDrag(cancel); editingIndex = -1
        if (returnFocus) captionList.forceActiveFocus()
    }
    Connections {
        target: panel.controller
        function onSelectionChanged() {
            if (panel.editing && panel.editingIndex !== panel.controller.selectedCaption) { panel.editing = false; panel.editingIndex = -1; content.focus = false }
            if (panel.controller.selectedCaption >= 0) captionList.positionViewAtIndex(panel.controller.selectedCaption,ListView.Contain)
        }
    }
    Flow {
        Layout.fillWidth: true; Layout.margins: 12; spacing: 6
        UI.Button {
            id: importCaptions
            objectName: "importSrtButton"
            visible: !panel.hasCaptions
            text: "Import captions"; iconName: "import"
            helpText: "Import captions from an SRT file"
            onClicked: panel.controller.importSrtDialog()
        }
        UI.Button { objectName: "addCaptionButton"; text: "Add caption"; iconName: "plus"; iconOnly: panel.hasCaptions; onClicked: { panel.controller.addCaption(); panel.beginEditing(panel.controller.selectedCaption) } }
        UI.Button { text: "Delete selected captions"; iconName: "trash"; iconOnly: true; visible: panel.controller.selectedCaptions.length > 0; onClicked: panel.controller.deleteCaptions() }
    }
    Label {
        visible: !panel.hasCaptions
        text: "Import an SRT file or add a caption."
        wrapMode: Text.Wrap; color: UI.Theme.muted
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
    }
    Repeater {
        model: panel.controller.captionWarnings
        Label { required property string modelData; text: modelData; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; Accessible.role: Accessible.AlertMessage }
    }
    UI.Button { text: "Mark timing reviewed"; iconName: "check"; visible: panel.timingReviewNeeded; Layout.leftMargin: 12; onClicked: panel.controller.reviewCaptions() }
    ListView {
        id: captionList
        objectName: "captionList"
        visible: panel.hasCaptions
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
        Layout.preferredHeight: Math.min(160,Math.max(100,contentHeight))
        model: panel.controller.captions
        clip: true; spacing: 4; activeFocusOnTab: true
        currentIndex: panel.controller.selectedCaption
        Accessible.name: "Captions and project intervals in seconds"
        ScrollBar.vertical: ScrollBar {}
        Keys.onReturnPressed: panel.beginEditing(panel.controller.selectedCaption)
        Keys.onEnterPressed: panel.beginEditing(panel.controller.selectedCaption)
        Keys.onUpPressed: panel.controller.navigateCaption(-1)
        Keys.onDownPressed: panel.controller.navigateCaption(1)
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            objectName: "captionRow" + index
            width: captionList.width
            height: Math.max(72,contentItem.implicitHeight + topPadding + bottomPadding)
            highlighted: panel.controller.selectedCaptions.indexOf(index) >= 0
            Accessible.name: "Caption " + (index + 1) + ", " + panel.seconds(modelData.startFrame) + " to " + panel.seconds(modelData.startFrame + modelData.durationFrames) + " seconds, " + modelData.text
            contentItem: ColumnLayout {
                spacing: 3
                Label { text: panel.seconds(row.modelData.startFrame) + " – " + panel.seconds(row.modelData.startFrame + row.modelData.durationFrames) + " s"; color: UI.Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: (row.modelData.attached ? "Follows clip" : "Project time") + (row.modelData.overlap ? " · overlap" : ""); color: UI.Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: row.modelData.text || "Empty caption"; maximumLineCount: 2; wrapMode: Text.Wrap; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            background: Rectangle { radius: UI.Theme.radius; color: row.highlighted ? UI.Theme.selected : row.hovered ? UI.Theme.hover : UI.Theme.panel; border.width: row.activeFocus || row.highlighted ? 2 : 0; border.color: UI.Theme.accent }
            MouseArea {
                anchors.fill: parent
                onClicked: mouse => { row.forceActiveFocus(); panel.controller.selectCaption(row.index,mouse.modifiers) }
                onDoubleClicked: panel.beginEditing(row.index)
            }
            Keys.onReturnPressed: panel.beginEditing(index)
            Keys.onEnterPressed: panel.beginEditing(index)
            Keys.onUpPressed: panel.controller.navigateCaption(-1)
            Keys.onDownPressed: panel.controller.navigateCaption(1)
        }
    }
    Flow {
        visible: panel.hasCaptions
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 4
        UI.Button { text: "Previous caption"; iconName: "step-back"; iconOnly: true; onClicked: panel.controller.navigateCaption(-1) }
        UI.Button { text: "Next caption"; iconName: "step-forward"; iconOnly: true; onClicked: panel.controller.navigateCaption(1) }
        UI.Button { objectName: "previewCaptionButton"; text: "Preview caption start"; iconName: "play"; iconOnly: true; visible: panel.hasSelection; onClicked: panel.controller.previewCaption() }
        UI.Button { text: "Edit caption"; iconName: "edit"; iconOnly: true; visible: panel.hasSelection; onClicked: panel.beginEditing(panel.controller.selectedCaption) }
    }
    TextArea {
        id: content
        objectName: "captionContentField"
        visible: panel.editing
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.minimumHeight: 100
        textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; selectByMouse: true; padding: 10
        color: UI.Theme.text; selectionColor: UI.Theme.accent; selectedTextColor: UI.Theme.accentText
        Accessible.name: "Edit caption text. Enter commits; Shift Enter adds a line; Escape cancels."
        background: Rectangle { color: UI.Theme.field; radius: UI.Theme.radius; border.color: content.activeFocus ? UI.Theme.accent : UI.Theme.controlBorder; border.width: content.activeFocus ? 2 : 1 }
        onTextChanged: if (panel.editing && panel.editingIndex === panel.controller.selectedCaption) {
            if (text.length > 2000) remove(2000,text.length)
            panel.controller.previewCaptionContent(text)
        }
        onActiveFocusChanged: if (!activeFocus && panel.editing) panel.finishEditing(false,false)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) { panel.finishEditing(true); event.accepted = true }
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) { panel.finishEditing(false); event.accepted = true }
        }
    }
    ColumnLayout {
        visible: panel.hasSelection
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 8
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Start" }
                UI.TextField {
                    objectName: "captionStartField"
                    property int editingIndex: -1
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    text: panel.seconds(panel.controller.currentCaption.startFrame)
                    suffix: "s"; stepper: true
                    validator: DoubleValidator { bottom: 0; top: 86400; decimals: 3; locale: "C" }
                    Accessible.name: "Caption start in seconds. Arrow keys adjust one frame."
                    onActiveFocusChanged: if (activeFocus) editingIndex = panel.controller.selectedCaption
                    onEditingFinished: if (acceptableInput && editingIndex === panel.controller.selectedCaption) panel.controller.setCaptionTiming(editingIndex,Math.round(Number(text)*60),panel.controller.currentCaption.durationFrames)
                    onStepRequested: direction => {
                        if (acceptableInput && editingIndex === panel.controller.selectedCaption) {
                            panel.controller.setCaptionTiming(editingIndex,Math.max(0,Math.min(5183999,Math.round(Number(text)*60) + direction)),panel.controller.currentCaption.durationFrames)
                            text = Qt.binding(function() { return panel.seconds(panel.controller.currentCaption.startFrame) })
                        }
                    }
                    Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return panel.seconds(panel.controller.currentCaption.startFrame) }); focus = false; event.accepted = true }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Length" }
                UI.TextField {
                    objectName: "captionDurationField"
                    property int editingIndex: -1
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    text: panel.seconds(panel.controller.currentCaption.durationFrames || 1)
                    suffix: "s"; stepper: true
                    validator: DoubleValidator { bottom: 1/60; top: 86400; decimals: 3; locale: "C" }
                    Accessible.name: "Caption duration in seconds. Arrow keys adjust one frame."
                    onActiveFocusChanged: if (activeFocus) editingIndex = panel.controller.selectedCaption
                    onEditingFinished: if (acceptableInput && editingIndex === panel.controller.selectedCaption) panel.controller.setCaptionTiming(editingIndex,panel.controller.currentCaption.startFrame,Math.round(Number(text)*60))
                    onStepRequested: direction => {
                        if (acceptableInput && editingIndex === panel.controller.selectedCaption) {
                            panel.controller.setCaptionTiming(editingIndex,panel.controller.currentCaption.startFrame,Math.max(1,Math.min(5184000,Math.round(Number(text)*60) + direction)))
                            text = Qt.binding(function() { return panel.seconds(panel.controller.currentCaption.durationFrames || 1) })
                        }
                    }
                    Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return panel.seconds(panel.controller.currentCaption.durationFrames || 1) }); focus = false; event.accepted = true }
                }
            }
        }
        UI.InspectorDetails {
            objectName: "captionAppearanceDetails"
            title: "Appearance"
            summary: panel.controller.selectedCaptions.length + " selected · " + (panel.controller.currentCaption.fontFamily || "Arial") + " " + (panel.controller.currentCaption.fontSize || 57)
            Layout.fillWidth: true
            Label { text: "Style changes affect the selected captions."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted }
            UI.TextInspector { controller: appearance; appearanceOnly: true; Layout.fillWidth: true }
        }
        UI.InspectorDetails {
            objectName: "captionAttachmentDetails"
            title: "Attachment"
            summary: panel.controller.currentCaption.attached ? "Follows clip" : "Project time"
            Layout.fillWidth: true
            UI.TextAttachment { controller: panel.controller; caption: true; Layout.fillWidth: true }
        }
        UI.InspectorDetails {
            objectName: "captionGuidesDetails"
            title: "Guides"
            summary: panel.controller.textGuides ? "Safe margins on" : "Off"
            Layout.fillWidth: true
            CheckBox { text: "Show safe-margin guides"; checked: panel.controller.textGuides; onToggled: panel.controller.textGuides = checked }
        }
    }
    QtObject {
        id: appearance
        property int selectedText: panel.controller.selectedCaption
        property var currentText: panel.controller.currentCaption
        property var textOverlays: panel.controller.captions
        property var fontFamilies: panel.controller.fontFamilies
        property bool exporting: panel.controller.exporting
        function updateText(index, fields) { if (index === panel.controller.selectedCaption) panel.controller.styleCaptions(fields) }
        function chooseTextColor() { panel.controller.chooseCaptionColor() }
        function beginTextDrag() {}
        function endTextDrag(cancel) {}
        function previewTextContent(text) {}
        function selectText(index) { panel.controller.selectCaption(index) }
        function deleteText() { panel.controller.deleteCaptions() }
    }
    UI.InspectorDetails {
        objectName: "captionFileDetails"
        visible: panel.hasCaptions
        title: "Files and all captions"
        summary: panel.controller.burnCaptions ? "Captions in video · SRT" : "SRT only"
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
        Flow {
            Layout.fillWidth: true; spacing: 6
            UI.Button { text: "Import captions (append)"; iconName: "import"; onClicked: panel.controller.importSrtDialog() }
            UI.Button { objectName: "exportSrtButton"; text: "Export SRT"; iconName: "export"; onClicked: panel.controller.exportSrtDialog() }
        }
        CheckBox {
            objectName: "burnCaptionsCheck"
            text: "Burn captions into video"
            hoverEnabled: true
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: "Captions appear in preview and MP4. Titles are unchanged; SRT output stays separate."
            checked: panel.controller.burnCaptions
            onToggled: panel.controller.setBurnCaptions(checked)
        }
        UI.Button {
            objectName: "applyCaptionStyleToAll"
            text: "Apply style to all captions"
            visible: panel.hasSelection
            Layout.fillWidth: true
            onClicked: {
                var c = panel.controller.currentCaption
                panel.controller.styleCaptions({style:c.style,fontFamily:c.fontFamily,fontSize:c.fontSize,color:c.color,bold:c.bold,shadow:c.shadow,fade:c.fade,x:c.x,y:c.y},true)
            }
        }
    }
    Item { Layout.preferredHeight: 12 }
}
