import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Pro: permissions and owner for many files at once. Each box has three
// states — leave as it is (partly filled), turn on (ticked), turn off
// (empty) — so a mixed selection keeps whatever nobody asked to change.
// Through every subfolder if asked, and only for files or only for folders.
OmDialog {
    id: root
    objectName: "batchPermissionsDialog"

    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 40 : 520)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Change Permissions")

    readonly property Item closeButton: DialogCloseButton { dialog: root }

    property var paths: []
    signal flashRequested(string text)

    BatchPermissions {
        id: batch
        onFinished: {
            root.flashRequested(batch.failedCount > 0
                ? qsTr("Changed %1; %2 failed — %3").arg(batch.changedCount).arg(batch.failedCount)
                                                     .arg(batch.errors[0] || "")
                : qsTr("Changed %1 items").arg(batch.changedCount));
            if (batch.failedCount === 0)
                root.close();
        }
    }

    function askAbout(selection) {
        paths = selection;
        for (const box of [ur, uw, ux, gr, gw, gx, or_, ow, ox])
            box.checkState = Qt.PartiallyChecked;
        recursiveBox.checked = false;
        scopeCombo.currentIndex = 0;
        ownerField.text = "";
        groupField.text = "";
        open();
    }

    function bits() {
        let set = 0, clear = 0;
        for (const box of [ur, uw, ux, gr, gw, gx, or_, ow, ox]) {
            if (box.checkState === Qt.Checked) set |= box.bit;
            else if (box.checkState === Qt.Unchecked) clear |= box.bit;
        }
        return { set: set, clear: clear };
    }

    // Tri-state: partly = leave, ticked = on, empty = off; clicks cycle.
    component BitBox: CheckBox {
        property int bit: 0
        tristate: true
        checkState: Qt.PartiallyChecked
        nextCheckState: function() {
            return checkState === Qt.PartiallyChecked ? Qt.Checked
                 : checkState === Qt.Checked ? Qt.Unchecked : Qt.PartiallyChecked;
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: root.paths.length === 1 ? Platform.baseName(root.paths[0])
                                          : qsTr("%1 items").arg(root.paths.length)
            color: Colors.text
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideMiddle
        }

        GridLayout {
            columns: 4
            columnSpacing: 20
            rowSpacing: 2

            Text { textFormat: Text.PlainText; text: ""; Layout.preferredWidth: 70 }
            Text { textFormat: Text.PlainText; text: qsTr("Read"); color: Colors.textDim; font.pixelSize: 12 }
            Text { textFormat: Text.PlainText; text: qsTr("Write"); color: Colors.textDim; font.pixelSize: 12 }
            Text { textFormat: Text.PlainText; text: qsTr("Execute / Enter"); color: Colors.textDim; font.pixelSize: 12 }

            Text { textFormat: Text.PlainText; text: qsTr("Owner"); color: Colors.text; font.pixelSize: 12 }
            BitBox { id: ur; bit: 0o400 }
            BitBox { id: uw; bit: 0o200 }
            BitBox { id: ux; bit: 0o100 }

            Text { textFormat: Text.PlainText; text: qsTr("Group"); color: Colors.text; font.pixelSize: 12 }
            BitBox { id: gr; bit: 0o040 }
            BitBox { id: gw; bit: 0o020 }
            BitBox { id: gx; bit: 0o010 }

            Text { textFormat: Text.PlainText; text: qsTr("Others"); color: Colors.text; font.pixelSize: 12 }
            BitBox { id: or_; bit: 0o004 }
            BitBox { id: ow; bit: 0o002 }
            BitBox { id: ox; bit: 0o001 }
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: qsTr("A partly filled box leaves that permission as it is on each item.")
            color: Colors.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        RowLayout {
            spacing: 12
            CheckBox {
                id: recursiveBox
                text: qsTr("Include everything inside folders")
                font.pixelSize: 12
            }
            OmComboBox {
                id: scopeCombo
                Layout.preferredWidth: 150
                model: [qsTr("Files and folders"), qsTr("Files only"), qsTr("Folders only")]
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 10
            rowSpacing: 6
            Text { textFormat: Text.PlainText; text: qsTr("New owner"); color: Colors.text; font.pixelSize: 12 }
            TextField {
                id: ownerField
                Layout.fillWidth: true
                placeholderText: qsTr("leave unchanged")
                font.pixelSize: 12
                color: Colors.text
            }
            Text { textFormat: Text.PlainText; text: qsTr("New group"); color: Colors.text; font.pixelSize: 12 }
            TextField {
                id: groupField
                Layout.fillWidth: true
                placeholderText: qsTr("leave unchanged")
                font.pixelSize: 12
                color: Colors.text
            }
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: batch.failedCount > 0 && !batch.running
            text: batch.errors.join("\n")
            color: Colors.error
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            maximumLineCount: 6
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            BusyIndicator {
                running: batch.running
                visible: running
                Layout.preferredHeight: 24
                Layout.preferredWidth: 24
            }
            OmButton {
                text: qsTr("Cancel")
                onClicked: { batch.cancel(); root.close(); }
            }
            OmButton {
                primary: true
                text: qsTr("Apply")
                enabled: !batch.running && root.paths.length > 0
                onClicked: {
                    const b = root.bits();
                    batch.apply(root.paths, b.set, b.clear, recursiveBox.checked,
                                ["all", "files", "folders"][scopeCombo.currentIndex],
                                ownerField.text, groupField.text);
                }
            }
        }
    }
}
