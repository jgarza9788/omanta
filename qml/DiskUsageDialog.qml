import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Pro: the disk usage map. One box per item in the folder, sized by the
// space it takes on disk (squarified, so boxes stay close to square and
// easy to compare). Click a folder's box to go into it; the crumbs or
// Backspace come back up. Right-click shows it in the window.
OmDialog {
    id: root
    objectName: "diskUsageDialog"

    anchors.centerIn: Overlay.overlay
    width: Math.min(820, Overlay.overlay ? Overlay.overlay.width - 40 : 820)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 60 : 640)
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    title: qsTr("Disk Usage")

    readonly property Item closeButton: DialogCloseButton { dialog: root }

    signal revealRequested(string path)

    DiskUsage { id: usage }

    function scan(path) {
        usage.start(path);
        open();
    }

    // A tinted ramp from the accent, so neighbouring boxes read apart.
    function boxColor(index, isDir) {
        const base = Colors.accent;
        const shade = 0.55 + 0.35 * ((index * 37) % 10) / 10;
        return isDir ? Qt.rgba(base.r * shade, base.g * shade, base.b * shade, 0.85)
                     : Qt.alpha(Colors.textDim, 0.25 + 0.2 * ((index * 53) % 5) / 5);
    }

    // Squarified treemap (Bruls, Huizing, van Wijk): lay the items out in
    // rows along the shorter side, starting a new row whenever adding the
    // next item would make the row's worst aspect ratio worse.
    function layout(items, w, h) {
        const out = [];
        const total = items.reduce((sum, item) => sum + item.size, 0);
        if (total <= 0 || w <= 0 || h <= 0)
            return out;
        const scale = (w * h) / total;
        const areas = items.map(item => item.size * scale);
        let x = 0, y = 0, width = w, height = h;
        let row = [];
        const worst = (r, side) => {
            const sum = r.reduce((a, b) => a + b, 0);
            const max = Math.max(...r), min = Math.min(...r);
            return Math.max(side * side * max / (sum * sum), (sum * sum) / (side * side * min));
        };
        const place = (r, start) => {
            const sum = r.reduce((a, b) => a + b, 0);
            if (width >= height) {
                const colW = sum / height;
                let cy = y;
                for (let i = 0; i < r.length; ++i) {
                    const boxH = r[i] / colW;
                    out.push({ index: start + i, x: x, y: cy, w: colW, h: boxH });
                    cy += boxH;
                }
                x += colW;
                width -= colW;
            } else {
                const rowH = sum / width;
                let cx = x;
                for (let i = 0; i < r.length; ++i) {
                    const boxW = r[i] / rowH;
                    out.push({ index: start + i, x: cx, y: y, w: boxW, h: rowH });
                    cx += boxW;
                }
                y += rowH;
                height -= rowH;
            }
        };
        let start = 0;
        for (let i = 0; i < areas.length; ++i) {
            const side = Math.min(width, height);
            if (row.length === 0 || worst(row.concat([areas[i]]), side) <= worst(row, side)) {
                row.push(areas[i]);
            } else {
                place(row, start);
                start = i;
                row = [areas[i]];
            }
        }
        if (row.length > 0)
            place(row, start);
        return out;
    }

    contentItem: ColumnLayout {
        spacing: 10
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_H || event.key === Qt.Key_Left) {
                usage.up();
                event.accepted = true;
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            OmButton {
                text: "↑"
                implicitWidth: 36
                leftPadding: 0
                rightPadding: 0
                enabled: usage.currentPath !== usage.rootPath && !usage.running
                onClicked: usage.up()
            }

            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: usage.currentPath
                color: Colors.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }

            Text {
                textFormat: Text.PlainText
                text: usage.running
                      ? qsTr("Scanning… %1 in %2 files").arg(Platform.formatSize(usage.scannedBytes))
                                                         .arg(usage.scannedFiles)
                      : Platform.formatSize(usage.currentSize)
                color: Colors.textDim
                font.pixelSize: 12
            }
        }

        Item {
            id: mapArea
            objectName: "diskUsageMap"
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property var boxes: root.layout(usage.items, width, height)

            BusyIndicator {
                anchors.centerIn: parent
                running: usage.running
                visible: running
            }

            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: !usage.running && (usage.error !== "" || usage.items.length === 0)
                text: usage.error !== "" ? usage.error : qsTr("This folder is empty.")
                color: usage.error !== "" ? Colors.error : Colors.textDim
                font.pixelSize: 13
            }

            Repeater {
                model: usage.running ? [] : mapArea.boxes

                Rectangle {
                    id: box
                    required property var modelData
                    readonly property var item: usage.items[modelData.index]
                    x: modelData.x + 1
                    y: modelData.y + 1
                    width: Math.max(0, modelData.w - 2)
                    height: Math.max(0, modelData.h - 2)
                    radius: 3
                    color: root.boxColor(modelData.index, item.isDir)
                    border.color: boxMouse.containsMouse ? Colors.text : "transparent"
                    border.width: 1.5

                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 1
                        visible: box.width > 46 && box.height > 30
                        clip: true

                        Text {
                            textFormat: Text.PlainText
                            width: parent.width
                            text: box.item.name + (box.item.isDir ? "/" : "")
                            color: box.item.isDir ? "#ffffff" : Colors.text
                            font.pixelSize: 12
                            font.bold: box.item.isDir
                            elide: Text.ElideMiddle
                        }
                        Text {
                            textFormat: Text.PlainText
                            width: parent.width
                            text: Platform.formatSize(box.item.size)
                            color: box.item.isDir ? "#ffffff" : Colors.textDim
                            opacity: 0.85
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: boxMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: box.item.isDir ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton && box.item.path !== "") {
                                root.revealRequested(box.item.path);
                                root.close();
                            } else if (box.item.isDir) {
                                usage.enter(box.item.path);
                            }
                        }
                    }

                    ToolTip.visible: boxMouse.containsMouse
                    ToolTip.delay: 400
                    ToolTip.text: box.item.name + " — " + Platform.formatSize(box.item.size)
                                  + (box.item.files > 1 ? "  ·  " + qsTr("%1 files").arg(box.item.files) : "")
                }
            }
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: qsTr("Click a folder to go into it · right-click to show it in the window · Backspace goes up")
            color: Colors.textDim
            font.pixelSize: 11
        }
    }
}
