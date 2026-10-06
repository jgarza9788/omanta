import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Pro: Find Duplicates. Groups of files with identical contents below a
// folder, the biggest waste first. In each group the first copy is kept and
// the rest are ticked; untick or tick to choose. Ticked copies go to the
// trash (undoable), or are replaced with hard links to the kept copy (same
// contents, one copy on disk — not undoable, so it asks first).
OmDialog {
    id: root
    objectName: "duplicatesDialog"

    anchors.centerIn: Overlay.overlay
    width: Math.min(760, Overlay.overlay ? Overlay.overlay.width - 40 : 760)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 60 : 640)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Find Duplicates")

    readonly property Item closeButton: DialogCloseButton { dialog: root }

    // Ticked paths (to remove), keyed by path.
    property var marked: ({})
    property string message: ""

    signal trashRequested(var paths)
    signal flashRequested(string text)

    DuplicateFinder {
        id: finder
        onGroupsChanged: root.resetMarks()
    }

    function search(path) {
        message = "";
        marked = ({});
        finder.start(path, hiddenBox.checked);
        open();
    }

    function resetMarks() {
        const next = {};
        for (const group of finder.groups)
            for (let i = 1; i < group.paths.length; ++i)
                next[group.paths[i]] = true;
        marked = next;
    }

    function toggle(path) {
        const next = Object.assign({}, marked);
        if (next[path])
            delete next[path];
        else
            next[path] = true;
        marked = next;
    }

    readonly property var markedPaths: Object.keys(marked)
    // A group with every copy ticked would lose the file entirely.
    readonly property bool wholeGroupMarked: finder.groups.some(g => g.paths.every(p => marked[p]))
    readonly property real markedBytes: {
        let total = 0;
        for (const group of finder.groups)
            for (const path of group.paths)
                if (marked[path])
                    total += group.size;
        return total;
    }

    function replaceWithLinks() {
        let replaced = 0;
        const errors = [];
        for (const group of finder.groups) {
            const keep = group.paths.find(p => !marked[p]);
            const extras = group.paths.filter(p => marked[p]);
            if (!keep || extras.length === 0)
                continue;
            replaced += finder.replaceWithLinks(keep, extras);
            errors.push(...finder.linkErrors());
        }
        root.message = errors.length > 0
            ? qsTr("Linked %1; %2 could not be: %3").arg(replaced).arg(errors.length).arg(errors[0])
            : qsTr("Replaced %1 copies with hard links.").arg(replaced);
        root.flashRequested(root.message);
        finder.start(finder.rootPath, hiddenBox.checked);
    }

    ConfirmDialog {
        id: linkConfirm
        message: qsTr("Replace %1 copies with hard links?").arg(root.markedPaths.length)
        detail: qsTr("Each ticked copy becomes another name for the kept file: editing one edits all of them. This can't be undone.")
        confirmText: qsTr("Replace with Links")
        onConfirmed: root.replaceWithLinks()
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: finder.rootPath
                color: Colors.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }
            CheckBox {
                id: hiddenBox
                text: qsTr("Include hidden files")
                font.pixelSize: 12
                onToggled: finder.start(finder.rootPath, checked)
            }
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: finder.running ? (finder.status || qsTr("Looking…"))
                  : finder.error !== "" ? finder.error
                  : finder.groups.length === 0 ? qsTr("No duplicates here.")
                  : qsTr("%1 groups · %2 could be freed").arg(finder.groups.length)
                                                       .arg(Platform.formatSize(finder.wastedBytes))
            color: finder.error !== "" ? Colors.error : Colors.textDim
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Colors.radius
            color: Colors.window
            border.color: Colors.border
            border.width: 1
            clip: true

            BusyIndicator {
                anchors.centerIn: parent
                running: finder.running
                visible: running
            }

            ListView {
                id: groupList
                objectName: "duplicateGroups"
                anchors.fill: parent
                anchors.margins: 6
                visible: !finder.running
                clip: true
                spacing: 8
                model: finder.groups
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Column {
                    id: group
                    required property var modelData
                    width: groupList.width
                    spacing: 2

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("%1 copies of %2").arg(group.modelData.paths.length)
                                                     .arg(Platform.formatSize(group.modelData.size))
                        color: Colors.accent
                        font.pixelSize: 12
                        font.bold: true
                        bottomPadding: 2
                    }

                    Repeater {
                        model: group.modelData.paths
                        Rectangle {
                            id: copyRow
                            required property string modelData
                            width: group.width
                            height: 26
                            radius: 4
                            color: root.marked[modelData] ? Qt.alpha(Colors.error, 0.1)
                                 : copyMouse.containsMouse ? Colors.hover : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 8
                                spacing: 8
                                CheckBox {
                                    padding: 0
                                    checked: !!root.marked[copyRow.modelData]
                                    onToggled: root.toggle(copyRow.modelData)
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    text: copyRow.modelData.startsWith(finder.rootPath + "/")
                                          ? copyRow.modelData.slice(finder.rootPath.length + 1)
                                          : copyRow.modelData
                                    color: root.marked[copyRow.modelData] ? Colors.textDim : Colors.text
                                    font.pixelSize: 12
                                    font.strikeout: !!root.marked[copyRow.modelData]
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    text: root.marked[copyRow.modelData] ? qsTr("remove") : qsTr("keep")
                                    color: root.marked[copyRow.modelData] ? Colors.error : Colors.accent
                                    font.pixelSize: 11
                                }
                            }
                            MouseArea {
                                id: copyMouse
                                anchors.fill: parent
                                anchors.leftMargin: 30
                                hoverEnabled: true
                                onClicked: root.toggle(copyRow.modelData)
                            }
                        }
                    }
                }
            }
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: root.wholeGroupMarked
            text: qsTr("Every copy in a group is ticked — keep at least one.")
            color: Colors.error
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: root.markedPaths.length > 0
                      ? qsTr("%1 ticked · %2").arg(root.markedPaths.length).arg(Platform.formatSize(root.markedBytes))
                      : ""
                color: Colors.textDim
                font.pixelSize: 12
            }
            OmButton {
                text: qsTr("Replace with Links…")
                enabled: root.markedPaths.length > 0 && !root.wholeGroupMarked && !finder.running
                onClicked: linkConfirm.open()
            }
            OmButton {
                primary: true
                text: qsTr("Move to Trash")
                enabled: root.markedPaths.length > 0 && !root.wholeGroupMarked && !finder.running
                onClicked: {
                    root.trashRequested(root.markedPaths);
                    root.close();
                }
            }
        }
    }
}
