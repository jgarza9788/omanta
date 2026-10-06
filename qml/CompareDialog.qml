import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Pro: compare the split view's two panes (Ctrl+Shift+C). Lists what is
// only on one side, newer on one side, or different, through every
// subfolder; tick rows and copy them across, or mirror one side onto the
// other. Every copy goes through FileOperations, so it shows in the progress
// popover and Ctrl+Z undoes it.
OmDialog {
    id: root
    objectName: "compareDialog"

    anchors.centerIn: Overlay.overlay
    width: Math.min(760, Overlay.overlay ? Overlay.overlay.width - 40 : 760)
    height: Math.min(620, Overlay.overlay ? Overlay.overlay.height - 60 : 620)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Compare Panes")

    readonly property Item closeButton: DialogCloseButton { dialog: root }

    property alias leftPath: compare.leftPath
    property alias rightPath: compare.rightPath
    // Which rows are ticked, by relative path.
    property var checked: ({})
    property bool showSame: false

    // Mirroring trashes the other side's extras; the window confirms.
    signal mirrorRequested(var copyPaths, string copyDestinationRoot, var trashPaths)

    FolderCompare { id: compare }

    function compareNow(left, right) {
        compare.leftPath = left;
        compare.rightPath = right;
        checked = ({});
        compare.start();
        open();
    }

    readonly property var visibleEntries: compare.entries.filter(e => showSame || e.state !== "same")
    readonly property int checkedCount: Object.keys(checked).length

    readonly property var stateLabels: ({
        onlyLeft: qsTr("Only on the left"), onlyRight: qsTr("Only on the right"),
        newerLeft: qsTr("Newer on the left"), newerRight: qsTr("Newer on the right"),
        differ: qsTr("Different size"), same: qsTr("Same")
    })
    readonly property var stateArrows: ({
        onlyLeft: "◀", onlyRight: "▶", newerLeft: "◀", newerRight: "▶", differ: "≠", same: "="
    })

    function toggle(path) {
        const next = Object.assign({}, checked);
        if (next[path])
            delete next[path];
        else
            next[path] = true;
        checked = next;
    }

    function checkAll(states) {
        const next = {};
        for (const entry of compare.entries)
            if (states.indexOf(entry.state) >= 0)
                next[entry.path] = true;
        checked = next;
    }

    function join(base, rel) { return base.endsWith("/") ? base + rel : base + "/" + rel; }
    function parentOf(rel) {
        const cut = rel.lastIndexOf("/");
        return cut < 0 ? "" : rel.slice(0, cut);
    }

    // Copies the ticked rows that exist on `fromLeft`'s side to the other,
    // replacing what's there: grouped by destination folder, one operation
    // each.
    function copyChecked(fromLeft) {
        const from = fromLeft ? compare.leftPath : compare.rightPath;
        const to = fromLeft ? compare.rightPath : compare.leftPath;
        const byFolder = {};
        for (const entry of compare.entries) {
            if (!checked[entry.path])
                continue;
            // A size of -1 means the entry isn't on that side at all.
            if ((fromLeft ? entry.leftSize : entry.rightSize) < 0)
                continue;
            const destination = join(to, parentOf(entry.path)).replace(/\/$/, "");
            (byFolder[destination] = byFolder[destination] || []).push(join(from, entry.path));
        }
        let operations = 0;
        for (const destination in byFolder) {
            FileOperations.copy(byFolder[destination], destination, FileOperations.Replace);
            ++operations;
        }
        return operations;
    }

    // Left becomes right's twin: copy what's missing or newer, and the
    // window confirms trashing what exists only on the right.
    function mirror(fromLeft) {
        const copyStates = fromLeft ? ["onlyLeft", "newerLeft", "differ"]
                                    : ["onlyRight", "newerRight", "differ"];
        const extraState = fromLeft ? "onlyRight" : "onlyLeft";
        const from = fromLeft ? compare.leftPath : compare.rightPath;
        const to = fromLeft ? compare.rightPath : compare.leftPath;
        const copies = [];
        const extras = [];
        for (const entry of compare.entries) {
            if (copyStates.indexOf(entry.state) >= 0)
                copies.push(entry.path);
            else if (entry.state === extraState)
                extras.push(join(to, entry.path));
        }
        root.mirrorRequested(copies.map(rel => ({ source: join(from, rel),
                                                  destination: join(to, parentOf(rel)).replace(/\/$/, "") })),
                             to, extras);
    }

    contentItem: ColumnLayout {
        spacing: 10

        // The two roots, left ◀ ▶ right.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: compare.leftPath
                color: Colors.text
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
            Text { textFormat: Text.PlainText; text: "⇆"; color: Colors.accent; font.pixelSize: 14 }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                horizontalAlignment: Text.AlignRight
                text: compare.rightPath
                color: Colors.text
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }

        // Counts per state, and the filters that tick them.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: ["onlyLeft", "newerLeft", "differ", "newerRight", "onlyRight", "same"]
                Rectangle {
                    required property string modelData
                    readonly property int count: compare.counts[modelData] || 0
                    width: chipLabel.implicitWidth + 16
                    height: 24
                    radius: 12
                    color: chipMouse.containsMouse ? Colors.hover : "transparent"
                    border.color: count > 0 && modelData !== "same" ? Colors.accent : Colors.border
                    border.width: 1
                    opacity: count > 0 ? 1 : 0.5
                    Text {
                        id: chipLabel
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        text: root.stateArrows[modelData] + " " + root.stateLabels[modelData] + "  " + count
                        color: Colors.text
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData === "same")
                                root.showSame = !root.showSame;
                            else
                                root.checkAll([modelData]);
                        }
                    }
                    ToolTip.visible: chipMouse.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: modelData === "same" ? qsTr("Show or hide identical files")
                                                       : qsTr("Tick all of these")
                }
            }
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
                running: compare.running
                visible: running
            }

            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: !compare.running && (compare.error !== "" || root.visibleEntries.length === 0)
                text: compare.error !== "" ? compare.error
                      : compare.entries.length === 0 ? qsTr("Both folders are empty.")
                      : qsTr("The two folders match.")
                color: compare.error !== "" ? Colors.error : Colors.textDim
                font.pixelSize: 13
            }

            ListView {
                id: entryList
                objectName: "compareList"
                anchors.fill: parent
                anchors.margins: 4
                visible: !compare.running
                clip: true
                model: root.visibleEntries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: entryRow
                    required property var modelData
                    width: entryList.width
                    height: 28
                    radius: 4
                    color: root.checked[modelData.path] ? Qt.alpha(Colors.accent, 0.15)
                         : entryMouse.containsMouse ? Colors.hover : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 8
                        spacing: 8

                        CheckBox {
                            Layout.preferredWidth: 24
                            padding: 0
                            checked: !!root.checked[entryRow.modelData.path]
                            onToggled: root.toggle(entryRow.modelData.path)
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.preferredWidth: 18
                            text: root.stateArrows[entryRow.modelData.state]
                            color: entryRow.modelData.state === "same" ? Colors.textDim : Colors.accent
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: entryRow.modelData.path + (entryRow.modelData.isDir ? "/" : "")
                            color: Colors.text
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                        Text {
                            textFormat: Text.PlainText
                            text: root.stateLabels[entryRow.modelData.state]
                            color: Colors.textDim
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        id: entryMouse
                        anchors.fill: parent
                        anchors.leftMargin: 34
                        hoverEnabled: true
                        onClicked: root.toggle(entryRow.modelData.path)
                    }
                }
            }
        }

        Text {
            textFormat: Text.PlainText
            visible: compare.truncated
            Layout.fillWidth: true
            text: qsTr("Very large folders: only the first %1 entries were compared.").arg(50000)
            color: Colors.error
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            OmButton {
                text: qsTr("Copy ▶ Right")
                enabled: root.checkedCount > 0 && !compare.running
                onClicked: { root.copyChecked(true); root.close(); }
            }
            OmButton {
                text: qsTr("◀ Copy Left")
                enabled: root.checkedCount > 0 && !compare.running
                onClicked: { root.copyChecked(false); root.close(); }
            }
            Item { Layout.fillWidth: true }
            OmButton {
                text: qsTr("Mirror Left ▶ Right…")
                enabled: compare.entries.length > 0 && !compare.running
                onClicked: root.mirror(true)
            }
            OmButton {
                text: qsTr("Compare Again")
                enabled: !compare.running
                onClicked: { root.checked = ({}); compare.start(); }
            }
        }
    }
}
