import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

UI.Pane {
    id: inspector
    required property var controller
    property int section: 0
    readonly property bool compactTabs: width < tabMetrics.width + 160
    TextMetrics { id: tabMetrics; font: inspector.font; text: "Clip Audio Fades Text Visual Captions" }
    function focusFirst() {
        if (section === 5) { captionPanel.focusFirst(); return }
        if (section === 4) { compositionPanel.focusFirst(); return }
        if (section === 3) { textPanel.focusFirst(); return }
        var target = section === 0 ? trimStartField : section === 1 ? voiceChoose : fadeField
        if (target.enabled && target.visible) target.forceActiveFocus()
        else if (compactTabs) sectionChoice.forceActiveFocus()
        else tabs.itemAt(section).forceActiveFocus()
    }
    function revealFocus() {
        var host = inspector.Window.window
        if (host && typeof host.revealFocusedControl === "function") Qt.callLater(host.revealFocusedControl)
    }
    padding: 0
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        UI.TabBar {
            id: tabs
            Layout.fillWidth: true
            visible: !inspector.compactTabs
            currentIndex: inspector.section
            onCurrentIndexChanged: inspector.section = currentIndex
            UI.TabButton { text: "Clip" }
            UI.TabButton { text: "Audio" }
            UI.TabButton { text: "Fades" }
            UI.TabButton { text: "Text" }
            UI.TabButton { text: "Visual" }
            UI.TabButton { text: "Captions" }
        }
        UI.ComboBox {
            id: sectionChoice
            visible: inspector.compactTabs
            Layout.fillWidth: true; Layout.margins: 8
            model: ["Clip", "Audio", "Fades", "Text", "Visual", "Captions"]
            currentIndex: inspector.section
            Accessible.name: "Inspector section"
            onActivated: index => inspector.section = index
        }
        ScrollView {
            onContentHeightChanged: inspector.revealFocus()
            onHeightChanged: inspector.revealFocus()
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            StackLayout {
                width: parent.width
                currentIndex: tabs.currentIndex
                ColumnLayout {
                    spacing: 12
                    Label {
                        objectName: "clipInspectorEmpty"
                        visible: controller.selectedClip < 0
                        text: "Select a clip to edit"
                        wrapMode: Text.Wrap; color: UI.Theme.muted
                        Layout.fillWidth: true; Layout.margins: 16
                    }
                    ColumnLayout {
                        visible: controller.selectedClip >= 0
                        Layout.fillWidth: true; Layout.margins: 16
                        spacing: 10
                        Label { text: controller.currentClip.name || ""; wrapMode: Text.Wrap; Layout.fillWidth: true; font.weight: Font.DemiBold }
                        Label { visible: controller.selectedIndices.length > 1; text: controller.selectedIndices.length + " clips selected"; color: UI.Theme.muted }
                        Label { text: "Start" }
                        Flow {
                            Layout.fillWidth: true; spacing: 6
                            UI.TextField {
                                id: trimStartField
                                objectName: "trimStartField"
                                width: Math.max(Math.min(parent.width,font.pixelSize * 6),parent.width - startEarlier.implicitWidth - startLater.implicitWidth - 12)
                                enabled: controller.selectedClip >= 0
                                suffix: "s"
                                text: Number(controller.currentClip.inSeconds || 0).toFixed(3)
                                Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.currentClip.inSeconds || 0).toFixed(3) }); focus = false; event.accepted = true }
                                Accessible.name: "Selected clip source start in seconds"
                                validator: DoubleValidator { bottom: 0; decimals: 6; locale: "C" }
                                onEditingFinished: if (acceptableInput) controller.setTrimStart(Number(text))
                            }
                            UI.Button { id: startEarlier; text: "Move trim start one frame earlier"; iconName: "step-back"; iconOnly: true; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(0,-1) }
                            UI.Button { id: startLater; objectName: "trimStartLaterButton"; text: "Move trim start one frame later"; iconName: "step-forward"; iconOnly: true; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(0,1) }
                        }
                        Label { text: "Length" }
                        Flow {
                            Layout.fillWidth: true; spacing: 6
                            UI.TextField {
                                objectName: "trimLengthField"
                                width: Math.max(Math.min(parent.width,font.pixelSize * 6),parent.width - lengthEarlier.implicitWidth - lengthLater.implicitWidth - 12)
                                enabled: controller.selectedClip >= 0
                                suffix: "s"
                                text: Number(controller.currentClip.lengthSeconds || 0).toFixed(3)
                                Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.currentClip.lengthSeconds || 0).toFixed(3) }); focus = false; event.accepted = true }
                                Accessible.name: "Selected clip length in seconds"
                                validator: DoubleValidator { bottom: 0.016; decimals: 6; locale: "C" }
                                onEditingFinished: if (acceptableInput) controller.setTrimLength(Number(text))
                            }
                            UI.Button { id: lengthEarlier; text: "Shorten selected clip by one frame"; iconName: "step-back"; iconOnly: true; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(1,-1) }
                            UI.Button { id: lengthLater; text: "Lengthen selected clip by one frame"; iconName: "step-forward"; iconOnly: true; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(1,1) }
                        }
                        Label { text: "Rotate"; font.weight: Font.DemiBold; Layout.topMargin: 8 }
                        Flow {
                            Layout.fillWidth: true; spacing: 6
                            UI.Button { text: "Rotate left 90°"; iconName: "rotate-left"; iconOnly: true; onClicked: controller.rotate(-90) }
                            UI.Button { text: "Rotate right 90°"; iconName: "rotate-right"; iconOnly: true; onClicked: controller.rotate(90) }
                            UI.Button { text: "180°"; helpText: "Turn 180°"; onClicked: controller.rotate(180) }
                        }
                        Label { text: "Rotation: " + (controller.currentClip.rotation || 0) + "°"; color: UI.Theme.muted }
                    }
                    UI.Button { text: "Clip audio"; visible: controller.selectedClip >= 0; Layout.leftMargin: 16; enabled: controller.selectedClip >= 0; onClicked: { controller.selectAudio(0); inspector.section = 1 } }
                }
                ColumnLayout {
                    spacing: 10
                    Label { text: "Narration"; font.weight: Font.DemiBold; Layout.topMargin: 16; Layout.leftMargin: 16 }
                    Label { text: controller.voicePath ? controller.voicePath.split("/").pop() : "No narration"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: 16 }
                    Flow {
                        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 4
                        UI.Button { id: voiceChoose; text: "Choose file"; iconName: "folder"; Accessible.name: "Choose narration audio file"; onClicked: controller.chooseVoice() }
                        UI.Button { text: "Clear narration audio file"; iconName: "close"; iconOnly: true; Accessible.name: text; enabled: !!controller.voicePath; onClicked: controller.setVoicePath("") }
                    }
                    Label { text: "Start"; Layout.leftMargin: 16 }
                    UI.TextField {
                        Layout.fillWidth: true; Layout.margins: 16
                        suffix: "s"
                        text: Number(controller.voiceStart).toFixed(3)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.voiceStart).toFixed(3) }); focus = false; event.accepted = true }; enabled: !!controller.voicePath
                        Accessible.name: "Narration start in project seconds"
                        validator: DoubleValidator { bottom: 0; decimals: 6; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setVoiceStart(Number(text))
                    }
                    UI.AudioMixer { controller: inspector.controller; Layout.fillWidth: true; Layout.margins: 16 }
                    Label { text: "Background music"; font.weight: Font.DemiBold; Layout.topMargin: 12; Layout.leftMargin: 16 }
                    Label { text: controller.musicPath ? controller.musicPath.split("/").pop() : "No background music"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: 16 }
                    Flow {
                        Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; spacing: 4
                        UI.Button { text: "Choose file"; iconName: "folder"; Accessible.name: "Choose background music file"; onClicked: controller.chooseMusic() }
                        UI.Button { text: "Clear background music file"; iconName: "close"; iconOnly: true; Accessible.name: text; enabled: !!controller.musicPath; onClicked: controller.setMusicPath("") }
                    }
                    UI.Button { text: "Browse Mixkit"; iconName: "external-link"; helpText: "Browse Mixkit music in your browser"; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.maximumWidth: parent.width - 32; onClicked: controller.browseMusic() }
                    UI.Button {
                        text: "Recent"; iconName: "recent"; helpText: "Recent audio downloads"; Layout.leftMargin: 16
                        onClicked: { recent.entries = controller.recentMusic(); recent.popup() }
                        Menu {
                            id: recent
                            property var entries: []
                            Instantiator {
                                model: recent.entries
                                delegate: MenuItem { required property var modelData; text: modelData.name; onTriggered: controller.setMusicPath(modelData.path) }
                                onObjectAdded: (index,object) => recent.insertItem(index,object)
                                onObjectRemoved: (index,object) => recent.removeItem(object)
                            }
                            MenuItem { text: "No recent audio files"; enabled: false; visible: recent.entries.length === 0 }
                        }
                    }
                    Label { text: "Volume"; Layout.leftMargin: 16; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    UI.TextField {
                        Layout.fillWidth: true; Layout.margins: 16
                        suffix: "dB"
                        text: Number(controller.musicDb).toFixed(1)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.musicDb).toFixed(1) }); focus = false; event.accepted = true }
                        enabled: !!controller.musicPath; Accessible.name: "Background music volume in decibels"
                        validator: DoubleValidator { bottom: -60; top: 12; decimals: 2; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setMusicDb(Number(text))
                    }
                    Label { text: controller.currentAudio.loopToEnd ? "Loops to end" : "Plays once"; color: UI.Theme.muted; Layout.leftMargin: 16; Layout.bottomMargin: 16 }
                }
                ColumnLayout {
                    spacing: 12
                    Label { text: "Crossfades"; font.weight: Font.DemiBold; Layout.leftMargin: 16; Layout.topMargin: 16 }
                    Label { text: "Duration (all clips)"; Layout.leftMargin: 16 }
                    UI.TextField {
                        Layout.fillWidth: true; Layout.margins: 16
                        id: fadeField
                        suffix: "s"
                        text: Number(controller.crossfade).toFixed(3)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.crossfade).toFixed(3) }); focus = false; event.accepted = true }
                        Accessible.name: "Shared crossfade duration in seconds"
                        validator: DoubleValidator { bottom: 0; top: 10; decimals: 6; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setCrossfade(Number(text))
                    }
                    Label { text: "Shared duration applies to every active cut. Split points stay straight cuts."; Layout.fillWidth: true; Layout.margins: 16; wrapMode: Text.Wrap }
                    UI.CutInspector { controller: inspector.controller; Layout.fillWidth: true; Layout.margins: 16 }
                }
                UI.TextInspector { id: textPanel; controller: inspector.controller }
                UI.CompositionInspector { id: compositionPanel; controller: inspector.controller }
                UI.CaptionInspector { id: captionPanel; controller: inspector.controller }
            }
        }
    }
}
