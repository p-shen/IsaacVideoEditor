import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI
import Isaac 1.0

ApplicationWindow {
    id: window
    objectName: "editorWindow"
    width: 1320; height: 860
    minimumWidth: 760; minimumHeight: 560
    visible: true
    title: editor.title + (editor.dirty ? " — Unsaved changes" : editor.hasProjectFile ? " — Saved" : "") + " — IsaacVideoEditor"
    property bool compact: width < Math.max(1040, font.pixelSize * 75)
    property bool textEditing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    onCompactChanged: if (!compact && browserSection === 2) browserSection = 1
    onActiveChanged: if (!active) { timeline.cancelGesture(); viewer.cancelTextDrag(); sourceControls.cancelScrub() }
    property bool buttonFocused: activeFocusItem !== null && (activeFocusItem.hasOwnProperty("checkable") || activeFocusItem.hasOwnProperty("popup"))
    property int browserSection: 0
    property int inspectorSection: 0
    property int lastSelectedText: -1
    property int lastSelectedCaption: -1
    readonly property real upperMinimumHeight: Math.max(editor.captions.length > 0 ? 320 : 210, window.font.pixelSize * 10)
    property string lastExportState: ""
    property int lastSelectedSource: -1
    property bool lastSourceMode: false
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
    palette.placeholderText: UI.Theme.muted
    palette.mid: UI.Theme.border
    palette.midlight: UI.Theme.selected
    palette.shadow: UI.Theme.controlBorder
    // Basic menus use Light for highlighted rows and Dark for their borders.
    palette.light: UI.Theme.selected
    palette.dark: UI.Theme.controlBorder
    palette.disabled.text: disabledColors.text
    palette.disabled.buttonText: disabledColors.buttonText
    property color accent: palette.highlight
    color: palette.window
    property bool updateCloseApproved: false
    onClosing: close => { close.accepted = updateCloseApproved || (updater.state !== UpdateChecker.Installing && editor.requestClose()) }
    Component.onCompleted: editor.attachTimeline(timeline)
    function revealFocusedControl() {
        var target = window.activeFocusItem
        if (!target) return
        // Reveal the focused control in each nested scrolling area, from the inside out.
        for (var ancestor = target.parent; ancestor; ancestor = ancestor.parent) {
            if (!ancestor.hasOwnProperty("contentY") || !ancestor.hasOwnProperty("contentHeight") || !ancestor.contentItem) continue
            var point = target.mapToItem(ancestor.contentItem,0,0)
            var maximum = Math.max(0,ancestor.contentHeight - ancestor.height)
            if (point.y < ancestor.contentY) ancestor.contentY = Math.max(0,point.y - 8)
            else if (point.y + target.height > ancestor.contentY + ancestor.height)
                ancestor.contentY = Math.min(maximum,point.y + target.height - ancestor.height + 8)
        }
    }
    onActiveFocusItemChanged: Qt.callLater(revealFocusedControl)
    function focusInspector(section, focusControls = true) {
        inspectorSection = section
        if (compact) browserSection = 2
        if (wideInspector.item) wideInspector.item.section = section
        if (compactInspector.item) compactInspector.item.section = section
        if (focusControls) Qt.callLater(function() {
            var panel = window.compact ? compactInspector.item : wideInspector.item
            if (panel) panel.focusFirst()
        })
    }
    Connections {
        target: editor
        function onStateChanged() {
            if (editor.exportState !== "" && window.lastExportState !== editor.exportState) {
                window.lastExportState = editor.exportState
                exportPanel.open()
            }
        }
        function onSourceChanged() {
            if (editor.sourceMode && (!window.lastSourceMode || editor.selectedSource !== window.lastSelectedSource)) window.browserSection = 0
            window.lastSourceMode = editor.sourceMode; window.lastSelectedSource = editor.selectedSource
        }
        function onSelectionChanged() {
            if (editor.selectedCaption >= 0 && editor.selectedCaption !== window.lastSelectedCaption) window.focusInspector(5,false)
            window.lastSelectedCaption = editor.selectedCaption
            if (editor.selectedText >= 0 && editor.selectedText !== window.lastSelectedText) window.focusInspector(3,false)
            if (editor.selectedClip >= 0 && !editor.sourceMode) window.browserSection = 1
            window.lastSelectedText = editor.selectedText
        }
    }
    Action { id: newAction; text: "New project"; shortcut: StandardKey.New; onTriggered: editor.newProject() }
    Action { id: openAction; text: "Open…"; shortcut: StandardKey.Open; onTriggered: editor.openProject() }
    Action { id: saveAction; text: "Save"; shortcut: StandardKey.Save; onTriggered: editor.save() }
    Action { id: saveAsAction; text: "Save As…"; shortcut: StandardKey.SaveAs; onTriggered: editor.saveAs() }
    Action { id: importAction; text: "Import"; shortcut: "Ctrl+I"; onTriggered: editor.importDialog() }
    Action { id: exportAction; text: "Export…"; shortcut: "Ctrl+E"; enabled: !editor.exporting && editor.duration > 0; onTriggered: editor.exportDialog() }
    Action { id: undoAction; text: editor.undoText ? "Undo " + editor.undoText : "Undo"; shortcut: StandardKey.Undo; enabled: editor.canUndo && !window.textEditing; onTriggered: editor.undo() }
    Action { id: redoAction; text: editor.redoText ? "Redo " + editor.redoText : "Redo"; shortcut: StandardKey.Redo; enabled: editor.canRedo && !window.textEditing; onTriggered: editor.redo() }
    Action { id: splitAction; text: "Split"; shortcut: "Ctrl+B"; enabled: editor.canSplit && !window.textEditing; onTriggered: editor.split() }
    Shortcut { sequence: "Space"; enabled: !window.textEditing && !window.buttonFocused; onActivated: editor.togglePlayback() }
    Shortcut { sequence: "Delete"; enabled: !window.textEditing; onActivated: editor.removeSelection() }
    Shortcut { sequence: "Backspace"; enabled: !window.textEditing; onActivated: editor.removeSelection() }
    Shortcut { sequence: "Ctrl+1"; onActivated: { browserSection = 1; clipList.forceActiveFocus() } }
    Shortcut { sequence: "I"; enabled: editor.sourceMode && !window.textEditing; onActivated: editor.markSourceIn() }
    Shortcut { sequence: "O"; enabled: editor.sourceMode && !window.textEditing; onActivated: editor.markSourceOut() }
    Shortcut { sequence: "Left"; enabled: editor.sourceMode && !window.textEditing; onActivated: editor.stepSource(-1) }
    Shortcut { sequence: "Right"; enabled: editor.sourceMode && !window.textEditing; onActivated: editor.stepSource(1) }
    Shortcut { sequence: "Ctrl+2"; onActivated: timeline.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+4"; onActivated: focusInspector(0) }
    Shortcut { sequence: "="; enabled: !window.textEditing; onActivated: timeline.zoomIn() }
    Shortcut { sequence: "-"; enabled: !window.textEditing; onActivated: timeline.zoomOut() }
    Shortcut { sequence: "Shift+Z"; enabled: !window.textEditing; onActivated: timeline.fitToWidth() }
    UpdateChecker { id: updater; objectName: "updateChecker" }
    Connections {
        target: updater
        function onRestartRequested() { window.updateCloseApproved = true; window.close() }
    }
    menuBar: MenuBar {
        Menu { title: "File"; MenuItem { action: newAction }
            MenuItem { action: openAction }
            MenuItem { action: saveAction }
            MenuItem { action: saveAsAction }
            MenuItem { objectName: "recoveryMenuItem"; text: "Recovery and offline media…"; onTriggered: recoveryPanel.open() }
            MenuSeparator {}
            MenuItem { action: importAction }
            MenuItem { text: "Import and append…"; onTriggered: editor.importAndAppendDialog() }
            MenuItem { objectName: "exportMenuItem"; action: exportAction }
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
            MenuItem { text: "Clips"; onTriggered: { browserSection = 1; clipList.forceActiveFocus() } }
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
        Menu {
            title: "Help"
            MenuItem {
                objectName: "checkForUpdatesMenuItem"
                text: "Check for Updates…"
                onTriggered: { softwareUpdateDialog.open(); if (!updater.busy && !updater.readyToInstall) updater.check() }
            }
        }
    }
    Dialog {
        id: softwareUpdateDialog
        objectName: "softwareUpdateDialog"
        title: updater.updateAvailable ? "Update available" : "Software update"
        anchors.centerIn: parent
        width: Math.min(window.width - 40, 520)
        height: Math.min(implicitHeight, window.height - 40)
        modal: true
        focus: true
        closePolicy: updater.state === UpdateChecker.Installing ? Popup.NoAutoClose : Popup.CloseOnEscape
        standardButtons: updater.state === UpdateChecker.Installing ? Dialog.NoButton : Dialog.Close
        contentItem: ScrollView {
            id: updateContent
            objectName: "updateContentScroll"
            implicitHeight: updateColumn.implicitHeight
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: updateColumn
            width: updateContent.availableWidth
            spacing: UI.Theme.gutter
            Label {
                text: "Installed version: " + updater.currentVersion
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                BusyIndicator { running: updater.busy && updater.state !== UpdateChecker.Downloading; visible: running; Accessible.name: "Preparing update" }
                Label {
                    objectName: "softwareUpdateMessage"
                    text: updater.message
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
            ProgressBar {
                objectName: "updateDownloadProgress"
                Layout.fillWidth: true
                visible: updater.state === UpdateChecker.Downloading
                value: updater.progress
                Accessible.name: "Update download"
            }
            Label {
                visible: updater.state === UpdateChecker.Downloading
                text: Math.round(updater.progress * 100) + "% downloaded"
            }
            Flow {
                Layout.fillWidth: true
                spacing: UI.Theme.gutter
                UI.Button {
                    objectName: "downloadUpdateButton"
                    text: "Download Update"
                    visible: updater.canDownload
                    highlighted: true
                    onClicked: updater.download()
                }
                UI.Button {
                    objectName: "cancelUpdateDownloadButton"
                    text: "Cancel Download"
                    visible: updater.state === UpdateChecker.Downloading
                    onClicked: updater.cancelDownload()
                }
                UI.Button {
                    objectName: "installUpdateButton"
                    text: "Restart and Install"
                    visible: updater.readyToInstall
                    highlighted: true
                    onClicked: if (editor.requestClose()) updater.install()
                }
                UI.Button {
                    text: "Check again"
                    visible: !updater.busy && !updater.readyToInstall
                    onClicked: updater.check()
                }
            }
        }
        }
    }
    Component {
        id: splitHandle
        Rectangle {
            implicitWidth: UI.Theme.gutter
            implicitHeight: UI.Theme.gutter
            color: SplitHandle.hovered ? UI.Theme.selected : UI.Theme.background
        }
    }
    ScrollView {
        id: workspaceScroll
        objectName: "workspaceScroll"
        onContentHeightChanged: Qt.callLater(window.revealFocusedControl)
        onHeightChanged: Qt.callLater(window.revealFocusedControl)
        anchors.fill: parent
        anchors.margins: UI.Theme.gutter
        contentWidth: availableWidth
        contentHeight: workspace.height
        clip: true
    ColumnLayout {
        id: workspace
        width: workspaceScroll.availableWidth
        height: Math.max(window.contentItem.height - 2 * UI.Theme.gutter, window.upperMinimumHeight + (editor.sourceMode ? sourceControls.implicitHeight : 0) + timeline.implicitHeight + timelineTools.implicitHeight + 24 + (noticeFrame.visible ? noticeFrame.implicitHeight : 0))
        spacing: UI.Theme.gutter
        Frame {
            id: noticeFrame
            visible: !!editor.notice
            Layout.fillWidth: true
            padding: 6
            RowLayout {
                anchors.fill: parent
                Label { text: editor.notice; wrapMode: Text.Wrap; Layout.fillWidth: true; Accessible.role: Accessible.AlertMessage }
                UI.Button { objectName: "noticeUndoButton"; text: "Undo"; visible: editor.noticeCanUndo; enabled: editor.canUndo; helpText: undoAction.text; onClicked: editor.undo() }
                UI.Button { text: "Dismiss notice"; iconName: "close"; iconOnly: true; flat: true; onClicked: editor.dismissNotice() }
            }
        }
        SplitView {
            id: verticalSplit
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: splitHandle
            SplitView {
                orientation: Qt.Horizontal
                handle: splitHandle
                SplitView.minimumHeight: window.upperMinimumHeight + (editor.sourceMode ? sourceControls.implicitHeight : 0)
                SplitView.fillHeight: true
                UI.Pane {
                    padding: 0
                    SplitView.preferredWidth: window.compact ? Math.min(window.width - 260,Math.max(260,window.font.pixelSize * 15)) : 250
                    SplitView.minimumWidth: 210
                    SplitView.maximumWidth: window.compact ? Math.max(440,window.width - 260) : 440
                    ColumnLayout {
                        anchors.fill: parent; spacing: 0
                        UI.ComboBox {
                            id: compactBrowserSelector; objectName: "compactBrowserSelector"
                            visible: window.compact; Layout.fillWidth: true
                            model: ["Media","Clips","Inspector"]; currentIndex: window.browserSection
                            Accessible.name: "Browser or inspector panel"
                            onActivated: index => window.browserSection = index
                        }
                        UI.TabBar {
                            visible: !window.compact
                            Layout.fillWidth: true
                            currentIndex: window.browserSection
                            onCurrentIndexChanged: window.browserSection = currentIndex
                            UI.TabButton { text: "Media" }
                            UI.TabButton { text: "Clips" }
                        }
                        StackLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
                            currentIndex: window.browserSection
                            UI.MediaBrowser { id: mediaBrowser; controller: editor }
                            ColumnLayout {
                                spacing: 0
                                Label { text: "Clips  ·  " + editor.clips.length; font.weight: Font.DemiBold; Layout.margins: 14 }
                                ListView {
                                    id: clipList
                                    objectName: "clipList"
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    clip: true; spacing: 6
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
                                        width: clipList.width - 12
                                        x: 6
                                        height: Math.max(76,window.font.pixelSize * 4)
                                        highlighted: editor.selectedIndices.indexOf(index) >= 0
                                        Accessible.name: "Clip " + (index + 1) + ", " + modelData.name + ", " + modelData.durationText
                                        background: Rectangle {
                                            radius: UI.Theme.radius
                                            color: clipDelegate.highlighted ? UI.Theme.selected : clipDelegate.hovered ? UI.Theme.hover : UI.Theme.panel
                                            border.width: clipDelegate.visualFocus || clipDelegate.highlighted ? 2 : 0
                                            border.color: window.palette.highlight
                                        }
                                        contentItem: RowLayout {
                                            spacing: 9
                                            CachedFrame { cache: derivedMedia; source: modelData.path; seconds: modelData.inSeconds; sourceRotation: modelData.rotation; Layout.preferredWidth: 72; Layout.preferredHeight: 48 }
                                            ColumnLayout {
                                                Layout.fillWidth: true; spacing: 4
                                                Label { color: UI.Theme.text; text: (index + 1) + ". " + modelData.name; elide: Text.ElideMiddle; Layout.fillWidth: true; font.weight: Font.DemiBold }
                                                Label { text: modelData.durationText; color: UI.Theme.muted }
                                            }
                                        }
                                        onClicked: editor.selectClip(index,0)
                                        MouseArea { anchors.fill: parent; onClicked: mouse => { parent.forceActiveFocus(); editor.selectClip(index,mouse.modifiers) } }
                                        Keys.onReturnPressed: editor.selectClip(index,0)
                                        Keys.onSpacePressed: event => editor.selectClip(index,event.modifiers)
                                    }
                                    Keys.onUpPressed: event => editor.selectClip(Math.max(0,editor.selectedClip - 1),event.modifiers)
                                    Keys.onDownPressed: event => editor.selectClip(Math.min(editor.clips.length - 1,editor.selectedClip + 1),event.modifiers)
                                    Label { anchors.centerIn: parent; width: parent.width - 32; horizontalAlignment: Text.AlignHCenter; text: "Drop videos"; wrapMode: Text.Wrap; visible: editor.clips.length === 0; color: UI.Theme.muted }
                                }
                                Flow {
                                    Layout.fillWidth: true; Layout.margins: 6; spacing: 3
                                    UI.Button { text: "Move earlier"; iconName: "arrow-up"; iconOnly: true; flat: true; enabled: editor.selectedIndices.length > 0; onClicked: editor.moveSelection(-1) }
                                    UI.Button { text: "Move later"; iconName: "arrow-down"; iconOnly: true; flat: true; enabled: editor.selectedIndices.length > 0; onClicked: editor.moveSelection(1) }
                                    UI.Button { text: "Remove from edit"; iconName: "trash"; iconOnly: true; flat: true; enabled: editor.selectedIndices.length > 0; onClicked: editor.removeSelection() }
                                    UI.Button {
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
                            Loader { id: compactInspector; Layout.minimumHeight: 0; active: window.compact; sourceComponent: inspectorComponent; onLoaded: item.section = window.inspectorSection }
                        }
                    }
                }
                UI.Pane {
                    padding: 4
                    background: Item {}
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 220
                    ColumnLayout {
                        anchors.fill: parent; spacing: 10
                        RowLayout {
                            visible: editor.sourceMode || editor.clips.length > 0 || editor.soloLane >= 0
                            Layout.fillWidth: true
                            Label { visible: editor.sourceMode || editor.soloLane >= 0; text: editor.sourceMode ? "Source · " + (editor.currentSource.name || "") : "SOLO PREVIEW"; elide: Text.ElideMiddle; Layout.fillWidth: true; font.weight: Font.DemiBold }
                            UI.Button { visible: !editor.sourceMode && editor.soloLane >= 0; text: "Full mix"; Accessible.name: "Disable preview only solo"; onClicked: editor.setSolo(-1) }
                            Item { Layout.fillWidth: true }
                            Label { visible: editor.sourceMode || editor.clips.length > 0; text: editor.sourceMode ? (editor.currentSource.offline ? "Offline" : "Original source") : editor.loading ? "Loading frame…" : editor.previewStatus; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0; horizontalAlignment: Text.AlignRight; color: UI.Theme.muted }
                        }
                        VideoPreview {
                            id: viewer
                            objectName: "viewer"
                            controller: editor
                            onTextSelected: window.focusInspector(3,false)
                            onCaptionSelected: window.focusInspector(5,false)
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Accessible.name: "Video preview"
                            ColumnLayout {
                                id: emptyImportState
                                anchors.centerIn: parent
                                width: Math.max(0,parent.width - 32)
                                visible: editor.clips.length === 0 && !editor.sourceMode
                                spacing: 12
                                UI.Button {
                                    id: openProjectButton
                                    objectName: "openProjectButton"
                                    text: "Open Project"
                                    highlighted: true
                                    enabled: openAction.enabled
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.minimumWidth: 0; Layout.maximumWidth: emptyImportState.width
                                    helpText: "Open a saved project (⌘/Ctrl+O)"
                                    onClicked: openAction.trigger()
                                    contentItem: Text {
                                        text: openProjectButton.text
                                        font: openProjectButton.font
                                        color: openProjectButton.foreground
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                Label {
                                    objectName: "emptyImportHint"
                                    text: "or drag / drop videos to start"
                                    color: "#B9BDC6"
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }
                            }
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
                        UI.SourceControls { id: sourceControls; controller: editor; visible: editor.sourceMode; Layout.fillWidth: true }
                        ColumnLayout {
                            visible: editor.sourceMode || editor.clips.length > 0
                            Layout.fillWidth: true
                            spacing: 4
                        Flow {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0; spacing: 4
                            UI.Button { objectName: "playButton"; text: editor.playing ? "Pause" : "Play"; iconName: editor.playing ? "pause" : "play"; iconOnly: true; helpText: text + " preview (Space)"; enabled: editor.sourceMode ? editor.selectedSource >= 0 : editor.duration > 0; Accessible.name: text + " preview"; onClicked: editor.togglePlayback() }
                            UI.Button { text: editor.sourceMode ? "Project" : "Source"; helpText: editor.sourceMode ? "Show Project viewer" : "Show Source viewer"; enabled: editor.sourceMode || editor.selectedSource >= 0; onClicked: editor.sourceMode ? editor.showProject() : editor.selectSource(editor.selectedSource) }
                        }
                            Label { text: editor.sourceMode ? editor.sourceTimeText : editor.timeText; font.family: window.font.family; font.features: ({"tnum": 1}); font.pixelSize: Math.max(11,window.font.pixelSize - 1); Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Accessible.name: editor.timeText }
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
            UI.Pane {
                padding: 0
                SplitView.preferredHeight: 290
                SplitView.minimumHeight: timeline.implicitHeight + timelineTools.implicitHeight + 24
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    Flow {
                        id: timelineTools
                        objectName: "timelineTools"
                        Layout.fillWidth: true; Layout.margins: 8
                        spacing: 12
                        Row {
                            spacing: 4
                            UI.Button { objectName: "undoButton"; text: "Undo"; iconName: "undo"; iconOnly: true; flat: true; enabled: undoAction.enabled; onClicked: editor.undo(); helpText: undoAction.text + " (⌘/Ctrl+Z)" }
                            UI.Button { objectName: "redoButton"; text: "Redo"; iconName: "redo"; iconOnly: true; flat: true; enabled: redoAction.enabled; onClicked: editor.redo(); helpText: redoAction.text }
                        }
                        Row {
                            spacing: 4
                            UI.Button { action: splitAction; iconName: "split"; flat: true; helpText: "Split at the playhead (⌘/Ctrl+B), away from crossfades" }
                            UI.Button { objectName: "addTextButton"; text: "Add text"; iconName: "add-text"; flat: true; enabled: editor.duration > 0; onClicked: { editor.addText(); window.focusInspector(3) } }
                        }
                        Flow {
                            width: Math.min(timelineTools.width,captionTool.implicitWidth+transformTool.implicitWidth+4)
                            spacing: 4
                            UI.Button { id: captionTool; text: "Captions"; iconName: "add-text"; flat: true; onClicked: window.focusInspector(5) }
                            UI.Button { id: transformTool; text: "Transform"; iconName: "fit"; flat: true; onClicked: { window.focusInspector(4); editor.transformMode = true } }
                        }
                        Flow {
                            width: Math.min(timelineTools.width,snapButton.width + skimCheck.width + zoomTools.width + waveformScale.width + fitButton.width + spacing * 4)
                            spacing: 4
                            UI.Button { id: snapButton; objectName: "snapButton"; text: "Snap"; iconName: "magnet"; flat: true; checkable: true; checked: timeline.snapping; onToggled: timeline.snapping = checked; helpText: "Snap to clip edges. N toggles; hold Option/Alt to bypass." }
                            CheckBox { id: skimCheck; text: "Skim"; checked: timeline.skimming; onToggled: timeline.skimming = checked; ToolTip.visible: hovered || visualFocus; ToolTip.text: "Preview under the pointer without moving the playhead" }
                            Row {
                                id: zoomTools; spacing: 2
                                UI.Button { text: "Zoom timeline out"; iconName: "minus"; iconOnly: true; flat: true; onClicked: timeline.zoomOut() }
                                UI.Button { text: "Zoom timeline in"; iconName: "plus"; iconOnly: true; flat: true; onClicked: timeline.zoomIn() }
                            }
                            UI.ComboBox {
                                id: waveformScale
                                implicitWidth: Math.max(140,window.font.pixelSize * 9.5)
                                model: ["Waveforms 1×","Waveforms 2×","Waveforms 4×"]
                                Accessible.name: "Waveform display scale"
                                onActivated: index => timeline.waveformScale = Math.pow(2,index)
                                ToolTip.visible: hovered || visualFocus
                                ToolTip.text: "Waveform display height; audio volume stays the same"
                            }
                            UI.Button { id: fitButton; objectName: "fitTimelineButton"; text: "Fit"; iconName: "fit"; flat: true; Accessible.name: "Fit entire edit in timeline"; helpText: "Fit entire edit in timeline (Shift+Z)"; onClicked: timeline.fitToWidth() }
                        }
                        Label { text: editor.clips.length + (editor.clips.length === 1 ? " clip · " : " clips · ") + (editor.duration / 60).toFixed(1) + " s"; padding: 8; color: UI.Theme.muted }
                    }
                    Label { visible: !!timeline.gestureText; text: timeline.gestureText; Layout.fillWidth: true; Layout.leftMargin: 12; wrapMode: Text.Wrap; color: window.accent }
                    QuickTimeline {
                        id: timeline
                        objectName: "timeline"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        position: editor.position
                        selection: editor.selectedIndices
                        selectedText: editor.selectedText
                        selectedCaption: editor.selectedCaption
                        selectedCaptions: editor.selectedCaptions
                        playing: editor.playing
                        mediaCache: derivedMedia
                        font: window.font
                        Accessible.role: Accessible.Pane
                        Accessible.name: "Editing timeline"
                        Accessible.description: (selectionMediaStatus ? selectionMediaStatus + ". " : "") + "Ruler seeks. Clip bodies select; edges trim. Arrow keys seek one frame. Option or Alt with arrow keys moves selected text one frame; add Shift to change its duration. The inspector provides numeric and text controls."
                        ToolTip.visible: hoverMediaStatus.length > 0 || (activeFocus && selectionMediaStatus.length > 0)
                        ToolTip.text: hoverMediaStatus || selectionMediaStatus
                        ToolTip.delay: activeFocus ? 0 : 600
                        onSeekRequested: frame => editor.seek(frame)
                        onScrubStarted: editor.beginScrub()
                        onScrubFinished: canceled => editor.finishScrub(canceled)
                        onSkimRequested: frame => editor.skim(frame)
                        onSkimEnded: editor.endSkim()
                        onSelectionRequested: (index,modifiers) => editor.selectClip(index,modifiers)
                        onCaptionSelected: (index,modifiers) => { editor.beginCaptionEdit(index,modifiers); window.focusInspector(5,false) }
                        onCaptionTimingRequested: (index,start,frames) => editor.setCaptionTiming(index,start,frames)
                        onTextSelected: index => { editor.beginTextDrag(index); window.focusInspector(3,false) }
                        onTextGestureFinished: canceled => editor.endTextDrag(canceled)
                        onTextTimingRequested: (index,start,frames) => editor.setTextTiming(index,start,frames)
                        onClearSelectionRequested: editor.clearSelection()
                        onGroupMoveRequested: (indices,boundary) => editor.moveGroup(indices,boundary)
                        onTrimRequested: (index,start,end) => editor.trimClip(index,start,end)
                        onAudioSelected: lane => { editor.selectAudio(lane); window.focusInspector(1,false) }
                        selectedTransition: editor.selectedTransition
                        onAudioGestureStarted: editor.beginAudioGesture()
                        onAudioPreviewRequested: (lane,fields) => editor.updateAudio(lane,fields)
                        onAudioGestureFinished: canceled => editor.endAudioGesture(canceled)
                        onTransitionSelected: index => { editor.selectTransition(index); window.focusInspector(2,false) }
                        onTransitionPreviewRequested: (index,frames) => editor.setTransition(index,frames > 0 ? "dissolve" : "cut",frames)
                        onTransitionPresetRequested: (index,type,frames) => editor.setTransition(index,type,frames)
                        onFilesInserted: (paths,frame) => editor.dropMediaAt(paths,frame)
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
    Component { id: inspectorComponent; UI.Inspector { controller: editor } }
    DropArea { anchors.fill: parent; z: -1; onDropped: drop => { if (drop.hasUrls) { editor.importMediaUrls(drop.urls); drop.acceptProposedAction() } } }
    Dialog {
        id: recoveryPanel
        objectName: "recoveryPanel"
        anchors.centerIn: parent
        title: "Recovery and offline media"
        modal: true
        standardButtons: Dialog.Close
        width: Math.min(window.width - 40,680)
        height: Math.min(window.height - 40,480)
        contentItem: ScrollView {
            id: recoveryScroll
            clip: true
            contentWidth: availableWidth
            UI.WorkStatus {
                objectName: "recoveryTools"
                controller: editor
                expanded: true
                width: recoveryScroll.availableWidth
            }
        }
    }
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
        id: exportPanel
        objectName: "exportPanel"
        anchors.centerIn: parent
        modal: false; dim: false
        closePolicy: Popup.CloseOnEscape
        width: Math.min(window.width - 40,520)
        ColumnLayout {
            width: parent.width
            Label { text: "MP4 · " + editor.exportState; font.weight: Font.DemiBold }
            Label { text: editor.exportDestination; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            Label { text: editor.exportDetails; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: editor.exportRevision; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: "You can keep editing. This export uses the revision shown above."; visible: editor.exporting; Layout.fillWidth: true; wrapMode: Text.Wrap }
            ProgressBar { from: 0; to: 100; value: editor.exportProgress; Layout.fillWidth: true }
            Label { text: editor.exportProgress + "%" }
            Flow {
                Layout.fillWidth: true; spacing: 6
                UI.Button { text: "Cancel export"; visible: editor.exporting; enabled: editor.exportState !== "Canceling"; onClicked: editor.cancelExport() }
                UI.Button { text: "Retry same revision"; visible: editor.exportState === "Failed" || editor.exportState === "Canceled"; onClicked: editor.retryExport() }
                UI.Button { text: "Reveal in folder"; visible: editor.exportState === "Completed"; onClicked: editor.revealExport() }
                UI.Button { text: "Open export log"; visible: editor.exportLog !== ""; onClicked: editor.openExportLog() }
                UI.Button { text: "Open video"; visible: editor.exportState === "Completed"; onClicked: editor.openExport() }
                UI.Button { text: editor.exporting ? "Keep editing" : "Close"; onClicked: exportPanel.close() }
            }
        }
    }
}
