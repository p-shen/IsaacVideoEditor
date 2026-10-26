# Descript-inspired editor styling

The approved Descript preview is applied to the migrated Qt Quick interface. The Clips, Viewer, Inspector and timeline placements, compact inspector tabs and editing handlers are preserved. Panels have soft neutral surfaces, 10 px corner radii and gutters; fields and buttons use 8 px corners. The viewer retains a dark surround.

## Shared theme

`src/theme.cpp` owns the application palette shared by the QML controls, native dialogs and painted timeline. `qml/Theme.qml` maps those palette roles into semantic surface, text, border and selection values. `qml/Main.qml` and `qml/Inspector.qml` explicitly import local controls as `UI` so the custom buttons, fields, tabs, panels and combo boxes render consistently.

The approved Graphite + iris palette replaces the initial lavender neutrals with graphite greys. Violet remains the action and selection accent, with a lighter iris and dark action labels in dark mode.

| Role | Light | Dark |
| --- | --- | --- |
| Workspace | `#F2F2F4` | `#18191D` |
| Panels | `#FFFFFF` | `#202126` |
| Fields and controls | `#F6F6F8` | `#292B31` |
| Body text | `#272832` | `#F1F1F5` |
| Secondary text | `#696B79` | `#B0B2C0` |
| Accent | `#6B50C8` | `#9476EB` |
| Action labels | `#FFFFFF` | `#15111F` |
| Selection | `#EEE9FA` | `#352B49` |

The Descript panel geometry, font, video surround and editing behavior are preserved.

## Typography

Plus Jakarta Sans is bundled in `assets/fonts/PlusJakartaSans.ttf` and loaded through `QFontDatabase`. The application's existing point size is preserved, including large-text layouts. The font and its SIL Open Font License come from the Google Fonts repository: https://github.com/google/fonts/tree/main/ofl/plusjakartasans. The license is included in the source assets and application resources.

## Verification

The theme checks exercise bundled font availability, text-size preservation, system appearance changes, 4.5:1 normal/secondary text contrast and 3:1 field boundaries. The QML shell additionally checks the rendered primary-action label contrast in both appearances and pressed states, keyboard routes, focus, timeline gestures, source trim, file drops and large-text scrolling. Native captures cover light, dark and 24 pt text at 760×560.

Live VoiceOver, physical trackpad interaction and Windows/Linux device checks retain the migration's existing verification limitations.
