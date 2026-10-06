import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime
import "Keymap.js" as Keymap

// Pro: the keymap editor. Every single-key vim action, with the key that
// triggers it; click a key and press another to move the action there.
// Saved to Settings.vimKeyRemap on top of the built-in keys — Reset puts
// them all back. Ctrl/Alt/F-key shortcuts are not remappable here.
OmDialog {
    id: root
    objectName: "keymapEditor"

    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 40 : 520)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 60 : 640)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Edit Vim Keys")

    readonly property Item closeButton: DialogCloseButton { dialog: root }

    // The working copy: action → key, written to Settings on every change.
    property var remap: ({})
    // The action waiting for its new key ("" when none).
    property string capturing: ""

    onAboutToShow: {
        remap = Keymap.parseRemap(Settings.vimKeyRemap);
        capturing = "";
    }

    function keyFor(action) { return remap[action] || action; }

    // Which other action already answers to `key`, or "".
    function clashFor(action, key) {
        for (const row of Keymap.vimActions)
            if (row[0] !== action && keyFor(row[0]) === key)
                return row[1];
        return "";
    }

    function assign(action, key) {
        const next = Object.assign({}, remap);
        if (key === action)
            delete next[action];
        else
            next[action] = key;
        remap = next;
        Settings.vimKeyRemap = Keymap.formatRemap(next);
        capturing = "";
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: qsTr("Click a key, then press the new one. Esc cancels. Two actions can't share a key.")
            color: Colors.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        ListView {
            id: list
            objectName: "keymapList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Keymap.vimActions
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: actionRow
                required property var modelData
                readonly property string action: modelData[0]
                readonly property bool changed: root.keyFor(action) !== action
                width: list.width
                height: 32
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    spacing: 10

                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: actionRow.modelData[1]
                        color: Colors.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Text {
                        textFormat: Text.PlainText
                        visible: actionRow.changed
                        text: qsTr("was %1").arg(actionRow.action)
                        color: Colors.textDim
                        font.pixelSize: 11
                    }

                    Rectangle {
                        id: keyCap
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 26
                        radius: 4
                        color: root.capturing === actionRow.action ? Qt.alpha(Colors.accent, 0.2)
                             : capMouse.containsMouse ? Colors.hover : Colors.window
                        border.color: root.capturing === actionRow.action || actionRow.changed
                                      ? Colors.accent : Colors.border
                        border.width: 1
                        focus: root.capturing === actionRow.action

                        Text {
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            text: root.capturing === actionRow.action ? qsTr("press…")
                                                                      : root.keyFor(actionRow.action)
                            color: Colors.text
                            font.pixelSize: 13
                            font.family: "monospace"
                        }

                        MouseArea {
                            id: capMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.capturing = actionRow.action;
                                keyCap.forceActiveFocus();
                            }
                        }

                        Keys.onPressed: event => {
                            event.accepted = true;
                            if (event.key === Qt.Key_Escape) {
                                root.capturing = "";
                                return;
                            }
                            const key = event.text;
                            if (key.length !== 1 || key.charCodeAt(0) <= 0x20
                                || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)))
                                return;
                            const clash = root.clashFor(actionRow.action, key);
                            if (clash !== "") {
                                clashNote.text = qsTr("“%1” already does: %2").arg(key).arg(clash);
                                return;
                            }
                            clashNote.text = "";
                            root.assign(actionRow.action, key);
                        }
                    }
                }
            }
        }

        Text {
            id: clashNote
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: text !== ""
            color: Colors.error
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            OmButton {
                text: qsTr("Reset All")
                enabled: Object.keys(root.remap).length > 0
                onClicked: {
                    root.remap = ({});
                    Settings.vimKeyRemap = "";
                    clashNote.text = "";
                }
            }
            OmButton {
                primary: true
                text: qsTr("Done")
                onClicked: root.close()
            }
        }
    }
}
