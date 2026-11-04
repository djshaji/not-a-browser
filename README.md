# Stream Gallery

A native Linux video browser, bookmark library, and media player built with C++17 and GTK3. Browse webpages through WebKitGTK, extract HLS streams from their rendered DOM, and play them through libmpv. Development follows [Plan.md](Plan.md).

## Current status

The current build provides:

- Native window, resizable sidebar, Library/Discover/Player navigation, and a session-only light/dark theme switch.
- SQLite bookmarks, canonical URL deduplication, transactional migrations, and full-text indexing.
- Grid/list gallery with selection, newest/oldest/title sorting, prefix search, and 24-item pagination.
- Background database queries and thumbnail loading, cancellation of obsolete work, and a bounded thumbnail cache.
- A command-line bookmark tool, plus database, thumbnail, player, DOM extraction, and UI tests.
- Native libmpv playback and WebKitGTK webpage loading with DOM-based HLS extraction.

Folder hierarchies, tags, and video relationships have repository APIs; their editing UI is planned. The Player page supports local files and direct HTTP/HTTPS media URLs through libmpv. Adding a bookmark records the supplied metadata. Select a bookmark and choose **Discover & Play selected** to load its page and look for an HLS stream.

## Requirements

- Linux with an X11 or Wayland desktop session to run the GUI.
- C++17 compiler, CMake 3.16+, and pkg-config.
- GTK3 3.22+ and GLib 2.66+ development files, including `glib-compile-resources`.
- SQLite development files with FTS5 support for the library build.
- libmpv and libepoxy development files, plus a working OpenGL desktop session for playback.
- WebKitGTK 4.1 version 2.40+ development files for webpage discovery.
- Optional libsoup 3 development files for HTTP/HTTPS thumbnail downloads in builds without discovery.
- Optional `xvfb-run` for display-based tests and `ffmpeg` for the end-to-end HLS test.

Common development packages:

| Distribution | Packages |
| --- | --- |
| Fedora | `gcc-c++ cmake pkgconf-pkg-config gtk3-devel glib2-devel sqlite-devel libsoup3-devel mpv-devel libepoxy-devel webkit2gtk4.1-devel` |
| Debian/Ubuntu | `build-essential cmake pkg-config libgtk-3-dev libglib2.0-dev-bin libsqlite3-dev libsoup-3.0-dev libmpv-dev libepoxy-dev libwebkit2gtk-4.1-dev` |

The player build requires libmpv and libepoxy development files (`mpv-devel libepoxy-devel` on Fedora). Disable it with `-DBUILD_PLAYER=OFF` for a gallery-only build. Discovery requires WebKitGTK 4.1 version 2.40 or newer. Disable it with `-DBUILD_DISCOVERY=OFF`. yt-dlp is not used.

Install the full development dependencies on Fedora:

```sh
sudo dnf install gcc-c++ cmake pkgconf-pkg-config gtk3-devel glib2-devel sqlite-devel libsoup3-devel mpv-devel libepoxy-devel webkit2gtk4.1-devel
```

## Build and run

```sh
cmake -S . -B build -DBUILD_LIBRARY=ON
cmake --build build
./build/stream-gallery
```

GTK Builder XML, CSS, and the HLS extraction script are embedded in the executable, so it can run from any working directory. The commands above assume the repository root.

| CMake option | Default | Behavior |
| --- | --- | --- |
| `BUILD_LIBRARY` | `ON` | Build the SQLite library, gallery, and bookmark CLI. Set `OFF` to omit the SQLite library and gallery. |
| `BUILD_PLAYER` | `ON` | Build the libmpv player with GtkGLArea rendering. |
| `BUILD_DISCOVERY` | `ON` | Build WebKit DOM-based HLS discovery when the player is enabled. |
| `BUILD_TESTING` | `ON` | Build and register tests with CTest. |

For a library and gallery build without playback or discovery:

```sh
cmake -S . -B build -DBUILD_PLAYER=OFF -DBUILD_DISCOVERY=OFF
cmake --build build
```

For the GTK shell only, also set `-DBUILD_LIBRARY=OFF`. Re-enable the desired options when returning to the full build.

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
5. Select a card to display its page URL. Choose **Discover & Play selected** to load that page in WebKit and look for a playable HLS stream.

Thumbnail decoding and database access run outside the GTK main thread. Only the current page's images are requested. Thumbnails are scaled within 192 × 108 pixels, with a disk cache capped at 256 entries. Downloads have a 10-second session timeout and a 5 MiB input limit. Missing or invalid images retain a generic video icon.

## Playback

Open **Player**, then choose **Open file…**, or enter a direct HTTP/HTTPS media URL and choose **Open URL**. libmpv handles local files, ordinary MP4 URLs, and HLS streams. For a webpage URL, use **Discover → Load & Play** or the selected bookmark's discovery action.

Playback starts muted. Use **M** or the mute button to enable audio. Volume changes preserve mute state. Controls provide play/pause, seeking, volume, and playback speeds of 0.5×, 1×, 1.5×, and 2×. Seeking depends on the stream; live streams may have a limited seek window.

At the end of a video, the file stays loaded with its final frame visible. **P** or **Replay** starts it again. Fullscreen shows only the video; exiting restores the previous page and controls.

A working OpenGL desktop session is required. Errors appear below the playback controls; hover over a shortened message to read its full text.

libmpv saves resume state when replacing a playing file or closing the application. Resume state is separate from library bookmarks; temporary stream URLs can expire and require loading the webpage again.

## Keyboard shortcuts

Shortcuts work throughout the main window, including in fullscreen and while text fields have focus. Shift variants are accepted; Ctrl/Alt/Super combinations retain their normal behavior. These single-key shortcuts also take precedence over typing their characters into an entry.

| Key | Action |
| --- | --- |
| **F** | Toggle video-only fullscreen. |
| **Escape** | Leave fullscreen and restore the previous page. |
| **P** | Toggle play/pause, or replay an ended video. |
| **M** | Toggle mute. |
| **+** / **−** | Raise/lower volume by 5 points, within 0–100. Numeric keypad add/subtract also work. |
| **↑** / **↓** | Seek forward/backward 300 seconds. |
| **→** / **←** | Seek forward/backward 60 seconds. |
| **1–9** | Jump to 10–90% of the video. Numeric keypad digits also work. |
| **Q** | Quit immediately without confirmation. |

## Storage

| Data | Location | Default |
| --- | --- | --- |
| Library | `$XDG_DATA_HOME/stream-gallery/library.db` | `~/.local/share/stream-gallery/library.db` |
| Thumbnails | `$XDG_CACHE_HOME/stream-gallery/thumbnails/` | `~/.cache/stream-gallery/thumbnails/` |
| Playback resume | `$XDG_STATE_HOME/stream-gallery/watch-later/` | `~/.local/state/stream-gallery/watch-later/` |
| Browser website data | `$XDG_DATA_HOME/stream-gallery/browser/` | `~/.local/share/stream-gallery/browser/` |
| Browser cache | `$XDG_CACHE_HOME/stream-gallery/browser/` | `~/.cache/stream-gallery/browser/` |

Schema version 2 adds thumbnail URLs and a title-sort index. Existing version-1 databases migrate automatically while preserving bookmarks, folders, tags, and relationships. Use temporary XDG directories when experimenting with disposable data.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

With the library, player, and discovery enabled, four C++ tests are registered:

| Test | Coverage |
| --- | --- |
| `database` | Persistence, migrations, deduplication, relationships, FTS triggers/search, sorting, and pagination across 10,000 records. |
| `thumbnails` | Image scaling, disk-cache reuse, cancellation, missing images, and unsupported URI handling. |
| `discovery` | Rendered JavaScript DOM, relative and escaped HLS URLs, deduplication, navigation cancellation, destruction during extraction, and local webpage-to-HLS playback when ffmpeg is installed. |
| `ui-smoke` | Window lifecycle, numeric locale initialization, navigation, gallery paging/search, theme switching, window shortcuts, mute/volume synchronization, fullscreen restoration, video EOF retention/replay, and quit cleanup. |

When Python 3 is available, CTest also registers `adapter-runner` for the local
generic JSON adapter runner. See [the adapter example](examples/adapters/README.md)
for CLI usage, fixture mappings, supported operations, and integration limits.

UI tests use Adwaita and treat GTK warnings as failures. CMake runs them through `xvfb-run` when available; otherwise they require a working display. Missing displays cause a skip. Thumbnail tests also skip when permission restrictions prevent the system image codec sandbox from running. Review skipped tests separately from passing tests. The discovery test uses ffmpeg when available to generate a local HLS video fixture; without it, that playback subcheck is reported as skipped while DOM checks still run.

For manual validation:

1. Add bookmarks, search, switch views, resize the window/sidebar, and toggle the theme. Restart and confirm bookmarks persist.
2. Add more than 24 records to exercise pagination and supply an image URL to check thumbnails.
3. Open a local video. Check muted startup, volume, seeking, fullscreen, EOF retention, and replay.
4. Use **Discover → Load & Play** with a page exposing an `.m3u8` URL in its DOM. Check automatic native playback and **Stop loading**.
5. Test shortcuts with a control focused and in fullscreen; use **Q** last because it quits immediately.

## Webpage HLS discovery

In **Discover**, enter an HTTP/HTTPS webpage URL and choose **Load & Play** (or press Enter). The embedded WebKitGTK view loads the page and runs its JavaScript. The app scans media sources, DOM attributes, and inline scripts for `.m3u8` URLs, resolves relative links, and removes duplicates. It automatically sends the first result to libmpv and switches to Player. Choose another result and **Play stream** to use an alternative.

Extraction retries every 500 ms for up to 20 seconds after loading. If the page needs interaction, use its player in Discover, then choose **Extract & Play**. **Stop loading** cancels both loading and extraction. New navigation cancels stale results; page loading has a 30-second timeout.

The native player receives the webpage referrer and browser user agent. Browser cookies stay in WebKit's website data store under the app's XDG data/cache directories; they are not copied to libmpv. Streams that require browser cookies or are exposed only through network traffic, inaccessible frames, or protected players may not play through this DOM-only workflow. There is no network interception or yt-dlp fallback.

Library bookmarks retain their durable webpage URLs. Temporary manifest URLs are used for playback and are not saved as bookmarks. Website-specific listing adapters, metadata import, and pagination remain planned.

## Roadmap and contribution

The application includes the library/gallery, a libmpv player through `GtkGLArea`, and an initial WebKit DOM-based HLS discovery workflow. Future work includes site-specific listing adapters, metadata import, pagination automation, folder/tag editing, and release hardening. See [Plan.md](Plan.md) for the longer-term design; some planned features remain unimplemented.

See [Plan.md](Plan.md) for architecture and phase acceptance criteria, and [AGENTS.md](AGENTS.md) for contributor guidelines. Source modules live in `src/app/`, `src/ui/`, `src/database/`, `src/models/`, and `src/services/`; embedded assets are in `resources/`, and tests are in `tests/`.
