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
| Application | Native window, resizable sidebar, Library/Discover/Player/Files/Recent videos navigation, session theme switch, and keyboard shortcuts. |
| Library | SQLite schema version 3, transactional migrations, canonical HTTP(S) URL deduplication, metadata preservation on duplicate saves, bookmark CLI, asynchronous selected-bookmark metadata/tag/top-level folder editing, and removal with session Undo. |
| Gallery | Grid/list views, selection, title/description prefix search, exact-name single-tag and top-level folder filters, sorting, persisted Recent playback history for saved bookmarks, 24-item pagination, asynchronous queries, and bounded thumbnail caching. |
| Playback | GtkGLArea/libmpv rendering, local files and direct media URLs, muted startup, seeking, reverse/forward 2×, speed controls, fullscreen, EOF/replay, and mpv resume files. |
| Files | Native local file chooser, video/all-file filters, playback activation, and session folder/selection restoration across page switches. Browsing remains available without playback. |
| Video-page discovery | WebKit DOM extraction, title/poster metadata, bounded native HLS verification, stream selection, automatic playback, cancellation, and asynchronous explicit library saves. |
| Listing discovery | Two embedded sample profiles, native cards, per-card Play/Save, DOM readiness observers, bounded next-link/load-more pagination, and deduplication. |
| User profiles | Validated JSON listing profiles and a generic HLS override from the XDG config directory, with Reload profiles and built-in fallback. |
| Validation | Database, thumbnails, adapter profiles, HLS inspection, rendered discovery, listings, UI smoke, and Python fixture-runner tests. |

Bookmark metadata, tags, top-level folder assignments, combined filters, and
removal with one session Undo slot are implemented. Nested-folder editing remains
planned; existing nested assignments are preserved. The `Video` model and
repository queries expose descriptions, assigned tags, and top-level folder names;
metadata updates maintain FTS through existing triggers. Playback resume belongs to libmpv. Recent records each saved bookmark’s latest successful video-page playback; favorited local files are included, while unsaved files and pages are excluded.

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

1. **Edit metadata.** The editor updates the selected bookmark's title and
   description. The model/repository read and update descriptions.
   The canonical URL and bookmark identity stay stable. The gallery refreshes after
   success and FTS results reflect edits.
2. **Remove with Undo.** Removal captures metadata, saved timestamp,
   and folder/tag relationships and deletes the record in one transaction. One
   in-memory Undo slot lasts until the next successful removal or gallery/window
   destruction. Undo restores the snapshot transactionally. Duplicate URLs and
   missing relationships fail without partial changes, retaining the snapshot for
   retry. Reuse the original record ID when free; allocate a new ID if occupied,
   which can change newest/oldest ordering. Removal never deletes a media file.
3. **Assign and filter tags.** The existing editor shows assigned
   tags one per line. Save creates names and replaces assignments atomically with
   metadata updates; removing a line unassigns it without deleting the global tag.
   Trim surrounding spaces, ignore blank lines, and deduplicate repeated names.
   The gallery's exact-name, case-sensitive tag field composes with text search,
   sorting, counts, and pagination. Changing filters resets the page, and a save
   that removes the active tag refreshes the results. Undo restores assignments.
4. **Top-level folders.** The editor creates and assigns folders
   from one name per line; deleting a line unassigns that top-level name. Save
   metadata, tags, and folder assignments together, preserving nested assignments.
   Reuse existing roots by name; preserve retained duplicate-name memberships.
   The exact-name Folder filter composes with Tag, text search, sorting, and
   pagination, and matches roots only. Undo restores all folder relationships.

These management slices originally used schema version 2; playback history now adds schema version 3. Global tag
renaming/deletion, folder trees, folder rename/deletion, and multiple-tag
filters remain unimplemented. Keep repository queries and rendered gallery pages bounded; introduce transactional, versioned
migrations only when storage changes are required.

All writes run in database workers. Capture the selected record's identity at
action time, disable conflicting pending actions, and guard callbacks against
selection changes and destroyed widgets. Preserve existing metadata on duplicate
imports and finish initiated saves across navigation.

Regression requirements:

- Edited title/description survive restart and appear correctly in search.
- Removal updates counts and pagination; Undo restores metadata and relationships.
- Tag/folder assignments survive restart; combined filters work with search and each sort order, and nested memberships survive top-level edits.
- Errors are visible and retryable without freezing GTK or corrupting data.
- Navigation and destruction during pending work cannot apply stale UI updates.
- Existing discovery, file browsing, playback, and duplicate-save behavior remain intact.

Validation: database and UI smoke tests cover persistence, FTS updates,
transactional edits and rollback, folder reuse, nested/duplicate-name memberships,
combined filters, removal/Undo conflicts, and asynchronous lifetimes. For changes
to these workflows, run:

```sh
ctest --test-dir build -R '^(database|ui-smoke)$' --output-on-failure
```

Manually inspect editor fields, filter results, and removal/Undo feedback. Use
temporary XDG directories. Run the full suite when shared model/repository changes
affect discovery or listing saves.

## Tag and folder choosers (implemented)

Library Tag/Folder filters complete existing names from literal, case-sensitive
prefixes. The bookmark editor provides editable dropdowns with Add actions that
append existing or new names to its per-line fields, deduplicating against trimmed
lines. Save remains atomic and Cancel discards chooser changes. Root folders only
are suggested, with duplicate names collapsed; unassigned global names remain
available. Each asynchronous lookup returns at most 50 names, with debouncing,
cancellation, and generation/lifetime guards. Narrow large lists by typing a
prefix. Lookups refresh on focus and after Library queries. No migration is needed.

## Local file favorites (implemented)

Files can explicitly save a regular local file to Library under the Favorites tag.
Saves validate paths and file type in workers, atomically add the bookmark and tag,
and preserve metadata/other assignments on duplicates. A normalized, encoded
absolute file URI supplies identity; no schema change is needed. Gallery selection
routes local entries directly to libmpv, including builds without discovery.
Successful playback of saved files updates Recent. Editing, filters, and removal/
Undo apply normally without deleting media. Files retains folder and selection.
Writes finish across navigation/destruction with guarded status callbacks.
Favoriting remains available without the player; builds without Library omit it.
Moved/missing paths require re-adding; symlink paths remain distinct.

## Following milestones

### A real website profile

Choose a website used regularly and develop a listing profile against its actual
DOM. Verify title/poster extraction, durable page links, pagination, cancellation,
and video-page HLS discovery. Add reproducible local fixtures and document the
profile's supported layouts and limits. The two bundled profiles currently target
fixture layouts; arbitrary websites are not automatically supported. Site choice
and expected behavior must be established before implementation.

### Playback history and Recent (implemented)

Schema version 3 adds a UTC microsecond last-played timestamp and index. Library's
dedicated Recent videos page (Alt+5) and Recent dropdown entry show played bookmarks ordered by timestamp, then descending
ID for ties; existing search/tag/folder predicates and bounded pagination apply.
Recent videos keeps its own filters and view mode, fixes ordering to last played,
and refreshes on navigation and successful playback/saves. The page retains the
existing editor/removal behavior and shares the Library Undo slot and mutation guards.
Successful libmpv file loading from video-page discovery records the durable page
URL in a database worker and refreshes the gallery through its lifetime guard.
Unsuccessful loads, direct media playback, unsaved local files, and unsaved pages do not
create entries. Late writes cannot replace newer timestamps. Removal and Undo
preserve the timestamp in the transactional snapshot. History survives restart;
existing libraries migrate with no invented playback history. Pausing and replay
within the loaded file do not update the timestamp. Resume remains owned by mpv.

Future slices may define history for unsaved discoveries, expose
playback times in cards, or add library-level resume with expiring-stream handling.

### Nested folder organization and richer metadata

Top-level folder assignment and filtering are implemented. Consider nested
folder management, a folder tree/chooser, folder rename/deletion, favorites,
saved collections, and export/import. Add duration or other metadata only with a reliable extraction source and clear update rules.
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
rules in AGENTS.md. Bookmark management and top-level folder organization are
implemented. Further work should select one of the milestones above; no real
website profile can be scoped until the target site and expected workflow are chosen.
