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
- Save discovered pages to Library with their title and poster, duplicate detection, and automatic gallery refresh.
- Listing-card discovery with two sample JSON profiles, per-video Play/Save actions, and bounded next-link/load-more pagination.
- User-provided listing profiles and a custom HLS discovery profile loaded from `~/.config/notabrowser/`, without rebuilding.

Folder hierarchies, tags, and video relationships have repository APIs; their editing UI is planned. The Player page supports local files and direct HTTP/HTTPS media URLs through libmpv. CLI bookmarks record the supplied metadata; Discover can save a page's extracted title and poster. Select a bookmark and choose **Discover & Play selected** to load its page and look for an HLS stream.

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

In builds without discovery, missing libsoup development files cause configuration to report that only local `file://` thumbnails and existing cache entries are supported. After installing libsoup, rerun configuration and rebuild to enable HTTP thumbnails.

## Add bookmarks

In **Discover**, load a page and choose **Save to Library** once its title appears.
If playback switches to Player automatically, return to Discover to save the page.
The app stores the webpage URL, page title (falling back to the URL), and poster
URL. It shows **Saved to Library** or **Already in Library** and refreshes the
gallery automatically. Existing bookmarks retain their original title and poster.
Saving is available when the SQLite library is enabled and does not require a
verified stream. A save already started finishes using the metadata captured at
the click, even if you navigate away; its status does not appear on the new page.
Errors appear beside the discovery status and the action can be retried.

Alternatively, add bookmarks through the CLI:

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

Playback starts muted. Use **M** or the mute button to enable audio. Volume changes preserve mute state. The centered transport row provides **−5 min**, **reverse 2×**, a large round **Play/Pause** button, **forward 2×**, and **+5 min**, in that order. The middle button changes to **Replay** at the end. Click an active 2× button again to return to normal forward playback. Play/Pause and the **P** shortcut reset fast/reverse mode to normal speed while toggling playback. Volume, speed selection (0.5×, 1×, 1.5×, 2×), and fullscreen sit below the transport row. Selecting a speed returns to forward playback. Reverse playback uses libmpv and depends on the file/stream supporting reverse decoding; it may be slower or unavailable for some streams. Seeking depends on the stream; live streams may have a limited seek window.

At the end of a video, the file stays loaded with its final frame visible. **P** or **Replay** starts it again. Fullscreen shows only the video; exiting restores the previous page and controls.

A working OpenGL desktop session is required. Errors appear below the playback controls; hover over a shortened message to read its full text.

libmpv saves resume state when replacing a playing file or closing the application. Resume state is separate from library bookmarks; temporary stream URLs can expire and require loading the webpage again.

## Keyboard shortcuts

Single-key shortcuts work throughout the main window and in fullscreen when focus is outside text fields and embedded web pages. Text fields and web pages receive their normal typing and navigation keys, including Q. Alt+1–4 page shortcuts remain available while typing. Shift variants are accepted; other Ctrl/Alt/Super combinations retain their normal behavior.

| Key | Action |
| --- | --- |
| **Alt+1** / **Alt+2** / **Alt+3** / **Alt+4** | Switch to Library / Discover / Player / Files, leaving fullscreen if needed. |
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
| User profiles | `$XDG_CONFIG_HOME/notabrowser/` | `~/.config/notabrowser/` |
| Browser website data | `$XDG_DATA_HOME/stream-gallery/browser/` | `~/.local/share/stream-gallery/browser/` |
| Browser cache | `$XDG_CACHE_HOME/stream-gallery/browser/` | `~/.cache/stream-gallery/browser/` |

Schema version 2 adds thumbnail URLs and a title-sort index. Existing version-1 databases migrate automatically while preserving bookmarks, folders, tags, and relationships. Use temporary XDG directories when experimenting with disposable data.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

With the library, player, and discovery enabled, seven C++ tests are registered:

| Test | Coverage |
| --- | --- |
| `database` | Persistence, migrations, atomic new/duplicate bookmark reporting, metadata preservation, relationships, FTS triggers/search, sorting, and pagination across 10,000 records. |
| `thumbnails` | Image scaling, disk-cache reuse, cancellation, missing images, and unsupported URI handling. |
| `adapter-profiles` | Config-directory discovery, filename ordering, listing pagination types, HLS override loading, and rejection of invalid, unsupported, and oversized profiles. |
| `hls-discovery` | Master/media classification, reference resolution, local HTTP redirects, invalid/oversized responses, request headers, and cancellation without a display. |
| `discovery` | Rendered DOM/title/poster extraction, verified playlists, cancellation, asynchronous saving/duplicates, metadata snapshots during navigation, save failure/retry, automatic gallery refresh, persisted library reopening, and local webpage-to-HLS playback when ffmpeg is installed. |
| `listings` | Config-loaded profile selection, two local listing fixtures, relative links/posters, missing titles, invalid URLs, per-card Play/Save, deduplication, next-link loops, page limits, dynamic button pagination, delayed DOM insertion, Stop/navigation, and destruction during extraction. |
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
5. Return to Discover and choose **Save to Library**. Confirm the page appears in Library without Refresh, save again to check duplicate feedback, then restart to check persistence.
6. Test shortcuts with a control focused and in fullscreen; use **Q** last because it quits immediately.

## Webpage HLS discovery

In **Discover**, enter an HTTP/HTTPS webpage URL and choose **Load & Play** (or press Enter). The embedded WebKitGTK view loads the page and runs its JavaScript. The app uses the [generic adapter profile](examples/adapters/generic-hls.json) to scan configured media sources, attributes, and quoted inline-script strings for `.m3u8` URLs. It resolves relative links and removes duplicates. Native workers fetch the candidates, reject invalid playlists, and distinguish master playlists from media playlists using their contents. The chooser shows verified results; the initial selection prefers a master and automatically starts native playback. Choose another result and **Play stream** to use an alternative.

Discover also shows a video poster, falling back to Open Graph/Twitter images when available. The selected master displays whether its inspected references are absolute, relative, or mixed. Relative references resolve against the final playlist URL after redirects. Original manifests are passed to libmpv without rewriting. Inspection is bounded to four concurrent requests, 1 MiB per playlist, five redirects, and a 10-second network timeout. See [adapter usage](examples/adapters/README.md) for the supported profile and local fixture runner.

Extraction retries every 500 ms within a 20-second extraction/verification window after loading. Candidate URLs are attempted once per extraction run. If the page needs interaction or a request fails temporarily, use its player in Discover, then choose **Extract & Play** to retry. **Stop loading** cancels loading, extraction, and pending verification. New navigation cancels stale results; page loading has a 30-second timeout.

The playlist inspector and native player receive the webpage referrer and browser user agent. Browser cookies stay in WebKit's website data store under the app's XDG data/cache directories; they are not copied to the inspector or libmpv. Streams that require browser cookies or are exposed only through network traffic, inaccessible frames, or protected players may not play through this DOM-only workflow. Unresolved HLS variables are unsupported. There is no network interception or yt-dlp fallback.

Library bookmarks retain their durable webpage URLs. Temporary manifest URLs are used for playback and are not saved as bookmarks.

## Video listing discovery

Open **Discover → Video listings**, select **Next-page links** or **Load-more
button**, enter a listing URL, and choose **Load listing**. Matching cards appear
above the browser preview with titles, posters, and **Play**/**Save** actions.
Play opens the durable video page in the existing HLS discovery tab; Save records
the card metadata in Library and refreshes the gallery. Save requires the library
build option. No stream URLs are saved from the listing.

The two embedded [sample profiles](examples/adapters/README.md#listing-profiles)
target the local fixture layouts. They are examples, not automatic support for
arbitrary websites. Copy one to the [user profile directory](#user-profile-files) and edit its selectors to match a target site. Changes to bundled profiles require a rebuild.
Both views share the browser website session. Listing pages do not automatically
trigger native playback.

Choose **Next page** or **Load more** to collect another page/batch. Requests are
user-triggered, deduplicated by normalized page URL, and capped at five pages or
batches and 120 cards per run. Each batch returns at most 24 new cards. Next links
must stay on the same origin; visited-page loops stop pagination. Load-more and
initial dynamic results use a DOM observer with an eight-second readiness limit;
page loading has a 30-second timeout. Choose **Find videos** to recheck the current
DOM, or **Stop** to cancel pending work while retaining collected cards. Loading
a new URL or changing profile clears the result set. Infinite scrolling and
production website profiles remain future work.

## User profile files

Place JSON profiles directly in `~/.config/notabrowser/` (or
`$XDG_CONFIG_HOME/notabrowser/` when set), then choose **Reload profiles** in the relevant Discover tab.
Copy `examples/adapters/listing-links.json` or `listing-load-more.json` to a
new `.json` filename and edit its `name`, selectors, and pagination settings.

For example, from the repository root:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/notabrowser"
cp examples/adapters/listing-links.json "${XDG_CONFIG_HOME:-$HOME/.config}/notabrowser/my-site.json"
```

Edit `my-site.json`, choose **Reload profiles**, and select its name in
**Discover → Video listings** before loading a listing URL.

Valid files appear after the built-in profiles in the listing profile chooser,
sorted by filename. Each profile declares `pagination.type` as `link` or `button`.

To customize video-page discovery, copy `examples/adapters/generic-hls.json` as
`generic-hls.json` in the same directory and edit its discovery settings. Keep
`interfaceVersion: 1` and `id: "generic-html-hls"`. This overrides the built-in
HLS profile; it does not add a separate HLS chooser or automatic site matching.

Files must be valid JSON with the required fields. Invalid, unsupported, or
oversized files (over 128 KiB) are ignored with a terminal diagnostic; HLS falls
back to the built-in profile. Up to 64 valid custom listing profiles are loaded.
Native network, pagination, and extraction safety limits still apply. Editing
user files requires **Reload profiles** in the relevant tab, but no restart or rebuild. Reloading stops current discovery and clears its results; choose **Find videos** or **Extract & Play** to extract again. The listing selection is preserved by filename when available, otherwise it falls back to Next-page links.

## Roadmap and contribution

The application includes the library/gallery, a libmpv player through `GtkGLArea`, WebKit DOM-based HLS discovery, and sample listing profiles with bounded pagination. Future work includes production website profiles, richer metadata, infinite scrolling, folder/tag editing, and release hardening. See [Plan.md](Plan.md) for the longer-term design; some planned features remain unimplemented.

See [Plan.md](Plan.md) for architecture and phase acceptance criteria, and [AGENTS.md](AGENTS.md) for contributor guidelines. Source modules live in `src/app/`, `src/ui/`, `src/database/`, `src/models/`, and `src/services/`; embedded assets are in `resources/`, and tests are in `tests/`.

The **Files** page browses local folders using the native file browser, including folder shortcuts and video/all-file filters. Double-click a file or choose **Play selected video** to open it in Player.
