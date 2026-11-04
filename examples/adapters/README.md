# Generic HTML/HLS adapter

Run the sample against local fixtures from the repository root:

```sh
python3 -B tools/adapter_runner.py examples/adapters/generic-hls.json \
  --html tests/fixtures/adapters/page.html \
  --page-url https://example.com/watch/123 \
  --playlist-map tests/fixtures/adapters/playlists.json
python3 -B tests/adapter_runner_tests.py
```

The runner prints structured JSON containing a poster, master playlists, media
playlists, reference classifications, inspection warnings, and per-candidate
errors. It does not execute JavaScript or make network requests. A missing local
playlist mapping produces an error entry. Fatal input errors exit with code 2;
per-candidate failures remain in the JSON so other candidates can succeed.

The mapping associates each resolved HTML candidate URL with a local UTF-8
playlist file. Paths are relative to the mapping file. Optional `finalUrl`
simulates a redirect target: child references resolve against that URL, without
inheriting its query parameters. It does not simulate an actual redirect request.

This first runner supports interface version 1 with id `generic-html-hls`.
The configurable parts are source selectors and attributes, poster source order,
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

The desktop WebKit discovery flow still uses its existing embedded script.
Connecting the runner's structured output to that asynchronous flow and
implementing bounded network fetching are subsequent integration work.
