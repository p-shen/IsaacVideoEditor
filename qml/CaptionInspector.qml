import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: panel
    required property var controller
    property bool editing: false
    property int editingIndex: -1
    spacing: 10
    readonly property bool timingReviewNeeded: {
        var cues = controller.captions
        for (var i = 0; i < cues.length; ++i) if (cues[i].timingNeedsReview) return true
        return false
    }
    function focusFirst() { captionList.forceActiveFocus() }
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
        Layout.fillWidth: true; Layout.margins: 12; spacing: 4
        UI.Button { objectName: "importSrtButton"; text: "Import SRT captions (append)"; iconName: "import"; iconOnly: true; onClicked: panel.controller.importSrtDialog() }
        UI.Button { objectName: "exportSrtButton"; text: "Export SRT captions"; iconName: "export"; iconOnly: true; enabled: panel.controller.captions.length > 0; onClicked: panel.controller.exportSrtDialog() }
        UI.Button { objectName: "addCaptionButton"; text: "Add caption"; iconName: "plus"; onClicked: { panel.controller.addCaption(); panel.beginEditing(panel.controller.selectedCaption) } }
        UI.Button { text: "Delete selected captions"; iconName: "trash"; iconOnly: true; enabled: panel.controller.selectedCaptions.length > 0; onClicked: panel.controller.deleteCaptions() }
    }
    ListView {
        id: captionList
        objectName: "captionList"
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
        Layout.preferredHeight: Math.min(260,Math.max(100,contentHeight))
        model: panel.controller.captions
        clip: true; spacing: 4; activeFocusOnTab: true
        currentIndex: panel.controller.selectedCaption
        Accessible.name: "Captions and project intervals"
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
            Accessible.name: "Caption " + (index + 1) + ", frames " + modelData.startFrame + " to " + (modelData.startFrame + modelData.durationFrames) + ", " + modelData.text
            contentItem: ColumnLayout {
                spacing: 3
                Label { text: row.modelData.startFrame + " – " + (row.modelData.startFrame + row.modelData.durationFrames) + " frames · " + (row.modelData.attached ? "Attached" : "Project time") + (row.modelData.overlap ? " · overlap" : ""); color: UI.Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
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
        Label { visible: panel.controller.captions.length === 0; anchors.fill: parent; text: "Import an SRT file or add a caption."; wrapMode: Text.Wrap; color: UI.Theme.muted }
    }
    Flow {
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 4
        UI.Button { text: "Previous caption"; iconName: "step-back"; iconOnly: true; enabled: panel.controller.captions.length > 0; onClicked: panel.controller.navigateCaption(-1) }
        UI.Button { text: "Next caption"; iconName: "step-forward"; iconOnly: true; enabled: panel.controller.captions.length > 0; onClicked: panel.controller.navigateCaption(1) }
        UI.Button { objectName: "previewCaptionButton"; text: "Preview caption start"; iconName: "play"; enabled: panel.controller.selectedCaption >= 0; onClicked: panel.controller.previewCaption() }
        UI.Button { text: "Edit caption"; enabled: panel.controller.selectedCaption >= 0; onClicked: panel.beginEditing(panel.controller.selectedCaption) }
    }
    CheckBox {
        objectName: "burnCaptionsCheck"
        Layout.leftMargin: 12; Layout.rightMargin: 12
        text: "Burn captions into video"
        hoverEnabled: true
        ToolTip.visible: hovered || activeFocus
        ToolTip.text: "Captions appear in preview and MP4. Titles are unchanged; SRT output stays separate."
        checked: panel.controller.burnCaptions
        onToggled: panel.controller.setBurnCaptions(checked)
    }
    Repeater {
        model: panel.controller.captionWarnings
        Label { required property string modelData; text: modelData; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; Accessible.role: Accessible.AlertMessage }
    }
    UI.Button { text: "Mark timing reviewed"; visible: panel.timingReviewNeeded; Layout.leftMargin: 12; onClicked: panel.controller.reviewCaptions() }
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
        visible: panel.controller.selectedCaption >= 0
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 8
        Label { text: panel.controller.selectedCaptions.length + " selected · style changes affect this selection"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: "Duration (frames)" }
        UI.TextField {
            objectName: "captionDurationField"
            property int editingIndex: -1
            Layout.fillWidth: true
            text: String(panel.controller.currentCaption.durationFrames || 1)
            validator: IntValidator { bottom: 1; top: 5184000 }
            Accessible.name: "Caption duration in frames"
            onActiveFocusChanged: if (activeFocus) editingIndex = panel.controller.selectedCaption
            onEditingFinished: if (acceptableInput && editingIndex === panel.controller.selectedCaption) panel.controller.setCaptionTiming(editingIndex,panel.controller.currentCaption.startFrame,Number(text))
            Keys.onEscapePressed: event => { editingIndex = -1; text = Qt.binding(function() { return String(panel.controller.currentCaption.durationFrames || 1) }); focus = false; event.accepted = true }
        }
        UI.TextAttachment { controller: panel.controller; caption: true; Layout.fillWidth: true }
        CheckBox { text: "Show safe-margin guides"; checked: panel.controller.textGuides; onToggled: panel.controller.textGuides = checked }
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
    UI.TextInspector { controller: appearance; appearanceOnly: true; Layout.fillWidth: true }
    UI.Button {
        objectName: "applyCaptionStyleToAll"
        text: "Apply style to all captions"
        visible: panel.controller.selectedCaption >= 0
        Layout.leftMargin: 12; Layout.rightMargin: 12
        onClicked: {
            var c = panel.controller.currentCaption
            panel.controller.styleCaptions({style:c.style,fontFamily:c.fontFamily,fontSize:c.fontSize,color:c.color,bold:c.bold,shadow:c.shadow,fade:c.fade,x:c.x,y:c.y},true)
        }
    }
    Item { Layout.preferredHeight: 12 }
}
