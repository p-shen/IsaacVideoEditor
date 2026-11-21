import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import "." as UI

UI.InspectorDetails {
    id: generator
    required property var controller
    readonly property var models: controller.subtitleModels || null
    readonly property bool running: controller.generatingSubtitles || false
    readonly property bool working: running || (models && models.busy)
    readonly property bool completed: controller.subtitleState === "Completed"
    readonly property string downloadSize: {
        if (!models) return ""
        for (var choice of models.choices)
            if (choice.id === models.selectedModel) return (Number(choice.bytes) / 1000000000).toFixed(1) + " GB"
        return ""
    }
    title: "Generate subtitles"
    summary: "Local"
    expanded: true
    visible: !!models
    objectName: "subtitleGenerator"

    RowLayout {
        id: modelRow
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Model"; color: UI.Theme.text }
        UI.ComboBox {
            id: modelChoice
            objectName: "subtitleModelSelector"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: generator.models ? generator.models.choices.map(function(choice) {
                return { id: choice.id, label: choice.id === "large-v3-turbo" ? "Turbo" : choice.id === "large-v3" ? "Large-v3" : choice.label }
            }) : []
            textRole: "label"; valueRole: "id"
            enabled: !generator.working
            hoverEnabled: true
            Accessible.name: "Subtitle transcription model"
            Accessible.description: "Whisper Large-v3 or Turbo. Transcribes the edited timeline locally."
            ToolTip.visible: hovered || visualFocus
            ToolTip.text: "Whisper Large-v3 uses the full model. Turbo favors speed."
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.tab: optionsButton
            currentIndex: {
                if (!generator.models) return -1
                var choices = generator.models.choices
                for (var i = 0; i < choices.length; ++i) if (choices[i].id === generator.models.selectedModel) return i
                return -1
            }
            onActivated: index => { generator.models.selectedModel = generator.models.choices[index].id }
        }
        IconImage {
            visible: !generator.working
            Layout.preferredWidth: 18; Layout.preferredHeight: 18
            source: Qt.resolvedUrl("icons/" + (generator.models && generator.models.ready ? "check" : "arrow-down") + ".svg")
            color: UI.Theme.muted
            Accessible.role: Accessible.StaticText
            Accessible.name: generator.models && generator.models.ready ? "Model ready" : "Model download required"
            ToolTip.visible: modelHover.hovered
            ToolTip.text: Accessible.name
            HoverHandler { id: modelHover }
        }
        UI.Button {
            id: optionsButton
            objectName: "subtitleOptionsButton"
            text: "Options"; iconName: "settings"; iconOnly: true; flat: true
            helpText: "Subtitle options"; Accessible.name: helpText
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.tab: cancelButton.visible ? cancelButton : downloadModel.visible ? downloadModel : generateButton
            KeyNavigation.backtab: modelChoice
            onClicked: options.open()
        }
    }
    Label {
        id: status
        objectName: "subtitleGenerationStatus"
        text: {
            if (generator.running) return (generator.controller.subtitleState || "Preparing audio") + (generator.controller.subtitleProgress >= 0 ? " · " + Math.round(generator.controller.subtitleProgress) + "%" : "…")
            if (generator.models && generator.models.busy) return generator.models.progress < 0 ? "Checking model…" : "Downloading · " + Math.round(generator.models.progress * 100) + "%"
            if (!generator.controller.subtitleEngineAvailable) return "Engine unavailable"
            if (generator.models && !generator.models.ready && generator.models.status !== "Download this model once to transcribe offline.") return generator.models.status
            if (generator.completed) return generator.controller.canImportGeneratedSubtitles ? "Ready to import" : "Saved · Import unavailable"
            if (generator.controller.subtitleState === "Failed") return "Failed"
            if (generator.controller.subtitleState === "Canceled") return "Canceled"
            if (generator.models && !generator.models.ready) return generator.models.status === "Download this model once to transcribe offline." ? "Download · " + generator.downloadSize : generator.models.status
            if (generator.controller.exporting) return "Export in progress"
            return generator.controller.duration > 0 ? "Ready" : "Add clips to begin"
        }
        wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText
        color: UI.Theme.muted; Accessible.role: Accessible.StaticText
        ToolTip.visible: statusHover.hovered
        ToolTip.text: !generator.controller.subtitleEngineAvailable ? "This build does not include the local Whisper engine." : generator.completed && !generator.controller.canImportGeneratedSubtitles ? "Already imported or the timeline changed. The saved SRT is still available through Reveal." : "Transcribe the edited timeline. English and Mandarin stay in their spoken language."
        HoverHandler { id: statusHover }
    }
    ProgressBar {
        visible: generator.working; Layout.fillWidth: true
        indeterminate: generator.running ? generator.controller.subtitleProgress < 0 : generator.models.progress < 0
        value: generator.running ? Math.max(0,generator.controller.subtitleProgress) / 100 : (generator.models ? generator.models.progress : 0)
        Accessible.name: "Subtitle generation progress"
    }
    Flow {
        Layout.fillWidth: true; spacing: 8
        UI.Button {
            id: downloadModel
            objectName: "downloadSubtitleModelButton"
            text: "Download"; iconName: "arrow-down"; highlighted: true
            helpText: "Download the selected model once (" + generator.downloadSize + ")"
            visible: !generator.working && generator.models && !generator.models.ready
            enabled: generator.controller.subtitleEngineAvailable
            KeyNavigation.backtab: optionsButton
            KeyNavigation.tab: generator.completed ? (importButton.enabled ? importButton : revealButton) : optionsButton
            onClicked: generator.models.download()
        }
        UI.Button {
            id: generateButton
            objectName: "generateSrtButton"
            text: generator.controller.subtitleState === "Failed" ? "Retry" : "Generate"
            iconName: generator.controller.subtitleState === "Failed" ? "reset" : "add-text"
            highlighted: !generator.completed
            helpText: "Save subtitles from the edited timeline as an SRT file"
            visible: !generator.working && generator.models && generator.models.ready
            enabled: !generator.controller.exporting && generator.controller.duration > 0 && generator.controller.subtitleEngineAvailable
            onClicked: generator.controller.generateSrtDialog()
            KeyNavigation.backtab: optionsButton
            KeyNavigation.tab: importButton.visible ? importButton : null
        }
        UI.Button {
            id: cancelButton
            objectName: "cancelSubtitleGenerationButton"
            text: "Cancel"; iconName: "close"; visible: generator.working
            helpText: "Cancel without replacing existing output"
            KeyNavigation.backtab: optionsButton
            KeyNavigation.tab: generator.completed ? (importButton.enabled ? importButton : revealButton) : optionsButton
            onClicked: { if (generator.running) generator.controller.cancelSubtitleGeneration(); else generator.models.cancel() }
        }
        UI.Button {
            id: importButton
            objectName: "importGeneratedSubtitlesButton"
            text: "Import"; iconName: "import"; visible: generator.completed
            highlighted: enabled
            helpText: "Add the generated captions to the timeline. You can undo the import."
            enabled: generator.controller.canImportGeneratedSubtitles || false
            KeyNavigation.backtab: cancelButton.visible ? cancelButton : downloadModel.visible ? downloadModel : generateButton
            KeyNavigation.tab: revealButton
            onClicked: generator.controller.confirmImportGeneratedSubtitles()
        }
        UI.Button {
            id: revealButton
            objectName: "revealGeneratedSubtitlesButton"
            text: "Reveal"; iconName: "folder"; visible: generator.completed
            helpText: generator.controller.subtitleDestination || "Show the saved SRT"
            KeyNavigation.backtab: importButton.enabled ? importButton : cancelButton.visible ? cancelButton : downloadModel.visible ? downloadModel : generateButton
            onClicked: generator.controller.revealGeneratedSubtitles()
        }
    }
    Repeater {
        model: generator.controller.subtitleWarnings || []
        Label { required property string modelData; text: modelData; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.text; Accessible.role: Accessible.AlertMessage }
    }
    Popup {
        id: options
        objectName: "subtitleOptionsPopup"
        parent: generator
        width: Math.min(320, generator.width)
        x: Math.max(0, generator.width - width)
        padding: 12; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: y = modelRow.mapToItem(generator, 0, modelRow.height).y + 4
        onOpened: {
            if (cpuChoice.enabled) cpuChoice.forceActiveFocus(Qt.TabFocusReason)
            else if (logButton.visible) logButton.forceActiveFocus(Qt.TabFocusReason)
        }
        onClosed: optionsButton.forceActiveFocus(Qt.PopupFocusReason)
        background: Rectangle { radius: UI.Theme.radius; color: UI.Theme.panel; border.color: UI.Theme.controlBorder }
        contentItem: ColumnLayout {
            spacing: 8
            CheckBox {
                id: cpuChoice
                objectName: "subtitleCpuCheckBox"
                text: "CPU"; checked: generator.controller.subtitleCpuOnly || false
                enabled: !generator.running
                hoverEnabled: true
                Accessible.name: "Use CPU for subtitle generation"
                Accessible.description: "Use the processor instead of the graphics card. Slower, but useful if GPU processing is unavailable."
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: Accessible.description
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.tab: logButton.visible ? logButton : cpuChoice
                KeyNavigation.backtab: logButton.visible ? logButton : cpuChoice
                onToggled: generator.controller.subtitleCpuOnly = checked
            }
            Label {
                text: generator.controller.subtitleEngineAvailable ? "Whisper · Local" : "Whisper engine missing from this build"
                textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
            }
            Label {
                visible: !!generator.controller.subtitleState
                text: (generator.controller.subtitleModel || "") + (generator.controller.subtitleDevice ? " · " + generator.controller.subtitleDevice : "")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true; color: UI.Theme.muted
            }
            UI.Button {
                id: logButton
                objectName: "subtitleLogButton"
                visible: !!generator.controller.subtitleState
                text: "Logs"; iconName: "external-link"; helpText: "Open subtitle diagnostics"
                KeyNavigation.backtab: cpuChoice
                KeyNavigation.tab: cpuChoice.enabled ? cpuChoice : logButton
                onClicked: { options.close(); generator.controller.openSubtitleLog() }
            }
        }
    }
}
