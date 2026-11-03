# Repository Guidelines

## Project Structure & Module Organization

This repository currently contains only `Plan.md`, the design and phased implementation plan for Stream Gallery, a native Linux video browser and media library. No application source, build configuration, tests, or assets exist yet.

Follow the proposed layout when implementing features: `src/app/` for startup and settings, `src/ui/` for GTK widgets, `src/database/` for persistence, `src/discovery/` for adapters and extraction, `src/playback/` for libmpv integration, and `src/services/` and `src/models/` for shared logic and data. Place GTK Builder files, CSS, and icons in `resources/`, site definitions in `adapters/`, and tests and HTML fixtures in `tests/`.

## Build, Test, and Development Commands

No build or test commands are currently operational. Once the planned CMake configuration exists, use:

- `cmake -S . -B build` — configure an out-of-source build.
- `cmake --build build` — compile the application.
- `ctest --test-dir build --output-on-failure` — run tests once registered with CTest.

The planned stack is C++17, GTK3, WebKit2GTK 4.1, libmpv, SQLite3 with FTS5, libsoup 3, and GLib/GIO; stream resolution uses the external `yt-dlp` executable. Document the executable path when the first runnable target is added.

## Coding Style & Architecture

Use four-space indentation for C++ and follow the plan’s `PascalCase` class/file names (`VideoRepository.cpp`), `.hpp` headers, and `camelCase` methods. No formatter or linter is configured yet. Use RAII and explicit subsystem interfaces. Keep GTK updates on the main thread and blocking database, network, and subprocess work outside event handlers. Keep site-specific logic inside adapters and durable page URLs separate from temporary stream URLs.

## Testing Guidelines

No test framework or coverage threshold is configured. Add database and URL unit tests plus fixture-based adapter tests, using names such as `database_tests.cpp` and `url_tests.cpp`. Test deduplication, persistence, pagination limits, cancellation, and failure recovery. Build and run relevant tests for each implemented phase; manually verify GTK behavior when UI changes.

## Commit & Pull Request Guidelines

Git history is unavailable in this checkout, so no existing commit convention can be confirmed. Use concise, imperative commit subjects. PRs should describe the implemented phase, changed behavior, validation performed, and remaining limitations. Link relevant issues and include screenshots for visible UI changes.

## Security & Implementation Scope

Validate external URLs, pass subprocess arguments without shell interpolation, and keep credentials and raw cookies out of the library database. Follow `Plan.md` incrementally, completing a working phase before proceeding.
