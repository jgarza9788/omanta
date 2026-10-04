# Security TODO

Findings from the October 2026 review, parked for a later pass. Most severe first.

## 1. Thumbnailers run unsandboxed — Medium-High
`src/ThumbnailProvider.cpp:320` (`ThumbnailCache::renderViaThumbnailer`) runs every
`*.thumbnailer` Exec directly with `QProcess`. ffmpegthumbnailer, PDF and office
thumbnailers parse any file that shows up in a viewed folder (e.g. a fresh download)
with no click. Nautilus (gnome-desktop) wraps the same thumbnailers in bubblewrap +
seccomp with no network.

**Fix:** when `bwrap` is available, run under `bwrap --unshare-all --die-with-parent
--clearenv`, ro-binding `/usr`, `/etc/ld.so.cache`, `/etc/fonts` and the input file,
with only the output temp dir writable. Fall back with a one-time warning when bwrap
is missing.

## 2. In-process image decoding without limits — Medium
Image thumbnails are decoded with `QImage` inside the omanta process. A bug in a Qt
image plugin (tiff/heic/webp) compromises the file manager itself.

**Fix:** decode through `QImageReader` with `setAllocationLimit`, check the reported
size first; optionally route exotic formats through the sandboxed path from #1.

## 3. Thumbnail cache permissions and non-atomic writes — Low-Medium
`src/ThumbnailProvider.cpp:118-155` (`store`, `markFailed`) use `QDir().mkpath` and
`QImage::save`, so the umask decides the modes (0755 / 0644). The freedesktop
thumbnail spec requires 0700 directories, 0600 files, and write-to-temp-then-rename.
Thumbnails otherwise leak private image content to other local users.

**Fix:** `QSaveFile` + `setPermissions(ReadOwner | WriteOwner)` (pattern in
`src/ServerStore.cpp`), create directories 0700.

## 4. Archive encryption is ZipCrypto only — Low-Medium
`src/ArchiveEngine.cpp:288` sets `zip:encryption=zipcrypt`, which is broken by
known-plaintext attacks.

**Fix:** offer AES-256 zip (`zip:encryption=aes256`) and encrypted 7z in
`qml/CompressDialog.qml`, default to AES, keep ZipCrypto as "Legacy (Windows
Explorer compatible)".

## 5. Extraction keeps archive permissions; no bomb guard — Low
`src/ArchiveEngine.cpp:388` uses `ARCHIVE_EXTRACT_PERM`, so stored modes bypass the
umask (0777 world-writable files, setgid directories). Nothing limits the expanded
size (especially the raw `.gz` path).

**Fix:** strip group/other write and setuid/setgid after extraction (or apply a
masked mode); add a cancellable guard when expansion exceeds ~50× the archive size
or the destination's free space, and ask before continuing.

## 6. Release supply chain — Low
`packaging/PKGBUILD` builds from `#tag=v$pkgver` with `sha256sums=('SKIP')` (tags are
mutable); the README has users `curl` an unsigned `.pkg.tar.zst` with no checksum.

**Fix:** pin `#commit=<sha>` (or signed tags + `validpgpkeys`), publish SHA256SUMS
with each release and show the verification step in the README.

## 7. `omanta-switch` replaces symlinked config files — Low
`packaging/bin/omanta-switch:82-96` (`mktemp` + `mv`) and `:165-168` (`sed -i`)
replace `bindings.lua` / `omarchy-menu.jsonc`. A symlink into a dotfiles repo becomes
a regular file, and the mktemp file's 0600 mode is carried over.

**Fix:** resolve with `readlink -f` and write through it (`cat "$tmp" > "$target"`),
keeping the original mode.

## Not issues (checked)
- D-Bus `OpenPaths` / `ShowItems` trust any session-bus caller: the normal same-user
  boundary.
- Custom action TOMLs only load from root-owned `/usr/share/omanta/actions` or the
  user's own `~/.config/omanta/actions`; commands run as argv, terminal wrappers
  are single-quote escaped.
- Full-text search binds user input as SPARQL parameters.
- Copies/moves use NOFOLLOW, staged publication and `RENAME_NOREPLACE`.
