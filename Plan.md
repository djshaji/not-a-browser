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
| Library | SQLite schema version 2, transactional migrations, canonical HTTP(S) URL deduplication, metadata preservation on duplicate saves, bookmark CLI, asynchronous selected-bookmark metadata/tag editing, and removal with session Undo. |
| Gallery | Grid/list views, selection, title/description prefix search, an exact-name single-tag filter, sorting, 24-item pagination, asynchronous queries, and bounded thumbnail caching. |
| Playback | GtkGLArea/libmpv rendering, local files and direct media URLs, muted startup, seeking, reverse/forward 2×, speed controls, fullscreen, EOF/replay, and mpv resume files. |
| Files | Native local file chooser, video/all-file filters, playback activation, and session folder/selection restoration across page switches. Browsing remains available without playback. |
| Video-page discovery | WebKit DOM extraction, title/poster metadata, bounded native HLS verification, stream selection, automatic playback, cancellation, and asynchronous explicit library saves. |
| Listing discovery | Two embedded sample profiles, native cards, per-card Play/Save, DOM readiness observers, bounded next-link/load-more pagination, and deduplication. |
| User profiles | Validated JSON listing profiles and a generic HLS override from the XDG config directory, with Reload profiles and built-in fallback. |
| Validation | Database, thumbnails, adapter profiles, HLS inspection, rendered discovery, listings, UI smoke, and Python fixture-runner tests. |

Folder creation and video relationship APIs exist, but folder editing UI is not
implemented. Bookmark metadata/tag editing, tag filtering, and removal with one
session Undo slot are implemented. The `Video` model and repository queries expose
descriptions and assigned tags; metadata updates maintain FTS through existing
triggers. Playback resume currently belongs to libmpv; there is no library-level
Recent/history workflow.

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

## Bookmark management (implemented)

The Library management milestone consists of these implemented slices:

1. **Edit metadata (implemented).** The first slice adds an editor for the selected bookmark's title and
   description. The model/repository read and update descriptions.
   The canonical URL and bookmark identity stay stable. The gallery refreshes after
   success and FTS results reflect edits.
2. **Remove with Undo (implemented).** Removal captures metadata, saved timestamp,
   and folder/tag relationships and deletes the record in one transaction. One
   in-memory Undo slot lasts until the next successful removal or gallery/window
   destruction. Undo restores the snapshot transactionally. Duplicate URLs and
   missing relationships fail without partial changes, retaining the snapshot for
   retry. Reuse the original record ID when free; allocate a new ID if occupied,
   which can change newest/oldest ordering. Removal never deletes a media file.
3. **Assign and filter tags (implemented).** The existing editor shows assigned
   tags one per line. Save creates names and replaces assignments atomically with
   metadata updates; removing a line unassigns it without deleting the global tag.
   Trim surrounding spaces, ignore blank lines, and deduplicate repeated names.
   The gallery's exact-name, case-sensitive tag field composes with text search,
   sorting, counts, and pagination. Changing filters resets the page, and a save
   that removes the active tag refreshes the results. Undo restores assignments.

These slices use schema version 2 without a migration. Global tag renaming/deletion,
a tag chooser, and multiple-tag filters remain unimplemented. Keep repository
queries and rendered gallery pages bounded; introduce transactional, versioned
migrations only when storage changes are required.

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
rules in AGENTS.md. The bookmark management slices are implemented. Choose the next development
slice from the following milestones according to the workflow being improved.
