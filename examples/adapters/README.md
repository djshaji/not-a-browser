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
URLs remain playback-only. Rebuild after changing the profile; runtime adapter
installation and arbitrary adapter IDs are not implemented.

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
