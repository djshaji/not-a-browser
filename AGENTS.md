# Repository Guidelines

## Project Structure & Module Organization

Stream Gallery is a native C++17/GTK3 Linux application with a SQLite gallery, libmpv playback, and WebKitGTK DOM-based HLS discovery. `README.md` documents current behavior; `Plan.md` tracks the implemented baseline, the bookmark-management milestone, and later work. Treat planned features as unimplemented until verified in code.

- `src/app/`: startup and application lifecycle.
- `src/ui/`: main window, grid/list gallery, libmpv player, WebKit page/listing browsers, and the native local file browser (`FilesView`).
- `src/database/`: migrations, repositories, and bookmark CLI.
- `src/models/`: shared data types.
- `src/services/`: thumbnail loading/caching, bounded HLS fetching/inspection, asynchronous bookmark saves, and user profile loading (`AdapterProfiles`).
- `resources/`: embedded GTK Builder XML, CSS, and HLS/listing extraction scripts.
- `examples/adapters/`: embedded HLS/listing JSON profiles and their usage documentation.
- `tools/adapter_runner.py`: local-only Python HLS profile runner; it does not execute JavaScript or make network requests.
- `tests/`: standalone C++ test executables registered with CTest.
- `tests/fixtures/`: local HTML, playlist, and listing fixtures for adapter/discovery tests.

## Build, Test, and Development Commands

Install CMake 3.16+, pkg-config, GTK3 3.22+, GLib 2.66+ development tools, SQLite with FTS5, libmpv, libepoxy, and WebKitGTK 4.1 version 2.40+ development files. Discovery requires libsoup 3 development headers; they are optional for HTTP thumbnails in builds without discovery.

```sh
cmake -S . -B build -DBUILD_LIBRARY=ON  # Configure library and gallery
cmake --build build                   # Compile
ctest --test-dir build --output-on-failure
./build/stream-gallery                # Launch desktop application
./build/stream-gallery-library bookmark 'https://example.com/video' 'Example'
./build/stream-gallery-library list
```

Press Refresh to display externally added bookmarks. Library, player, discovery, and testing options default to `ON`. Use `-DBUILD_PLAYER=OFF -DBUILD_DISCOVERY=OFF` for a gallery-only build; also set `-DBUILD_LIBRARY=OFF` for the GTK shell with local file browsing and playback disabled. Discovery requires the player. Keep generated files in `build/`.

## Coding Style & Architecture

Use four-space indentation, `PascalCase` class/file names, `.hpp` headers, and `camelCase` methods. Compiler warnings include `-Wall -Wextra -Wpedantic`; no formatter or linter is configured.

Use RAII for ownership. Keep GTK access on the main thread and database/network/image work in background tasks. Cancel superseded jobs and ignore stale callbacks. Bound gallery queries and caches. Preserve existing data through transactional, versioned migrations.

Preserve player lifecycle handling, GtkGLArea/OpenGL ownership, and asynchronous libmpv event delivery. Keep the centered transport order (−5 min, reverse 2×, Play/Pause, forward 2×, +5 min), update icons/accessibility labels from playback state, and synchronize speed/direction controls with observed libmpv properties. Discovery embeds `examples/adapters/generic-hls.json`, extracts structured DOM results, and verifies HLS in workers before playback. It does not use yt-dlp or network interception. Cancel loading/extraction/verification on navigation and destruction, and reject stale callbacks. Resolve playlist references against the final response URL after redirects.

Keep Files local-only and route regular-file activation through the existing player. Preserve its folder and selection across page switches and playback within the session. The file chooser remains available without `BUILD_PLAYER`, with its Play action disabled. Keep playback shortcuts out of Files so chooser navigation works normally; preserve Alt+1–4 page navigation, text-entry/WebKit typing guards, Escape fullscreen exit, and Q quit behavior. Initialize `LC_NUMERIC` to `C` before creating the player or gallery workers so libmpv works under desktop locales with non-dot decimal separators.

Explicit library saves run in database workers using a snapshot of page metadata. Finish an initiated save across navigation, refresh the gallery on success, and suppress messages for stale pages or destroyed widgets. Use atomic new/duplicate reporting and preserve existing bookmark metadata.

Keep listing profiles independent. Listing extraction uses `MutationObserver` readiness, bounded user-triggered pagination, same-origin next links, and URL deduplication. Cancel observers on navigation/Stop/destruction, preserve session guards for thumbnail/save callbacks, and route Play through durable video-page discovery.

Built-in adapter profiles are embedded at build time; rebuild after changing bundled JSON or extraction scripts. Keep the Python fixture runner's supported subset distinct from rendered WebKit extraction and native network verification. User profiles load from `$XDG_CONFIG_HOME/notabrowser/` (default `~/.config/notabrowser/`) when views are created or Reload profiles is clicked; `generic-hls.json` overrides HLS discovery, and other JSON files add listing profiles. Validate user profiles as data and serialize parsed JSON before inserting it into extraction scripts; never execute raw configuration text. Preserve schema validation, the 128 KiB per-file limit, the 64-custom-listing-profile limit, filename ordering, diagnostics for rejected files, and built-in fallback behavior. Read regular files only. Custom listing pagination behavior follows `pagination.type`, not the chooser index. Reload profiles cancels pending work and clears stale results in its view. Preserve listing selection by filename, falling back to the first built-in profile when removed. Editing user profiles requires neither restart nor rebuild. Arbitrary HLS adapter IDs are not implemented. Listing pagination is capped at five pages/batches, 120 total cards, and 24 new cards per batch; preserve these bounds when changing extraction.

## Testing Guidelines

Tests use GLib assertions or exception-based checks; no external framework or coverage threshold is configured. Name files `*_tests.cpp`. CTest registers `database`, `thumbnails`, `hls-discovery`, `discovery`, `listings`, and `ui-smoke` according to enabled features, plus `adapter-profiles` when discovery is enabled and `adapter-runner` when Python 3 is available. Cover persistence, migrations, deduplication, FTS, pagination, caching, HLS verification, DOM extraction/cancellation, playback lifecycle, shortcuts, and asynchronous UI behavior. Isolate test data with temporary XDG directories and prefer local fixtures/HTTP servers over live websites.

UI/discovery tests use Adwaita with fatal warnings and automatically use `xvfb-run` when installed. Missing displays or restricted image codecs can cause skips; report them separately from passing tests. Discovery's local HLS playback subcheck requires `ffmpeg`. Manually verify visible UI changes; playback needs a working OpenGL session. Check muted startup, ±5-minute seeking, reverse/forward 2× toggles, speed synchronization, fullscreen restoration, EOF/replay, shortcuts, and discovery cancellation when relevant. For Files changes, verify local file activation and folder/selection restoration after playback and page switches; run `ctest --test-dir build -R ui-smoke --output-on-failure`. Keep file chooser navigation and builds without the player working.

For profile-loader changes, run `ctest --test-dir build -R 'adapter-profiles|discovery|listings' --output-on-failure`. Cover isolated XDG config paths, missing directories, valid profiles, invalid/oversized input, ordering, fallback, and extraction through a selected custom listing profile.

For adapter-runner changes, run `python3 -B tests/adapter_runner_tests.py`. Changes to embedded profiles or DOM extraction also need the relevant native discovery/listing checks; the local Python runner does not interpret listing profiles or exercise WebKit behavior.

## Commit & Pull Request Guidelines

Use concise, imperative commit subjects, following recent history. PRs should describe behavior, implementation scope, validation, and limitations; link relevant issues and include screenshots for UI changes.

## Configuration & Future Work

Library/browser data and caches follow XDG data/cache directories under `stream-gallery/`; playback resume files use `$XDG_STATE_HOME/stream-gallery/watch-later/`. Never commit personal databases, credentials, or cookies. Validate URLs and pass subprocess arguments without shell interpolation. Browser cookies remain in WebKit's store and are not copied to libmpv; playback receives the page referrer and user agent. Keep durable bookmark page URLs separate from temporary playback URLs. Production website profiles, richer metadata, and infinite scrolling remain planned.

## Bookmark Management

Selected-bookmark title/description and tag editing is implemented through the Library editor and `LibraryRepository::updateBookmark` (with `updateMetadata` retained for metadata-only callers). Descriptions and assigned tag names are exposed by the `Video` model and repository queries; existing FTS triggers index edits. Preserve captured record identity, background writes that finish across navigation/destruction, retryable editor errors, and gallery refresh after success. No schema migration is needed for metadata editing.

Removal with Undo is implemented through `removeBookmark`/`restoreBookmark` and gallery database workers. Snapshot metadata, `added_at`, and folder/tag IDs in the same transaction as deletion. Restore atomically; reject duplicate URLs without overwriting the existing bookmark, and roll back entirely if a relationship cannot be restored. Reuse the original ID when free, otherwise allocate a new ID. Keep one in-memory Undo snapshot until the next successful removal or gallery destruction; failed removals/restores retain it. Preserve it through search, Refresh, and navigation. Disable conflicting edit/remove/Undo actions during mutations and while the editor is open; initiated mutations finish independently of query cancellation, with no widget access after destruction. Bookmark removal never deletes media files.

Save metadata and tag assignments atomically. The editor uses one tag name per line, ignoring blank lines; trim surrounding ASCII whitespace and deduplicate names using the existing unique tag constraint. Tag names are case-sensitive and may contain spaces or commas. Removing a line unassigns it only from that bookmark; retain global tag records so other assignments and Undo snapshots remain valid. Preserve assignments on duplicate imports and return assigned tags consistently from bookmark/gallery queries. Do not perform tag queries on the GTK thread.

The Tag field is an exact-name single-tag filter; empty means all bookmarks. Bind tag names as SQL parameters and combine the same predicates for counts and paginated rows. Invalidate pending results immediately on search/tag changes, debounce the new query, and reset pagination. Refresh after saves/removal/Undo using the current filters. Keep tag filtering through sort/view changes and Refresh. No schema migration is required. Global tag rename/deletion, a tag chooser, multiple-tag filters, and folder UI remain planned.

Keep canonical URLs and record identity stable during edits, update FTS consistently, and preserve metadata on duplicate imports. Preserve Undo lifetime and conflict behavior; restore metadata and folder/tag relationships transactionally. Bookmark removal must never remove local media files. Compose tag filters with search, sorting, counts, and bounded pagination, resetting or clamping pages when results shrink.

Run writes in database workers, capture record identity at action time, and reject stale UI callbacks after selection changes or destruction. Cover tag normalization/deduplication, transactional metadata/tag rollback, persistence, FTS edits, removal/Undo relationships and conflicts, combined filter pagination, and asynchronous lifetimes in database/UI tests. Manually verify visible controls. Follow Plan.md for milestone acceptance criteria and update README.md only as functionality becomes available.
