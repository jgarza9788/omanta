# Pro features for omanta

Advanced features under consideration for the `profeatures` branch. This file is a planning
list only; none of these features are built yet.

Features omanta already ships are left out (see `log.md` and `README.md`): vim keys, the four
views, split view with F5/F6, quick view, info panel, checksums, regex/glob search and filter,
batch rename, duplicate, spring-loaded folders, session restore, free-space bars,
compress/extract, TOML actions, starred, network places, and undo/redo.

**Priority:** P1 = high value and fits existing code · P2 = solid · P3 = ambitious

## Navigation and speed
- **P1 Fuzzy jump / command palette (Ctrl+P, `:`):** fuzzy-match folders you've visited
  (frecency, zoxide-style) and every action in `qml/Keymap.js`. Reuses the Keymap table and
  `NavigationHistory`.
- **P1 Vim marks:** `'a` sets a mark and `` `a `` jumps to it. Saved in `Settings`.
- **P1 Path bar completion:** Tab-complete in Ctrl+L (`PathBar.qml`).
- **P2 Flat view:** list every file below the current folder in one list, then sort or filter
  it. Reuses `SearchModel` crawling.
- **P2 View settings per folder:** remember the view, sort and hidden files for each folder.
- **P3 Workspaces:** named sets of tabs and panes you can save and restore. Builds on session
  restore.

## File operations
- **P1 Operation queue:** run copy/move jobs one after another, with pause, resume, reorder and
  a speed limit. Extends `FileOperationWorker` and `OpsPie.qml`.
- **P1 Verify after copy:** optionally checksum the copy against the source (`Checksum`).
- **P1 Compare and sync panes:** mark files that are only in one pane, newer, or the same, then
  sync one way or mirror. Uses the split view.
- **P2 Duplicate finder:** group files by size, then by hash, and offer to trash or hard-link the
  extras.
- **P2 Batch permissions and owner:** chmod/chown dialog with recursive and "folders only /
  files only" options. Extends `PropertiesDialog`.
- **P2 Symlink tools:** make an absolute or relative link, jump to the link's target, and flag
  broken links.
- **P2 Secure delete:** shred, with a warning that it doesn't work on SSD or CoW file systems.
- **P3 Run a command on the selection:** `!` prompt with `%f` / `%d` placeholders and output in
  a panel.

## Metadata and organisation
- **P1 Coloured tags:** freedesktop `user.xdg.tags` extended attributes, shown as a column, a
  filter and a sidebar entry. Generalises `StarredStore`.
- **P1 Extra columns:** image size, media length, audio artist/album and EXIF date, read through
  `QuickViewInfo`.
- **P2 Saved searches / smart folders:** save a search and its filters as a sidebar entry.
- **P2 Git status:** modified, untracked and ignored badges, plus an option to hide
  `.gitignore`d files.
- **P3 Metadata editor:** edit EXIF and audio tags; rename from metadata in `BatchRenamer`.

## Preview and inspection
- **P1 Syntax highlighting** for code in the quick view.
- **P1 Hex view** for binary files in the quick view.
- **P2 Disk usage map:** treemap or sunburst of folder sizes, reusing the folder-size code.
- **P2 Text and image diff:** compare two selected files side by side.
- **P3 More preview types:** office documents (sandboxed conversion), fonts, ePub.

## Remote, archives and storage
- **P1 Browse archives as folders:** open a zip or tar like a folder and copy out of it. Builds
  on `ArchiveEngine` listing.
- **P2 WebDAV and rclone remotes** in the Network sidebar (`NetworkModel`, `ServerStore`).
- **P2 Snapshots and versions:** browse btrfs/snapper/timeshift snapshots of the current folder
  and restore a file.
- **P3 Encrypted vaults:** create and unlock gocryptfs/cryfs folders from the sidebar.

## Customisation and automation
- **P1 Keymap editor:** rebind keys in Preferences, written to a user file on top of
  `Keymap.js`.
- **P1 Embedded terminal pane** (F4) that follows the current folder, plus "open terminal here".
- **P2 Folder hooks:** run an action when files arrive in a folder, e.g. sort Downloads.
- **P3 Plugin scripting:** JS/Lua plugins that can add columns, previews and actions, going
  beyond the TOML actions.

## Ranked by helpfulness

Score = how helpful the feature is day to day (10 = most), sorted from most to least helpful.
The score ignores effort; Priority (above) weighs helpfulness against how well the feature
fits the existing code.

| Rank | Feature | Score | Priority | Group | Why |
|---:|---|---:|:---:|---|---|
| 1 | Fuzzy jump / command palette | 10 | P1 | Navigation | Reach any folder or action in a few keys |
| 2 | Operation queue | 9 | P1 | File operations | Big copies no longer fight each other; pause and resume |
| 3 | Compare and sync panes | 9 | P1 | File operations | Backups and folder mirroring without leaving the app |
| 4 | Embedded terminal pane | 9 | P1 | Customisation | Shell and files side by side, always in the same folder |
| 5 | Browse archives as folders | 9 | P1 | Remote and archives | Grab one file without extracting everything |
| 6 | Path bar completion | 8 | P1 | Navigation | Typing paths becomes fast and error-free |
| 7 | Coloured tags | 8 | P1 | Metadata | Organise across folders; xattrs other apps can read |
| 8 | Syntax highlighting | 8 | P1 | Preview | Code in quick view becomes readable at a glance |
| 9 | Disk usage map | 8 | P2 | Preview | See at once what is filling the disk |
| 10 | Git status | 8 | P2 | Metadata | See repo state while browsing source trees |
| 11 | Vim marks | 7 | P1 | Navigation | Instant hops between a few working folders |
| 12 | Extra columns | 7 | P1 | Metadata | Sort photos and media by what matters |
| 13 | Verify after copy | 7 | P1 | File operations | Confidence that backups and USB copies are intact |
| 14 | Keymap editor | 7 | P1 | Customisation | Make the vim keys fit your own habits |
| 15 | Duplicate finder | 7 | P2 | File operations | Reclaim space from repeated downloads and photos |
| 16 | Batch permissions and owner | 7 | P2 | File operations | Fix permissions on a whole tree in one step |
| 17 | Saved searches / smart folders | 7 | P2 | Metadata | Searches you repeat are one click away |
| 18 | View settings per folder | 6 | P2 | Navigation | Photos in grid, projects in list, automatically |
| 19 | Flat view | 6 | P2 | Navigation | Work on files buried in nested folders at once |
| 20 | Symlink tools | 6 | P2 | File operations | Relative links and broken-link checks for dotfile setups |
| 21 | Text and image diff | 6 | P2 | Preview | Spot what changed between two versions |
| 22 | Hex view | 5 | P1 | Preview | Inspect binaries; niche but quick to add |
| 23 | Run a command on the selection | 5 | P3 | File operations | One-off scripts without opening a terminal |
| 24 | Snapshots and versions | 5 | P2 | Remote and archives | Get an old file back, if snapshots are set up |
| 25 | Workspaces | 5 | P3 | Navigation | Switch between projects' sets of tabs |
| 26 | WebDAV and rclone remotes | 5 | P2 | Remote and archives | Cloud storage in the sidebar |
| 27 | More preview types | 4 | P3 | Preview | Office files, fonts and ePub without opening an app |
| 28 | Folder hooks | 4 | P2 | Customisation | Auto-sort Downloads; few people set this up |
| 29 | Metadata editor | 4 | P3 | Metadata | Fix EXIF and music tags; dedicated tools do it better |
| 30 | Encrypted vaults | 3 | P3 | Remote and archives | Private folders; a small audience |
| 31 | Plugin scripting | 3 | P3 | Customisation | Unlimited extension, but TOML actions cover most needs |
| 32 | Secure delete | 2 | P2 | File operations | Unreliable on SSD and CoW file systems |
