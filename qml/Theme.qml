pragma Singleton
import QtQuick

QtObject {
    readonly property SystemPalette colors: SystemPalette { colorGroup: SystemPalette.Active }
    readonly property SystemPalette disabledColors: SystemPalette { colorGroup: SystemPalette.Disabled }
    readonly property color background: colors.window
    readonly property color panel: colors.base
    readonly property color field: colors.alternateBase
    readonly property color text: colors.text
    readonly property color muted: colors.placeholderText
    readonly property color disabledText: disabledColors.text
    readonly property color border: colors.mid
    readonly property color controlBorder: colors.shadow
    readonly property color accent: colors.highlight
    readonly property color accentText: colors.highlightedText
    readonly property color selected: colors.midlight
    readonly property color hover: Qt.tint(panel, Qt.rgba(accent.r, accent.g, accent.b, 0.08))
    readonly property int radius: 8
    readonly property int panelRadius: 10
    readonly property int gutter: 10
}
