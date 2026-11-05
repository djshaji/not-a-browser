# Stream Gallery — Implementation Roadmap

Stream Gallery is a native C++17/GTK3 Linux application combining a bookmark
library, local file browser, WebKitGTK discovery, and libmpv playback.
[README.md](README.md) documents current usage and build options;
[AGENTS.md](AGENTS.md) defines contributor constraints. This roadmap separates
implemented behavior from proposed work. Planned features below are not available
until implemented and validated.

## Current baseline

| Area | Implemented behavior |
| --- | --- |
| Application | Native window, resizable sidebar, Library/Discover/Player/Files navigation, session theme switch, and keyboard shortcuts. |
| Library | SQLite schema version 2, transactional migrations, canonical HTTP(S) URL deduplication, metadata preservation on duplicate saves, and bookmark CLI. |
| Gallery | Grid/list views, selection, title/description prefix search, sorting, 24-item pagination, asynchronous queries, and bounded thumbnail caching. |
| Playback | GtkGLArea/libmpv rendering, local files and direct media URLs, muted startup, seeking, reverse/forward 2×, speed controls, fullscreen, EOF/replay, and mpv resume files. |
| Files | Native local file chooser, video/all-file filters, playback activation, and session folder/selection restoration across page switches. Browsing remains available without playback. |
| Video-page discovery | WebKit DOM extraction, title/poster metadata, bounded native HLS verification, stream selection, automatic playback, cancellation, and asynchronous explicit library saves. |
| Listing discovery | Two embedded sample profiles, native cards, per-card Play/Save, DOM readiness observers, bounded next-link/load-more pagination, and deduplication. |
| User profiles | Validated JSON listing profiles and a generic HLS override from the XDG config directory, with Reload profiles and built-in fallback. |
| Validation | Database, thumbnails, adapter profiles, HLS inspection, rendered discovery, listings, UI smoke, and Python fixture-runner tests. |

Folder/tag creation and video relationship APIs exist. Their editing UI,
bookmark metadata editing, removal/Undo, and tag filtering are not implemented.
The database stores descriptions, but the current `Video` model and repository
queries do not expose them for editing. Playback resume currently belongs to
libmpv; there is no library-level Recent/history workflow.

## Architecture and boundaries

- `src/app/` owns startup and application lifecycle.
- `src/ui/` owns the native window, gallery, Files, player, and WebKit views.
- `src/database/` owns migrations, repositories, and the bookmark CLI.
- `src/models/` contains shared data types.
- `src/services/` provides thumbnail caching, HLS inspection, bookmark saves, and profile loading.
- `resources/` and `examples/adapters/` contain embedded UI, scripts, and profiles.
- `tests/` uses isolated data directories and local fixtures/HTTP servers.

GTK updates run on the main thread; database, network, and image work runs in
background tasks. Superseded jobs are cancelled and stale callbacks ignored.
Initiated explicit saves finish from a metadata snapshot even after navigation.
Maintain RAII ownership and the existing mpv/OpenGL lifecycle.

Discovery uses rendered DOM extraction followed by native playlist verification.
There is no yt-dlp subprocess, network interception, or cookie transfer to the
inspector or player. The inspector and player receive the page referrer and user
agent. Durable webpage URLs remain separate from temporary playback URLs.
The Python adapter runner is local-only and does not execute JavaScript or
exercise WebKit/network verification.

Keep user profiles as validated data, never executable configuration. Preserve
the current profile and extraction bounds documented in AGENTS.md: 128 KiB per
file, 64 custom listing profiles, five pages/batches, 120 total cards, and at most
24 new cards per batch. Site profiles cannot relax native safety limits.

## Next milestone: bookmark management

Make saved bookmarks manageable from Library. Deliver the work in small,
independently usable steps:

1. **Edit metadata.** Add an editor for the selected bookmark's title and
   description. Extend the model/repository to read and update descriptions.
   Keep the canonical URL and bookmark identity stable. Refresh the gallery after
   success and keep FTS results consistent with edits.
2. **Remove with Undo.** Add explicit removal and a clearly scoped Undo action.
   Define Undo lifetime and duplicate/conflict behavior before implementation.
   Restore bookmark metadata and folder/tag relationships transactionally;
   deleting a bookmark must never delete a local media file.
3. **Assign and filter tags.** Add tag listing and assignment/removal controls,
   plus a gallery tag filter. Extend repository queries so filters compose with
   text search, sorting, counts, and pagination. Reset or clamp pages when results
   shrink. Keep queries and rendered widgets bounded.

Existing folder/tag APIs are a starting point; metadata updates, deletion,
restoration, tag enumeration/removal, and filtered queries still need repository
support. Prefer the existing schema where sufficient. Introduce transactional,
versioned migrations only when storage changes are required.

All writes run in database workers. Capture the selected record's identity at
action time, disable conflicting pending actions, and guard callbacks against
selection changes and destroyed widgets. Preserve existing metadata on duplicate
imports and finish initiated saves across navigation.

Acceptance criteria:

- Edited title/description survive restart and appear correctly in search.
- Removal updates counts and pagination; Undo restores metadata and relationships.
- Tag assignment survives restart; tag filters work with search and each sort order.
- Errors are visible and retryable without freezing GTK or corrupting data.
- Navigation and destruction during pending work cannot apply stale UI updates.
- Existing discovery, file browsing, playback, and duplicate-save behavior remain intact.

Validation: extend database and UI smoke tests with persistence, FTS updates,
relationship restoration, Undo conflicts, filter pagination, and asynchronous
lifetime cases. Run the relevant CTest checks and manually inspect the editor,
removal/Undo feedback, and tag controls. Use temporary XDG directories.

## Following milestones

### A real website profile

Choose a website used regularly and develop a listing profile against its actual
DOM. Verify title/poster extraction, durable page links, pagination, cancellation,
and video-page HLS discovery. Add reproducible local fixtures and document the
profile's supported layouts and limits. The two bundled profiles currently target
fixture layouts; arbitrary websites are not automatically supported. Site choice
and expected behavior must be established before implementation.

### Playback history and Recent

Associate playback with durable bookmark identity, record last-played time, and
expose a Recent view. Define the handling of local files and unsaved discoveries.
If adding library-level resume, distinguish it from existing mpv watch-later
files and account for expiring stream URLs. Add schema migrations and tests only
once the persistence contract is defined.

### Folder organization and richer metadata

Expose existing folder relationships through a native UI, then consider nested
folder management, favorites, saved collections, and export/import. Add duration
or other metadata only with a reliable extraction source and clear update rules.
Preserve user edits when refreshing discovered metadata.

### Discovery expansion

Consider additional site profiles, related-video collections, and bounded
user-triggered infinite scrolling after fixture-backed site workflows are stable.
Keep listing profiles independent of HLS discovery and retain cancellation,
deduplication, same-origin next links, and native pagination limits.

### Release hardening

Profile large-library behavior, improve recoverable errors and accessibility,
and prepare packaging. Validate X11 and Wayland, OpenGL lifecycle, shutdown during
background work, disabled-feature builds, and migration from existing databases.
Report skipped display/codec/ffmpeg checks separately from passing tests.

## Delivery process

Implement one usable slice at a time. Review affected ownership, persistence, and
cancellation paths; make the change; build; run relevant checks; manually verify
visible UI changes; and update README.md with actual behavior. Keep this roadmap
current as milestones land. Do not present planned controls as working features.

Use the build and test commands in README.md and the feature-specific validation
rules in AGENTS.md. No application implementation is included in this roadmap
update; bookmark management is the next proposed development milestone.
