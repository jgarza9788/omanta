#include "IconImageProvider.h"

#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrlQuery>

namespace {

constexpr int kDefaultSize = 32;

// The glyph set: single-colour flat geometry in a 24x24 box, the same idiom
// as the app icon and omacalc. %C% is replaced with the requested colour.
// Holes (the play triangle, the clock face) are fill-rule="evenodd" subpaths,
// so the theme background shows through rather than a hardcoded second colour.
struct Glyph {
    const char *key;
    const char *svg;
};

const Glyph kGlyphs[] = {
    { "folder",
      R"(<path fill="%C%" d="M3 6.5 A2 2 0 0 1 5 4.5 h4.6 a2 2 0 0 1 1.5.7 l1.2 1.4 a2 2 0 0 0 1.5.7 H19 a2 2 0 0 1 2 2 v8.2 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z"/>)" },
    { "file",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2-2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2-2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z"/>)" },
    { "text",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2-2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2-2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z M9 12 h6 v1.5 H9 z M9 15.3 h6 v1.5 H9 z"/>)" },
    { "code",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2-2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2-2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z M10.6 11 9.4 12.1 l1.8 1.9 -1.8 1.9 1.2 1.1 2.9-3 z"/>)" },
    { "image",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M8.4 7.6 a1.9 1.9 0 1 1 0 3.8 a1.9 1.9 0 0 1 0-3.8 z M6.2 18 l3.7-5.3 2.9 3.6 2-2.5 3.3 4.2 z"/>)" },
    { "video",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M10 8.6 l6 3.4 -6 3.4 z"/>)" },
    { "audio",
      R"(<path fill="%C%" d="M17.5 3.1 a1 1 0 0 1 1.2 1 v10.6 a2.7 2.7 0 1 1 -1.8-2.5 V7.7 l-6.2 1.4 v8.3 a2.7 2.7 0 1 1 -1.8-2.5 V6.5 a1 1 0 0 1 .8-1 z"/>)" },
    { "archive",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 5 a1.5 1.5 0 0 1 1.5-1.5 h13 A1.5 1.5 0 0 1 20 5 v2 a1.5 1.5 0 0 1 -1 1.4 V18 a2.5 2.5 0 0 1 -2.5 2.5 h-9 A2.5 2.5 0 0 1 5 18 V8.4 A1.5 1.5 0 0 1 4 7 z M9.5 10.8 h5 v2 h-5 z"/>)" },
    { "terminal",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M7.6 8.8 6.4 10 l2 2 -2 2 1.2 1.2 3.2-3.2 z M12 14.6 h5 v1.6 h-5 z"/>)" },
    { "home",
      R"(<path fill="%C%" d="M12 3.4 3.6 10.6 a1 1 0 0 0 1.3 1.5 l.6-.5 V19 a2 2 0 0 0 2 2 h3.1 v-4.8 a1.4 1.4 0 0 1 2.8 0 V21 h3.1 a2 2 0 0 0 2-2 v-7.4 l.6.5 a1 1 0 0 0 1.3-1.5 z"/>)" },
    { "downloads",
      R"(<path fill="%C%" d="M11.1 3.5 h1.8 v6.9 l2.2-2.2 1.3 1.3 L12 13.9 7.6 9.5 l1.3-1.3 2.2 2.2 z M4 14.5 h3.4 l1.4 2 h6.4 l1.4-2 H20 v3.8 A2.7 2.7 0 0 1 17.3 21 H6.7 A2.7 2.7 0 0 1 4 18.3 z"/>)" },
    { "clock",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 3 a9 9 0 1 1 0 18 9 9 0 0 1 0-18 z M12 4.9 a7.1 7.1 0 1 0 0 14.2 7.1 7.1 0 0 0 0-14.2 z M11.1 7 h1.8 v5 l3.4 2 -.9 1.5 -4.3-2.5 z"/>)" },
    { "trash-full",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4.9 4.5 a1 1 0 0 0 0 2 h.6 l.9 12.7 A2.5 2.5 0 0 0 8.9 21.5 h6.2 a2.5 2.5 0 0 0 2.5-2.3 L18.5 6.5 h.6 a1 1 0 0 0 0-2 z M9.4 8.8 h1.6 l.2 9.5 h-1.6 z M13 8.8 h1.6 l-.2 9.5 h-1.6 z M10.3 1.3 a1.6 1.6 0 1 1 0 3.2 a1.6 1.6 0 0 1 0-3.2 z M13.7 1.95 a1.25 1.25 0 1 1 0 2.5 a1.25 1.25 0 0 1 0-2.5 z"/>)" },
    { "trash",
      R"(<path fill="%C%" fill-rule="evenodd" d="M9.8 2.5 a1.5 1.5 0 0 0 -1.4 1 l-.4 1 H4.9 a1 1 0 0 0 0 2 h.6 l.9 12.7 A2.5 2.5 0 0 0 8.9 21.5 h6.2 a2.5 2.5 0 0 0 2.5-2.3 L18.5 6.5 h.6 a1 1 0 0 0 0-2 h-3.1 l-.4-1 a1.5 1.5 0 0 0 -1.4-1 z M9.4 8.8 h1.6 l.2 9.5 h-1.6 z M13 8.8 h1.6 l-.2 9.5 h-1.6 z"/>)" },
    { "drive",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6.8 4.5 a1.5 1.5 0 0 1 1.4-1 h7.6 a1.5 1.5 0 0 1 1.4 1 L19.5 11 H4.5 z M4 13 a1.5 1.5 0 0 1 1.5-1.5 h13 A1.5 1.5 0 0 1 20 13 v4 a2.5 2.5 0 0 1 -2.5 2.5 h-11 A2.5 2.5 0 0 1 4 17 z M16 14.8 a1.3 1.3 0 1 1 0 2.6 1.3 1.3 0 0 1 0-2.6 z"/>)" },
    { "network",
      R"(<path stroke="%C%" stroke-width="1.7" fill="none" d="M12 6.4 6.4 17.6 M12 6.4 17.6 17.6 M6.4 17.6 h11.2"/><path fill="%C%" d="M12 3.9 a2.5 2.5 0 1 1 0 5 2.5 2.5 0 0 1 0-5 z M6.4 15.1 a2.5 2.5 0 1 1 0 5 2.5 2.5 0 0 1 0-5 z M17.6 15.1 a2.5 2.5 0 1 1 0 5 2.5 2.5 0 0 1 0-5 z"/>)" },
    { "grid",
      R"(<rect fill="%C%" x="4.6" y="4.6" width="6.5" height="6.5" rx="1.4"/><rect fill="%C%" x="12.9" y="4.6" width="6.5" height="6.5" rx="1.4"/><rect fill="%C%" x="4.6" y="12.9" width="6.5" height="6.5" rx="1.4"/><rect fill="%C%" x="12.9" y="12.9" width="6.5" height="6.5" rx="1.4"/>)" },
    { "sidebar",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M10.5 5.8 v12.4 H18 a.2 .2 0 0 0 .2-.2 V6 a.2 .2 0 0 0 -.2-.2 z"/>)" },
    { "bookmark",
      R"(<path fill="%C%" d="M7 4.7 A2.7 2.7 0 0 1 9.7 2 h4.6 A2.7 2.7 0 0 1 17 4.7 V20.8 a.8 .8 0 0 1 -1.3 .6 L12 18.3 8.3 21.4 A.8 .8 0 0 1 7 20.8 z"/>)" },
    { "star",
      R"(<path fill="%C%" d="M12 2.8 a1 1 0 0 1 .9 .6 l2.3 4.9 5.2 .7 a1 1 0 0 1 .6 1.7 l-3.9 3.7 1 5.3 a1 1 0 0 1 -1.5 1 L12 18.2 l-4.6 2.5 a1 1 0 0 1 -1.5-1 l1-5.3 -3.9-3.7 a1 1 0 0 1 .6-1.7 l5.2-.7 2.3-4.9 a1 1 0 0 1 .9-.6 z"/>)" },
    // ---- menu and chrome actions: the same flat idiom, a size smaller in use
    { "open",
      R"(<path fill="%C%" d="M5 5 h6 v2 H7 v10 h10 v-4 h2 v6 H5 z M13 3 h8 v8 h-2 V6.4 l-7.3 7.3 -1.4 -1.4 L17.6 5 H13 z"/>)" },
    { "eye",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 5 c5 0 8.6 4.4 9.7 6.4 a1.2 1.2 0 0 1 0 1.2 C20.6 14.6 17 19 12 19 S3.4 14.6 2.3 12.6 a1.2 1.2 0 0 1 0 -1.2 C3.4 9.4 7 5 12 5 z M12 8.5 a3.5 3.5 0 1 0 0 7 a3.5 3.5 0 0 0 0 -7 z M12 10.4 a1.6 1.6 0 1 1 0 3.2 a1.6 1.6 0 0 1 0 -3.2 z"/>)" },
    { "eye-off",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 5 c5 0 8.6 4.4 9.7 6.4 a1.2 1.2 0 0 1 0 1.2 C20.6 14.6 17 19 12 19 S3.4 14.6 2.3 12.6 a1.2 1.2 0 0 1 0 -1.2 C3.4 9.4 7 5 12 5 z M12 8.5 a3.5 3.5 0 1 0 0 7 a3.5 3.5 0 0 0 0 -7 z"/><path stroke="%C%" stroke-width="2.2" stroke-linecap="round" d="M4.5 4.5 L19.5 19.5"/>)" },
    { "tab-new",
      R"(<path fill="%C%" fill-rule="evenodd" d="M3 7 a2 2 0 0 1 2 -2 h5 l2 2 h7 a2 2 0 0 1 2 2 v9 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z M11.1 10.5 h1.8 v2.6 h2.6 v1.8 h-2.6 v2.6 h-1.8 v-2.6 H8.5 v-1.8 h2.6 z"/>)" },
    { "window-new",
      R"(<path fill="%C%" fill-rule="evenodd" d="M3 6 a2 2 0 0 1 2 -2 h14 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z M5 8 v10 h14 V8 z M11.1 10 h1.8 v2.6 h2.6 v1.8 h-2.6 V17 h-1.8 v-2.6 H8.5 v-1.8 h2.6 z"/>)" },
    { "cut",
      R"(<circle cx="7" cy="17" r="2.7" fill="none" stroke="%C%" stroke-width="1.9"/><circle cx="17" cy="17" r="2.7" fill="none" stroke="%C%" stroke-width="1.9"/><path stroke="%C%" stroke-width="1.9" stroke-linecap="round" d="M8.9 15 16.5 3.8 M15.1 15 7.5 3.8"/>)" },
    { "copy",
      R"(<rect x="8.5" y="8.5" width="12" height="12" rx="2.2" fill="%C%"/><path fill="none" stroke="%C%" stroke-width="1.9" stroke-linecap="round" d="M5.5 15.5 h-.4 A1.6 1.6 0 0 1 3.5 13.9 V5.1 A1.6 1.6 0 0 1 5.1 3.5 h8.8 a1.6 1.6 0 0 1 1.6 1.6 v.4"/>)" },
    { "paste",
      R"(<path fill="%C%" fill-rule="evenodd" d="M9 2.8 h6 a1 1 0 0 1 1 1 V5 h2 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2 -2 V7 a2 2 0 0 1 2 -2 h2 V3.8 a1 1 0 0 1 1 -1 z M9.6 4.4 v2 h4.8 v-2 z"/>)" },
    { "link",
      R"(<path fill="none" stroke="%C%" stroke-width="2.1" stroke-linecap="round" d="M10.5 13.5 a3.5 3.5 0 0 0 5 0 l3 -3 a3.5 3.5 0 0 0 -5 -5 l-1 1 M13.5 10.5 a3.5 3.5 0 0 0 -5 0 l-3 3 a3.5 3.5 0 0 0 5 5 l1 -1"/>)" },
    { "folder-new",
      R"(<path fill="%C%" fill-rule="evenodd" d="M3 6.5 A2 2 0 0 1 5 4.5 h4.6 a2 2 0 0 1 1.5 .7 l1.2 1.4 a2 2 0 0 0 1.5 .7 H19 a2 2 0 0 1 2 2 v8.2 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z M11.1 10 h1.8 v2.6 h2.6 v1.8 h-2.6 V17 h-1.8 v-2.6 H8.5 v-1.8 h2.6 z"/>)" },
    { "file-new",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2 -2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2 -2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z M11.1 11.5 h1.8 v2.6 h2.6 v1.8 h-2.6 v2.6 h-1.8 v-2.6 H8.5 v-1.8 h2.6 z"/>)" },
    { "rename",
      R"(<path fill="%C%" d="M15.6 3.9 a2 2 0 0 1 2.8 0 l1.7 1.7 a2 2 0 0 1 0 2.8 L9.4 19.1 4 20 l.9 -5.4 z"/>)" },
    { "undo",
      R"(<path fill="none" stroke="%C%" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" d="M9 13.5 4.5 9 9 4.5 M4.5 9 H14 a5.5 5.5 0 0 1 0 11 h-3"/>)" },
    { "redo",
      R"(<path fill="none" stroke="%C%" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" d="M15 13.5 19.5 9 15 4.5 M19.5 9 H10 a5.5 5.5 0 0 0 0 11 h3"/>)" },
    { "reload",
      R"(<path fill="none" stroke="%C%" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" d="M19.4 9.5 A8 8 0 1 0 20 13 M20 4.5 v5 h-5"/>)" },
    { "delete",
      R"(<path stroke="%C%" stroke-width="2.3" stroke-linecap="round" d="M6.5 6.5 l11 11 M17.5 6.5 l-11 11"/>)" },
    { "info",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 3 a9 9 0 1 1 0 18 a9 9 0 0 1 0 -18 z M11 10.5 h2 V17 h-2 z M12 6.6 a1.3 1.3 0 1 1 0 2.6 a1.3 1.3 0 0 1 0 -2.6 z"/>)" },
    { "select-all",
      R"(<rect x="4" y="4" width="16" height="16" rx="3" fill="none" stroke="%C%" stroke-width="1.8" stroke-dasharray="3 2"/><path fill="none" stroke="%C%" stroke-width="2.1" stroke-linecap="round" stroke-linejoin="round" d="M8 12.5 l2.8 2.8 L16.5 9"/>)" },
    { "location",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 2.5 a7 7 0 0 1 7 7 c0 5 -7 12 -7 12 S5 14.5 5 9.5 a7 7 0 0 1 7 -7 z M12 6.8 a2.7 2.7 0 1 0 0 5.4 a2.7 2.7 0 0 0 0 -5.4 z"/>)" },
    { "gear",
      R"(<circle cx="12" cy="12" r="4" fill="none" stroke="%C%" stroke-width="2.2"/><path stroke="%C%" stroke-width="2.4" stroke-linecap="round" d="M12 2.6 v2.4 M12 19 v2.4 M2.6 12 H5 M19 12 h2.4 M5.4 5.4 l1.7 1.7 M16.9 16.9 l1.7 1.7 M5.4 18.6 l1.7 -1.7 M16.9 7.1 l1.7 -1.7"/>)" },
    { "keyboard",
      R"(<path fill="%C%" fill-rule="evenodd" d="M3 7 a2 2 0 0 1 2 -2 h14 a2 2 0 0 1 2 2 v10 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z M6 8.5 h2 v2 H6 z M9.5 8.5 h2 v2 h-2 z M13 8.5 h2 v2 h-2 z M16.3 8.5 h1.7 v2 h-1.7 z M6 12 h2 v2 H6 z M16 12 h2 v2 h-2 z M8.5 15.2 h7 v1.6 h-7 z"/>)" },
    { "check",
      R"(<path fill="none" stroke="%C%" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" d="M5 12.5 l4.5 4.5 L19 7.5"/>)" },
    { "chevron",
      R"(<path fill="none" stroke="%C%" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round" d="M9.5 6 l6 6 -6 6"/>)" },
    { "bolt",
      R"(<path fill="%C%" d="M13.5 2 5 13.5 h6 L10 22 l9 -12 h-6.2 z"/>)" },
    { "list",
      R"(<path fill="%C%" d="M4 6 h2 v2 H4 z M8 6.2 h12 v1.6 H8 z M4 11 h2 v2 H4 z M8 11.2 h12 v1.6 H8 z M4 16 h2 v2 H4 z M8 16.2 h12 v1.6 H8 z"/>)" },
    { "columns",
      R"(<path fill="%C%" fill-rule="evenodd" d="M3 6 a2 2 0 0 1 2 -2 h14 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z M5 6 v12 h3.6 V6 z M10.4 6 v12 h3.2 V6 z M15.4 6 v12 H19 V6 z"/>)" },
    { "gallery",
      R"(<path fill="%C%" d="M3 5 a2 2 0 0 1 2 -2 h14 a2 2 0 0 1 2 2 v9 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z M4 18 h4 v3 H4 z M10 18 h4 v3 h-4 z M16 18 h4 v3 h-4 z"/>)" },
    { "sort",
      R"(<path fill="%C%" d="M4 6 h16 v1.8 H4 z M4 11.1 h11 v1.8 H4 z M4 16.2 h6 V18 H4 z"/>)" },
    { "eject",
      R"(<path fill="%C%" d="M12 4 l8 9 H4 z M4 16 h16 v3 H4 z"/>)" },
    // Pro features' menu rows.
    { "search",
      R"(<path fill="%C%" fill-rule="evenodd" d="M10 3.5 a6.5 6.5 0 0 1 5.2 10.4 l4.6 4.6 -1.3 1.3 -4.6 -4.6 A6.5 6.5 0 1 1 10 3.5 z M10 5.4 a4.6 4.6 0 1 0 0 9.2 a4.6 4.6 0 0 0 0 -9.2 z"/>)" },
    { "tag",
      R"(<path fill="%C%" fill-rule="evenodd" d="M3.5 4.5 a1 1 0 0 1 1 -1 h7 l9 9 a1 1 0 0 1 0 1.4 l-6.6 6.6 a1 1 0 0 1 -1.4 0 l-9 -9 z M8 6 a2 2 0 1 0 0 4 a2 2 0 0 0 0 -4 z"/>)" },
    { "chart",
      R"(<path fill="%C%" d="M3.5 3.5 h8 v8 h-8 z M12.5 3.5 h8 v5 h-8 z M12.5 9.5 h8 v11 h-8 z M3.5 12.5 h8 v8 h-8 z"/>)" },
    { "compare",
      R"(<path fill="%C%" d="M3.5 4 h7 v16 h-7 z M13.5 4 h7 v16 h-7 z M5 7 v1.6 h4 V7 z M15 7 v1.6 h4 V7 z M5 10.5 v1.6 h4 v-1.6 z M15 13 v1.6 h4 V13 z" fill-opacity="0.9"/>)" },
    { "lock",
      R"(<path fill="%C%" fill-rule="evenodd" d="M7.5 10 V7.5 a4.5 4.5 0 0 1 9 0 V10 h1 a1 1 0 0 1 1 1 v8.5 a1 1 0 0 1 -1 1 h-11 a1 1 0 0 1 -1 -1 V11 a1 1 0 0 1 1 -1 z M9.4 10 h5.2 V7.5 a2.6 2.6 0 0 0 -5.2 0 z"/>)" },
    { "extract",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 5 a1.5 1.5 0 0 1 1.5 -1.5 h13 A1.5 1.5 0 0 1 20 5 v2 a1.5 1.5 0 0 1 -1 1.4 V18 a2.5 2.5 0 0 1 -2.5 2.5 h-9 A2.5 2.5 0 0 1 5 18 V8.4 A1.5 1.5 0 0 1 4 7 z M11.1 10 h1.8 v4.2 l1.6 -1.6 1.2 1.2 L12 17.5 8.3 13.8 l1.2 -1.2 1.6 1.6 z"/>)" },
};

const char *glyphSvg(const QString &key)
{
    for (const Glyph &glyph : kGlyphs) {
        if (key == QLatin1String(glyph.key))
            return glyph.svg;
    }
    return nullptr;
}

// File views keep the folder silhouette even for special locations. Use
// GIO's identities (including user-dir configuration), never display names.
QString contentFolder(QString name)
{
    if (name.endsWith(QLatin1String("-symbolic")))
        name.chop(9);
    if (name == QLatin1String("user-home"))
        return QStringLiteral("content-folder-home");
    for (const char *kind : {"documents", "download", "pictures", "music", "videos"}) {
        if (name == QLatin1String("folder-") + QLatin1String(kind))
            return QStringLiteral("content-folder-") + QLatin1String(kind);
    }
    if (name.startsWith(QLatin1String("folder")) || name == QLatin1String("inode-directory"))
        return QStringLiteral("content-folder");
    return {};
}

QString folderSvg(const QString &glyph, bool details)
{
    // The back tab sits behind a rounded front panel. Two solid tones stay
    // legible at small sizes, without shadows or a fixed theme background.
    QString svg = QStringLiteral(
        "<path fill=\"%B%\" d=\"M2 6.5 A2 2 0 0 1 4 4.5 H9 "
        "a2 2 0 0 1 1.5.7 l1.3 1.6 H20 a2 2 0 0 1 2 2 V18 "
        "a2 2 0 0 1 -2 2 H4 a2 2 0 0 1 -2 -2 z\"/>"
        "<rect fill=\"%C%\" x=\"2\" y=\"9\" width=\"20\" height=\"11\" rx=\"2\"/>");

    // At 16/18px an emblem competes with the silhouette. Reveal the detail
    // once there is enough room to render its strokes cleanly.
    if (!details)
        return svg;

    QString emblem;
    if (glyph == QLatin1String("content-folder-documents"))
        emblem = QStringLiteral("<rect x=\"9\" y=\"11\" width=\"6\" height=\"7\" rx=\".7\"/>"
                                "<path d=\"M10.7 13.4 h2.6 M10.7 15.6 h2.6\"/>");
    else if (glyph == QLatin1String("content-folder-download"))
        emblem = QStringLiteral("<path d=\"M12 11 v5 M9.5 13.5 12 16 l2.5-2.5 "
                                "M9 17.8 h6\"/>");
    else if (glyph == QLatin1String("content-folder-pictures"))
        emblem = QStringLiteral("<rect x=\"8\" y=\"11.5\" width=\"8\" height=\"6\" rx=\".7\"/>"
                                "<circle cx=\"10.3\" cy=\"13.4\" r=\".65\" fill=\"%E%\" stroke=\"none\"/>"
                                "<path d=\"M8.5 17 11 14.5 l1.6 1.5 1.4-1 1.5 2\"/>");
    else if (glyph == QLatin1String("content-folder-music"))
        emblem = QStringLiteral("<path d=\"M11 16.6 v-5 l4-1 v5\"/>"
                                "<g fill=\"%E%\" stroke=\"none\">"
                                "<ellipse cx=\"9.8\" cy=\"16.8\" rx=\"1.7\" ry=\"1.2\"/>"
                                "<ellipse cx=\"13.8\" cy=\"15.8\" rx=\"1.7\" ry=\"1.2\"/></g>");
    else if (glyph == QLatin1String("content-folder-videos"))
        emblem = QStringLiteral("<path fill=\"%E%\" stroke=\"none\" d=\"M10 11.3 15.5 14.5 10 17.7 z\"/>");
    else if (glyph == QLatin1String("content-folder-home"))
        emblem = QStringLiteral("<path d=\"M8.3 14 12 10.8 15.7 14 "
                                "M9.3 13.4 v4.4 h5.4 v-4.4 M12 17.8 v-2.5\"/>");
    if (!emblem.isEmpty())
        svg += QStringLiteral("<g fill=\"none\" stroke=\"%E%\" stroke-width=\"1.15\" "
                              "stroke-linecap=\"round\" stroke-linejoin=\"round\">%1</g>").arg(emblem);
    return svg;
}

// One GIO icon-name candidate → a glyph key, or empty for "no opinion".
QString glyphForName(QString name)
{
    if (name.endsWith(QLatin1String("-symbolic")))
        name.chop(9);

    // A glyph key asked for by name (the menus' icons) is itself.
    if (glyphSvg(name))
        return name;

    // The app's own chrome (not a GIO name): the view-switch button.
    if (name == QLatin1String("view-grid")) return QStringLiteral("grid");
    if (name == QLatin1String("view-sidebar")) return QStringLiteral("sidebar");

    // The sidebar's specials and everything folder-ish.
    if (name == QLatin1String("user-home")) return QStringLiteral("home");
    if (name == QLatin1String("folder-documents")) return QStringLiteral("text");
    if (name == QLatin1String("folder-download")) return QStringLiteral("downloads");
    if (name == QLatin1String("folder-music")) return QStringLiteral("audio");
    if (name == QLatin1String("folder-pictures")) return QStringLiteral("image");
    if (name == QLatin1String("folder-videos")) return QStringLiteral("video");
    if (name == QLatin1String("document-open-recent")) return QStringLiteral("clock");
    if (name == QLatin1String("user-bookmarks")) return QStringLiteral("bookmark");
    if (name.startsWith(QLatin1String("starred")) || name.startsWith(QLatin1String("star")))
        return QStringLiteral("star");
    // Order matters: "user-trash-full" also startsWith "user-trash".
    if (name.startsWith(QLatin1String("user-trash-full"))) return QStringLiteral("trash-full");
    if (name.startsWith(QLatin1String("user-trash"))) return QStringLiteral("trash");
    if (name.startsWith(QLatin1String("network-"))) return QStringLiteral("network");
    if (name.startsWith(QLatin1String("folder")) || name == QLatin1String("inode-directory"))
        return QStringLiteral("folder");

    if (name.startsWith(QLatin1String("drive-")) || name.startsWith(QLatin1String("media-"))
        || name.startsWith(QLatin1String("phone")) || name.startsWith(QLatin1String("camera")))
        return QStringLiteral("drive");

    if (name.startsWith(QLatin1String("image-"))) return QStringLiteral("image");
    if (name.startsWith(QLatin1String("video-"))) return QStringLiteral("video");
    if (name.startsWith(QLatin1String("audio-"))) return QStringLiteral("audio");
    if (name.startsWith(QLatin1String("font-"))) return QStringLiteral("text");
    if (name == QLatin1String("application-x-executable")) return QStringLiteral("terminal");
    if (name == QLatin1String("application-pdf")) return QStringLiteral("text");

    if (name.startsWith(QLatin1String("package-")) || name.contains(QLatin1String("archive"))
        || name.contains(QLatin1String("compressed")) || name.contains(QLatin1String("-zip")))
        return QStringLiteral("archive");

    if (name.startsWith(QLatin1String("text-x-")) || name.contains(QLatin1String("script")))
        return name == QLatin1String("text-x-generic") ? QStringLiteral("text")
                                                       : QStringLiteral("code");
    if (name.startsWith(QLatin1String("text-"))) return QStringLiteral("text");

    return {};
}

} // namespace

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap IconImageProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int edge = requestedSize.width() > 0 ? requestedSize.width() : kDefaultSize;

    const qsizetype queryStart = id.indexOf(QLatin1Char('?'));
    const QUrlQuery query(queryStart < 0 ? QString() : id.mid(queryStart + 1));
    QPixmap pixmap = !query.hasQueryItem(QStringLiteral("c"))
        ? themedPixmap(id, edge)
        : glyphPixmap(id.left(queryStart), query.queryItemValue(QStringLiteral("c")), edge,
                      query.queryItemValue(QStringLiteral("style")) == QLatin1String("content"),
                      edge >= 24 && query.queryItemValue(QStringLiteral("detail")) != QLatin1String("simple"));

    if (size)
        *size = pixmap.size();
    return pixmap;
}

QPixmap IconImageProvider::themedPixmap(const QString &names, int edge) const
{
    QIcon icon;
    const QStringList candidates = names.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &name : candidates) {
        if (QIcon::hasThemeIcon(name)) {
            icon = QIcon::fromTheme(name);
            break;
        }
    }

    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));

    QPixmap pixmap = icon.pixmap(QSize(edge, edge));

    if (pixmap.isNull()) {
        // A themeless system would otherwise render a grid of broken-image
        // glyphs. A blank square of the right size is a better failure.
        pixmap = QPixmap(edge, edge);
        pixmap.fill(Qt::transparent);
    }
    return pixmap;
}

QPixmap IconImageProvider::glyphPixmap(const QString &names, const QString &colorHex, int edge,
                                      bool content, bool details)
{
    QString glyph;
    const QStringList candidates = names.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &name : candidates) {
        glyph = content ? contentFolder(name) : QString();
        if (glyph.isEmpty())
            glyph = glyphForName(name);
        if (!glyph.isEmpty())
            break;
    }
    if (glyph.isEmpty())
        glyph = QStringLiteral("file");

    const bool folder = glyph.startsWith(QLatin1String("content-folder"));
    const QString cacheKey = glyph + QLatin1Char('|') + colorHex + QLatin1Char('|')
                             + QString::number(edge)
                             + (folder && details ? QLatin1String("|full") : QLatin1String("|simple"));
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_cache.constFind(cacheKey);
        if (it != m_cache.constEnd())
            return it.value();
    }

    QColor color(QLatin1Char('#') + colorHex);
    if (!color.isValid())
        color = Qt::gray;

    // SVG has no 8-digit hex; the colour goes in as #rrggbb and any alpha
    // (a QML colour stringifies as #aarrggbb) rides on painter opacity.
    QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">%1</svg>")
        .arg(folder ? folderSvg(glyph, details) : QString::fromLatin1(glyphSvg(glyph)));
    svg.replace(QLatin1String("%C%"), color.name(QColor::HexRgb));
    if (folder) {
        svg.replace(QLatin1String("%B%"), color.darker(135).name(QColor::HexRgb));
        // Contrast against the front panel while retaining a little of its
        // hue. This also keeps emblems visible on white/black selected icons.
        const qreal luminance = .2126 * color.redF() + .7152 * color.greenF() + .0722 * color.blueF();
        const qreal target = luminance > .5 ? 0 : 1;
        const QColor emblem = QColor::fromRgbF(.4 * color.redF() + .6 * target,
                                               .4 * color.greenF() + .6 * target,
                                               .4 * color.blueF() + .6 * target);
        svg.replace(QLatin1String("%E%"), emblem.name(QColor::HexRgb));
    }

    QImage image(edge, edge, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg.toUtf8());
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(color.alphaF());
    renderer.render(&painter);
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(image);
    QMutexLocker locker(&m_mutex);
    m_cache.insert(cacheKey, pixmap);
    return pixmap;
}
