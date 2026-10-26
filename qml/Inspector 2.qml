import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: inspector
    required property var controller
    property int section: 0
    function focusFirst() {
        var target = section === 0 ? trimStartField : section === 1 ? voiceChoose : fadeField
        if (target.enabled) target.forceActiveFocus()
        else tabs.itemAt(section).forceActiveFocus()
    }
    padding: 0
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TabBar {
            id: tabs
            Layout.fillWidth: true
            currentIndex: inspector.section
            onCurrentIndexChanged: inspector.section = currentIndex
            TabButton { text: "Clip" }
            TabButton { text: "Audio" }
            TabButton { text: "Fades" }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            StackLayout {
                width: parent.width
                currentIndex: tabs.currentIndex
                ColumnLayout {
                    spacing: 12
                    Label { text: "CLIP"; font.bold: true; Layout.topMargin: 16; Layout.leftMargin: 12 }
                    Label { text: controller.currentClip.name || "No clip selected"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: 12; font.bold: true }
                    Label { text: controller.selectedIndices.length > 1 ? controller.selectedIndices.length + " clips selected" : "Source trim · 60 fps"; Layout.leftMargin: 12; opacity: 0.7 }
                    Label { text: "Start (seconds)"; Layout.leftMargin: 12 }
                    TextField {
                        id: trimStartField
                        objectName: "trimStartField"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        enabled: controller.selectedClip >= 0
                        text: Number(controller.currentClip.inSeconds || 0).toFixed(3)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.currentClip.inSeconds || 0).toFixed(3) }); focus = false; event.accepted = true }
                        Accessible.name: "Selected clip source start in seconds"
                        validator: DoubleValidator { bottom: 0; decimals: 6; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setTrimStart(Number(text))
                    }
                    RowLayout {
                        Layout.leftMargin: 12
                        Button { text: "−1 frame"; Accessible.name: "Move trim start one frame earlier"; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(0,-1) }
                        Button { text: "+1 frame"; Accessible.name: "Move trim start one frame later"; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(0,1) }
                    }
                    Label { text: "Length (seconds)"; Layout.leftMargin: 12 }
                    TextField {
                        objectName: "trimLengthField"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        enabled: controller.selectedClip >= 0
                        text: Number(controller.currentClip.lengthSeconds || 0).toFixed(3)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.currentClip.lengthSeconds || 0).toFixed(3) }); focus = false; event.accepted = true }
                        Accessible.name: "Selected clip length in seconds"
                        validator: DoubleValidator { bottom: 0.016; decimals: 6; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setTrimLength(Number(text))
                    }
                    RowLayout {
                        Layout.leftMargin: 12
                        Button { text: "−1 frame"; Accessible.name: "Shorten selected clip by one frame"; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(1,-1) }
                        Button { text: "+1 frame"; Accessible.name: "Lengthen selected clip by one frame"; enabled: controller.selectedClip >= 0; onClicked: controller.nudgeTrim(1,1) }
                    }
                    Label { text: "Rotate"; font.bold: true; Layout.leftMargin: 12; Layout.topMargin: 12 }
                    Flow {
                        Layout.fillWidth: true; Layout.margins: 12
                        spacing: 4
                        Button { text: "Left 90°"; enabled: controller.selectedClip >= 0; onClicked: controller.rotate(-90) }
                        Button { text: "Right 90°"; enabled: controller.selectedClip >= 0; onClicked: controller.rotate(90) }
                        Button { text: "180°"; enabled: controller.selectedClip >= 0; onClicked: controller.rotate(180) }
                    }
                    Label { text: "Rotation: " + (controller.currentClip.rotation || 0) + "°"; Layout.leftMargin: 12; Layout.bottomMargin: 16 }
                }
                ColumnLayout {
                    spacing: 10
                    Label { text: "NARRATION"; font.bold: true; Layout.topMargin: 16; Layout.leftMargin: 12 }
                    Label { text: controller.voicePath ? controller.voicePath.split("/").pop() : "No narration"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: 12 }
                    RowLayout {
                        Layout.leftMargin: 12
                        Button { id: voiceChoose; text: "Choose"; Accessible.name: "Choose narration audio file"; onClicked: controller.chooseVoice() }
                        Button { text: "Clear"; Accessible.name: "Clear narration audio file"; enabled: !!controller.voicePath; onClicked: controller.setVoicePath("") }
                    }
                    Label { text: "Starts at (seconds)"; Layout.leftMargin: 12 }
                    TextField {
                        Layout.fillWidth: true; Layout.margins: 12
                        text: Number(controller.voiceStart).toFixed(3)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.voiceStart).toFixed(3) }); focus = false; event.accepted = true }; enabled: !!controller.voicePath
                        Accessible.name: "Narration start in project seconds"
                        validator: DoubleValidator { bottom: 0; decimals: 6; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setVoiceStart(Number(text))
                    }
                    Label { text: "BACKGROUND MUSIC"; font.bold: true; Layout.topMargin: 12; Layout.leftMargin: 12 }
                    Label { text: controller.musicPath ? controller.musicPath.split("/").pop() : "No background music"; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.margins: 12 }
                    RowLayout {
                        Layout.leftMargin: 12
                        Button { text: "Choose"; Accessible.name: "Choose background music file"; onClicked: controller.chooseMusic() }
                        Button { text: "Clear"; Accessible.name: "Clear background music file"; enabled: !!controller.musicPath; onClicked: controller.setMusicPath("") }
                    }
                    Button { text: "Browse Mixkit"; Layout.leftMargin: 12; onClicked: controller.browseMusic() }
                    Button {
                        text: "Recent downloads"; Layout.leftMargin: 12
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
                    Label { text: "Music volume (dB) · loops to end"; Layout.leftMargin: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    TextField {
                        Layout.fillWidth: true; Layout.margins: 12
                        text: Number(controller.musicDb).toFixed(1)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.musicDb).toFixed(1) }); focus = false; event.accepted = true }
                        enabled: !!controller.musicPath; Accessible.name: "Background music volume in decibels"
                        validator: DoubleValidator { bottom: -60; top: 12; decimals: 2; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setMusicDb(Number(text))
                    }
                }
                ColumnLayout {
                    spacing: 12
                    Label { text: "CROSSFADES"; font.bold: true; Layout.leftMargin: 12; Layout.topMargin: 16 }
                    Label { text: "Shared duration (seconds)"; Layout.leftMargin: 12 }
                    TextField {
                        Layout.fillWidth: true; Layout.margins: 12
                        id: fadeField
                        text: Number(controller.crossfade).toFixed(3)
                        Keys.onEscapePressed: event => { text = Qt.binding(function() { return Number(controller.crossfade).toFixed(3) }); focus = false; event.accepted = true }
                        Accessible.name: "Shared crossfade duration in seconds"
                        validator: DoubleValidator { bottom: 0; top: 10; decimals: 6; locale: "C" }
                        onEditingFinished: if (acceptableInput) controller.setCrossfade(Number(text))
                    }
                    Label { text: "Short clips may shorten all crossfades. Split points stay straight cuts."; Layout.fillWidth: true; Layout.margins: 12; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
