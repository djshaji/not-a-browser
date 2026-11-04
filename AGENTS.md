# Repository Guidelines

## Project Structure & Module Organization

Stream Gallery is a native C++17/GTK3 Linux application with a SQLite gallery, libmpv playback, and WebKitGTK DOM-based HLS discovery. `README.md` documents current behavior; `Plan.md` describes the broader roadmap, including features not yet implemented.

- `src/app/`: startup and application lifecycle.
- `src/ui/`: main window, grid/list gallery, libmpv player, and WebKit page/listing browsers.
- `src/database/`: migrations, repositories, and bookmark CLI.
- `src/models/`: shared data types.
- `src/services/`: thumbnail loading/caching and bounded HLS fetching/inspection.
- `resources/`: embedded GTK Builder XML, CSS, and HLS/listing extraction scripts.
- `tests/`: standalone C++ test executables registered with CTest.

## Build, Test, and Development Commands

Install CMake 3.16+, pkg-config, GTK3 3.22+, GLib 2.66+ development tools, SQLite with FTS5, libmpv, libepoxy, and WebKitGTK 4.1 version 2.40+ development files. Optional libsoup 3 development headers enable HTTP thumbnails when available.

```sh
cmake -S . -B build -DBUILD_LIBRARY=ON  # Configure library and gallery
cmake --build build                   # Compile
ctest --test-dir build --output-on-failure
./build/stream-gallery                # Launch desktop application
./build/stream-gallery-library bookmark 'https://example.com/video' 'Example'
./build/stream-gallery-library list
```

Press Refresh to display externally added bookmarks. Library, player, discovery, and testing options default to `ON`. Use `-DBUILD_PLAYER=OFF -DBUILD_DISCOVERY=OFF` for a gallery-only build; also set `-DBUILD_LIBRARY=OFF` for the GTK shell alone. Discovery requires the player. Keep generated files in `build/`.

## Coding Style & Architecture

Use four-space indentation, `PascalCase` class/file names, `.hpp` headers, and `camelCase` methods. Compiler warnings include `-Wall -Wextra -Wpedantic`; no formatter or linter is configured.

Use RAII for ownership. Keep GTK access on the main thread and database/network/image work in background tasks. Cancel superseded jobs and ignore stale callbacks. Bound gallery queries and caches. Preserve existing data through transactional, versioned migrations.

Preserve player lifecycle handling, GtkGLArea/OpenGL ownership, and asynchronous libmpv event delivery. Discovery embeds `examples/adapters/generic-hls.json`, extracts structured DOM results, and verifies HLS in workers before playback. It does not use yt-dlp or network interception. Cancel loading/extraction/verification on navigation and destruction, and reject stale callbacks. Resolve playlist references against the final response URL after redirects.

Explicit library saves run in database workers using a snapshot of page metadata. Finish an initiated save across navigation, refresh the gallery on success, and suppress messages for stale pages or destroyed widgets. Use atomic new/duplicate reporting and preserve existing bookmark metadata.

Keep listing profiles independent. Listing extraction uses `MutationObserver` readiness, bounded user-triggered pagination, same-origin next links, and URL deduplication. Cancel observers on navigation/Stop/destruction, preserve session guards for thumbnail/save callbacks, and route Play through durable video-page discovery.

## Testing Guidelines

Tests use GLib assertions or exception-based checks; no external framework or coverage threshold is configured. Name files `*_tests.cpp`. CTest registers `database`, `thumbnails`, `hls-discovery`, `discovery`, `listings`, and `ui-smoke` according to enabled features, plus `adapter-runner` when Python 3 is available. Cover persistence, migrations, deduplication, FTS, pagination, caching, HLS verification, DOM extraction/cancellation, playback lifecycle, shortcuts, and asynchronous UI behavior. Isolate test data with temporary XDG directories and prefer local fixtures/HTTP servers over live websites.

UI/discovery tests use Adwaita with fatal warnings and automatically use `xvfb-run` when installed. Missing displays or restricted image codecs can cause skips; report them separately from passing tests. Discovery's local HLS playback subcheck requires `ffmpeg`. Manually verify visible UI changes; playback needs a working OpenGL session. Check muted startup, seeking, fullscreen restoration, EOF/replay, shortcuts, and discovery cancellation when relevant.

## Commit & Pull Request Guidelines

Use concise, imperative commit subjects, following recent history. PRs should describe behavior, implementation scope, validation, and limitations; link relevant issues and include screenshots for UI changes.

## Configuration & Future Work

Library/browser data and caches follow XDG data/cache directories under `stream-gallery/`; playback resume files use `$XDG_STATE_HOME/stream-gallery/watch-later/`. Never commit personal databases, credentials, or cookies. Validate URLs and pass subprocess arguments without shell interpolation. Browser cookies remain in WebKit's store and are not copied to libmpv; playback receives the page referrer and user agent. Keep durable bookmark page URLs separate from temporary playback URLs. Production website profiles, richer metadata, and infinite scrolling remain planned.
