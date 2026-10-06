import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// The places sidebar: user folders, devices, bookmarks, network. Every colour
// comes from Colors, so it wears the active Omarchy theme like the rest of
// the app — flat surfaces, subtle borders, the accent only where it means
// something.
Rectangle {
    id: root

    // The location the window is showing, for highlighting the matching row.
    property string currentLocation: ""

    // The window's Mounter — volumes that need credentials ask through it.
    property Mounter mounter: null

    signal navigateRequested(string location)
    signal mountError(string name, string message)
    // Files were dropped on a row; the window decides what that means
    // (Trash trashes, anywhere else transfers).
    signal dropRequested(var urls, string location)
    signal openInNewTabRequested(string location)
    // The window owns the confirm dialog; emptying is never one click.
    signal emptyTrashRequested()
    // The operations popover closed; the window hands the keyboard back —
    // the same contract every dialog honours (see the Phase 3 findings).
    signal opsPopoverClosed()
    // Keyboard browsing (vim `b`) ended — the window hands the keys back.
    signal keyboardReleased()
    // Pro: a saved search was clicked; the window runs it.
    signal savedSearchRequested(var search)

    readonly property bool keyboardActive: list.activeFocus

    // Vim `b`: the places list takes the keys, starting on the current place.
    function focusPlaces() {
        const row = places.rowForLocation(root.currentLocation);
        list.currentIndex = row >= 0 ? row : 0;
        list.forceActiveFocus();
    }

    // What a click does, by row index — navigate, or mount first.
    function activateRow(row) {
        const place = places.get(row);
        if (!place.name)
            return;
        if (place.location !== "")
            root.navigateRequested(place.location);
        else if (place.mountable)
            places.mount(row);
    }

    // Vim 1–9: the Nth row, counted down the sidebar as drawn.
    function activatePlace(number) {
        if (number >= 1 && number <= places.count)
            activateRow(number - 1);
    }

    function isBookmarked(location) {
        return places.isBookmarked(location);
    }

    // Bookmark the folder or drop the bookmark — Ctrl+D in the window.
    function toggleBookmark(location) {
        if (places.isBookmarked(location))
            places.removeBookmark(location);
        else
            places.addBookmark(location);
    }

    implicitWidth: 200
    color: Colors.chrome

    PlacesModel {
        id: places
        mounter: root.mounter
    }

    // Model count published for the UI verification script.
    readonly property int placesCount: places.count
    // Whether the New Bookmark row is up — for the same scripts.
    readonly property bool bookmarkDropTarget: places.bookmarkDropTarget

    // ---- dropping a folder to bookmark it ----------------------------------
    //
    // While a drag carrying folders that are not yet bookmarked hovers the
    // sidebar, the model shows a "New Bookmark" row at the end of the
    // Bookmarks section; dropping on it bookmarks them. The folders are worked
    // out as the drag comes in; rows report arrivals and departures, and a
    // short settle keeps the placeholder from flickering as the pointer
    // crosses from one row to the next (each hop is an exit then an enter).
    property var pendingBookmarks: []

    function dragArrived(drag) {
        dragSettle.stop();
        pendingBookmarks = places.bookmarkable(Platform.locationsFromUrls(drag.urls));
        places.bookmarkDropTarget = pendingBookmarks.length > 0;
    }

    function dragLeft() {
        dragSettle.restart();
    }

    function dragEnded() {
        dragSettle.stop();
        places.bookmarkDropTarget = false;
        pendingBookmarks = [];
    }


    Timer {
        id: dragSettle
        interval: 150
        onTriggered: root.dragEnded()
    }

    Connections {
        target: places
        function onMounted(location) {
            root.navigateRequested(location);
        }
        function onMountFailed(name, message) {
            root.mountError(name, message);
        }
    }

    // The gaps between rows and the empty space below them. Behind the list,
    // so a row's own DropArea wins where they overlap; a drop here does
    // nothing, but a drag passing over it keeps the bookmark placeholder up.
    DropArea {
        id: backdrop

        anchors.fill: list
        onEntered: drag => root.dragArrived(drag)
        onExited: root.dragLeft()
        onDropped: drop => {
            root.dragEnded();
            drop.accepted = false;
        }
    }

    ListView {
        id: list

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: proSection.top
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        clip: true
        model: places
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false

        Keys.onPressed: event => {
            const step = d => {
                list.currentIndex = Math.max(0, Math.min(list.count - 1, list.currentIndex + d));
                list.positionViewAtIndex(list.currentIndex, ListView.Contain);
            };
            if (event.key === Qt.Key_J || event.key === Qt.Key_Down) step(1);
            else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) step(-1);
            else if (event.key === Qt.Key_G)
                step((event.modifiers & Qt.ShiftModifier) ? list.count : -list.count);
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                     || event.key === Qt.Key_L || event.key === Qt.Key_Right) {
                root.activateRow(list.currentIndex);
                root.keyboardReleased();
            } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_H
                       || event.key === Qt.Key_Left || event.key === Qt.Key_B)
                root.keyboardReleased();
            else
                return;
            event.accepted = true;
        }

        // Nautilus draws no section headers — just a hairline between the
        // fixed places, the bookmarks and the devices. Same here.
        section.property: "section"
        section.delegate: Item {
            required property string section

            width: list.width
            height: section === "Places" ? 4 : 15

            Rectangle {
                visible: parent.section !== "Places"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: list.width - 16
                height: 1
                color: Colors.border
            }
        }

        delegate: Rectangle {
            id: row

            required property int index
            required property string name
            required property string location
            required property string iconSource
            required property string section
            required property bool mountable
            required property bool ejectable
            required property real freeBytes
            required property real totalBytes
            readonly property real usedFraction: totalBytes > 0 ? 1 - freeBytes / totalBytes : 0
            required property bool placeholder

            readonly property bool current: location !== "" && location === root.currentLocation
            // Recent is read-only and Network is not a folder; everywhere
            // else with a location can take a drop — dropping on Starred
            // stars, on Trash trashes, elsewhere transfers (Nautilus's rules).
            // The New Bookmark placeholder exists only to be dropped on.
            readonly property bool droppable: placeholder
                                              || (location !== ""
                                                  && location !== "recent:///"
                                                  && location !== "network:///")

            // ListView places its delegates at x 0 and ignores a delegate's
            // own x, so the 6px inset on each side lives on the view instead.
            width: list.width
            height: 30
            radius: Colors.radius
            color: current ? Colors.selection
                 : rowDrop.containsDrag ? Colors.hover
                 : rowMouse.containsMouse ? Colors.hover : "transparent"
            readonly property bool keyCursor: list.activeFocus && list.currentIndex === index
            // The placeholder is outlined so it reads as a slot, not a place.
            border.color: rowDrop.containsDrag || keyCursor ? Colors.accent
                        : placeholder ? Colors.border : "transparent"
            border.width: rowDrop.containsDrag || keyCursor || placeholder ? 1 : 0

            FileDropArea {
                id: rowDrop

                anchors.fill: parent
                enabled: row.droppable
                // The New Bookmark row's pseudo-location: DragState labels it
                // "Bookmark", and a drop adds the folders worked out on entry.
                destination: row.placeholder ? "bookmark:" : row.location
                // Hold over a place and it opens, to keep digging into it.
                // Trash, Starred and the placeholder have nothing to dig into.
                springLoaded: !row.placeholder && row.location !== "trash:///"
                              && row.location !== "starred:///"
                onEntered: drag => root.dragArrived(drag)
                onExited: root.dragLeft()
                onFilesDropped: urls => {
                    if (row.placeholder) {
                        for (const folder of root.pendingBookmarks)
                            places.addBookmark(folder);
                    } else {
                        root.dropRequested(urls, row.location);
                    }
                    root.dragEnded();
                }
                onSprung: root.navigateRequested(row.location)
            }

            Image {
                id: icon

                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                source: Colors.tint(row.iconSource,
                                    row.current ? Colors.selectionText
                                    : row.placeholder && rowDrop.containsDrag ? Colors.accent
                                    : Colors.textDim)
                sourceSize: Qt.size(16, 16)
                opacity: row.mountable ? 0.6 : 1
            }

            Text {
                textFormat: Text.PlainText
                anchors.left: icon.right
                anchors.leftMargin: 8
                anchors.right: ejectButton.visible ? ejectButton.left : parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                color: row.current ? Colors.selectionText
                     : row.mountable || row.placeholder ? Colors.textDim : Colors.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }

            Text {
                textFormat: Text.PlainText
                id: ejectButton

                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: row.ejectable
                text: "⏏"
                color: ejectMouse.containsMouse ? Colors.accent : Colors.textDim
                font.pixelSize: 12

                MouseArea {
                    id: ejectMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    onClicked: places.eject(row.index)
                }
            }

            // Disk usage under Home and the devices: a hairline meter that
            // turns to the error colour past 90% full.
            Rectangle {
                id: usageTrack
                visible: row.totalBytes > 0
                anchors.left: icon.right
                anchors.leftMargin: 8
                anchors.right: ejectButton.visible ? ejectButton.left : parent.right
                anchors.rightMargin: 8
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                height: 2
                radius: 1
                color: Colors.border

                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, row.usedFraction))
                    height: parent.height
                    radius: 1
                    color: row.usedFraction > 0.9 ? Colors.error
                         : row.current ? Colors.selectionText : Colors.accent
                }
            }

            ToolTip.visible: rowMouse.containsMouse && row.totalBytes > 0
            ToolTip.delay: 600
            ToolTip.text: qsTr("%1 free of %2").arg(Platform.formatSize(row.freeBytes))
                                                 .arg(Platform.formatSize(row.totalBytes))

            MouseArea {
                id: rowMouse

                anchors.fill: parent
                anchors.rightMargin: row.ejectable ? 24 : 0
                // The placeholder is a drop target only; a click does nothing.
                enabled: !row.placeholder
                hoverEnabled: !row.placeholder
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        rowMenu.rowLocation = row.location;
                        rowMenu.rowSection = row.section;
                        rowMenu.popup();
                    } else if (mouse.button === Qt.MiddleButton) {
                        if (row.location !== "" && row.location !== "network:///")
                            root.openInNewTabRequested(row.location);
                    } else if (row.location !== "") {
                        root.navigateRequested(row.location);
                    } else if (row.mountable) {
                        places.mount(row.index);
                    }
                }
            }
        }
    }

    // Pro: coloured tags in use and saved searches, under the places.
    Column {
        id: proSection
        objectName: "proSidebarSection"

        readonly property bool any: Settings.proFeatures
            && (Tags.usedTags.length > 0 || SavedSearches.searches.length > 0)

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: opsArea.top
        anchors.leftMargin: 6
        anchors.rightMargin: 7
        visible: any
        height: any ? implicitHeight + 6 : 0
        spacing: 0

        Rectangle {
            width: parent.width - 16
            x: 8
            height: 1
            color: Colors.border
        }
        Item { width: 1; height: 6 }

        component ProRow: Rectangle {
            id: proRow
            property string label: ""
            property string location: ""
            property color dot: "transparent"
            property string glyph: ""
            signal activated()
            signal menuRequested()
            readonly property bool current: location !== "" && location === root.currentLocation

            width: parent.width
            height: 28
            radius: Colors.radius
            color: current ? Colors.selection : proMouse.containsMouse ? Colors.hover : "transparent"

            Rectangle {
                id: proDot
                visible: proRow.glyph === ""
                anchors.left: parent.left
                anchors.leftMargin: 11
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                color: proRow.dot
            }
            Image {
                id: proIcon
                visible: proRow.glyph !== ""
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                sourceSize: Qt.size(16, 16)
                source: proRow.glyph !== "" ? Colors.tint("image://fileicon/" + proRow.glyph,
                                                          proRow.current ? Colors.selectionText : Colors.textDim)
                                            : ""
            }
            Text {
                textFormat: Text.PlainText
                anchors.left: parent.left
                anchors.leftMargin: 32
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: proRow.label
                color: proRow.current ? Colors.selectionText : Colors.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }
            MouseArea {
                id: proMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => mouse.button === Qt.RightButton ? proRow.menuRequested()
                                                                    : proRow.activated()
            }
        }

        Repeater {
            model: Settings.proFeatures ? Tags.usedTags : []
            ProRow {
                required property string modelData
                label: modelData
                dot: Tags.colorFor(modelData)
                location: "tag:///" + encodeURIComponent(modelData)
                onActivated: root.navigateRequested(location)
            }
        }

        Repeater {
            model: Settings.proFeatures ? SavedSearches.searches : []
            ProRow {
                required property var modelData
                label: modelData.name
                glyph: "search"
                onActivated: root.savedSearchRequested(modelData)
                onMenuRequested: {
                    savedSearchMenu.searchId = modelData.id;
                    savedSearchMenu.popup();
                }
            }
        }

        OmMenu {
            id: savedSearchMenu
            property string searchId: ""
            OmMenuItem {
                text: qsTr("Remove Saved Search")
                glyph: "delete"
                destructive: true
                onTriggered: SavedSearches.remove(savedSearchMenu.searchId)
            }
        }
    }

    // Nautilus 50's operations indicator: progress lives at the bottom of the
    // sidebar — one row per operation, a pie that fills as it runs beside a
    // live short status ("Copying “name”"). Click for the per-operation
    // popover; the whole thing lingers a few seconds once the queue drains,
    // and pulses the accent when an operation starts.
    Item {
        id: opsArea

        readonly property bool hasOps: FileOperations.operations.length > 0
        property bool linger: false

        onHasOpsChanged: {
            if (hasOps) {
                opsLinger.stop();
                linger = false;
                attention.restart();
            } else {
                linger = true;
                opsLinger.restart();
            }
        }

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 1 // stay clear of the separating hairline
        visible: hasOps || linger || opsPopover.visible
        height: visible ? opsColumn.height + 14 : 0

        Timer {
            id: opsLinger
            interval: 4000
            onTriggered: opsArea.linger = false
        }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 1
            color: opsPopover.visible ? Colors.selection
                 : opsMouse.containsMouse ? Colors.hover : "transparent"
        }

        // The attention pulse, over the hover wash and under the rows.
        Rectangle {
            id: attentionWash
            anchors.fill: parent
            anchors.topMargin: 1
            color: Colors.accent
            opacity: 0
        }

        SequentialAnimation {
            id: attention
            loops: 2
            NumberAnimation {
                target: attentionWash; property: "opacity"
                from: 0; to: 0.3; duration: 220
            }
            NumberAnimation {
                target: attentionWash; property: "opacity"
                from: 0.3; to: 0; duration: 220
            }
        }

        // The hairline separating the indicator from the places above it.
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Colors.border
        }

        Column {
            id: opsColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            // The drained state: full pie, past-tense line, then gone.
            Row {
                visible: !opsArea.hasOps
                spacing: 8
                OpsPie { fraction: 1; done: true }
                Text {
                    textFormat: Text.PlainText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Operations complete")
                    color: Colors.textDim
                    font.pixelSize: 12
                }
            }

            Repeater {
                model: FileOperations.operations

                Row {
                    required property var modelData

                    width: opsColumn.width
                    spacing: 8

                    OpsPie {
                        fraction: modelData.state === "running" ? modelData.progress : 0
                        done: false
                    }

                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 22
                        text: modelData.shortStatus
                        color: modelData.state === "running" ? Colors.text : Colors.textDim
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        MouseArea {
            id: opsMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: opsPopover.visible ? opsPopover.close() : opsPopover.open()
        }

        ToolTip.visible: opsMouse.containsMouse && !opsPopover.visible
        ToolTip.text: qsTr("File operations")
        ToolTip.delay: 600

        // The detail popover, above the indicator: per-operation rows with a
        // progress bar, the current file with byte/rate details and the time
        // estimate, and a per-operation cancel.
        OmPopup {
            id: opsPopover
            // Opens upward from the indicator.
            transformOrigin: Popup.Bottom

            x: 6
            y: -height - 8
            width: 340
            padding: 14
            // Take the keyboard while open, like a GTK popover: without
            // focus, CloseOnEscape never fires and Escape cannot reach it —
            // the popover became unclosable from the keyboard and the window
            // shortcuts stayed half-dead behind it (found by the mouse
            // checklist, 2026-08-09).
            focus: true
            onClosed: root.opsPopoverClosed()

            contentItem: Column {
                spacing: 12

                // Pro: the queue's controls — pause everything, and cap
                // how fast copies go so the disk stays usable meanwhile.
                Row {
                    visible: Settings.proFeatures
                    width: 340 - 28
                    spacing: 8

                    OmButton {
                        objectName: "queuePauseButton"
                        text: FileOperations.paused ? qsTr("Resume") : qsTr("Pause")
                        primary: FileOperations.paused
                        implicitHeight: 28
                        onClicked: FileOperations.paused = !FileOperations.paused
                    }

                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Speed")
                        color: Colors.textDim
                        font.pixelSize: 12
                    }

                    OmComboBox {
                        id: speedCombo
                        width: 120
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property var values: ["off", "100", "50", "20", "10", "5", "1"]
                        model: [qsTr("No limit"), "100 MB/s", "50 MB/s", "20 MB/s", "10 MB/s",
                                "5 MB/s", "1 MB/s"]
                        currentIndex: Math.max(0, values.indexOf(Settings.transferSpeedLimit))
                        onActivated: Settings.transferSpeedLimit = values[currentIndex]
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    visible: Settings.proFeatures && FileOperations.verifying
                    text: qsTr("Verifying copies…")
                    color: Colors.accent
                    font.pixelSize: 12
                }

                Text {
                    textFormat: Text.PlainText
                    visible: FileOperations.operations.length === 0
                    text: qsTr("All operations complete")
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                Repeater {
                    model: FileOperations.operations

                    Column {
                        required property var modelData

                        width: 340 - 28
                        spacing: 4

                        Row {
                            width: parent.width
                            spacing: 6

                            Text {
                                textFormat: Text.PlainText
                                width: parent.width - 22 - (queueArrows.visible ? queueArrows.width + 6 : 0)
                                text: modelData.state === "queued"
                                      ? modelData.label + qsTr(" — waiting")
                                      : modelData.state === "paused"
                                      ? modelData.label + qsTr(" — paused")
                                      : modelData.label
                                color: Colors.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            // Pro: reorder what's waiting.
                            Row {
                                id: queueArrows
                                visible: Settings.proFeatures && modelData.movable === true
                                spacing: 4
                                Repeater {
                                    model: [{ symbol: "▲", delta: -1 }, { symbol: "▼", delta: 1 }]
                                    Text {
                                        required property var modelData
                                        textFormat: Text.PlainText
                                        text: modelData.symbol
                                        color: arrowMouse.containsMouse ? Colors.text : Colors.textDim
                                        font.pixelSize: 10
                                        MouseArea {
                                            id: arrowMouse
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: FileOperations.moveOperation(queueArrows.parent.parent.modelData.id,
                                                                                    modelData.delta)
                                        }
                                    }
                                }
                            }

                            // Per-operation cancel: interrupts the
                            // running one, drops a queued one.
                            Text {
                                textFormat: Text.PlainText
                                text: "✕"
                                color: cancelOneMouse.containsMouse
                                       ? Colors.text : Colors.textDim
                                font.pixelSize: 12

                                MouseArea {
                                    id: cancelOneMouse
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: FileOperations.cancelOperation(modelData.id)
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Colors.border
                            visible: modelData.state === "running"

                            Rectangle {
                                width: parent.width * modelData.progress
                                height: parent.height
                                radius: 2
                                color: Colors.accent
                            }
                        }

                        // The current file on its own line — a long name must
                        // not elide the numbers off the line below it.
                        Text {
                            textFormat: Text.PlainText
                            visible: (modelData.detail || "") !== ""
                            width: parent.width
                            text: modelData.detail || ""
                            color: Colors.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }

                        // Nautilus's details line: bytes, rate, time.
                        Text {
                            textFormat: Text.PlainText
                            readonly property string line: {
                                const bits = [];
                                if (modelData.transferred) bits.push(modelData.transferred);
                                if (modelData.remaining) bits.push(modelData.remaining);
                                return bits.join(" — ");
                            }
                            visible: line !== ""
                            width: parent.width
                            text: line
                            color: Colors.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // One menu for every row; the click stamps which row it is about.
    OmMenu {
        id: rowMenu

        property string rowLocation: ""
        property string rowSection: ""

        OmMenuItem {
            text: qsTr("Open in New Tab")
            glyph: "tab-new"
            enabled: rowMenu.rowLocation !== "" && rowMenu.rowLocation !== "network:///"
            onTriggered: root.openInNewTabRequested(rowMenu.rowLocation)
        }

        OmMenuItem {
            text: qsTr("Remove Bookmark")
            glyph: "bookmark"
            visible: rowMenu.rowSection === "Bookmarks"
            height: visible ? implicitHeight : 0
            onTriggered: places.removeBookmark(rowMenu.rowLocation)
        }

        OmMenuItem {
            text: qsTr("Empty Trash…")
            glyph: "trash"
            destructive: true
            visible: rowMenu.rowLocation === "trash:///"
            height: visible ? implicitHeight : 0
            onTriggered: root.emptyTrashRequested()
        }
    }

    // The hairline that separates the sidebar from the view.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Colors.border
    }
}
