import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Pro: the command palette (Ctrl+P, or `:` in vim keys). Type a few letters
// and it offers the folders you go to most — ranked by Frecency — and every
// action the window knows, fuzzy-matched. A typed path (/…, ~/…) goes
// straight there. Enter runs the top row; ↑/↓ or Ctrl+J/K pick another.
Popup {
    id: root
    objectName: "commandPalette"

    // [{name, detail, shortcut, run}] from the window.
    property var actions: []
    // The window: navigate(path) is asked of it.
    signal navigateRequested(string path)

    parent: Overlay.overlay
    x: Math.round((parent ? parent.width - width : 0) / 2)
    y: 60
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(contentColumn.implicitHeight + topPadding + bottomPadding,
                     parent ? parent.height - 100 : 480)
    padding: 8
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    transformOrigin: Popup.Top
    background: OmCard { borderWidth: 1.5; borderOpacity: 0.7 }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.25) }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Colors.fadeInMs; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: Colors.popInScale; to: 1; duration: Colors.popInMs; easing.type: Easing.OutCubic }
        }
    }

    property string query: ""
    property int current: 0

    // The rows on offer: a typed path first, then folders and actions
    // interleaved by score — folders lead on an empty query.
    readonly property var rows: {
        const q = query.trim();
        const out = [];
        if (q.startsWith("/") || q.startsWith("~")) {
            const resolved = Platform.resolvePath(q, Platform.homePath());
            if (Platform.isDir(resolved))
                out.push({ kind: "folder", name: resolved, detail: qsTr("Go to this folder"),
                           shortcut: "", path: resolved, score: 1e9 });
        }
        for (const hit of Frecency.search(q, 12)) {
            out.push({ kind: "folder", name: hit.name, detail: hit.path, shortcut: "",
                       path: hit.path, score: q === "" ? 1e6 + hit.score : hit.score });
        }
        for (const action of root.actions) {
            if (action.available !== undefined && !action.available)
                continue;
            const score = q === "" ? 0 : Frecency.fuzzyScore(q, action.name + " " + (action.keywords || ""));
            if (score < 0)
                continue;
            // Actions compete with folders on the same scale: the match ×10.
            out.push({ kind: "action", name: action.name, detail: action.detail || "",
                       shortcut: action.shortcut || "", action: action,
                       score: q === "" ? -out.length : score * 10 + 5 });
        }
        out.sort((a, b) => b.score - a.score);
        return out.slice(0, 40);
    }

    onRowsChanged: current = 0

    function show() {
        query = "";
        field.text = "";
        current = 0;
        open();
    }

    function runRow(index) {
        const row = rows[index];
        if (!row)
            return;
        close();
        if (row.kind === "folder")
            root.navigateRequested(row.path);
        else
            row.action.run();
    }

    function move(delta) {
        if (rows.length === 0)
            return;
        current = (current + delta + rows.length) % rows.length;
        list.positionViewAtIndex(current, ListView.Contain);
    }

    onOpened: field.forceActiveFocus()

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 6

        TextField {
            id: field
            objectName: "paletteField"
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Colors.text
            font.pixelSize: 14
            placeholderText: qsTr("Jump to a folder or run an action…")
            selectByMouse: true
            background: Rectangle {
                radius: Colors.radius
                color: Colors.window
                border.color: Colors.accent
                border.width: 1
            }
            onTextEdited: root.query = text
            Keys.onPressed: event => {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
                if (event.key === Qt.Key_Down || (ctrl && event.key === Qt.Key_J)
                    || (ctrl && event.key === Qt.Key_N)) {
                    root.move(1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Up || (ctrl && event.key === Qt.Key_K)
                           || (ctrl && event.key === Qt.Key_P)) {
                    root.move(-1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.runRow(root.current);
                    event.accepted = true;
                }
            }
        }

        Text {
            textFormat: Text.PlainText
            visible: root.rows.length === 0
            Layout.fillWidth: true
            Layout.margins: 10
            text: root.query.trim() === ""
                  ? qsTr("Folders you visit show up here.")
                  : qsTr("Nothing matches “%1”").arg(root.query)
            color: Colors.textDim
            font.pixelSize: 12
        }

        ListView {
            id: list
            objectName: "paletteList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 380)
            visible: root.rows.length > 0
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: rowItem
                required property var modelData
                required property int index

                width: list.width
                height: 40
                radius: Colors.radius
                color: index === root.current ? Colors.selection
                     : rowMouse.containsMouse ? Colors.hover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Image {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        sourceSize: Qt.size(32, 32)
                        source: Colors.tint("image://fileicon/" + (rowItem.modelData.kind === "folder"
                                                                   ? "folder" : "bolt"),
                                            rowItem.index === root.current ? Colors.selectionText
                                                                           : Colors.accent)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: rowItem.modelData.name
                            color: rowItem.index === root.current ? Colors.selectionText : Colors.text
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: rowItem.modelData.detail
                            color: rowItem.index === root.current ? Colors.selectionText : Colors.textDim
                            opacity: 0.8
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        visible: text !== ""
                        text: rowItem.modelData.shortcut
                        color: rowItem.index === root.current ? Colors.selectionText : Colors.textDim
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.runRow(rowItem.index)
                }
            }
        }
    }
}
