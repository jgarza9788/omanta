import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Pro: the terminal pane (F4), docked under the files. Commands run in the
// folder the current tab shows and print here; `cd` moves the tab. It runs
// commands rather than emulating a terminal, so for vim, htop and the like
// the ⧉ button opens a real terminal in the same folder.
Rectangle {
    id: root
    objectName: "terminalPane"

    property var tab: null
    readonly property string folder: tab ? tab.path : ""
    readonly property alias session: shell

    signal closeRequested()
    // Esc in the input hands the keys back to the files, pane still open.
    signal focusReleased()

    color: Colors.chrome

    function focusInput() { input.forceActiveFocus(); }

    ShellSession {
        id: shell
        directory: Platform.isLocal(root.folder) ? root.folder : ""
        onChangeDirectoryRequested: path => {
            if (root.tab)
                root.tab.navigate(path);
        }
    }

    // History browsing with ↑/↓: an index from the end, -1 = the live line.
    property int historyIndex: -1

    Rectangle {
        width: parent.width
        height: 1
        color: Colors.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 1
        spacing: 0

        // Title strip: where commands run, and the pane's buttons.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            spacing: 6

            Text {
                textFormat: Text.PlainText
                text: "❯_"
                color: Colors.accent
                font.pixelSize: 12
                font.family: "monospace"
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: shell.directory !== "" ? shell.directory
                                             : qsTr("Commands run in folders on this computer")
                color: Colors.textDim
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
            ToolbarButton {
                symbol: "■"
                symbolSize: 10
                tip: qsTr("Stop the running command")
                visible: shell.running
                onTriggered: shell.stop()
            }
            ToolbarButton {
                symbol: "⌫"
                symbolSize: 12
                tip: qsTr("Clear")
                onTriggered: shell.clear()
            }
            ToolbarButton {
                glyph: "terminal"
                tip: qsTr("Open a full terminal here")
                enabled: shell.directory !== ""
                onTriggered: Platform.openTerminal(shell.directory)
            }
            ToolbarButton {
                symbol: "✕"
                symbolSize: 11
                tip: qsTr("Close (F4)")
                onTriggered: root.closeRequested()
            }
        }

        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            TextArea {
                id: outputArea
                objectName: "terminalOutput"
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: shell.output
                color: Colors.text
                font.family: "monospace"
                font.pixelSize: 12
                background: Rectangle { color: Colors.window }
                leftPadding: 10
                rightPadding: 10
                // Follow the output to its end.
                onTextChanged: cursorPosition = length
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Colors.window
            border.color: input.activeFocus ? Colors.accent : Colors.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 6

                Text {
                    textFormat: Text.PlainText
                    text: shell.running ? "…" : "$"
                    color: Colors.accent
                    font.family: "monospace"
                    font.pixelSize: 12
                }

                TextField {
                    id: input
                    objectName: "terminalInput"
                    Layout.fillWidth: true
                    background: null
                    color: Colors.text
                    font.family: "monospace"
                    font.pixelSize: 12
                    enabled: !shell.running
                    placeholderText: qsTr("Type a command — ls, git status, cd ..")
                    onAccepted: {
                        shell.run(text);
                        text = "";
                        root.historyIndex = -1;
                    }
                    Keys.onUpPressed: {
                        const h = shell.history;
                        if (h.length === 0)
                            return;
                        root.historyIndex = Math.min(h.length - 1, root.historyIndex + 1);
                        text = h[h.length - 1 - root.historyIndex];
                    }
                    Keys.onDownPressed: {
                        const h = shell.history;
                        root.historyIndex = Math.max(-1, root.historyIndex - 1);
                        text = root.historyIndex < 0 ? "" : h[h.length - 1 - root.historyIndex];
                    }
                    Keys.onEscapePressed: root.focusReleased()
                }
            }
        }
    }
}
