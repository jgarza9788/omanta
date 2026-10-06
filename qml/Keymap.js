.pragma library

// The one key table, read by the `?` overlay (ShortcutsDialog). Tab.qml (vim keys) and Main.qml (Shortcuts) do the
// binding; when a binding moves, move its row here too.
//
// Vim keys follow the Omarchy plugins (flank, notification, loadout): hjkl
// to move, g/G for the ends, / to filter (Enter keeps it, Esc clears it),
// ? for every key, Esc backs out one layer at a time.

var vimGroups = [
    { name: "Move", rows: [
        ["j / k", "Down / up"],
        ["h / l", "Parent / open (list) — left / right (grid)"],
        ["- / Backspace", "Parent folder"],
        ["~", "Home folder"],
        ["g / G", "First / last item"],
        ["J / K", "Extend the selection down / up"],
        ["1 – 9", "Jump to sidebar place N"],
        ["b", "Focus the sidebar (j/k, Enter, Esc)"],
        ["Tab", "Other pane (split view)"]] },
    { name: "Select and find", rows: [
        ["v", "Toggle the current item"],
        ["V", "Select all"],
        ["*", "Select by pattern"],
        ["/", "Filter this folder (text, *.glob, re:regex)"],
        ["f", "Search below this folder"],
        ["Alt+R", "Regex on/off (filter and search fields)"],
        ["Esc", "Close preview › clear filter › clear selection"]] },
    { name: "Act", rows: [
        ["Enter", "Open"],
        ["Space", "Quick view (j/k step, Space/Esc close)"],
        ["p (in quick view) / P", "Picture-in-picture: preview in a corner, keys to the files"],
        ["i", "Info panel"],
        ["y / x / p", "Copy / cut / paste"],
        ["Y", "Duplicate"],
        ["c / m", "Copy / move to the other pane"],
        ["r", "Rename"],
        ["a", "New folder"],
        ["D", "Move to trash"],
        ["u / U", "Undo / redo"],
        ["o", "Actions menu"]] },
    { name: "Columns and gallery", rows: [
        ["h / l (columns)", "Back up a column / into the folder (a new column opens)"],
        ["h / l (gallery)", "Previous / next in the strip"],
        ["Ctrl+ + / -", "Row size (columns) / strip thumbnails (gallery)"]] },
    { name: "View", rows: [
        [".", "Show hidden files"],
        ["s", "Cycle sort: name › modified › size › type"],
        ["t / q", "New tab / close tab"],
        ["?", "This list"]] }
];

var classicGroups = [
    { name: "Windows and tabs", rows: [
        ["Ctrl+N", "New window"], ["Ctrl+Shift+W", "Close window"],
        ["Ctrl+T", "New tab"], ["Ctrl+W", "Close tab"],
        ["Ctrl+Tab / Ctrl+PgDn", "Next tab"], ["Ctrl+Shift+Tab / Ctrl+PgUp", "Previous tab"],
        ["F3", "Split view"], ["Ctrl+F6", "Switch pane"],
        ["F9", "Toggle sidebar"], ["F11", "Info panel"]] },
    { name: "Navigation", rows: [
        ["Alt+Left / Alt+Right", "Back / forward"], ["Alt+Up", "Parent folder"],
        ["Alt+Home", "Home folder"], ["Ctrl+L", "Edit the location"],
        ["Enter", "Open the selection"], ["Space", "Quick view"],
        ["Backspace", "Parent folder"]] },
    { name: "View", rows: [
        ["Ctrl+1 / Ctrl+2", "List / grid view"],
        ["Ctrl+3 / Ctrl+4", "Columns / gallery view"], ["Ctrl+H", "Show hidden files"],
        ["Ctrl++ / Ctrl+-", "Zoom in / out"], ["Ctrl+0", "Reset zoom"],
        ["Ctrl+R", "Reload"]] },
    { name: "Search and select", rows: [
        ["Ctrl+F", "Search the current folder"],
        ["Ctrl+Shift+F", "Search file contents"],
        ["Ctrl+Shift+S", "Filter this folder"],
        ["Ctrl+S", "Select by pattern"],
        ["Alt+R", "Regex on/off (in the field)"]] },
    { name: "Files", rows: [
        ["Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste"],
        ["Ctrl+Shift+D", "Duplicate"],
        ["F5 / F6", "Copy / move to the other pane (split view)"],
        ["Ctrl+Z", "Undo"], ["Ctrl+Shift+Z", "Redo"],
        ["Ctrl+A", "Select all"],
        ["F2", "Rename (batch rename on a multi-selection)"],
        ["Delete", "Move to trash"], ["Shift+Delete", "Delete permanently"],
        ["Ctrl+Shift+N", "New folder"], ["Ctrl+D", "Bookmark this folder"],
        ["Ctrl+I / Alt+Return", "Properties"]] },
    { name: "While dragging files", rows: [
        ["Ctrl", "Copy"],
        ["Shift", "Move"],
        ["Ctrl+Shift / Alt", "Create a link"],
        ["Rest on a folder", "It opens (spring-loaded; Preferences sets the delay)"]] },
    { name: "Application", rows: [
        ["Ctrl+,", "Preferences"], ["Ctrl+?", "Keyboard shortcuts"]] }
];

// Pro features (main menu → Pro Features): listed only while they are on.
var proGroups = [
    { name: "Pro features", rows: [
        ["Ctrl+P", "Command palette: jump to a folder or run an action"],
        [": (vim keys)", "Command palette"],
        ["' then a letter (vim keys)", "Mark this folder under that letter"],
        ["` then a letter (vim keys)", "Jump to the folder marked with that letter"],
        ["F4", "Terminal pane in the current folder"],
        ["Tab (location bar)", "Complete the folder name"],
        ["Ctrl+Shift+C", "Compare the two panes (split view)"],
        ["Enter on an archive", "Browse inside it as a folder"]] }
];

// The overlay: vim keys first when they are on, then everything that works
// in both modes, then the pro keys while pro features are on.
function groups(mode, pro) {
    const base = mode === "vim" ? vimGroups.concat(classicGroups) : classicGroups;
    return pro ? base.concat(proGroups) : base;
}

// The vim keys the keymap editor can move: one row per single-key action,
// [default key, what it does]. Tab.qml dispatches on the default key; a
// remap only changes which key reaches it.
var vimActions = [
    ["j", "Move down"], ["k", "Move up"], ["h", "Parent / left"], ["l", "Open / right"],
    ["J", "Extend selection down"], ["K", "Extend selection up"],
    ["g", "First item"], ["G", "Last item"], ["-", "Parent folder"], ["~", "Home folder"],
    ["v", "Toggle the current item"], ["V", "Select all"], ["/", "Filter this folder"],
    ["*", "Select by pattern"], ["f", "Search below this folder"], [".", "Show hidden files"],
    ["s", "Cycle sort"], ["i", "Info panel"], ["y", "Copy"], ["x", "Cut"], ["p", "Paste"],
    ["Y", "Duplicate"], ["P", "Picture-in-picture"], ["c", "Copy to the other pane"],
    ["m", "Move to the other pane"], ["r", "Rename"], ["a", "New folder"],
    ["D", "Move to trash"], ["u", "Undo"], ["U", "Redo"], ["o", "Actions menu"],
    ["t", "New tab"], ["q", "Close tab"], ["b", "Browse the sidebar"], ["?", "All keys"],
    [":", "Command palette (pro)"], ["'", "Set a mark (pro)"], ["`", "Jump to a mark (pro)"]
];

// "j=n;k=e" → { j: "n", k: "e" }: each action's custom key.
function parseRemap(text) {
    const out = {};
    for (const part of (text || "").split(";")) {
        const eq = part.indexOf("=");
        if (eq <= 0)
            continue;
        const action = part.slice(0, eq);
        const key = part.slice(eq + 1);
        if (key.length === 1 && vimActions.some(a => a[0] === action))
            out[action] = key;
    }
    return out;
}

function formatRemap(map) {
    const parts = [];
    for (const action in map)
        if (map[action] && map[action] !== action)
            parts.push(action + "=" + map[action]);
    return parts.join(";");
}

// What a pressed key means under a remap: the default key of the action it
// now triggers, "" for a default key whose action moved elsewhere (it does
// nothing), or the key itself when no remap touches it.
function translate(remap, key) {
    if (!remap)
        return key;
    let moved = false;
    for (const action in remap) {
        if (remap[action] === key)
            return action;
        if (action === key)
            moved = true;
    }
    return moved ? "" : key;
}
