pragma Singleton

import QtQuick
import Omanta.Runtime

// What a file drag over omanta would do right now, shared by every drop
// target and the label beside the pointer ("+ Copy to “Photos”").
//
// The action follows the modifiers, Nautilus's and Windows's convention:
//   Ctrl              copy
//   Shift             move
//   Ctrl+Shift / Alt  link
//   none              move within one filesystem, copy across
// The keys are re-read on a short timer while a target is hovered, so the
// label changes the moment a key goes down — no mouse movement needed.
QtObject {
    id: state

    // The drop target being hovered, or null.
    property Item area: null
    property var paths: []
    property string destination: ""
    // "copy" | "move" | "link" | "trash" | "star" | "bookmark" — what a drop
    // does.
    property string action: ""
    // Scene position of the pointer, and the window it is in.
    property real x: 0
    property real y: 0
    property var window: null
    // omanta's own drag: the card ([▣ 5 items | Move]) is drawn by the
    // window under the pointer, live, from these — the native drag picture
    // is blank, since Qt cannot change it mid-drag. False for drags from
    // other apps, which get a badge beside the pointer instead.
    property bool ownDrag: false
    property string cardText: ""
    property url cardIcon: ""
    property point cardHotSpot: Qt.point(28, 28)

    readonly property bool active: area !== null && action !== ""
    // Trash is the one destructive drop: drawn in the error colour.
    readonly property bool destructive: action === "trash"
    // The one word that goes beside the drag card's "5 items".
    readonly property string word: {
        switch (action) {
        case "copy": return qsTr("Copy");
        case "move": return qsTr("Move");
        case "link": return qsTr("Link");
        case "trash": return qsTr("Trash");
        case "star": return qsTr("Star");
        case "bookmark": return qsTr("Bookmark");
        }
        return "";
    }
    readonly property string label: {
        const where = destination === "" ? "" : "“" + Platform.baseName(destination) + "”";
        switch (action) {
        case "copy": return "+  " + qsTr("Copy to %1").arg(where);
        case "move": return "→  " + qsTr("Move to %1").arg(where);
        case "link": return "↗  " + qsTr("Link in %1").arg(where);
        case "trash": return "→  " + qsTr("Move to Trash");
        case "star": return "★  " + qsTr("Star");
        case "bookmark": return "+  " + (paths.length === 1
                                         ? qsTr("Bookmark “%1”").arg(Platform.baseName(paths[0]))
                                         : qsTr("Bookmark %1 folders").arg(paths.length));
        }
        return "";
    }

    // The action for these sources landing in `target`, from the keys held
    // now. Trash, Starred and the sidebar's New Bookmark row ("bookmark:")
    // have one meaning each.
    function actionFor(sources, target) {
        if (target === "trash:///")
            return "trash";
        if (target === "starred:///")
            return "star";
        if (target === "bookmark:")
            return "bookmark";
        if (sources.length === 0 || !target)
            return "";
        const mods = Platform.keyboardModifiers();
        const ctrl = (mods & Qt.ControlModifier) !== 0;
        const shift = (mods & Qt.ShiftModifier) !== 0;
        if ((ctrl && shift) || (mods & Qt.AltModifier))
            return "link";
        if (ctrl)
            return "copy";
        if (shift)
            return "move";
        return Platform.sameFilesystem(sources[0], target) ? "move" : "copy";
    }

    // A drop that would do nothing: a folder into itself, or things back into
    // the folder they are in. Trash and Starred are never the parent.
    function isNoop(sources, target) {
        return sources.length > 0
               && (sources.indexOf(target) >= 0
                   || sources.every(p => Platform.parentPath(p) === target));
    }

    function hover(dropArea, urls, target, sceneX, sceneY, win) {
        if (area !== dropArea) {
            area = dropArea;
            paths = Platform.locationsFromUrls(urls);
            destination = target;
        }
        x = sceneX;
        y = sceneY;
        window = win;
        refresh();
    }

    function leave(dropArea) {
        if (area === dropArea) {
            area = null;
            action = "";
            paths = [];
        }
    }

    function refresh() {
        if (!area)
            return;
        // Letting go in the folder things came from is a drag abandoned, not
        // a request, whatever key is held (Ctrl used to make a "name (copy)"
        // beside the original; a duplicate is Ctrl+C, Ctrl+V). Say nothing
        // rather than promise one; Tab.requestDrop refuses it the same way.
        action = isNoop(paths, destination) ? "" : actionFor(paths, destination);
    }

    // Keys can change while the pointer rests; a dead area (its delegate
    // destroyed, the drag cancelled) is let go here too.
    property Timer poll: Timer {
        interval: 100
        repeat: true
        running: state.area !== null
        onTriggered: {
            if (!state.area || !state.area.containsDrag)
                state.leave(state.area);
            else
                state.refresh();
        }
    }
}
