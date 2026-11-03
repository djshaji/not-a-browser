# Stream Gallery

A native Linux video browser and media library built with C++17 and GTK3. The project is being implemented incrementally from [Plan.md](Plan.md).

## Current status

Phases 1–3 provide:

- Native window, resizable sidebar, Library/Discover/Player navigation, and a session-only light/dark theme switch.
- SQLite bookmarks, canonical URL deduplication, transactional migrations, and full-text indexing.
- Grid/list gallery with selection, newest/oldest/title sorting, prefix search, and 24-item pagination.
- Background database queries and thumbnail loading, cancellation of obsolete work, and a bounded thumbnail cache.
- A command-line bookmark tool, plus database, thumbnail, and UI tests.

Folder hierarchies, tags, and video relationships have repository APIs; their editing UI is planned. Discover is explicitly marked as unimplemented. The Player page supports local files and direct HTTP/HTTPS media URLs through libmpv. Adding a bookmark currently records the supplied metadata; it does not parse the webpage or resolve a playable stream.

## Requirements

- Linux with an X11 or Wayland desktop session to run the GUI.
- C++17 compiler, CMake 3.16+, and pkg-config.
- GTK3 3.22+ and GLib 2.66+ development files, including `glib-compile-resources`.
- SQLite development files with FTS5 support for the library build.
- Optional libsoup 3 development files for HTTP/HTTPS thumbnail downloads.

Common development packages:

| Distribution | Packages |
| --- | --- |
| Fedora | `gcc-c++ cmake pkgconf-pkg-config gtk3-devel glib2-devel sqlite-devel libsoup3-devel` |
| Debian/Ubuntu | `build-essential cmake pkg-config libgtk-3-dev libglib2.0-dev-bin libsqlite3-dev libsoup-3.0-dev` |

The player build requires libmpv and libepoxy development files (`mpv-devel libepoxy-devel` on Fedora). Disable it with `-DBUILD_PLAYER=OFF` for a gallery-only build. WebKitGTK and yt-dlp belong to future phases.

## Build and run

```sh
cmake -S . -B build -DBUILD_LIBRARY=ON
cmake --build build
./build/stream-gallery
```

GTK Builder XML and CSS are embedded in the executable, so it can run from any working directory. The commands above assume the repository root.

| CMake option | Default | Behavior |
| --- | --- | --- |
| `BUILD_LIBRARY` | `ON` | Build the SQLite library, gallery, and bookmark CLI. Set `OFF` for the GTK shell only. |
| `BUILD_PLAYER` | `ON` | Build the libmpv player with GtkGLArea rendering. |
| `BUILD_TESTING` | `ON` | Build and register tests with CTest. |

Without libsoup development files, configuration reports that only local `file://` thumbnails and existing cache entries are supported. After installing libsoup, rerun configuration and rebuild to enable HTTP thumbnails.

## Add bookmarks

```sh
./build/stream-gallery-library bookmark 'https://example.com/watch?v=1' 'Example video'
./build/stream-gallery-library bookmark 'https://example.com/watch?v=2' 'With thumbnail' 'file:///absolute/path/thumbnail.png'
./build/stream-gallery-library list
```

CLI syntax:

```text
stream-gallery-library bookmark URL TITLE [THUMBNAIL_URL]
stream-gallery-library list
```

Bookmark URLs must use HTTP or HTTPS without embedded credentials. The optional thumbnail URL accepts local `file://` images or HTTP/HTTPS images. Remote images require libsoup support at build time.

URL normalization handles scheme/host casing, default ports, empty paths, and fragments while preserving query parameters. Re-adding an existing bookmark returns its ID and preserves its original title and thumbnail metadata.

## Browse the library

1. Add bookmarks through the CLI, then open Library and press **Refresh**.
2. Choose **Grid** or **List**, and sort by newest, oldest, or title.
3. Enter search terms to match title/description prefixes. Multiple terms must all match; FTS operators and folder/tag filters are not exposed.
4. Use **Previous** and **Next** to browse 24 records per page.
5. Select a card to display its page URL; selection does not start playback.

Thumbnail decoding and database access run outside the GTK main thread. Only the current page's images are requested. Thumbnails are scaled within 192 × 108 pixels, with a disk cache capped at 256 entries. Downloads have a 10-second session timeout and a 5 MiB input limit. Missing or invalid images retain a generic video icon.

## Playback

Open **Player**, then choose **Open file…**, or enter a direct HTTP/HTTPS media URL and choose **Open URL**. Ordinary MP4 and HLS streams are handled by libmpv; webpage URLs are not resolved yet.

Playback starts muted; use **M** or the mute button to enable audio. **+** raises volume by 5 points and **−** lowers it by 5 points within 0–100. Changing volume preserves mute state. Controls provide play/pause, seeking, volume, playback speed, and window fullscreen. At the end of a video, the file stays loaded with its final frame visible; **P** or **Replay** starts it again. Fullscreen shows only the video, hiding the sidebar, title bar, and controls. **F** toggles it; **Escape** leaves it and restores the previous page. Window shortcuts work on every page and while controls have focus, with or without Shift: **F** toggles fullscreen, **P** toggles play/pause, **Up/Down** seek forward/backward 300 seconds, **Right/Left** seek forward/backward 60 seconds, and **1–9** jump to 10–90% of the video. **Q** quits immediately, including in fullscreen. Ctrl/Alt/Super combinations retain their normal behavior.

A working OpenGL desktop session is required. Errors appear below the controls.

libmpv saves resume state when replacing a playing file or closing the application, under `$XDG_STATE_HOME/stream-gallery/watch-later/` (normally `~/.local/state/stream-gallery/watch-later/`). Resume is separate from library bookmarks. Gallery selection does not start playback.

## Storage

| Data | Location | Default |
| --- | --- | --- |
| Library | `$XDG_DATA_HOME/stream-gallery/library.db` | `~/.local/share/stream-gallery/library.db` |
| Thumbnails | `$XDG_CACHE_HOME/stream-gallery/thumbnails/` | `~/.cache/stream-gallery/thumbnails/` |

Schema version 2 adds thumbnail URLs and a title-sort index. Existing version-1 databases migrate automatically while preserving bookmarks, folders, tags, and relationships. Use temporary XDG directories when experimenting with disposable data.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

With `BUILD_LIBRARY=ON`, three tests are registered:

| Test | Coverage |
| --- | --- |
| `database` | Persistence, migrations, deduplication, relationships, FTS triggers/search, sorting, and pagination across 10,000 records. |
| `thumbnails` | Image scaling, disk-cache reuse, cancellation, missing images, and unsupported URI handling. |
| `ui-smoke` | Window lifecycle, navigation, grid/list modes, paging, search, accessibility name, and theme switching against an isolated library. |

UI tests use Adwaita and treat GTK warnings as failures. CMake runs them through `xvfb-run` when available; otherwise they require a working display. Missing displays cause a skip. Thumbnail tests also skip when permission restrictions prevent the system image codec sandbox from running. Review skipped tests separately from passing tests.

For manual validation, add bookmarks, search, switch views, select cards, resize the window/sidebar, toggle the theme, and restart. Confirm that bookmarks persist. Add more than 24 records to exercise pagination and supply an image URL to check thumbnails.

## Website discovery design

Website discovery is planned around a shared engine and one adapter per supported website. WebKitGTK will render JavaScript-enabled pages; adapters will extract video-page links, titles, thumbnails, durations, and pagination controls from the rendered DOM. Simple adapters will use JSON selectors; complex sites can use code implementations.

The engine will handle normalization, deduplication, cancellation, timeouts, and page limits. Dynamic pagination will observe DOM changes with `MutationObserver`, supporting next-page links, Load More buttons, and infinite scrolling. Adapter failures should remain isolated to the affected website. Validate the adapter interface against two sites with different listing behavior and saved HTML fixtures.

Discovery and playback resolution remain separate: the library stores durable webpage URLs, while yt-dlp will later resolve temporary playback URLs. Browser sessions will remain in WebKit's website data store. These capabilities are planned, not implemented.

## Roadmap and contribution

Phase 4 now has an initial libmpv player through `GtkGLArea`, transport controls, seeking, fullscreen, and libmpv resume state. Desktop playback validation remains required. Subsequent phases add stream resolution, WebKitGTK browsing, site adapters, pagination automation, library editing, and release hardening.

See [Plan.md](Plan.md) for architecture and phase acceptance criteria, and [AGENTS.md](AGENTS.md) for contributor guidelines. Source modules live in `src/app/`, `src/ui/`, `src/database/`, `src/models/`, and `src/services/`; embedded assets are in `resources/`, and tests are in `tests/`.
