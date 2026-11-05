# Stream Gallery

A native Linux video browser, bookmark library, and media player built with C++17 and GTK3. Browse webpages through WebKitGTK, extract HLS streams from their rendered DOM, and play them through libmpv. See [Plan.md](Plan.md) for the implementation roadmap and next milestones.

## Current status

The current build provides:

- Native window, resizable sidebar, Library/Discover/Player/Files navigation, and a session-only light/dark theme switch.
- SQLite bookmarks, canonical URL deduplication, transactional migrations, and full-text indexing.
- Grid/list gallery with selection, newest/oldest/title sorting and Recent playback history, prefix search, exact-name tag/folder filters, and 24-item pagination.
- Bookmark title/description editing and tag/top-level folder assignment, with transactional saves and removal/Undo.
- Background database queries and thumbnail loading, cancellation of obsolete work, and a bounded thumbnail cache.
- A command-line bookmark tool, plus database, thumbnail, player, DOM extraction, and UI tests.
- Native libmpv playback and WebKitGTK webpage loading with DOM-based HLS extraction.
- Local folder browsing with video/all-file filters and folder/selection restoration when switching pages.
- Save discovered pages to Library with their title and poster, duplicate detection, and automatic gallery refresh.
- Listing-card discovery with two sample JSON profiles, per-video Play/Save actions, and bounded next-link/load-more pagination.
- User-provided listing profiles and a custom HLS discovery profile loaded from `~/.config/notabrowser/`, without rebuilding.

Selected bookmarks support title/description, tag, and top-level folder editing, plus removal with Undo. Nested folder management remains planned. The Player page supports local files and direct HTTP/HTTPS media URLs through libmpv. CLI bookmarks record the supplied metadata; Discover can save a page's extracted title and poster. Select a bookmark and choose **Discover & Play selected** to load its page and look for an HLS stream.

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

GTK Builder XML, CSS, adapter profiles, and the HLS/listing extraction scripts are embedded in the executable, so it can run from any working directory. The commands above assume the repository root.

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

For the GTK shell with local file browsing but no playback, also set `-DBUILD_LIBRARY=OFF`. Re-enable the desired options when returning to the full build.

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

Open **Library** to browse saved bookmarks. Press **Refresh** after adding records
through the CLI or another application; saves made in Discover refresh it automatically.

- Choose **Grid** or **List**, then sort by newest, oldest, or title.
- Choose **Recent** in the same dropdown to show only played bookmarks, most recently played first. Search, Tag, Folder, pagination, editing, and Undo work in Recent too.
- Search matches title/description prefixes. Multiple terms must all match; FTS operators are not exposed.
- **Tag** and **Folder** suggest existing names as you type. Choose a suggestion to fill the complete name; each filter accepts one exact, case-sensitive name. They intersect with text search and each other. Surrounding spaces are trimmed; an empty field adds no restriction. Folder matches top-level memberships only, without including nested-folder contents.
- Changing search or either filter resets pagination. **Previous** and **Next** browse 24 records per page.
- Select a card to display its page URL. **Discover & Play selected** loads that durable webpage and looks for HLS streams.

### Edit metadata and assignments

Select a bookmark and choose **Edit selected**. Change its title, description,
tags, and top-level folders, then choose **Save**. All changes save together in
one background database transaction, then refresh the current gallery/search
results. The page URL and thumbnail stay unchanged. **Cancel** discards unsaved
edits. An initiated save continues across page navigation; errors remain in the
editor for retry.

Use the editor's **Choose or type a tag/folder** dropdowns to select existing
names, then click **Add tag** or **Add folder**. You can also type a new name
there. Add appends a line without duplicating an existing name; the changes take
effect only when you choose **Save**. **Cancel** discards them. Dropdowns show up
to 50 names; type a case-sensitive prefix to narrow the choices. Filter suggestions
use the same limit. Suggestions include unassigned names, and folder choices
include top-level names only, collapsing duplicate root names. Lookups run in
background workers and refresh when fields receive focus or Library refreshes.

Enter tags and folders one per line. Saving creates missing names and assigns
them. Blank lines are ignored, surrounding spaces are trimmed, and repeated names
do not create duplicate assignments. Names are case-sensitive; spaces and commas
inside a name are literal. Remove a line to unassign it, or clear a field to remove
all its assignments. Unassignment keeps the global tag/folder record.

Folder editing affects top-level memberships only and preserves existing nested
assignments. For duplicate-named top-level folders, retained memberships are
preserved, new assignments use the oldest matching folder, and the Folder filter
matches all roots with that name. Library folders are collections; they do not
create or modify directories in Files. Folder trees, rename/delete actions, and
global tag rename/delete actions are not available yet.

### Remove and Undo

Choose **Remove selected** to remove a bookmark from Library. **Undo removal**
restores its title, description, thumbnail, saved timestamp, and folder/tag
relationships. One Undo is retained until another removal succeeds or the
application closes. Navigation, search, filters, and Refresh retain it. Removing
a bookmark never deletes media files.

If the same URL was saved again, Undo reports a conflict and keeps the existing
bookmark and Undo snapshot. Missing folders/tags also prevent restoration.
Failures leave the database unchanged and Undo available. Restoration reuses the
original record ID when free; otherwise it allocates a new ID, which may change
the bookmark's position in newest/oldest sorting.

Thumbnail decoding and database access run outside the GTK main thread. Only the current page's images are requested. Thumbnails are scaled within 192 × 108 pixels, with a disk cache capped at 256 entries. Downloads have a 10-second session timeout and a 5 MiB input limit. Missing or invalid images retain a generic video icon.

## Browse local files

Open **Files** (or press **Alt+4**) to browse local folders with the native GTK
file chooser. It starts in your home directory and provides folder shortcuts,
a **Videos** filter, and an **All files** filter. Select a file and choose
**Play selected video**, or double-click it, to open it in Player. Activating a
folder navigates into it. The current folder and selected file are retained when
switching pages, including when returning from playback, for the current session.

Files remains available in builds without the player, with playback disabled.
Browsing local files does not add them to the bookmark library.

## Playback

Open **Player**, then choose **Open file…**, or enter a direct HTTP/HTTPS media URL and choose **Open URL**. libmpv handles local files, ordinary MP4 URLs, and HLS streams. For a webpage URL, use **Discover → Load & Play** or the selected bookmark's discovery action.

Playback starts muted. Use **M** or the mute button to enable audio. Volume changes preserve mute state. The centered transport row provides **−5 min**, **reverse 2×**, a large round **Play/Pause** button, **forward 2×**, and **+5 min**, in that order. The middle button changes to **Replay** at the end. Click an active 2× button again to return to normal forward playback. Play/Pause and the **P** shortcut reset fast/reverse mode to normal speed while toggling playback. Volume, speed selection (0.5×, 1×, 1.5×, 2×), and fullscreen sit below the transport row. Selecting a speed returns to forward playback. Reverse playback uses libmpv and depends on the file/stream supporting reverse decoding; it may be slower or unavailable for some streams. Seeking depends on the stream; live streams may have a limited seek window.

At the end of a video, the file stays loaded with its final frame visible. **P** or **Replay** starts it again. Fullscreen shows only the video; exiting restores the previous page and controls.

A working OpenGL desktop session is required. Errors appear below the playback controls; hover over a shortened message to read its full text.

Successful video-page playback updates the saved page's last-played time in a
background worker and refreshes Library automatically. Recent stores one latest
playback time per bookmark and survives restart. Loading failures, local files,
direct media URLs, and pages absent from Library when history is written do not
create history entries or bookmarks. Saving a page after playback does not
retroactively record it; play it again to include it in Recent. Removing a
bookmark removes its history; Undo restores that time along with its metadata.
Recent opens the durable webpage through discovery again, so expired stream URLs
are never used as bookmark identities. Pause/resume and EOF replay within an
already loaded file do not create additional history updates.

libmpv saves resume state when replacing a playing file or closing the application. Resume state is separate from library bookmarks; temporary stream URLs can expire and require loading the webpage again.

## Keyboard shortcuts

Single-key shortcuts work throughout the main window and in fullscreen when focus is outside text fields and embedded web pages. Text fields and web pages receive their normal typing and navigation keys, including Q. On Files, playback shortcuts are disabled so the file chooser keeps its normal navigation keys; Escape and page shortcuts remain available, and Q still quits when focus is outside a text field. Alt+1–4 page shortcuts remain available while typing. Shift variants are accepted; other Ctrl/Alt/Super combinations retain their normal behavior.

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

The current schema is version 3, which adds a last-played timestamp and Recent index. Existing version-1 and version-2 databases migrate automatically while preserving bookmarks, folders, tags, and relationships. Existing bookmarks start without playback history. Use temporary XDG directories when experimenting with disposable data.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

With the library, player, and discovery enabled, seven C++ tests are registered:

| Test | Coverage |
| --- | --- |
| `database` | Persistence, version-3 history migration, Recent ordering/filtering/pagination, history preservation through edits/duplicates/removal/Undo, atomic metadata/tag/folder editing, root-folder reuse and nested-membership preservation, tag normalization/deduplication, combined search/tag/folder filtering and pagination, FTS updates, transactional removal/restoration, relationship rollback, duplicate and reused-ID conflicts, missing-record errors, migrations, atomic new/duplicate bookmark reporting, metadata preservation, relationships, FTS triggers/search, sorting, and pagination across 10,000 records. |
| `thumbnails` | Image scaling, disk-cache reuse, cancellation, missing images, and unsupported URI handling. |
| `adapter-profiles` | Config-directory discovery, filename ordering, listing pagination types, HLS override loading, and rejection of invalid, unsupported, and oversized profiles. |
| `hls-discovery` | Master/media classification, reference resolution, local HTTP redirects, invalid/oversized responses, request headers, and cancellation without a display. |
| `discovery` | Rendered DOM/title/poster extraction, verified playlists, cancellation, asynchronous saving/duplicates, metadata snapshots during navigation, save failure/retry, automatic gallery refresh, persisted library reopening, and local webpage-to-HLS playback when ffmpeg is installed. |
| `listings` | Config-loaded profile selection, two local listing fixtures, relative links/posters, missing titles, invalid URLs, per-card Play/Save, deduplication, next-link loops, page limits, dynamic button pagination, delayed DOM insertion, Stop/navigation, and destruction during extraction. |
| `ui-smoke` | Window lifecycle, numeric locale initialization, navigation, gallery paging/search, metadata/tag/folder editing, cancelled edits, assignment persistence across window recreation, combined filters, tag/folder unassignment, and removal/Undo across page navigation, filtered removal, pagination updates, Undo conflicts/retry, destruction during removal, theme switching, window shortcuts, mute/volume synchronization, fullscreen restoration, video EOF retention/replay, transport seeking and reverse/forward speed state, local file playback and folder/selection restoration, successful playback appearing in Recent, failed loads excluded from history, and quit cleanup. |

When Python 3 is available, CTest also registers `adapter-runner` for the local
generic JSON adapter runner. See [the adapter example](examples/adapters/README.md)
for CLI usage, fixture mappings, supported operations, and integration limits.

UI tests use Adwaita and treat GTK warnings as failures. CMake runs them through `xvfb-run` when available; otherwise they require a working display. Missing displays cause a skip. Thumbnail tests also skip when permission restrictions prevent the system image codec sandbox from running. Review skipped tests separately from passing tests. The discovery test uses ffmpeg when available to generate a local HLS video fixture; without it, that playback subcheck is reported as skipped while DOM checks still run.

For manual validation:

1. Add bookmarks, search, switch views, resize the window/sidebar, and toggle the theme. Edit a bookmark and assign tags, filter by one tag with text search, then remove an assignment and check the filtered result disappears. Remove/Undo a bookmark and confirm tags return. Assign a top-level folder and combine its filter with Tag and search. Unassign it while filtered and confirm the result disappears; Undo a removal and confirm folder memberships return. Restart and confirm metadata and assignments persist.
2. Add more than 24 records to exercise pagination and supply an image URL to check thumbnails.
3. Open a local video. Check muted startup, volume, ±5-minute seeking, reverse/forward 2× toggles, speed selection, fullscreen, EOF retention, and replay.
4. Use **Discover → Load & Play** with a page exposing an `.m3u8` URL in its DOM. Check automatic native playback and **Stop loading**.
5. Return to Discover and choose **Save to Library**. Confirm the page appears in Library without Refresh, save again to check duplicate feedback, then restart to check persistence.
6. Browse Files, play a selected file, and return to confirm its folder and selection are retained. Switch to another page and back to check the same behavior.
7. Test shortcuts with a control focused and in fullscreen, plus Alt+1–4 while typing. Confirm Files retains normal chooser navigation; use **Q** last because it quits immediately.

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

Library supports title/description, tag, and top-level folder editing, combined exact-name tag/folder filters, and removal with a session Undo slot. Folder trees, folder rename/deletion, global tag renaming/deletion and multiple-tag filters remain future work.

Following milestones are a profile for a regularly used website, nested folder organization, richer metadata, discovery expansion, and release hardening. Recent playback history is implemented for saved bookmarks. See [Plan.md](Plan.md) for scope, acceptance criteria, and validation expectations.

See [Plan.md](Plan.md) for architecture and milestone acceptance criteria, and [AGENTS.md](AGENTS.md) for contributor guidelines. Source modules live in `src/app/`, `src/ui/`, `src/database/`, `src/models/`, and `src/services/`; embedded assets are in `resources/`, and tests are in `tests/`.
