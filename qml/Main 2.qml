import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Isaac 1.0

ApplicationWindow {
    id: window
    objectName: "editorWindow"
    width: 1320; height: 860
    minimumWidth: 760; minimumHeight: 560
    visible: true
    title: editor.title + " — IsaacVideoEditor"
    property bool compact: width < Math.max(1040, font.pixelSize * 75)
    property bool textEditing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    onActiveChanged: if (!active && positionSlider) positionSlider.cancelScrub()
    property int browserSection: 0
    property int inspectorSection: 0
    font: applicationFont
    SystemPalette { id: appColors; colorGroup: SystemPalette.Active }
    SystemPalette { id: disabledColors; colorGroup: SystemPalette.Disabled }
    palette.window: appColors.window
    palette.windowText: appColors.windowText
    palette.base: appColors.base
    palette.alternateBase: appColors.alternateBase
    palette.text: appColors.text
    palette.button: appColors.button
    palette.buttonText: appColors.buttonText
    palette.highlight: appColors.highlight
    palette.highlightedText: appColors.highlightedText
    palette.placeholderText: appColors.mid
    palette.disabled.text: disabledColors.text
    palette.disabled.buttonText: disabledColors.buttonText
    property color accent: palette.highlight
    color: palette.window
    onClosing: close => { close.accepted = editor.requestClose() }
    Component.onCompleted: editor.attachTimeline(timeline)
    function focusInspector(section) {
        inspectorSection = section
        if (compact) browserSection = 1
        if (wideInspector.item) wideInspector.item.section = section
        if (compactInspector.item) compactInspector.item.section = section
        Qt.callLater(function() {
            var panel = window.compact ? compactInspector.item : wideInspector.item
            if (panel) panel.focusFirst()
        })
    }
    Action { id: newAction; text: "New project"; shortcut: StandardKey.New; enabled: !editor.exporting; onTriggered: editor.newProject() }
    Action { id: openAction; text: "Open…"; shortcut: StandardKey.Open; enabled: !editor.exporting; onTriggered: editor.openProject() }
    Action { id: saveAction; text: "Save"; shortcut: StandardKey.Save; enabled: !editor.exporting; onTriggered: editor.save() }
    Action { id: saveAsAction; text: "Save As…"; shortcut: StandardKey.SaveAs; enabled: !editor.exporting; onTriggered: editor.saveAs() }
    Action { id: importAction; text: "Import"; shortcut: "Ctrl+I"; enabled: !editor.exporting; onTriggered: editor.importDialog() }
    Action { id: exportAction; text: "Export…"; shortcut: "Ctrl+E"; enabled: !editor.exporting && editor.duration > 0; onTriggered: editor.exportDialog() }
    Action { id: undoAction; text: editor.undoText ? "Undo " + editor.undoText : "Undo"; shortcut: StandardKey.Undo; enabled: editor.canUndo && !editor.exporting && !window.textEditing; onTriggered: editor.undo() }
    Action { id: redoAction; text: editor.redoText ? "Redo " + editor.redoText : "Redo"; shortcut: StandardKey.Redo; enabled: editor.canRedo && !editor.exporting && !window.textEditing; onTriggered: editor.redo() }
    Action { id: splitAction; text: "Split"; shortcut: "Ctrl+B"; enabled: editor.canSplit && !window.textEditing; onTriggered: editor.split() }
    Shortcut { sequence: "Space"; enabled: !window.textEditing && !editor.exporting; onActivated: editor.togglePlayback() }
    Shortcut { sequence: "Delete"; enabled: !window.textEditing && !editor.exporting; onActivated: editor.removeSelection() }
    Shortcut { sequence: "Backspace"; enabled: !window.textEditing && !editor.exporting; onActivated: editor.removeSelection() }
    Shortcut { sequence: "Ctrl+1"; onActivated: { browserSection = 0; clipList.forceActiveFocus() } }
    Shortcut { sequence: "Ctrl+2"; onActivated: timeline.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+4"; onActivated: focusInspector(0) }
    Shortcut { sequence: "="; enabled: !window.textEditing; onActivated: timeline.zoomIn() }
    Shortcut { sequence: "-"; enabled: !window.textEditing; onActivated: timeline.zoomOut() }
    Shortcut { sequence: "Shift+Z"; enabled: !window.textEditing; onActivated: timeline.fitToWidth() }
    menuBar: MenuBar {
        Menu { title: "File"; MenuItem { action: newAction }
            MenuItem { action: openAction }
            MenuItem { action: saveAction }
            MenuItem { action: saveAsAction }
            MenuSeparator {}
            MenuItem { action: importAction }
            MenuItem { action: exportAction }
            MenuSeparator {}
            MenuItem { text: "Quit"; onTriggered: window.close() } }
        Menu {
            title: "Edit"
            MenuItem { action: undoAction }
            MenuItem { action: redoAction }
            MenuItem { action: splitAction }
            MenuItem { text: "Select all clips"; enabled: !window.textEditing; onTriggered: editor.selectAll() }
            MenuItem { text: "Clear selection"; onTriggered: editor.clearSelection() }
            MenuItem { text: "Move earlier"; onTriggered: editor.moveSelection(-1) }
            MenuItem { text: "Move later"; onTriggered: editor.moveSelection(1) }
            MenuItem { text: "Remove selected clips"; onTriggered: editor.removeSelection() }
        }
        Menu {
            title: "View"
            MenuItem { text: "Clips"; onTriggered: { browserSection = 0; clipList.forceActiveFocus() } }
            MenuItem { text: "Timeline"; onTriggered: timeline.forceActiveFocus() }
            MenuItem { text: "Inspector"; onTriggered: focusInspector(0) }
            MenuItem { text: "Fit timeline"; onTriggered: timeline.fitToWidth() }
            MenuItem { text: "Retry previews"; onTriggered: editor.retryPreviews() }
        }
        Menu {
            title: "Clip"
            MenuItem { text: "Rotate left 90°"; enabled: editor.selectedClip >= 0; onTriggered: editor.rotate(-90) }
            MenuItem { text: "Rotate right 90°"; enabled: editor.selectedClip >= 0; onTriggered: editor.rotate(90) }
            MenuItem { text: "Turn 180°"; enabled: editor.selectedClip >= 0; onTriggered: editor.rotate(180) }
        }
    }
    header: ToolBar {
        padding: 12
        RowLayout {
            anchors.fill: parent
            spacing: 12
            Image { source: "qrc:/isaac-logo.png"; sourceSize.width: 28; sourceSize.height: 28; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            ColumnLayout {
                spacing: 2
                Label { text: editor.title; font.bold: true; elide: Text.ElideMiddle; Layout.maximumWidth: window.compact ? 160 : 320 }
                Label { text: editor.dirty ? "Unsaved changes" : "Saved locally"; font.pixelSize: Math.max(11,window.font.pixelSize - 2); opacity: 0.7 }
            }
            Item { Layout.fillWidth: true }
            Label { text: editor.format; visible: !window.compact; opacity: 0.75 }
            Button { action: importAction }
            Button { action: exportAction; highlighted: true }
        }
    }
    ScrollView {
        id: workspaceScroll
        objectName: "workspaceScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: workspace.height
        clip: true
    ColumnLayout {
        id: workspace
        width: workspaceScroll.availableWidth
        height: Math.max(window.contentItem.height, Math.max(210,window.font.pixelSize * 10) + timeline.implicitHeight + timelineTools.implicitHeight + 24 + (noticeFrame.visible ? noticeFrame.implicitHeight : 0))
        spacing: 0
        enabled: !editor.exporting
        Frame {
            id: noticeFrame
            visible: !!editor.notice
            Layout.fillWidth: true
            padding: 6
            RowLayout {
                anchors.fill: parent
                Label { text: editor.notice; wrapMode: Text.Wrap; Layout.fillWidth: true; Accessible.role: Accessible.AlertMessage }
                ToolButton { text: "Dismiss"; onClicked: editor.dismissNotice() }
            }
        }
        SplitView {
            id: verticalSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            SplitView {
                orientation: Qt.Horizontal
                SplitView.minimumHeight: Math.max(210,window.font.pixelSize * 10)
                SplitView.fillHeight: true
                Pane {
                    padding: 0
                    SplitView.preferredWidth: window.compact ? 260 : 250
                    SplitView.minimumWidth: 210
                    SplitView.maximumWidth: 440
                    ColumnLayout {
                        anchors.fill: parent; spacing: 0
                        TabBar {
                            visible: window.compact
                            Layout.fillWidth: true
                            currentIndex: window.browserSection
                            onCurrentIndexChanged: window.browserSection = currentIndex
                            TabButton { text: "Clips" }
                            TabButton { text: "Inspector" }
                        }
                        StackLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            currentIndex: window.compact ? window.browserSection : 0
                            ColumnLayout {
                                spacing: 0
                                Label { text: "CLIPS  ·  " + editor.clips.length; font.bold: true; Layout.margins: 14 }
                                ListView {
                                    id: clipList
                                    objectName: "clipList"
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    clip: true; spacing: 2
                                    model: editor.clips
                                    currentIndex: editor.selectedClip
                                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex,ListView.Contain)
                                    activeFocusOnTab: true
                                    Accessible.name: "Clips in edit order"
                                    ScrollBar.vertical: ScrollBar {}
                                    delegate: ItemDelegate {
                                        id: clipDelegate
                                        required property var modelData
                                        required property int index
                                        width: clipList.width
                                        height: Math.max(76,window.font.pixelSize * 4)
                                        highlighted: editor.selectedIndices.indexOf(index) >= 0
                                        Accessible.name: "Clip " + (index + 1) + ", " + modelData.name + ", " + modelData.durationText
                                        background: Rectangle {
                                            color: clipDelegate.highlighted ? window.palette.highlight : window.palette.base
                                            border.width: clipDelegate.visualFocus ? 2 : 0
                                            border.color: window.palette.highlight
                                        }
                                        contentItem: RowLayout {
                                            spacing: 9
                                            CachedFrame { cache: derivedMedia; source: modelData.path; seconds: modelData.inSeconds; sourceRotation: modelData.rotation; Layout.preferredWidth: 72; Layout.preferredHeight: 48 }
                                            ColumnLayout {
                                                Layout.fillWidth: true; spacing: 4
                                                Label { color: clipDelegate.highlighted ? window.palette.highlightedText : window.palette.text; text: (index + 1) + ". " + modelData.name; elide: Text.ElideMiddle; Layout.fillWidth: true; font.bold: true }
                                                Label { color: clipDelegate.highlighted ? window.palette.highlightedText : window.palette.text; text: modelData.durationText; opacity: clipDelegate.highlighted ? 1 : 0.7 }
                                            }
                                        }
                                        onClicked: editor.selectClip(index,0)
                                        MouseArea { anchors.fill: parent; onClicked: mouse => { parent.forceActiveFocus(); editor.selectClip(index,mouse.modifiers) } }
                                        Keys.onReturnPressed: editor.selectClip(index,0)
                                        Keys.onSpacePressed: event => editor.selectClip(index,event.modifiers)
                                    }
                                    Keys.onUpPressed: event => editor.selectClip(Math.max(0,editor.selectedClip - 1),event.modifiers)
                                    Keys.onDownPressed: event => editor.selectClip(Math.min(editor.clips.length - 1,editor.selectedClip + 1),event.modifiers)
                                    Label { anchors.centerIn: parent; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter; text: "Drop videos here\nor click Import"; wrapMode: Text.Wrap; visible: editor.clips.length === 0; opacity: 0.7 }
                                }
                                Flow {
                                    Layout.fillWidth: true; Layout.margins: 6; spacing: 3
                                    Button { text: "Earlier"; enabled: editor.selectedIndices.length > 0; onClicked: editor.moveSelection(-1) }
                                    Button { text: "Later"; enabled: editor.selectedIndices.length > 0; onClicked: editor.moveSelection(1) }
                                    Button { text: "Remove"; enabled: editor.selectedIndices.length > 0; onClicked: editor.removeSelection() }
                                    Button {
                                        text: "Selection"
                                        onClicked: selectionMenu.popup()
                                        Menu {
                                            id: selectionMenu
                                            MenuItem { text: "Select all"; onTriggered: editor.selectAll() }
                                            MenuItem { text: "Select range…"; onTriggered: rangeDialog.open() }
                                            MenuItem { text: "Clear selection"; onTriggered: editor.clearSelection() }
                                        }
                                    }
                                }
                            }
                            Loader { id: compactInspector; active: window.compact; sourceComponent: inspectorComponent; onLoaded: item.section = window.inspectorSection }
                        }
                    }
                }
                Pane {
                    padding: 12
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 220
                    ColumnLayout {
                        anchors.fill: parent; spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "VIEWER"; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Label { text: editor.loading ? "Loading frame…" : editor.previewStatus; elide: Text.ElideRight; Layout.maximumWidth: Math.max(80,parent.width - 90); opacity: 0.7 }
                        }
                        VideoPreview {
                            id: viewer
                            objectName: "viewer"
                            controller: editor
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Accessible.name: "Video preview"
                            Label { anchors.centerIn: parent; width: parent.width - 30; horizontalAlignment: Text.AlignHCenter; color: "#B9BDC6"; text: "Your story starts here\n\nImport video clips to begin"; wrapMode: Text.Wrap; visible: editor.duration === 0 }
                            Rectangle {
                                visible: timeline.trimPreview.active || false
                                anchors.fill: parent; color: "#101114"
                                RowLayout {
                                    anchors.fill: parent; spacing: 2
                                    ColumnLayout {
                                        Layout.fillWidth: true; Layout.fillHeight: true
                                        CachedFrame { objectName: "trimOutgoingFrame"; priority: true; cache: derivedMedia; source: timeline.trimPreview.outPath || ""; seconds: timeline.trimPreview.outTime || 0; sourceRotation: timeline.trimPreview.outRotation || 0; Layout.fillWidth: true; Layout.fillHeight: true }
                                        Label { text: "Outgoing last frame"; color: "#F3F4F6"; Layout.alignment: Qt.AlignHCenter }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; Layout.fillHeight: true
                                        CachedFrame { objectName: "trimIncomingFrame"; priority: true; cache: derivedMedia; source: timeline.trimPreview.inPath || ""; seconds: timeline.trimPreview.inTime || 0; sourceRotation: timeline.trimPreview.inRotation || 0; Layout.fillWidth: true; Layout.fillHeight: true }
                                        Label { text: "Incoming first frame"; color: "#F3F4F6"; Layout.alignment: Qt.AlignHCenter }
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Button { objectName: "playButton"; text: editor.playing ? "Pause" : "Play"; enabled: editor.duration > 0; Accessible.name: text + " preview"; onClicked: editor.togglePlayback() }
                            Slider {
                                id: positionSlider
                                objectName: "positionSlider"
                                property bool pointerScrubbing: false
                                function cancelScrub() {
                                    if (!pointerScrubbing) return
                                    pointerScrubbing = false
                                    editor.finishScrub(true)
                                }
                                Layout.fillWidth: true; from: 0; to: Math.max(0,editor.duration - 1); stepSize: 1; value: editor.position
                                enabled: editor.duration > 0; activeFocusOnTab: true
                                Accessible.name: "Playback position in frames"
                                onMoved: if (!pointerScrubbing) editor.seek(Math.round(value))
                                Keys.onEscapePressed: event => { cancelScrub(); event.accepted = true }
                                onActiveFocusChanged: if (!activeFocus) cancelScrub()
                                MouseArea {
                                    id: scrubPointer
                                    anchors.fill: parent
                                    preventStealing: true
                                    function seekAt(x) {
                                        var fraction = Math.max(0,Math.min(1,(x - positionSlider.leftPadding) / Math.max(1,width - positionSlider.leftPadding - positionSlider.rightPadding)))
                                        editor.seek(Math.round(positionSlider.from + fraction * (positionSlider.to - positionSlider.from)))
                                    }
                                    onPressed: mouse => { positionSlider.forceActiveFocus(); positionSlider.pointerScrubbing = true; editor.beginScrub(); seekAt(mouse.x) }
                                    onPositionChanged: mouse => { if (positionSlider.pointerScrubbing) seekAt(mouse.x) }
                                    onReleased: mouse => {
                                        if (positionSlider.pointerScrubbing) { seekAt(mouse.x); positionSlider.pointerScrubbing = false; editor.finishScrub(false) }
                                    }
                                    onCanceled: positionSlider.cancelScrub()
                                }
                                Keys.onLeftPressed: editor.step(-1)
                                Keys.onRightPressed: editor.step(1)
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_PageUp) { editor.step(60); event.accepted = true }
                                    else if (event.key === Qt.Key_PageDown) { editor.step(-60); event.accepted = true }
                                }
                            }
                        }
                            Label { text: editor.timeText; font.family: "Menlo"; font.pixelSize: Math.max(11,window.font.pixelSize - 1); Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WrapAnywhere; Accessible.name: editor.timeText }
                        }
                    }
                }
                Loader {
                    id: wideInspector
                    active: !window.compact
                    visible: active
                    sourceComponent: inspectorComponent
                    SplitView.preferredWidth: 300
                    SplitView.minimumWidth: active ? 260 : 0
                    SplitView.maximumWidth: active ? 460 : 0
                    onLoaded: item.section = window.inspectorSection
                }
            }
            Pane {
                padding: 0
                SplitView.preferredHeight: 290
                SplitView.minimumHeight: timeline.implicitHeight + timelineTools.implicitHeight + 24
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    Flow {
                        id: timelineTools
                        Layout.fillWidth: true; Layout.margins: 8
                        spacing: 5
                        Button { action: splitAction; ToolTip.visible: hovered; ToolTip.text: "Split the selected clip at the playhead, away from crossfades." }
                        Button { text: "Undo"; enabled: undoAction.enabled; onClicked: editor.undo(); ToolTip.visible: hovered; ToolTip.text: undoAction.text }
                        Button { text: "Redo"; enabled: redoAction.enabled; onClicked: editor.redo() }
                        CheckBox { text: "Snap"; checked: timeline.snapping; onToggled: timeline.snapping = checked; ToolTip.visible: hovered; ToolTip.text: "N toggles snapping. Hold Option/Alt to override." }
                        CheckBox { text: "Skim"; checked: timeline.skimming; onToggled: timeline.skimming = checked; ToolTip.visible: hovered; ToolTip.text: "Preview under the pointer without moving the playhead." }
                        Button { text: "−"; Accessible.name: "Zoom timeline out"; onClicked: timeline.zoomOut() }
                        Button { text: "+"; Accessible.name: "Zoom timeline in"; onClicked: timeline.zoomIn() }
                        ComboBox {
                            implicitWidth: Math.max(140,window.font.pixelSize * 9.5)
                            model: ["Waveforms 1×","Waveforms 2×","Waveforms 4×"]
                            Accessible.name: "Waveform display scale"
                            onActivated: index => timeline.waveformScale = Math.pow(2,index)
                        }
                        Button { text: "Fit"; Accessible.name: "Fit entire edit in timeline"; onClicked: timeline.fitToWidth() }
                        Label { text: editor.clips.length + " clips · " + (editor.duration / 60).toFixed(1) + " s"; padding: 8; opacity: 0.7 }
                    }
                    Label { visible: !!timeline.gestureText; text: timeline.gestureText; Layout.fillWidth: true; Layout.leftMargin: 12; wrapMode: Text.Wrap; color: window.accent }
                    QuickTimeline {
                        id: timeline
                        objectName: "timeline"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        position: editor.position
                        selection: editor.selectedIndices
                        playing: editor.playing
                        mediaCache: derivedMedia
                        font: window.font
                        Accessible.role: Accessible.Pane
                        Accessible.name: "Editing timeline"
                        Accessible.description: "Ruler seeks. Clip bodies select; edges trim. Arrow keys move one frame. Clips list and inspector provide alternatives to dragging."
                        activeFocusOnTab: true
                        onSeekRequested: frame => editor.seek(frame)
                        onScrubStarted: editor.beginScrub()
                        onScrubFinished: canceled => editor.finishScrub(canceled)
                        onSkimRequested: frame => editor.skim(frame)
                        onSkimEnded: editor.endSkim()
                        onSelectionRequested: (index,modifiers) => editor.selectClip(index,modifiers)
                        onClearSelectionRequested: editor.clearSelection()
                        onGroupMoveRequested: (indices,boundary) => editor.moveGroup(indices,boundary)
                        onTrimRequested: (index,start,end) => editor.trimClip(index,start,end)
                        onAudioSelected: lane => window.focusInspector(1)
                        onFilesDropped: paths => editor.importFiles(paths)
                        PinchHandler {
                            target: null
                            property real priorScale: 1
                            onActiveChanged: priorScale = 1
                            onActiveScaleChanged: { timeline.zoomAt(activeScale / priorScale, centroid.position.x); priorScale = activeScale }
                        }
                    }
                    ScrollBar {
                        orientation: Qt.Horizontal
                        Layout.fillWidth: true
                        policy: timeline.maximumOffset > 0 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        size: timeline.maximumOffset > 0 ? timeline.width / (timeline.width + timeline.maximumOffset) : 1
                        position: timeline.maximumOffset > 0 ? (1 - size) * timeline.contentOffset / timeline.maximumOffset : 0
                        onPositionChanged: if (pressed) timeline.contentOffset = position * timeline.maximumOffset / Math.max(0.001,1 - size)
                    }
                }
            }
        }
    }
    }
    Component { id: inspectorComponent; Inspector { controller: editor } }
    DropArea { anchors.fill: parent; z: -1; onDropped: drop => { if (drop.hasUrls) { editor.importUrls(drop.urls); drop.acceptProposedAction() } } }
    Dialog {
        id: rangeDialog
        anchors.centerIn: parent
        title: "Select clip range"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Label { text: "First clip" }
            SpinBox { id: rangeFirst; from: 1; to: Math.max(1,editor.clips.length); editable: true; Accessible.name: "First clip in range" }
            Label { text: "Last clip" }
            SpinBox { id: rangeLast; from: 1; to: Math.max(1,editor.clips.length); value: to; editable: true; Accessible.name: "Last clip in range" }
        }
        onAccepted: editor.selectRange(rangeFirst.value - 1,rangeLast.value - 1)
    }
    Popup {
        anchors.centerIn: parent
        modal: true; closePolicy: Popup.NoAutoClose
        visible: editor.exporting
        width: Math.min(window.width - 40,420)
        ColumnLayout {
            width: parent.width
            Label { text: "Exporting MP4…"; font.bold: true }
            Label { text: "Original media · " + editor.format; Layout.fillWidth: true; wrapMode: Text.Wrap }
            ProgressBar { from: 0; to: 100; value: editor.exportProgress; Layout.fillWidth: true }
            Label { text: editor.exportProgress + "%" }
        }
    }
}
