import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Asked before an operation starts, once, for the whole set.
//
// "Keep both" is the default and the first button: it is the only choice that
// cannot lose data, and it should be the one a hurried Return keypress picks.
OmDialog {
    id: root

    property var conflicting: []

    signal chosen(int policy)

    anchors.centerIn: Overlay.overlay
    width: 460
    modal: true
    closePolicy: Popup.CloseOnEscape

    function askAbout(names) {
        conflicting = names;
        open();
    }

    function choose(policy) {
        root.chosen(policy);
        root.close();
    }

    // The button Return acts on. Keep both comes first, so Return alone is
    // the safe answer.
    property Item current: keepBothButton

    function select(button) {
        if (!visible)
            return;
        current = button;
        button.forceActiveFocus(Qt.TabFocusReason);
        // Reopening, the popup restores focus to it with a non-keyboard
        // reason first, and that would hide the focus ring.
        button.focusReason = Qt.TabFocusReason;
    }

    // The arrows walk the buttons as they sit on screen, which the button
    // box arranges by role (Skip · Replace · Keep both), not as declared.
    function step(by) {
        const row = [keepBothButton, skipButton, replaceButton].sort((a, b) => a.x - b.x);
        const at = row.indexOf(current);
        select(row[Math.max(0, Math.min(row.length - 1, at + by))]);
    }

    onOpened: select(keepBothButton)

    // While this is open, the keys belong to it. A menu or picker that
    // closes as this opens hands the focus back to the view behind, where the
    // arrows would move the selection instead; take it straight back.
    Connections {
        target: root.visible ? root.contentItem.Window.window : null
        function onActiveFocusItemChanged() {
            if (!root.current.activeFocus)
                Qt.callLater(root.select, root.current);
        }
    }

    Shortcut { enabled: root.visible; sequence: "K"; onActivated: root.choose(FileOperations.RenameNew) }
    Shortcut { enabled: root.visible; sequence: "S"; onActivated: root.choose(FileOperations.Skip) }
    Shortcut { enabled: root.visible; sequence: "R"; onActivated: root.choose(FileOperations.Replace) }
    Shortcut { enabled: root.visible; sequences: ["Left", "H"]; onActivated: root.step(-1) }
    Shortcut { enabled: root.visible; sequences: ["Right", "L"]; onActivated: root.step(1) }
    Shortcut { enabled: root.visible; sequences: ["Return", "Enter"]; onActivated: root.current.clicked() }

    footer: OmButtonBox {
        OmButton {
            id: keepBothButton
            objectName: "conflictKeepBoth"
            text: qsTr("Keep both")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onActiveFocusChanged: if (activeFocus) root.current = keepBothButton
            onClicked: root.choose(FileOperations.RenameNew)
        }
        OmButton {
            id: skipButton
            objectName: "conflictSkip"
            text: qsTr("Skip")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onActiveFocusChanged: if (activeFocus) root.current = skipButton
            onClicked: root.choose(FileOperations.Skip)
        }
        OmButton {
            id: replaceButton
            objectName: "conflictReplace"
            text: qsTr("Replace")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onActiveFocusChanged: if (activeFocus) root.current = replaceButton
            onClicked: root.choose(FileOperations.Replace)
        }
    }

    Column {
        width: parent.width
        spacing: 8

        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: root.conflicting.length === 1
                  ? qsTr("“%1” already exists here.").arg(root.conflicting[0])
                  : qsTr("%1 items already exist here.").arg(root.conflicting.length)
            color: Colors.text
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }

        Text {
            textFormat: Text.PlainText
            width: parent.width
            visible: root.conflicting.length > 1
            text: root.conflicting.slice(0, 6).join(", ")
                  + (root.conflicting.length > 6 ? "…" : "")
            color: Colors.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: qsTr("K keep both · S skip · R replace · Esc cancel. Replacing cannot be undone.")
            color: Colors.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
}
