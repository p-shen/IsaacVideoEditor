import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

UI.InspectorDetails {
    id: generator
    required property var controller
    readonly property var models: controller.subtitleModels || null
    readonly property bool running: controller.generatingSubtitles || false
    readonly property bool working: running || (models && models.busy)
    title: "Generate subtitles"
    summary: "Local · Whisper"
    expanded: true
    visible: !!models
    objectName: "subtitleGenerator"
    Label { text: "Transcribe the edited timeline’s audio. English and Mandarin stay in their spoken language."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted }
    Label { text: "Model"; color: UI.Theme.text }
    UI.ComboBox {
        id: modelChoice
        objectName: "subtitleModelSelector"
        Layout.fillWidth: true
        model: generator.models ? generator.models.choices : []
        textRole: "label"; valueRole: "id"
        enabled: !generator.working
        Accessible.name: "Subtitle transcription model"
        KeyNavigation.priority: KeyNavigation.BeforeItem
        KeyNavigation.tab: downloadModel.visible ? downloadModel : cpuChoice
        currentIndex: {
            if (!generator.models) return -1
            var choices = generator.models.choices
            for (var i = 0; i < choices.length; ++i) if (choices[i].id === generator.models.selectedModel) return i
            return -1
        }
        onActivated: index => { generator.models.selectedModel = generator.models.choices[index].id }
    }
    Label {
        visible: generator.models && !generator.models.ready
        text: {
            if (!generator.models) return ""
            var choices = generator.models.choices
            for (var i = 0; i < choices.length; ++i) if (choices[i].id === generator.models.selectedModel) return "One-time model download: " + (Number(choices[i].bytes) / 1000000000).toFixed(1) + " GB"
            return ""
        }
        wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
    }
    UI.Button {
        id: downloadModel
        objectName: "downloadSubtitleModelButton"
        text: "Download model"; iconName: "arrow-down"
        visible: generator.models && !generator.models.ready && !generator.models.busy
        enabled: !generator.running
        KeyNavigation.tab: cpuChoice
        KeyNavigation.backtab: modelChoice
        onClicked: generator.models.download()
    }
    CheckBox {
        id: cpuChoice
        objectName: "subtitleCpuCheckBox"
        text: "Use CPU"; checked: generator.controller.subtitleCpuOnly || false
        enabled: !generator.running
        KeyNavigation.priority: KeyNavigation.BeforeItem
        KeyNavigation.tab: generateButton.enabled ? generateButton : cancelButton.visible ? cancelButton : null
        KeyNavigation.backtab: downloadModel.visible ? downloadModel : modelChoice
        Accessible.description: "Use the processor instead of the graphics card. Slower, but useful if GPU processing is unavailable."
        onToggled: generator.controller.subtitleCpuOnly = checked
    }
    Label {
        visible: !generator.controller.subtitleEngineAvailable
        text: "The local subtitle engine is not included in this build. Use a build with Whisper support."
        wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
    }
    Flow {
        Layout.fillWidth: true; spacing: 8
        UI.Button {
            id: generateButton
            objectName: "generateSrtButton"
            text: "Generate SRT"; iconName: "add-text"
            enabled: !generator.working && !generator.controller.exporting && generator.controller.duration > 0 && generator.models && generator.models.ready && generator.controller.subtitleEngineAvailable
            onClicked: generator.controller.generateSrtDialog()
            KeyNavigation.backtab: cpuChoice
        }
        UI.Button {
            id: cancelButton
            objectName: "cancelSubtitleGenerationButton"
            text: "Cancel"; iconName: "close"; visible: generator.working
            KeyNavigation.backtab: cpuChoice
            onClicked: { if (generator.running) generator.controller.cancelSubtitleGeneration(); else generator.models.cancel() }
        }
    }
    ProgressBar {
        visible: generator.working; Layout.fillWidth: true
        indeterminate: generator.running ? generator.controller.subtitleProgress < 0 : generator.models.progress < 0
        value: generator.running ? Math.max(0,generator.controller.subtitleProgress) / 100 : (generator.models ? generator.models.progress : 0)
        Accessible.name: "Subtitle generation progress"
    }
    Label {
        objectName: "subtitleGenerationStatus"
        text: generator.models ? generator.models.status : ""
        wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText
        color: UI.Theme.muted; Accessible.role: Accessible.StaticText
    }
    Label {
        text: (generator.controller.subtitleState || "") + (generator.controller.subtitleModel ? " · " + generator.controller.subtitleModel : "") + (generator.controller.subtitleDevice ? " · " + generator.controller.subtitleDevice : "")
        visible: !!generator.controller.subtitleState
        color: UI.Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap
    }
    Label {
        visible: generator.controller.subtitleState === "Completed"
        text: generator.controller.subtitleDestination || ""
        wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText; color: UI.Theme.muted
    }
    Repeater {
        model: generator.controller.subtitleWarnings || []
        Label { required property string modelData; text: modelData; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted; Accessible.role: Accessible.AlertMessage }
    }
    Label {
        visible: generator.controller.subtitleState === "Completed" && !generator.controller.canImportGeneratedSubtitles
        text: "Saved SRT remains available. It was imported already, or the project has changed."
        wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
    }
    Flow {
        visible: generator.controller.subtitleState === "Completed"
        Layout.fillWidth: true; spacing: 8
        UI.Button {
            objectName: "importGeneratedSubtitlesButton"
            text: "Import generated captions"; iconName: "import"
            enabled: generator.controller.canImportGeneratedSubtitles || false
            onClicked: generator.controller.confirmImportGeneratedSubtitles()
        }
        UI.Button { text: "Show file"; iconName: "folder"; onClicked: generator.controller.revealGeneratedSubtitles() }
    }
    UI.Button { visible: generator.controller.subtitleState === "Failed"; text: "Open subtitle log"; onClicked: generator.controller.openSubtitleLog() }
}
