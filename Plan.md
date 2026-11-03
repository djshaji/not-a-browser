# Stream Gallery — Complete GTK3 Linux Application Design

Architecture specification v1.0 · Linux · GTK3 · C++17 · SQLite · WebKitGTK · libmpv

Stream Gallery will be a native Linux application combining a traditional file manager, a video bookmark library, a website browser and a high-performance streaming player.

The application will let you browse supported video websites without relying on their video players for normal playback, import related videos from JavaScript-generated listings, organize videos into folders with multiple tags, and play HLS/m3u8 streams through libmpv when independently accessible.

The design is intended to be implemented incrementally using Codex, with a working application at the end of each major development phase.

## 1. Product definition

## Stream Gallery

Native video browser and media library

Proposed

Website browsing

Discover and import videos

Thumbnail gallery

Native grid and list views

Library management

Folders, tags and bookmarks

HLS playback

Integrated libmpv engine

Instant search

SQLite full-text index

Playback history

Resume and recently watched

### Key functional requirements

| Module           | Required behavior                                         |
| ---------------- | --------------------------------------------------------- |
| Website browser  | Open sites, navigate, handle JavaScript and pagination    |
| Discovery        | Detect video cards, extract metadata, load more results   |
| Native gallery   | Grid/list display, large collections, thumbnail caching   |
| Bookmarks        | Save video pages without downloading the video            |
| Organization     | Nested folders, multiple tags, smart collections          |
| Player           | HLS, MP4, local files, seek, pause, speed, fullscreen     |
| Library database | SQLite persistence, deduplication, search                 |
| Import           | Individual pages, playlists and related-video collections |
| Reliability      | Resume after restarts, recover from stream errors         |

## 2. Main window design

The application should resemble a native file manager more than a web browser.

Stream Gallery

Library / All Videos

NAVIGATION

All Videos

Discover

Bookmarks

Recent

Favorites

FOLDERS

Music

Documentaries

Tutorials

Watch Later

SOURCES

Website A

Website B

Local Files

All Videos

Thumbnail gallery

[Exploring Western Canada, Land of Big Mountains and Turquoise Lakes](https://images.openai.com/static-rsc-4/X8CZjC6YMox4tod8khZjALqk9Cl8hz_SVssYvH5pYb6zmML6G-gS0fVwFdgB5DMcXmGsapuam02zhPV6PncRnw1ZYLPUpDpp4jGNAJC1r4T1sawByhdE5MdZHE5WP4BU0O-o8ufcj2Qocou4EGU4OkdUiVPHS1yeSujB7mksW2Q?purpose=inline)

Nature Collection

Nature · Bookmarked

[Top 11 Modern Rock Albums To Have In Your Record Collection | The Revolver Club](https://images.openai.com/static-rsc-4/-X306z27xh4njpwd-qzlMaYNL8IaMMYVpl7vZbieQ2VA05yp-gGLak2xrs7888cveaS19to2zvNCfsdJlNSNDIDu2gZPC7pm8KRuF8fE1FPSTm0xUJU7Bi6qbehD6KIZ1ypMS6jj4hyJrUE3nY1hlqtomF0uEzLaBL91jNYjA0k?purpose=inline)

Guitar Performance

Music · 14 min

[Center Of Brussels From Above - Stock Video | Motion Array](https://images.openai.com/static-rsc-4/kn-86FonxBflFNQgbHlcQmqT0qGP_f87LIS1blcN67iRLRyjmTe5O4Xm09LY8GpRZbKCsd78wN5VAbfxtBIBwDLPqyPTzzALziq8BfKl3bQSXdL6C1jOexUHbsx44TJEGvYfJhdXXjuEcQWDsUcpWa350Iy4XpCYmOubFhM9B_0?purpose=inline)

City Documentary

Travel · 31 min

[Top Data Engineering Trends 2026: Innovations Unveiled](https://images.openai.com/static-rsc-4/Ic1x_XDytTxZrmD0ZyKhsOHjTJ7La1Q0wLcdoiqicWy2IJoVNwyONe49vH5OE1-sPrKdREIcfvcztnsBZGdCXUr3ayJd8plF6z5T1K-UwiBFdLEVJa_xemBxaW9JHmiL2-coBQQIqaNlD6NQ0xS4m02UiYRcp1Q0sYfclA72-EY?purpose=inline)

Linux Tutorial

Technology · 20 min

Selected video

Library ready

Grid · Sort: Recently added

Conceptual interface. Thumbnails are illustrative, not imported video records.

The main window has four functional regions:

- Left sidebar: Folder tree, saved sources, tag collections, history and playlists.
- Main content: Thumbnail gallery, website discovery results, or the embedded browser.
- Player region: Docked at the bottom, expanded into the main pane, or fullscreen.
- Toolbar: Navigation, search, view mode, sorting, import commands and settings.

In GTK3, I would use `GtkPaned` for resizable regions, `GtkStack` for switching between pages, `GtkTreeView` for the folder sidebar, and `GtkFlowBox` for the initial thumbnail gallery.

For very large libraries, replace an unbounded `GtkFlowBox` with a paged or recycled-item view so that thousands of GTK widgets are not created simultaneously.

## 3. Technology decisions

| Layer            | Selected technology    | Reason                                               |
| ---------------- | ---------------------- | ---------------------------------------------------- |
| Language         | C++17                  | RAII, safer ownership, simple C API interoperability |
| GUI              | GTK3                   | Native desktop UI                                    |
| Build            | CMake + pkg-config     | Portable Linux build                                 |
| Embedded browser | WebKit2GTK 4.1         | GTK3 and JavaScript-capable web pages                |
| Player           | libmpv                 | HLS support and programmable playback                |
| Video extraction | yt-dlp process         | Wide range of supported sites                        |
| Database         | SQLite3                | Lightweight and local                                |
| Search           | SQLite FTS5            | Indexed text search                                  |
| HTTP             | libsoup 3              | Network requests                                     |
| Image decoding   | GdkPixbuf              | GTK-compatible image objects                         |
| Async work       | GLib/GIO + worker pool | Responsive GUI                                       |

Important compatibility point: `webkit2gtk-4.1` is the GTK3/libSoup3 API, whereas `webkitgtk-6.0` is for GTK4. The former is the correct choice for this project.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://planet.webkitgtk.org\&sz=32)

planet.webkitgtk.org+1



I would keep WebKitGTK confined to the browser/discovery subsystem rather than using HTML for the entire user interface.

## 4. High-level architecture

Presentation Layer — GTK3

MainWindow · GalleryView · BrowserView · PlayerView · FolderTree

Application Services

Navigation · Library · Discovery · Import · Playback · Search

Site Adapter Engine

DOM extraction · Pagination

Stream Resolver

yt-dlp · Direct URLs

Media Player

libmpv · Seek · Events

Library Repository

SQLite · Transactions

Thumbnail Service

Download · Cache · Decode

Job Scheduler

Queue · Cancel · Retry

Persistence and System

SQLite Database · Thumbnail Cache · Configuration · Logs

All GTK calls must run on the GTK main thread. Background extraction, HTTP requests and database operations should communicate through thread-safe queues and dispatch UI updates through the GLib main context.

The browser subsystem and the player subsystem must remain independent: a browser page can be closed without deleting imported metadata or affecting an already-resolved playable stream.

## 5. Website discovery and JavaScript adapter system

This is the most important subsystem because websites differ considerably in how they present videos.

I recommend a three-stage discovery engine:

Stage 1 — Load website

WebKitGTK loads the actual page, runs JavaScript and maintains the normal browser session.

Stage 2 — Discover video cards

Site adapters find video links, titles, thumbnails, durations, related-video sections and pagination controls.

Stage 3 — Import into native gallery

Normalize URLs, eliminate duplicates, cache metadata and display discovered items as native GTK video cards.

### Site adapter model

Create a separate adapter for each supported website. Use JSON definitions for simple sites, with C++ adapter implementations for complicated cases.

Example of a hypothetical website adapter:

```

{
  "id": "example_site",
  "name": "Example Video Site",
  "domains": ["videos.example.com"],
  "enabled": true,
  "selectors": {
    "video_card": ".video-card",
    "video_link": "a.video-link",
    "title": ".video-title",
    "thumbnail": "img",
    "duration": ".duration",
    "related_container": ".related-videos",
    "load_more": "button.load-more"
  },
  "pagination": {
    "type": "button",
    "max_pages": 20,
    "wait_for_new_items_ms": 8000
  }
}

```

The selectors are illustrative; actual definitions must be developed from the relevant websites.

### JavaScript-driven pagination

Do not rely on a fixed delay after clicking "Load more". Some websites return additional content after 100 milliseconds, while others may take several seconds.

Instead, observe DOM changes using `MutationObserver`.

```

(() => {
  const selector = ".video-card";
  const loadMore = "button.load-more";

  const count = () =>
    document.querySelectorAll(selector).length;

  const before = count();
  const button = document.querySelector(loadMore);

  if (!button || button.disabled) {
    return Promise.resolve({
      status: "end",
      count: before
    });
  }

  return new Promise(resolve => {
    let completed = false;

    const finish = status => {
      if (completed) return;
      completed = true;
      observer.disconnect();
      clearTimeout(timeout);
      resolve({ status, count: count() });
    };

    const observer = new MutationObserver(() => {
      if (count() > before) {
        finish("loaded");
      }
    });

    observer.observe(document.body, {
      childList: true,
      subtree: true
    });

    const timeout = setTimeout(
      () => finish("timeout"),
      8000
    );

    button.click();
  });
})();

```

The native application runs this script asynchronously through WebKitGTK, processes the JSON result and performs the next import step. WebKitGTK's `evaluate_javascript` API supports asynchronous evaluation in a web view.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://webkitgtk.org\&sz=32)

webkitgtk.org



A production adapter should additionally support infinite scrolling, pagination links, AJAX-driven updates, iframes, duplicate detection and cancellation. The `max_pages` value must be enforced by the application, not trusted to the adapter alone.

### Native discovery mode

There should be two ways to browse websites:

Web mode: Displays the original website inside WebKitGTK. Useful for navigation, login and understanding the source.

Gallery mode: Displays discovered videos as native GTK thumbnails, bypassing the need to interact with the website's own listing UI.

An optional split-screen mode could show the original webpage alongside the imported native gallery.

For sites using protected video playback or requiring a particular advertising-supported player, retain an Open Original Page fallback.

## 6. Database design

I recommend treating videos, folders, tags and websites as distinct entities.

A video should be able to belong to multiple folders and have multiple tags. Folders and tags should not be encoded into a single text column.

### Primary SQLite schema

```

PRAGMA foreign_keys = ON;

CREATE TABLE sources (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    base_url TEXT NOT NULL,
    adapter_id TEXT,
    enabled INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE videos (
    id INTEGER PRIMARY KEY,
    source_id INTEGER REFERENCES sources(id),
    canonical_url TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL DEFAULT '',
    description TEXT NOT NULL DEFAULT '',
    thumbnail_url TEXT,
    thumbnail_path TEXT,
    duration_seconds REAL,
    added_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_played_at TEXT,
    last_checked_at TEXT,
    playback_position REAL NOT NULL DEFAULT 0,
    play_count INTEGER NOT NULL DEFAULT 0,
    is_favorite INTEGER NOT NULL DEFAULT 0,
    resolution_status TEXT NOT NULL DEFAULT 'unresolved'
);

CREATE TABLE folders (
    id INTEGER PRIMARY KEY,
    parent_id INTEGER REFERENCES folders(id)
        ON DELETE CASCADE,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE video_folders (
    video_id INTEGER NOT NULL REFERENCES videos(id)
        ON DELETE CASCADE,
    folder_id INTEGER NOT NULL REFERENCES folders(id)
        ON DELETE CASCADE,
    PRIMARY KEY (video_id, folder_id)
);

CREATE TABLE tags (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE video_tags (
    video_id INTEGER NOT NULL REFERENCES videos(id)
        ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags(id)
        ON DELETE CASCADE,
    PRIMARY KEY (video_id, tag_id)
);

CREATE TABLE collections (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    source_url TEXT,
    query_json TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE collection_videos (
    collection_id INTEGER NOT NULL
        REFERENCES collections(id) ON DELETE CASCADE,
    video_id INTEGER NOT NULL
        REFERENCES videos(id) ON DELETE CASCADE,
    sort_order INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (collection_id, video_id)
);

CREATE TABLE playback_history (
    id INTEGER PRIMARY KEY,
    video_id INTEGER NOT NULL REFERENCES videos(id)
        ON DELETE CASCADE,
    started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_position REAL NOT NULL DEFAULT 0,
    completed INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_videos_added
    ON videos(added_at DESC);

CREATE INDEX idx_videos_source
    ON videos(source_id);

CREATE INDEX idx_history_video
    ON playback_history(video_id, started_at DESC);

CREATE VIRTUAL TABLE videos_fts USING fts5(
    title,
    description,
    content='videos',
    content_rowid='id'
);

```

Add triggers to synchronize the FTS5 index on inserts, updates and deletes, and rebuild the index when migrating existing records. SQLite documents this requirement for external-content full-text tables.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.sqlite.org\&sz=32)

SQLite



For a complete schema, add `source_video_id`, a persistent `discovery_edges` table for related-video relationships, per-domain settings, import jobs and failed-resolution records.

### Example search capabilities

The application should support queries such as:

- `guitar` — Search video titles and descriptions.
- `tag:concert` — Only videos tagged concert.
- `folder:music` — Only videos in the music collection.
- `site:example.com` — Filter by source.
- `is:unwatched` — Never played.
- `duration:>600` — Videos longer than 10 minutes.

Implement these as a small query parser that combines FTS5 with relational filters rather than passing the entire user input directly into FTS5.

### Storage layout

Use the standard Linux XDG directories:

```
~/.config/stream-gallery/
    settings.ini
    adapters/
        example-site.json

~/.local/share/stream-gallery/
    library.db

~/.cache/stream-gallery/
    thumbnails/
    browser/
    temp/
```

Never store browser passwords or raw authentication cookies in SQLite. Use the WebKit website data store, with system credential storage where a separate secret is necessary.

## 7. libmpv playback design

The player must be a native GTK widget with its own controls, instead of an embedded webpage.

For the final design, use the libmpv render API with GTK3's `GtkGLArea`.

This provides a better path toward both X11 and Wayland than the older X11-window-ID embedding technique. mpv's documentation recommends its render API and describes the native window embedding limitations.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub+1



### Playback initialization sequence

1. Create the `GtkGLArea` and wait for its OpenGL context to become available.
2. Create and configure the `mpv_handle`.
3. Initialize mpv.
4. Create `mpv_render_context` using the OpenGL render API.
5. Implement mpv's OpenGL function resolver and render the frame into the GTK GL framebuffer.
6. Connect frame-update notifications to GTK redraw scheduling.
7. Process mpv events and property changes on a separate event-handling path.
8. Destroy the rendering context before destroying the mpv core.

The render API has strict OpenGL context and threading requirements. In particular, an mpv update callback must schedule drawing rather than directly call the rendering function.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub



### Player interface

Native libmpv video surface

03:42

13:50

1.0×

Conceptual player controls; the final video surface is rendered by libmpv.

### Playback URL resolution

Use this order:

1. Local file or direct media URL: send directly to libmpv.
2. Known HLS/MP4 stream: send to libmpv with the required authorized request context, where supported.
3. Supported webpage: ask yt-dlp to resolve a playable URL and metadata.
4. Unsupported webpage: attempt the site's permitted playback adapter.
5. Unavailable or protected stream: offer the original webpage.

Invoke yt-dlp as a subprocess using `GSubprocess`. Use structured output such as `--dump-single-json`, not human-oriented progress messages. The yt-dlp project explicitly recommends machine-readable interfaces for callers.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub



Keep the canonical page URL in the database. Cache resolved stream URLs only temporarily because they may expire.

### Seeking strategy

The player should expose two seek modes:

| Mode       | Purpose                                                         |
| ---------- | --------------------------------------------------------------- |
| Fast seek  | Responsive scrubbing, potentially landing near a keyframe       |
| Exact seek | More accurate positioning, potentially requiring extra decoding |

Also implement a buffered-range indicator, a seek debounce mechanism, resume-position persistence and optional disk cache.

For HLS, mpv can reuse buffered data for short seeks when its demuxer cache permits; seeking beyond the buffered range may require a new network request.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://mpv.io\&sz=32)

mpv.io



For the initial implementation, prioritize correct and stable seeking. Avoid building a custom HLS downloader or segment cache before measuring libmpv's behavior.

## 8. Recommended C++ project structure

Keep each subsystem independent and use explicit interfaces so Codex can work on one module without changing the entire application.

```

stream-gallery/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── architecture.md
│   ├── database.md
│   └── site-adapters.md
├── resources/
│   ├── application.css
│   ├── icons/
│   └── ui/
│       ├── main-window.ui
│       ├── player.ui
│       └── preferences.ui
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── Application.cpp
│   │   ├── ApplicationController.cpp
│   │   └── Settings.cpp
│   ├── ui/
│   │   ├── MainWindow.cpp
│   │   ├── Sidebar.cpp
│   │   ├── GalleryView.cpp
│   │   ├── VideoCard.cpp
│   │   ├── BrowserView.cpp
│   │   ├── PlayerView.cpp
│   │   ├── SearchBar.cpp
│   │   └── ImportDialog.cpp
│   ├── database/
│   │   ├── Database.cpp
│   │   ├── VideoRepository.cpp
│   │   ├── FolderRepository.cpp
│   │   ├── TagRepository.cpp
│   │   └── Migrations.cpp
│   ├── discovery/
│   │   ├── DiscoveryEngine.cpp
│   │   ├── SiteAdapter.cpp
│   │   ├── AdapterRegistry.cpp
│   │   ├── DOMExtractor.cpp
│   │   ├── PaginationController.cpp
│   │   └── UrlNormalizer.cpp
│   ├── playback/
│   │   ├── MpvPlayer.cpp
│   │   ├── MpvGLRenderer.cpp
│   │   ├── StreamResolver.cpp
│   │   └── PlaybackController.cpp
│   ├── services/
│   │   ├── ThumbnailService.cpp
│   │   ├── ImportService.cpp
│   │   ├── SearchService.cpp
│   │   └── JobScheduler.cpp
│   └── models/
│       ├── Video.hpp
│       ├── Folder.hpp
│       ├── Tag.hpp
│       └── Source.hpp
├── adapters/
│   └── example-site.json
├── scripts/
│   └── resolve_video.py
└── tests/
    ├── database_tests.cpp
    ├── discovery_tests.cpp
    ├── url_tests.cpp
    └── fixtures/
        └── sample-video-list.html

```

### Important architectural interfaces

I recommend designing these interfaces before writing the GUI implementation.

```

struct VideoMetadata {
    std::string canonicalUrl;
    std::string title;
    std::string description;
    std::string thumbnailUrl;
    std::optional<double> durationSeconds;
    std::string sourceId;
};

struct StreamInfo {
    std::string url;
    std::string mediaType;
    std::map<std::string, std::string> headers;
    std::optional<std::chrono::system_clock::time_point>
        expiresAt;
};

class SiteAdapter {
public:
    virtual ~SiteAdapter() = default;

    virtual bool supports(
        const std::string& url) const = 0;

    virtual std::string extractionScript() const = 0;

    virtual std::string loadMoreScript() const = 0;
};

class StreamResolver {
public:
    virtual ~StreamResolver() = default;

    virtual void resolveAsync(
        const std::string& pageUrl,
        std::function<void(
            std::optional<StreamInfo>,
            std::string error)> callback) = 0;
};

class Player {
public:
    virtual ~Player() = default;

    virtual void open(const StreamInfo& stream) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void stop() = 0;
};

```

These are starting interfaces, not a complete application implementation. In particular, callbacks need defined execution contexts, cancellation support and object-lifetime handling before production use.

## 9. Performance and reliability requirements

Responsive gallery

Priority 1

Use paginated database queries, lazy thumbnail decoding and a bounded image cache. Do not block GTK while fetching network thumbnails.

Fast seeking

Priority 1

Debounce repeated seeks while dragging. Distinguish pending seek requests from the actual playback position. Expose buffering status.

Resilient discovery

Priority 1

Use timeouts, cancellation, maximum page limits, URL deduplication and rate limiting. Import partial results even when subsequent pages fail.

Local-first library

Priority 1

Browse saved bookmarks and metadata offline. Only resolving or playing remote videos should require network access.

Security

Required

Restrict untrusted adapter code, validate URLs, avoid arbitrary command execution, protect browser session data, and never automatically bypass DRM, paywalls or authentication controls.

Suggested initial targets, to be verified on the intended Linux hardware: responsive scrolling with 10,000 saved items, no noticeable main-thread blocking during imports, and rapid playback controls when the network and media permit.

## 10. Incremental implementation plan for Codex

### 01

Application skeleton

CMake, GTK3 application startup, main window, sidebar, stack navigation, dark/light theme support.

Acceptance: Application launches reliably on Linux.

### 02

SQLite media library

Database migrations, video model, repositories, folders, tags, bookmarks, URL deduplication.

Acceptance: Library persists after restarting.

### 03

Native thumbnail gallery

Video cards, grid/list modes, selection, sorting, pagination, thumbnail cache, search.

Acceptance: Large imported collections can be browsed smoothly.

### 04

libmpv player

GtkGLArea renderer, playback controller, transport controls, seeking, fullscreen, resume.

Acceptance: Local MP4 and ordinary HLS VOD streams play correctly.

### 05

Stream resolution

yt-dlp subprocess, JSON parsing, stream selection, expiry and error handling.

Acceptance: Supported webpage URLs can be bookmarked and played.

### 06

WebKitGTK browser

Browser tab, navigation, session persistence, site policies, JavaScript execution.

Acceptance: Supported websites function in the embedded browser.

### 07

Video discovery engine

Adapter registry, DOM extraction, thumbnail metadata, related-video discovery.

Acceptance: A test website imports video cards into the gallery.

### 08

Load More automation

MutationObserver handling, button pagination, scroll pagination, cancellation and import limits.

Acceptance: Multiple pages can be imported without duplicates.

### 09

Library enhancements

Saved collections, tag editor, smart folders, history, favorites, resume and export/import.

Acceptance: Complete bookmark and organization workflow.

### 10

Hardening and packaging

Automated tests, error recovery, accessibility, performance profiling, application packaging.

Acceptance: Usable release build with tested X11/Wayland support.

Do not ask Codex to generate all ten phases in a single pass. In particular, video rendering and browser automation should be integrated and tested independently before combining them.

## 11. Codex project instructions

Below is an initial `AGENTS.md` you can place at the repository root. It gives Codex project-wide implementation constraints.

# Stream Gallery — Codex Project Instructions

## Project Goal

Build Stream Gallery, a native GTK3 Linux application for browsing online video websites, discovering video listings, bookmarking videos, organizing them in folders and tags, and playing supported video streams through libmpv.

The application must be a native desktop program, not an Electron application or a website wrapped in a browser.

## Technology

- C++17
- GTK3 C API
- CMake
- WebKit2GTK 4.1
- libmpv with OpenGL render API
- SQLite3 with FTS5
- GLib/GIO for asynchronous work
- libsoup 3 for HTTP operations
- yt-dlp as an external executable
- GdkPixbuf for thumbnail decoding

## Architecture Rules

1. Separate GUI, application services, database, discovery and playback into independent modules.
2. Use RAII for ownership of C/C++ resources wherever practical.
3. All GTK widget manipulation must happen on the GTK main thread.
4. Never perform blocking network, database or subprocess operations in GTK event handlers.
5. Use explicit interfaces and dependency injection for testable components.
6. Keep site-specific scraping logic inside adapters.
7. Store canonical video page URLs separately from temporary playback URLs.
8. Never store credentials or raw cookies in the media library database.
9. Validate externally supplied URLs and never construct shell command strings from them.
10. Ensure proper cleanup of mpv, OpenGL resources, WebKit objects and asynchronous jobs.

## User Interface

Use a native file-manager-like layout:

- Left sidebar for folders, websites, tags, history and favorites.
- Main thumbnail gallery with grid and list modes.
- Website discovery browser.
- Integrated video player with seeking and fullscreen.
- Search, sort, filter and import controls.

Build UI using GTK3 widgets and GTK Builder resource files.

## Website Discovery

Implement a site-adapter abstraction with:

- Domain matching
- Video-card extraction
- Title and thumbnail extraction
- Related-video discovery
- Load More button handling
- Infinite scroll handling
- Pagination limits
- Cancellation and timeout support

Use WebKitGTK for JavaScript-enabled pages.

Do not implement DRM circumvention, access-control bypasses or adversarial evasion of website protections.

## Video Playback

Use libmpv as the playback engine.

Prefer the libmpv OpenGL render API with GtkGLArea rather than platform-specific native window embedding.

Support local files, direct media URLs and supported HLS streams.

Implement play, pause, seek, volume, speed, fullscreen, playback errors and resume position.

## Data Model

Use SQLite with migration files.

Required entities:

- Videos
- Sources
- Folders
- Tags
- Video-folder relationships
- Video-tag relationships
- Collections
- Playback history
- Import jobs

Use SQLite FTS5 for indexed text search.

## Quality Requirements

- No blocking GTK main-thread operations
- No memory leaks from long-lived browser and player objects
- Clear failure messages and recoverable errors
- Structured logging
- Unit tests for database and URL logic
- Fixture-based tests for website adapters
- Correct cleanup during application shutdown

## Development Process

Implement one phase at a time.

For each phase:

1. Explain which files will be created or changed.
2. Implement the smallest working vertical slice.
3. Build the application.
4. Run relevant tests.
5. Fix reported build and test errors.
6. Report implemented functionality and remaining limitations.

Do not create placeholder functionality that appears operational but performs no real action.

Preserve existing working features when implementing new modules.

## Initial Task

Implement Phase 1: the application skeleton.

Create a CMake-based C++17 GTK3 application with:

- Main window
- Navigation sidebar
- GtkStack for page navigation
- Empty gallery view
- Placeholder browser and player regions clearly marked as not implemented
- Application stylesheet
- Clean separation between application startup and window construction

Verify the application builds and runs before proceeding to Phase 2.

## 12. Final recommendation

I would make three deliberate architectural choices:

First, build the native library and player before the website crawler. These are stable subsystems that can be tested using local files and ordinary publicly accessible HLS streams.

Second, treat website extraction as a plugin-like adapter system. Websites frequently change their HTML structure. A broken adapter should not break the gallery, database or video player.

Third, keep both the original webpage and the playable stream as separate concepts. The webpage is the durable bookmark. The stream is a potentially temporary playback resource.

This yields an application that starts as a useful native Linux media organizer and progressively becomes a website-aware video gallery without making the core dependent on individual websites.

The next concrete step is Phase 1: create the CMake project, GTK3 main window, sidebar and navigation system, using the `AGENTS.md` above as the development contract.
