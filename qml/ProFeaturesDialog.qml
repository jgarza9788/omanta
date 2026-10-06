import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime
import "ProFeatures.js" as ProFeatures

// The Pro Features panel (main menu → Pro Features), laid out like
// Preferences: the master switch first, then a card per feature from
// ProFeatures.js, most helpful first. The cards dim while the switch is off.
//
// The switch is sampled in onAboutToShow rather than bound, as in
// Preferences: toggling a Switch writes `checked` and would sever a binding.
OmDialog {
    id: root
    objectName: "proFeaturesDialog"

    anchors.centerIn: Overlay.overlay
    width: 560
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 80 : 640)
    modal: true
    // Nothing to lose here, so a click on the dimmed window closes it too.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Held by a property, not the default one: that would hand it to the
    // contentItem (a ScrollView here and there) before it lifts itself out.
    readonly property Item closeButton: DialogCloseButton { dialog: root }
    title: qsTr("Pro Features")
    padding: 24
    // Keep the cards centred while leaving room for the bar in the margin.
    leftPadding: rightPadding
    rightPadding: Math.max(24, scroller.ScrollBar.vertical.width + 12)
    topPadding: 12

    readonly property var features: ProFeatures.features

    onAboutToShow: {
        enableSwitch.checked = Settings.proFeatures;
        // A dialog that reopens mid-scroll looks broken.
        scroller.contentItem.contentY = 0;
    }

    component SectionTitle: Text {
        width: parent.width
        textFormat: Text.PlainText
        color: Colors.text
        font.pixelSize: 14
        font.bold: true
        topPadding: 16
    }

    component SectionCaption: Text {
        width: parent.width
        textFormat: Text.PlainText
        color: Colors.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        bottomPadding: 4
    }

    // A small rounded label: the status on each card.
    component Badge: Rectangle {
        property alias text: badgeText.text
        property color tone: Colors.accent

        implicitWidth: badgeText.implicitWidth + 12
        implicitHeight: 20
        radius: height / 2
        color: Qt.alpha(tone, 0.15)
        border.color: Qt.alpha(tone, 0.5)
        border.width: 1

        Text {
            id: badgeText
            anchors.centerIn: parent
            textFormat: Text.PlainText
            color: parent.tone
            font.pixelSize: 11
        }
    }

    contentItem: ScrollView {
        id: scroller

        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        // Place the bar outside the clipped content, within the dialog's
        // right margin. The cards then have equal space on both sides.
        ScrollBar.vertical.parent: scroller.parent
        ScrollBar.vertical.x: scroller.x + scroller.width + 6
        ScrollBar.vertical.y: scroller.y
        ScrollBar.vertical.height: scroller.height

        Column {
            width: scroller.availableWidth
            spacing: 8

            // The master switch, styled like a Preferences row.
            Rectangle {
                width: parent.width
                height: 52
                radius: Colors.radius
                color: Colors.window
                border.color: Settings.proFeatures ? Colors.accent : Colors.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 8

                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: qsTr("Enable Pro Features")
                        color: Colors.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Switch {
                        id: enableSwitch
                        objectName: "proFeaturesSwitch"
                        padding: 0
                        leftPadding: 12
                        spacing: 0
                        implicitHeight: 32
                        onToggled: Settings.proFeatures = checked
                    }
                }
            }

            SectionCaption {
                topPadding: 4
                text: qsTr("Advanced tools for heavy use. They stay hidden while this is off, so the everyday window doesn't change.")
            }

            SectionTitle { text: qsTr("Features") }

            Repeater {
                model: root.features

                delegate: Rectangle {
                    id: card
                    required property var modelData
                    required property int index

                    width: parent ? parent.width : 0
                    height: cardColumn.implicitHeight + 24
                    radius: Colors.radius
                    color: Colors.window
                    border.color: Colors.border
                    border.width: 1
                    opacity: Settings.proFeatures ? 1.0 : 0.55

                    Behavior on opacity { NumberAnimation { duration: Colors.fadeInMs } }

                    Column {
                        id: cardColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 6

                        RowLayout {
                            width: parent.width
                            spacing: 8

                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: (card.index + 1) + ". " + card.modelData.name
                                color: Colors.text
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Badge {
                                text: card.modelData.ready ? qsTr("Available") : qsTr("Planned")
                                tone: card.modelData.ready ? Colors.accent : Colors.textDim
                            }
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: card.modelData.summary
                            color: Colors.text
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            lineHeight: 1.15
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: card.modelData.group + "  ·  " + card.modelData.keys
                            color: Colors.textDim
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        // The feature's own switches, where it has any.
                        Repeater {
                            model: card.modelData.options || []

                            RowLayout {
                                id: optionRow
                                required property var modelData
                                width: parent.width
                                spacing: 8

                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    text: optionRow.modelData.label
                                    color: Colors.text
                                    font.pixelSize: 12
                                }

                                Switch {
                                    id: optionSwitch
                                    objectName: "option-" + optionRow.modelData.key
                                    padding: 0
                                    implicitHeight: 26
                                    enabled: Settings.proFeatures
                                    // Imperative, as above: a toggle severs a binding.
                                    Component.onCompleted: checked = Settings[optionRow.modelData.key]
                                    Connections {
                                        target: root
                                        function onAboutToShow() {
                                            optionSwitch.checked = Settings[optionRow.modelData.key];
                                        }
                                    }
                                    onToggled: Settings[optionRow.modelData.key] = checked
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
