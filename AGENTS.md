# Repository Guidelines

## Project Structure & Module Organization

Stream Gallery is a native C++17/GTK3 Linux application. Phases 1–3 provide the application shell, SQLite library, and paginated gallery. `Plan.md` describes later playback and website-discovery phases; `README.md` documents current usage.

- `src/app/`: startup and application lifecycle.
- `src/ui/`: main window and grid/list gallery.
- `src/database/`: migrations, repositories, and bookmark CLI.
- `src/models/`: shared data types.
- `src/services/`: thumbnail loading and caching.
- `resources/`: embedded GTK Builder XML and CSS.
- `tests/`: standalone C++ test executables registered with CTest.

## Build, Test, and Development Commands

Install CMake 3.16+, pkg-config, GTK3 3.22+, GLib 2.66+ development tools, and SQLite development headers with FTS5. Optional libsoup 3 development headers enable HTTP thumbnails.

```sh
cmake -S . -B build -DBUILD_LIBRARY=ON  # Configure library and gallery
cmake --build build                   # Compile
ctest --test-dir build --output-on-failure
./build/stream-gallery                # Launch desktop application
./build/stream-gallery-library bookmark 'https://example.com/video' 'Example'
./build/stream-gallery-library list
```

Press Refresh to display externally added bookmarks. `-DBUILD_LIBRARY=OFF` builds only the GTK shell. Keep generated files in `build/`.

## Coding Style & Architecture

Use four-space indentation, `PascalCase` class/file names, `.hpp` headers, and `camelCase` methods. Compiler warnings include `-Wall -Wextra -Wpedantic`; no formatter or linter is configured.

Use RAII for ownership. Keep GTK access on the main thread and database/network/image work in background tasks. Cancel superseded jobs and ignore stale callbacks. Bound gallery queries and caches. Preserve existing data through transactional, versioned migrations.

## Testing Guidelines

Tests use GLib assertions or exception-based checks; no external framework or coverage threshold is configured. Name files `*_tests.cpp`. Cover persistence, migrations, deduplication, FTS search, pagination, thumbnail caching, and asynchronous UI behavior. Isolate test data with temporary XDG directories.

UI tests use Adwaita with fatal warnings and automatically use `xvfb-run` when installed. Missing displays or restricted image codecs can cause skips; report them separately from passing tests. Manually verify visible UI changes.

## Commit & Pull Request Guidelines

History currently contains only `first commit`, so no established convention exists. Use concise, imperative subjects. PRs should describe behavior, implementation scope, validation, and limitations; link relevant issues and include screenshots for UI changes.

## Configuration & Future Work

Persistent data and thumbnails follow XDG data/cache directories under `stream-gallery/`. Never commit personal databases, credentials, or cookies. Validate URLs and pass subprocess arguments without shell interpolation. Follow `Plan.md` incrementally; keep future website adapters independent and durable page URLs separate from temporary playback URLs.
