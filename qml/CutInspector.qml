import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as UI

ColumnLayout {
    id: cuts
    required property var controller
    property var cut: controller.currentTransition
    Layout.fillWidth: true
    spacing: 8
    Label { text: "Individual cut"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.weight: Font.DemiBold }
    UI.ComboBox {
        Layout.fillWidth: true
        model: controller.transitions
        textRole: "name"
        currentIndex: controller.selectedTransition - 1
        Accessible.name: "Choose a cut to edit"
        onActivated: controller.selectTransition(currentIndex + 1)
    }
    UI.ComboBox {
        Layout.fillWidth: true
        model: ["Hard cut", "Dissolve"]
        currentIndex: cuts.cut.type === "dissolve" ? 1 : 0
        enabled: controller.selectedTransition > 0
        Accessible.name: "Transition type"
        onActivated: controller.setTransition(controller.selectedTransition, currentIndex === 0 ? "cut" : "dissolve", currentIndex === 0 ? 0 : Math.max(1, cuts.cut.frames || 30))
    }
    Label { text: "Overlap (frames)"; Layout.fillWidth: true; wrapMode: Text.Wrap }
    UI.TextField {
        Layout.fillWidth: true
        objectName: "localTransitionFrames"
        text: String(cuts.cut.frames || 0)
        enabled: controller.selectedTransition > 0
        Accessible.name: "Selected cut overlap in frames"
        validator: IntValidator { bottom: 0; top: 600 }
        onEditingFinished: if (acceptableInput) controller.setTransition(controller.selectedTransition, Number(text) > 0 ? "dissolve" : "cut", Number(text))
        Keys.onEscapePressed: { text = Qt.binding(function() { return String(cuts.cut.frames || 0) }); focus = false }
    }
    Flow {
        Layout.fillWidth: true; spacing: 4
        UI.Button { text: "−1 frame"; enabled: controller.selectedTransition > 0; onClicked: controller.setTransition(controller.selectedTransition, cuts.cut.frames > 1 ? "dissolve" : "cut", Math.max(0, cuts.cut.frames - 1)) }
        UI.Button { text: "+1 frame"; enabled: controller.selectedTransition > 0; onClicked: controller.setTransition(controller.selectedTransition, "dissolve", (cuts.cut.frames || 0) + 1) }
        UI.Button { text: "Remove"; enabled: controller.selectedTransition > 0; onClicked: controller.removeTransition() }
    }
    UI.Button {
        text: "Dissolve · 30f"
        Accessible.name: "Apply 30 frame dissolve at selected cut; can also drag onto a timeline cut"
        enabled: controller.transitions.length > 0
        onClicked: controller.setTransition(Math.max(1, controller.selectedTransition), "dissolve", 30)
        Drag.active: presetDrag.active
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction
        Drag.mimeData: ({"application/x-isaac-transition": "dissolve"})
        DragHandler { id: presetDrag; target: null }
    }
    Label { text: "Drag the transition block edge to preview its exact overlap and project duration. Each cut keeps its own setting. Short media clamps only neighboring overlaps."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: UI.Theme.muted }
}
