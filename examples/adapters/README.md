# Generic HTML/HLS adapter

Run the sample against local fixtures from the repository root:

```sh
python3 -B tools/adapter_runner.py examples/adapters/generic-hls.json \
  --html tests/fixtures/adapters/page.html \
  --page-url https://example.com/watch/123 \
  --playlist-map tests/fixtures/adapters/playlists.json
python3 -B tests/adapter_runner_tests.py
```

The runner prints structured JSON containing a title, poster, master playlists, media
playlists, reference classifications, inspection warnings, and per-candidate
errors. It does not execute JavaScript or make network requests. A missing local
playlist mapping produces an error entry. Fatal input errors exit with code 2;
per-candidate failures remain in the JSON so other candidates can succeed.

The mapping associates each resolved HTML candidate URL with a local UTF-8
playlist file. Paths are relative to the mapping file. Optional `finalUrl`
simulates a redirect target: child references resolve against that URL, without
inheriting its query parameters. It does not simulate an actual redirect request.

The Python runner supports interface version 1 with id `generic-html-hls`.
The configurable parts are source selectors and attributes, title/poster source order,
playlist extension, candidate count, and playlist byte limit. Selectors support
tag names, attribute presence/equality, comma-separated alternatives, and the
sample's `script:not([src])`. Quoted script strings support JSON escapes and
single-quoted slash/Unicode escapes; arbitrary JavaScript expressions are not
evaluated. HTML entities are decoded in attributes, not in script text.

Other fields describe the fixed generic profile or future network behavior;
this is not a general adapter interpreter. HTTP/HTTPS validation, credential
rejection, deduplication, fragment removal, and poster fallbacks are fixed.
The runner caps HTML at 2 MiB/20,000 elements, candidates at 64, and each playlist
at 1 MiB (or the lower configured limit). Network timeout, concurrency, private
address checks, and cancellation fields do not apply to this local-only CLI.

Master detection uses HLS tags rather than filenames. Variant URI lines and
the sample's listed URI attributes are inspected. Media segments are not
traversed. Unknown tags and unresolved HLS variables produce warnings; this is
not a full HLS validator. Posters are URL metadata and are not downloaded.

Desktop discovery embeds this same JSON profile at build time. Its rendered DOM
extractor uses the configured title/candidate/poster selectors, source order, candidate
limit, and extension. It returns structured page, candidate, and poster data to
the native application. Titles have normalized whitespace and a maximum of 1,024
characters (or the lower configured limit), with the page URL as fallback.
Discover can save the page URL, title, and poster to Library; transient manifest
URLs remain playback-only. Rebuild after changing the profile; arbitrary adapter IDs are not implemented.

Native workers fetch up to four candidates concurrently, validate HTTP/HTTPS
redirects (at most five), reject credentials, limit each playlist to 1 MiB, and
use a 10-second network timeout. These native bounds are currently fixed rather
than configurable through JSON. The overall extraction/verification window is
20 seconds. Navigation, Stop loading, and destruction cancel pending work and
ignore stale callbacks. Local HTTP pages are supported; the sample's proposed
`allowPrivateNetwork` policy is not implemented in desktop discovery.

Only inspected master/media playlists enter the chooser. Masters are preferred
when selecting the initial stream from the completed batch. Reference URLs are
resolved against the final HTTP response URL and the original manifest is sent
to libmpv without rewriting it. Native inspection rejects unresolved variables
and malformed variant references; it is not a full HLS validator. Browser cookies
are not copied to the inspector or player; both receive the page referrer and
user agent. The poster preview uses the existing bounded thumbnail service and
retains a generic icon if the image cannot be loaded.

## Listing profiles

Desktop discovery also embeds `listing-links.json` and `listing-load-more.json`.
They use a separate version-1 listing interface with `selectors.card`, `link`,
`title`, and `poster`, plus a pagination `type` (`link` or `button`) and `selector`.
The first fixture uses `.video-card`/`a.video-link`/`.video-title` and a next link;
the second uses `article.clip`/`a.watch`/`h2` and a JavaScript load-more button.
Fixtures live in `tests/fixtures/listings/`; CTest's `listings` target exercises
both through a local HTTP server and rendered WebKit DOM.

Edit the profiles for a target site's actual markup and rebuild. These sample
layouts are not domain detection rules or supported production websites. Infinite scrolling is not implemented. The Python HLS
fixture runner does not interpret listing profiles.

The listing extractor resolves links and `img` `src`/`data-src` against the document
base, rejects non-HTTP URLs/credentials/direct `.m3u8` manifests, normalizes titles
with a URL fallback, and returns structured card metadata. A `MutationObserver`
waits for initial cards or new cards after a single button click; cancellation,
completion, and the bounded readiness timer disconnect it. Pagination is always
initiated by the user. Next links are restricted to the current origin; loops are
detected in native code. Native caps are five pages/batches, 120 total cards, and
24 new cards per batch. Each extraction receives the native session's delivered URLs, so repeated
cards and earlier batches do not hide later results. Card scans inspect at most
20,000 elements and collect at most 120 distinct candidates per document.

Play uses the video's durable page URL in the existing HLS browser. Save reuses
the same asynchronous bookmark service as page discovery, including duplicate
reporting, metadata preservation, and gallery refresh. Work from a previous
listing cannot change new cards after navigation; explicit saves still finish.

## User profile files

Place JSON profiles directly in `~/.config/notabrowser/` (or
`$XDG_CONFIG_HOME/notabrowser/` when set), then choose **Reload profiles** in the relevant Discover tab.
Copy `examples/adapters/listing-links.json` or `listing-load-more.json` to a
new `.json` filename and edit its `name`, selectors, and pagination settings.
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
