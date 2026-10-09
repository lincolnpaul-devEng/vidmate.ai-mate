/*
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 * SPDX-FileCopyrightText: 2026 VidMate AI-Mate Contributors
 *
 * Modern Preferences Dialog QML Frontend
 * Sleek dark UI with fine-tuned surfaces, contrast, accent colors,
 * performance, timeline, and AI plugin configurations.
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.ki18n

Rectangle {
    id: root

    property string currentPage: "Appearance"
    property bool showSidebar: true
    property var backend: null

    color: "#0e1015"
    implicitWidth: 920
    implicitHeight: 660

    // Theme palette constants
    readonly property color bgSidebar: "#12141a"
    readonly property color bgContent: "#0e1015"
    readonly property color bgCard: "#171922"
    readonly property color bgCardSubtle: "#1c1f2b"
    readonly property color borderCard: "#262938"
    readonly property color borderSubtle: "#212433"
    readonly property color textPrimary: "#f4f4f5"
    readonly property color textSecondary: "#9ca3af"
    readonly property color textMuted: "#6b7280"
    readonly property color currentAccent: (root.backend && root.backend.accentColor) ? root.backend.accentColor : "#f59e0b"

    // Swatches matching the design system
    readonly property var swatchesRow1: [
        "#f59e0b", "#f97316", "#ef4444", "#f43f5e", "#ec4899",
        "#d946ef", "#a855f7", "#8b5cf6", "#6366f1", "#3b82f6"
    ]
    readonly property var swatchesRow2: [
        "#0ea5e9", "#06b6d4", "#14b8a6", "#10b981", "#22c55e",
        "#84cc16", "#eab308", "#78716c", "#64748b", "#71717a"
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==========================================
        // LEFT SIDEBAR NAVIGATION
        // ==========================================
        Rectangle {
            visible: root.showSidebar
            Layout.preferredWidth: root.showSidebar ? 230 : 0
            Layout.fillHeight: true
            color: root.bgSidebar

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: root.borderSubtle
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 16

                // Header
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: KI18n.i18n("Preferences")
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        color: root.textPrimary
                    }

                    Text {
                        text: KI18n.i18n("Customise your workspace")
                        font.pixelSize: 11
                        color: root.textSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.borderSubtle
                }

                // Nav items
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [
                            { id: "Appearance", title: KI18n.i18n("Appearance") },
                            { id: "Keyboard Shortcuts", title: KI18n.i18n("Keyboard Shortcuts") },
                            { id: "Performance", title: KI18n.i18n("Performance") },
                            { id: "Playback & Audio", title: KI18n.i18n("Playback & Audio") },
                            { id: "Timeline", title: KI18n.i18n("Timeline") },
                            { id: "AI & Plugins", title: KI18n.i18n("AI & Plugins") }
                        ]

                        delegate: Rectangle {
                            id: navPill
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 8

                            readonly property bool isSelected: root.currentPage === navPill.modelData.id
                            color: isSelected ? root.currentAccent : (pillMouse.containsMouse ? "#1c1f29" : "transparent")

                            Behavior on color {
                                ColorAnimation { duration: 120 }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                // Indicator dot for active item
                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: navPill.isSelected ? "#18181b" : "transparent"
                                    visible: navPill.isSelected
                                }

                                Text {
                                    text: navPill.modelData.title
                                    font.pixelSize: 13
                                    font.weight: navPill.isSelected ? Font.DemiBold : Font.Normal
                                    color: navPill.isSelected ? "#09090b" : (pillMouse.containsMouse ? root.textPrimary : root.textSecondary)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: pillMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.currentPage = navPill.modelData.id;
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Sidebar Footer: Auto-save info and Advanced Settings button
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.borderSubtle
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 6
                            Layout.preferredHeight: 6
                            radius: 3
                            color: "#10b981"
                        }

                        Text {
                            text: KI18n.i18n("Changes apply immediately")
                            font.pixelSize: 10
                            color: root.textMuted
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    Rectangle {
                        id: classicBtn
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        radius: 6
                        color: classicMouse.containsMouse ? "#242736" : "#1a1c26"
                        border.color: root.borderCard
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: KI18n.i18n("Advanced Settings…")
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            color: classicMouse.containsMouse ? root.textPrimary : root.textSecondary
                        }

                        MouseArea {
                            id: classicMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.backend) {
                                    root.backend.openClassicSettings();
                                }
                            }
                        }
                    }
                }
            }
        }

        // ==========================================
        // RIGHT MAIN CONTENT AREA
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.bgContent

            ScrollView {
                id: mainScroll
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true

                ScrollBar.vertical: ScrollBar {
                    active: true
                    policy: ScrollBar.AsNeeded
                }

                ColumnLayout {
                    width: parent.width
                    spacing: 20

                    // Main padding container
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: contentCol.implicitHeight + 40

                        ColumnLayout {
                            id: contentCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: root.showSidebar ? 24 : 16
                            spacing: 20

                            // ----------------------------------------------------
                            // TAB: APPEARANCE
                            // ----------------------------------------------------
                            ColumnLayout {
                                visible: root.currentPage === "Appearance"
                                Layout.fillWidth: true
                                spacing: 20

                                // Top Header
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: KI18n.i18n("Appearance")
                                        font.pixelSize: 20
                                        font.weight: Font.Bold
                                        color: root.textPrimary
                                    }

                                    Text {
                                        text: KI18n.i18n("Fine-tune UI surfaces, contrast, and accent highlights")
                                        font.pixelSize: 12
                                        color: root.textSecondary
                                    }
                                }

                                // CARD 1: Surfaces & text
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: cardSurfacesLayout.implicitHeight + 28
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        id: cardSurfacesLayout
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 14

                                        Text {
                                            text: KI18n.i18n("Surfaces & text")
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            color: root.textPrimary
                                        }

                                        // Brightness Slider
                                        ModernSliderRow {
                                            label: KI18n.i18n("Brightness")
                                            from: 0
                                            to: 100
                                            value: root.backend ? root.backend.brightness : 42
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.brightness = val; }
                                            onResetClicked: { if (root.backend) root.backend.resetSetting("brightness"); }
                                        }

                                        // Contrast Slider
                                        ModernSliderRow {
                                            label: KI18n.i18n("Contrast")
                                            from: -100
                                            to: 100
                                            value: root.backend ? root.backend.contrast : -70
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.contrast = val; }
                                            onResetClicked: { if (root.backend) root.backend.resetSetting("contrast"); }
                                        }

                                        // Borders Slider
                                        ModernSliderRow {
                                            label: KI18n.i18n("Borders")
                                            from: 0
                                            to: 100
                                            value: root.backend ? root.backend.borders : 48
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.borders = val; }
                                            onResetClicked: { if (root.backend) root.backend.resetSetting("borders"); }
                                        }

                                        // Text Slider
                                        ModernSliderRow {
                                            label: KI18n.i18n("Text")
                                            from: 0
                                            to: 100
                                            value: root.backend ? root.backend.textBrightness : 51
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.textBrightness = val; }
                                            onResetClicked: { if (root.backend) root.backend.resetSetting("text"); }
                                        }

                                        // Panel gaps Slider
                                        ModernSliderRow {
                                            label: KI18n.i18n("Panel gaps")
                                            from: 0
                                            to: 40
                                            value: root.backend ? root.backend.panelGaps : 10
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.panelGaps = val; }
                                            onResetClicked: { if (root.backend) root.backend.resetSetting("panelGaps"); }
                                        }

                                        // Neutral tone Slider
                                        ModernSliderRow {
                                            label: KI18n.i18n("Neutral tone")
                                            from: -50
                                            to: 50
                                            value: root.backend ? root.backend.neutralTone : 0
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.neutralTone = val; }
                                            onResetClicked: { if (root.backend) root.backend.resetSetting("neutralTone"); }
                                        }
                                    }
                                }

                                // CARD 2: Accent color
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: cardAccentLayout.implicitHeight + 28
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        id: cardAccentLayout
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 16

                                        ColumnLayout {
                                            spacing: 3
                                            Text {
                                                text: KI18n.i18n("Accent color")
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: root.textPrimary
                                            }
                                            Text {
                                                text: KI18n.i18n("Choose primary highlight tone for selections, buttons, and badges")
                                                font.pixelSize: 11
                                                color: root.textSecondary
                                            }
                                        }

                                        // Row 1 Swatches
                                        RowLayout {
                                            spacing: 10
                                            Repeater {
                                                model: root.swatchesRow1
                                                delegate: ColorSwatchPill {
                                                    required property var modelData
                                                    swatchColor: modelData
                                                    isSelected: (root.backend && root.backend.accentColor.toLowerCase() === modelData.toLowerCase())
                                                    onSelected: function(col) {
                                                        if (root.backend) root.backend.accentColor = col;
                                                    }
                                                }
                                            }
                                        }

                                        // Row 2 Swatches
                                        RowLayout {
                                            spacing: 10
                                            Repeater {
                                                model: root.swatchesRow2
                                                delegate: ColorSwatchPill {
                                                    required property var modelData
                                                    swatchColor: modelData
                                                    isSelected: (root.backend && root.backend.accentColor.toLowerCase() === modelData.toLowerCase())
                                                    onSelected: function(col) {
                                                        if (root.backend) root.backend.accentColor = col;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // CARD 3: Property value colors
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: cardPropLayout.implicitHeight + 28
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        id: cardPropLayout
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 14

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Text {
                                                text: KI18n.i18n("Property value colors")
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: root.textPrimary
                                                Layout.fillWidth: true
                                            }

                                            // Suggest from accent button
                                            Rectangle {
                                                id: suggestBtn
                                                implicitWidth: suggestText.implicitWidth + 24
                                                implicitHeight: 28
                                                radius: 6
                                                color: suggestMouse.containsMouse ? "#272a38" : "#1e212d"
                                                border.color: root.borderSubtle
                                                border.width: 1

                                                RowLayout {
                                                    anchors.centerIn: parent
                                                    spacing: 6

                                                    Rectangle {
                                                        Layout.preferredWidth: 7
                                                        Layout.preferredHeight: 7
                                                        radius: 3.5
                                                        color: root.currentAccent
                                                    }

                                                    Text {
                                                        id: suggestText
                                                        text: KI18n.i18n("Suggest from accent")
                                                        font.pixelSize: 11
                                                        font.weight: Font.Medium
                                                        color: suggestMouse.containsMouse ? root.textPrimary : root.textSecondary
                                                    }
                                                }

                                                MouseArea {
                                                    id: suggestMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        if (root.backend) root.backend.suggestFromAccent();
                                                    }
                                                }
                                            }
                                        }

                                        // 4 Color property chips
                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: 2
                                            rowSpacing: 10
                                            columnSpacing: 16

                                            PropertyChip {
                                                title: KI18n.i18n("Normal")
                                                chipColor: root.backend ? root.backend.normalColor : "#d97706"
                                            }
                                            PropertyChip {
                                                title: KI18n.i18n("Animated")
                                                chipColor: root.backend ? root.backend.animatedColor : "#eab308"
                                            }
                                            PropertyChip {
                                                title: KI18n.i18n("Animated and modified")
                                                chipColor: root.backend ? root.backend.animModColor : "#fca5a5"
                                            }
                                            PropertyChip {
                                                title: KI18n.i18n("Modified")
                                                chipColor: root.backend ? root.backend.modifiedColor : "#f472b6"
                                            }
                                        }
                                    }
                                }
                            }

                            // ----------------------------------------------------
                            // TAB: PERFORMANCE
                            // ----------------------------------------------------
                            ColumnLayout {
                                visible: root.currentPage === "Performance"
                                Layout.fillWidth: true
                                spacing: 20

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: KI18n.i18n("Performance")
                                        font.pixelSize: 20
                                        font.weight: Font.Bold
                                        color: root.textPrimary
                                    }

                                    Text {
                                        text: KI18n.i18n("Hardware acceleration, memory caching, and playback pipelines")
                                        font.pixelSize: 12
                                        color: root.textSecondary
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: perfCardLayout.implicitHeight + 28
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        id: perfCardLayout
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 16

                                        // Hardware acceleration toggle
                                        RowLayout {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    text: KI18n.i18n("Hardware Decoding & GPU Acceleration")
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: root.textPrimary
                                                }
                                                Text {
                                                    text: KI18n.i18n("Enable GPU-accelerated video decoding (VAAPI / NVDEC)")
                                                    font.pixelSize: 11
                                                    color: root.textSecondary
                                                }
                                            }

                                            ModernSwitch {
                                                checked: root.backend ? root.backend.hwAccel : true
                                                activeColor: root.currentAccent
                                                onToggled: function(c) { if (root.backend) root.backend.hwAccel = c; }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: root.borderSubtle
                                        }

                                        // RAM Cache
                                        ModernSliderRow {
                                            label: KI18n.i18n("RAM Cache Limit (MB)")
                                            from: 512
                                            to: 8192
                                            value: root.backend ? root.backend.ramCache : 2048
                                            unit: " MB"
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.ramCache = val; }
                                            onResetClicked: { if (root.backend) root.backend.ramCache = 2048; }
                                        }

                                        // Processing Threads
                                        ModernSliderRow {
                                            label: KI18n.i18n("Processing Threads")
                                            from: 1
                                            to: 32
                                            value: root.backend ? root.backend.threads : 8
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.threads = val; }
                                            onResetClicked: { if (root.backend) root.backend.threads = 8; }
                                        }
                                    }
                                }
                            }

                            // ----------------------------------------------------
                            // TAB: TIMELINE
                            // ----------------------------------------------------
                            ColumnLayout {
                                visible: root.currentPage === "Timeline"
                                Layout.fillWidth: true
                                spacing: 20

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: KI18n.i18n("Timeline")
                                        font.pixelSize: 20
                                        font.weight: Font.Bold
                                        color: root.textPrimary
                                    }

                                    Text {
                                        text: KI18n.i18n("Timeline display, scrolling behavior, and snapping precision")
                                        font.pixelSize: 12
                                        color: root.textSecondary
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: timelineCardLayout.implicitHeight + 28
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        id: timelineCardLayout
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 16

                                        // Auto-scroll toggle
                                        RowLayout {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    text: KI18n.i18n("Autoscroll While Playing")
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: root.textPrimary
                                                }
                                                Text {
                                                    text: KI18n.i18n("Keep the playback playhead centered in view")
                                                    font.pixelSize: 11
                                                    color: root.textSecondary
                                                }
                                            }

                                            ModernSwitch {
                                                checked: root.backend ? root.backend.autoScroll : true
                                                activeColor: root.currentAccent
                                                onToggled: function(c) { if (root.backend) root.backend.autoScroll = c; }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: root.borderSubtle
                                        }

                                        // Track Height
                                        ModernSliderRow {
                                            label: KI18n.i18n("Default Track Height (px)")
                                            from: 30
                                            to: 120
                                            value: root.backend ? root.backend.trackHeight : 50
                                            unit: " px"
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.trackHeight = val; }
                                            onResetClicked: { if (root.backend) root.backend.trackHeight = 50; }
                                        }

                                        // Snap Distance
                                        ModernSliderRow {
                                            label: KI18n.i18n("Snap Distance (px)")
                                            from: 2
                                            to: 30
                                            value: root.backend ? root.backend.snapDistance : 10
                                            unit: " px"
                                            accentColor: root.currentAccent
                                            onSliderMoved: function(val) { if (root.backend) root.backend.snapDistance = val; }
                                            onResetClicked: { if (root.backend) root.backend.snapDistance = 10; }
                                        }
                                    }
                                }
                            }

                            // ----------------------------------------------------
                            // TAB: AI & PLUGINS
                            // ----------------------------------------------------
                            ColumnLayout {
                                visible: root.currentPage === "AI & Plugins"
                                Layout.fillWidth: true
                                spacing: 20

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: KI18n.i18n("AI & Plugins")
                                        font.pixelSize: 20
                                        font.weight: Font.Bold
                                        color: root.textPrimary
                                    }

                                    Text {
                                        text: KI18n.i18n("Neural acceleration, Segment Anything 2, RVM, and speech engines")
                                        font.pixelSize: 12
                                        color: root.textSecondary
                                    }
                                }

                                // SAM 2 Card
                                PluginStatusCard {
                                    title: KI18n.i18n("Segment Anything 2 (SAM 2)")
                                    description: KI18n.i18n("Neural video object segmentation and auto-tracking model")
                                    statusText: root.backend ? root.backend.sam2Status : "Ready"
                                    isHealthy: (root.backend ? root.backend.sam2Status : "") === "Ready"
                                }

                                // RVM Card
                                PluginStatusCard {
                                    title: KI18n.i18n("Robust Video Matting (RVM)")
                                    description: KI18n.i18n("Real-time human portrait green-screen free background matting")
                                    statusText: root.backend ? root.backend.rvmStatus : "Ready"
                                    isHealthy: true
                                }

                                // Whisper Card
                                PluginStatusCard {
                                    title: KI18n.i18n("OpenAI Whisper")
                                    description: KI18n.i18n("On-device speech-to-text automatic subtitle generator")
                                    statusText: root.backend ? root.backend.whisperStatus : "Active"
                                    isHealthy: true
                                }
                            }

                            // ----------------------------------------------------
                            // TAB: KEYBOARD SHORTCUTS
                            // ----------------------------------------------------
                            ColumnLayout {
                                visible: root.currentPage === "Keyboard Shortcuts"
                                Layout.fillWidth: true
                                spacing: 20

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: KI18n.i18n("Keyboard Shortcuts")
                                        font.pixelSize: 20
                                        font.weight: Font.Bold
                                        color: root.textPrimary
                                    }

                                    Text {
                                        text: KI18n.i18n("Quickly trigger editing actions with key combos")
                                        font.pixelSize: 12
                                        color: root.textSecondary
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 180
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: 12

                                        Text {
                                            text: KI18n.i18n("Customize shortcuts in the dedicated shortcut manager")
                                            font.pixelSize: 13
                                            color: root.textSecondary
                                            horizontalAlignment: Text.AlignHCenter
                                        }

                                        Rectangle {
                                            Layout.alignment: Qt.AlignHCenter
                                            implicitWidth: 160
                                            implicitHeight: 34
                                            radius: 6
                                            color: root.currentAccent

                                            Text {
                                                anchors.centerIn: parent
                                                text: KI18n.i18n("Open Shortcuts Editor")
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: "#09090b"
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (root.backend) root.backend.openClassicSettings();
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // ----------------------------------------------------
                            // TAB: PLAYBACK & AUDIO
                            // ----------------------------------------------------
                            ColumnLayout {
                                visible: root.currentPage === "Playback & Audio"
                                Layout.fillWidth: true
                                spacing: 20

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: KI18n.i18n("Playback & Audio")
                                        font.pixelSize: 20
                                        font.weight: Font.Bold
                                        color: root.textPrimary
                                    }

                                    Text {
                                        text: KI18n.i18n("Monitor refresh rate, audio scrubbing, and buffer latency")
                                        font.pixelSize: 12
                                        color: root.textSecondary
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 160
                                    radius: 10
                                    color: root.bgCard
                                    border.color: root.borderCard
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 14

                                        RowLayout {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    text: KI18n.i18n("Audio Scrubbing")
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: root.textPrimary
                                                }
                                                Text {
                                                    text: KI18n.i18n("Play responsive audio while dragging playhead")
                                                    font.pixelSize: 11
                                                    color: root.textSecondary
                                                }
                                            }

                                            ModernSwitch {
                                                checked: true
                                                activeColor: root.currentAccent
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 1
                                            color: root.borderSubtle
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    text: KI18n.i18n("Drop Frames On Slow Playback")
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    color: root.textPrimary
                                                }
                                                Text {
                                                    text: KI18n.i18n("Prioritize real-time audio synchronization")
                                                    font.pixelSize: 11
                                                    color: root.textSecondary
                                                }
                                            }

                                            ModernSwitch {
                                                checked: true
                                                activeColor: root.currentAccent
                                            }
                                        }
                                    }
                                }
                            }

                        } // contentCol
                    } // padding container
                } // ColumnLayout inside scrollview
            } // ScrollView
        } // Right Rectangle
    } // RowLayout

    // ==========================================
    // REUSABLE SUB-COMPONENTS
    // ==========================================

    component ModernSliderRow: RowLayout {
        id: sliderRow
        property string label: ""
        property int from: 0
        property int to: 100
        property int value: 50
        property string unit: ""
        property color accentColor: "#f59e0b"
        signal sliderMoved(int val)
        signal resetClicked()

        Layout.fillWidth: true
        spacing: 12

        Text {
            text: sliderRow.label
            font.pixelSize: 12
            color: "#9ca3af"
            Layout.preferredWidth: 110
            elide: Text.ElideRight
        }

        // Custom Slider Track
        Slider {
            id: customSlider
            Layout.fillWidth: true
            from: sliderRow.from
            to: sliderRow.to
            stepSize: 1
            value: sliderRow.value

            onMoved: {
                sliderRow.sliderMoved(Math.round(customSlider.value));
            }

            background: Rectangle {
                x: customSlider.leftPadding
                y: customSlider.topPadding + customSlider.availableHeight / 2 - height / 2
                implicitWidth: 200
                implicitHeight: 4
                width: customSlider.availableWidth
                height: 4
                radius: 2
                color: "#252736"

                Rectangle {
                    width: customSlider.visualPosition * parent.width
                    height: parent.height
                    color: sliderRow.accentColor
                    radius: 2
                }
            }

            handle: Rectangle {
                x: customSlider.leftPadding + customSlider.visualPosition * (customSlider.availableWidth - width)
                y: customSlider.topPadding + customSlider.availableHeight / 2 - height / 2
                implicitWidth: 14
                implicitHeight: 14
                radius: 7
                color: "#ffffff"
                border.color: sliderRow.accentColor
                border.width: 1
            }
        }

        // Value readout pill
        Rectangle {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 22
            radius: 4
            color: "#1d202b"
            border.color: "#212433"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: sliderRow.value + sliderRow.unit
                font.pixelSize: 11
                font.weight: Font.Medium
                color: "#f4f4f5"
            }
        }

        // Reset button
        Rectangle {
            id: resetBtn
            Layout.preferredWidth: 48
            Layout.preferredHeight: 22
            radius: 4
            color: resetMouse.containsMouse ? "#272a39" : "transparent"

            Text {
                anchors.centerIn: parent
                text: KI18n.i18n("Reset")
                font.pixelSize: 11
                color: resetMouse.containsMouse ? sliderRow.accentColor : "#6b7280"
            }

            MouseArea {
                id: resetMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: sliderRow.resetClicked()
            }
        }
    }

    component ColorSwatchPill: Rectangle {
        id: swatchItem
        property color swatchColor: "#ffffff"
        property bool isSelected: false
        signal selected(string hexColor)

        Layout.preferredWidth: 24
        Layout.preferredHeight: 24
        radius: 12
        color: "transparent"

        // Outer selection ring
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "transparent"
            border.color: swatchItem.isSelected ? "#ffffff" : "transparent"
            border.width: 2
            visible: swatchItem.isSelected
        }

        // Inner filled color circle
        Rectangle {
            anchors.centerIn: parent
            width: swatchItem.isSelected ? 16 : 22
            height: swatchItem.isSelected ? 16 : 22
            radius: width / 2
            color: swatchItem.swatchColor

            Behavior on width {
                NumberAnimation { duration: 100 }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                swatchItem.selected(swatchItem.swatchColor.toString());
            }
        }
    }

    component PropertyChip: Rectangle {
        id: propChip
        property string title: ""
        property color chipColor: "#ffffff"

        Layout.fillWidth: true
        Layout.preferredHeight: 42
        radius: 8
        color: "#1d202b"
        border.color: "#212433"
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                radius: 7
                color: propChip.chipColor
                border.color: "#ffffff"
                border.width: 1
            }

            Text {
                text: propChip.title
                font.pixelSize: 12
                font.weight: Font.Medium
                color: "#f4f4f5"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Text {
                text: propChip.chipColor.toString().toUpperCase()
                font.pixelSize: 11
                color: "#6b7280"
            }
        }
    }

    component PluginStatusCard: Rectangle {
        id: statusCard
        property string title: ""
        property string description: ""
        property string statusText: "Ready"
        property bool isHealthy: true

        Layout.fillWidth: true
        implicitHeight: statusCardCol.implicitHeight + 28
        radius: 10
        color: "#171922"
        border.color: "#262938"
        border.width: 1

        RowLayout {
            id: statusCardCol
            anchors.fill: parent
            anchors.margins: 18
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: statusCard.title
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: "#f4f4f5"
                }

                Text {
                    text: statusCard.description
                    font.pixelSize: 11
                    color: "#9ca3af"
                }
            }

            Rectangle {
                implicitWidth: statusTextItem.implicitWidth + 20
                implicitHeight: 26
                radius: 13
                color: statusCard.isHealthy ? "#064e3b" : "#451a03"
                border.color: statusCard.isHealthy ? "#10b981" : "#f59e0b"
                border.width: 1

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6

                    Rectangle {
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        radius: 3
                        color: statusCard.isHealthy ? "#34d399" : "#fbbf24"
                    }

                    Text {
                        id: statusTextItem
                        text: statusCard.statusText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        color: statusCard.isHealthy ? "#a7f3d0" : "#fed7aa"
                    }
                }
            }
        }
    }

    component ModernSwitch: Rectangle {
        id: modernSwitch
        property bool checked: false
        property color activeColor: "#f59e0b"
        signal toggled(bool c)

        implicitWidth: 38
        implicitHeight: 22
        radius: 11
        color: modernSwitch.checked ? modernSwitch.activeColor : "#272a39"

        Behavior on color {
            ColorAnimation { duration: 120 }
        }

        Rectangle {
            x: modernSwitch.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: modernSwitch.checked ? "#09090b" : "#9ca3af"

            Behavior on x {
                NumberAnimation { duration: 120 }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                modernSwitch.checked = !modernSwitch.checked;
                modernSwitch.toggled(modernSwitch.checked);
            }
        }
    }
}
