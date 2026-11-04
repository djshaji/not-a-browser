#!/usr/bin/env python3
"""Local fixture runner for the generic HTML/HLS adapter contract."""
import argparse
import json
import re
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin, urlsplit, urlunsplit


def resolve(raw, base):
    raw = raw.strip()
    if not raw or len(raw) > 8192 or re.search(r'[\s<>"\\]', raw):
        raise ValueError('Invalid URL reference')
    url = urlsplit(urljoin(base, raw))
    if url.scheme.lower() not in ('http', 'https') or not url.hostname or url.username is not None or url.password is not None:
        raise ValueError('Expected HTTP/HTTPS URL without credentials')
    _ = url.port  # Validate malformed ports.
    return urlunsplit(url._replace(fragment=''))


def matches(selector, tag, attrs):
    for part in selector.split(','):
        match = re.fullmatch(r"\s*([\w-]+)?(?:\[([\w-]+)(?:='([^']*)')?\])?\s*", part)
        if not match or not any(match.groups()):
            raise ValueError('Unsupported selector: ' + selector)
        name, attribute, value = match.groups()
        if (not name or name == tag) and (not attribute or attribute in attrs and (value is None or attrs[attribute] == value)):
            return True
    return False


class Document(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.elements = []
        self.script = None
        self.feed(html)
        self.close()

    def handle_starttag(self, tag, attrs):
        if len(self.elements) >= 20000:
            raise ValueError('HTML exceeds 20000-element limit')
        element = [tag, dict(attrs), '']
        self.elements.append(element)
        if tag == 'script':
            self.script = element

    def handle_endtag(self, tag):
        if tag == 'script':
            self.script = None

    def handle_data(self, text):
        if self.script is not None:
            self.script[2] += text

    def values(self, source):
        selector = source['selector']
        # The sample uses this one additional selector to exclude external scripts.
        inline = selector == 'script:not([src])'
        for tag, attrs, text in self.elements:
            if inline and tag == 'script' and 'src' not in attrs:
                yield text
            elif not inline and matches(selector, tag, attrs):
                yield text if source.get('text') else attrs.get(source['attribute']) or ''


def script_urls(text):
    # Decode string escapes only inside quoted literals, never execute scripts.
    for match in re.finditer(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' ''', text, re.X):
        literal = match.group()
        if literal.startswith('"'):
            try:
                value = json.loads(literal)
            except ValueError:
                continue
        else:
            value = literal[1:-1].replace(r'\/', '/')
            value = re.sub(r'\\u([0-9a-fA-F]{4})', lambda m: chr(int(m[1], 16)), value)
        yield value


def inspect_playlist(text, url):
    lines = [line.strip() for line in text.lstrip('\ufeff').splitlines() if line.strip()]
    if not lines or lines[0] != '#EXTM3U':
        raise ValueError('Missing #EXTM3U header')
    references, warnings = [], []
    master = False
    pending = False
    tags = {'#EXT-X-I-FRAME-STREAM-INF', '#EXT-X-MEDIA', '#EXT-X-SESSION-KEY', '#EXT-X-SESSION-DATA'}
    for line in lines[1:]:
        tag = line.split(':', 1)[0]
        if tag in ('#EXT-X-STREAM-INF', '#EXT-X-I-FRAME-STREAM-INF'):
            master = True
        if tag == '#EXT-X-STREAM-INF':
            if pending:
                warnings.append('Missing variant URI')
            pending = True
            continue
        raw = None
        if not line.startswith('#') and pending:
            raw, tag, pending = line, '#EXT-X-STREAM-INF', False
        elif tag in tags:
            match = re.search(r'(?:^|,)URI="([^"]*)"(?:,|$)', line.partition(':')[2])
            if match:
                raw = match[1]
        elif line.startswith('#EXT-X-') and tag not in {'#EXT-X-VERSION', '#EXT-X-INDEPENDENT-SEGMENTS', '#EXT-X-START', '#EXT-X-TARGETDURATION', '#EXT-X-MEDIA-SEQUENCE', '#EXT-X-ENDLIST', '#EXT-X-PLAYLIST-TYPE', '#EXT-X-DISCONTINUITY', '#EXT-X-KEY', '#EXT-X-MAP', '#EXT-X-BYTERANGE'}:
            warnings.append('Uninspected tag: ' + tag)
        if raw is None:
            continue
        if '{$' in raw:
            warnings.append('Unresolved HLS variable: ' + raw)
            continue
        kind = ('absolute' if urlsplit(raw).scheme else 'scheme-relative' if raw.startswith('//') else
                'root-relative' if raw.startswith('/') else 'query-relative' if raw.startswith('?') else 'path-relative')
        references.append({'tag': tag, 'raw': raw, 'kind': kind, 'resolvedUrl': resolve(raw, url)})
    if pending:
        warnings.append('Missing variant URI')
    kinds = {r['kind'] == 'absolute' for r in references}
    summary = 'none' if not kinds else 'mixed' if len(kinds) == 2 else 'absolute-only' if True in kinds else 'relative-only'
    return master, {'url': url, 'referenceSummary': summary, 'references': references, 'warnings': warnings}


def run(config, html, page_url, playlists):
    if config.get('interfaceVersion') != 1 or config.get('id') != 'generic-html-hls':
        raise ValueError('Unsupported adapter interface or id')
    if len(html.encode('utf-8')) > 2 * 1024 * 1024:
        raise ValueError('HTML exceeds 2 MiB limit')
    document = Document(html)
    base = resolve(page_url, page_url)
    for tag, attrs, _ in document.elements:
        if tag == 'base' and attrs.get('href'):
            try:
                base = resolve(attrs['href'], base)
                break
            except ValueError:
                pass
    discovery = config['discovery']
    result = {'pageUrl': page_url, 'poster': None, 'masters': [], 'mediaPlaylists': [], 'errors': []}
    for source in discovery['poster']['sources']:
        for raw in document.values(source):
            try:
                result['poster'] = {'url': resolve(raw, base), 'source': source['selector']}
                break
            except ValueError:
                pass
        if result['poster']:
            break
    candidates = []
    settings = discovery['playlistCandidates']
    for source in settings['sources']:
        for value in document.values(source):
            for raw in script_urls(value) if source.get('extract') == 'quoted-m3u8-urls' else [value]:
                try:
                    url = resolve(raw, base)
                except ValueError:
                    continue
                if urlsplit(url).path.lower().endswith(settings['urlPathExtension'].lower()) and url not in candidates:
                    candidates.append(url)
    for url in candidates[:min(settings['maxCandidates'], 64)]:
        try:
            if url not in playlists:
                raise ValueError('No local playlist mapping; network fetching is not implemented')
            entry = playlists[url]
            final_url = resolve(entry.get('finalUrl', url), url)
            path = Path(entry['file'])
            limit = min(config['limits']['maxPlaylistBytes'], 1048576)
            with path.open('rb') as stream:
                content = stream.read(limit + 1)
            if len(content) > limit:
                raise ValueError('Playlist exceeds byte limit')
            master, inspected = inspect_playlist(content.decode('utf-8-sig'), final_url)
            result['masters' if master else 'mediaPlaylists'].append(inspected)
        except (ValueError, OSError) as error:
            result['errors'].append({'url': url, 'message': str(error)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('adapter', type=Path)
    parser.add_argument('--html', type=Path, required=True)
    parser.add_argument('--page-url', required=True)
    parser.add_argument('--playlist-map', type=Path, required=True,
                        help='JSON mapping candidate URLs to {file, optional finalUrl}; paths relative to mapping file')
    args = parser.parse_args()
    try:
        config = json.loads(args.adapter.read_text())
        mapping = json.loads(args.playlist_map.read_text())
        for entry in mapping.values():
            entry['file'] = str(args.playlist_map.parent / entry['file'])
        with args.html.open('rb') as stream:
            html = stream.read(2 * 1024 * 1024 + 1)
        if len(html) > 2 * 1024 * 1024:
            raise ValueError('HTML exceeds 2 MiB limit')
        result = run(config, html.decode('utf-8-sig'), args.page_url, mapping)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, str(error) + '\n')


if __name__ == '__main__':
    main()
