# Stream Gallery

A native Linux video browser and media library, built incrementally from [Plan.md](Plan.md).

Phase 1 implements the GTK3 application shell: a resizable navigation sidebar, Library/Discover/Player stack pages, an empty library view, and a light/dark theme switch. Browser and player pages explicitly identify their unimplemented status. Theme selection currently lasts for the running session.

## Build and run

Install a C++17 compiler, CMake 3.16+, pkg-config, GTK3 development headers (3.22+), and GLib development tools including `glib-compile-resources`. On Debian/Ubuntu these are typically provided by `build-essential cmake pkg-config libgtk-3-dev libglib2.0-dev-bin`; on Fedora use `gcc-c++ cmake pkgconf-pkg-config gtk3-devel glib2-devel`.

```sh
cmake -S . -B build
cmake --build build
./build/stream-gallery
```

A graphical X11 or Wayland session is required. UI and CSS resources are embedded into the executable, so it can run from any working directory. Later-phase dependencies such as WebKitGTK, SQLite, libmpv, and yt-dlp are not required yet.

## Validation

```sh
ctest --test-dir build --output-on-failure
```

The UI smoke test constructs and destroys windows, checks all navigation pages, and verifies both theme settings with GTK warnings treated as failures. CMake uses `xvfb-run` automatically when installed; otherwise a graphical display is required and the test reports skipped if none is available.

For manual validation, launch the executable, switch between all three sidebar entries, toggle the theme, resize the sidebar/window, and close the application. Database persistence, imports, playback, and website browsing belong to subsequent phases.

Validation in the implementation environment: compilation succeeded; the UI smoke test was skipped because the configured display could not be opened. A Broadway fallback was also blocked by the environment's socket restrictions. Run the smoke test and manual checks in a desktop session before treating runtime acceptance as verified.
