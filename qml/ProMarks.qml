import QtQuick
import Omanta.Runtime

// Pro: what a file row carries beside its name — one dot per coloured tag,
// then its git status as a small letter (M modified, A added, ? untracked,
// I ignored, ! conflict). Invisible, and zero-width, while pro features are
// off or there is nothing to show.
Row {
    id: root

    required property var tab
    required property string name
    required property string filePath
    property bool selected: false
    // The grid's corner badge: dots only, on a backing chip.
    property bool compact: false

    readonly property bool pro: Settings.proFeatures
    readonly property var tags: pro && Platform.isLocal(filePath)
                                ? (Tags.revision, Tags.tagsFor(filePath)) : []
    readonly property string gitState: pro && tab && tab.git && tab.git.enabled
                                       ? (tab.git.revision, tab.git.statusOf(name)) : ""

    readonly property var gitLetters: ({ modified: "M", added: "A", untracked: "?",
                                         ignored: "I", conflict: "!" })

    visible: tags.length > 0 || gitState !== ""
    width: visible ? implicitWidth : 0
    spacing: 3

    Repeater {
        model: root.tags

        Rectangle {
            required property string modelData
            anchors.verticalCenter: parent.verticalCenter
            width: root.compact ? 10 : 8
            height: width
            radius: width / 2
            color: Tags.colorFor(modelData)
            border.color: root.compact ? Colors.window : "transparent"
            border.width: root.compact ? 1.5 : 0
        }
    }

    Rectangle {
        visible: root.gitState !== ""
        anchors.verticalCenter: parent.verticalCenter
        width: gitLabel.implicitWidth + 8
        height: 15
        radius: 3
        color: root.gitState === "conflict" ? Qt.alpha(Colors.error, 0.25)
             : root.gitState === "ignored" ? Qt.alpha(Colors.textDim, 0.15)
             : Qt.alpha(Colors.accent, root.selected ? 0.35 : 0.18)

        Text {
            id: gitLabel
            textFormat: Text.PlainText
            anchors.centerIn: parent
            text: root.gitLetters[root.gitState] || ""
            color: root.gitState === "conflict" ? Colors.error
                 : root.selected ? Colors.selectionText
                 : root.gitState === "ignored" ? Colors.textDim : Colors.accent
            font.pixelSize: 10
            font.bold: true
            font.family: "monospace"
        }
    }
}
