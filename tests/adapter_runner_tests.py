import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('adapter_runner', ROOT / 'tools/adapter_runner.py')
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((ROOT / 'examples/adapters/generic-hls.json').read_text())
        self.fixtures = ROOT / 'tests/fixtures/adapters'

    def run_html(self, html, mapping=None):
        return runner.run(self.config, html, 'https://example.com/watch/1', mapping or {})

    def test_complete_fixture(self):
        mapping = json.loads((self.fixtures / 'playlists.json').read_text())
        for entry in mapping.values():
            entry['file'] = self.fixtures / entry['file']
        result = self.run_html((self.fixtures / 'page.html').read_text(), mapping)
        self.assertEqual(result['errors'], [])
        self.assertEqual(result['title'], 'Fixture video & poster')
        self.assertEqual(result['poster']['url'], 'https://example.com/videos/images/poster.jpg')
        self.assertEqual(len(result['masters']), 1)
        self.assertEqual(len(result['mediaPlaylists']), 1)
        master = result['masters'][0]
        self.assertEqual(master['referenceSummary'], 'mixed')
        self.assertEqual(master['references'][0]['resolvedUrl'], 'https://cdn.example.com/hls/audio/index.m3u8')
        self.assertEqual(master['references'][1]['resolvedUrl'], 'https://cdn.example.com/hls/720p/index.m3u8')
        self.assertEqual(master['references'][3]['kind'], 'scheme-relative')

    def test_relative_reference_types(self):
        master, result = runner.inspect_playlist('#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n/root.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2\n?v=1', 'https://example.com/hls/master.m3u8')
        self.assertTrue(master)
        self.assertEqual(result['referenceSummary'], 'relative-only')
        self.assertEqual([r['kind'] for r in result['references']], ['root-relative', 'query-relative'])
        self.assertEqual(result['references'][1]['resolvedUrl'], 'https://example.com/hls/master.m3u8?v=1')

    def test_absolute_only_and_media(self):
        _, result = runner.inspect_playlist('#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttps://example.com/a.m3u8', 'https://example.com/master.m3u8')
        self.assertEqual(result['referenceSummary'], 'absolute-only')
        master, result = runner.inspect_playlist((self.fixtures / 'media.m3u8').read_text(), 'https://example.com/master.m3u8')
        self.assertFalse(master)  # Filename alone does not identify a master.
        self.assertEqual(result['referenceSummary'], 'none')

    def test_posters_and_url_validation(self):
        self.assertEqual(self.run_html('<title>  </title>')['title'], 'https://example.com/watch/1')
        self.assertIsNone(self.run_html('<video></video>')['poster'])
        result = self.run_html('<video poster="javascript:bad"></video><meta property="og:image" content="/image.jpg">')
        self.assertEqual(result['poster']['source'], "meta[property='og:image']")
        for raw in ['https://user:password@example.com/a', 'file:///tmp/a', 'javascript:alert(1)', '//example.com:bad/a']:
            with self.assertRaises(ValueError):
                runner.resolve(raw, 'https://example.com/')

    def test_errors_and_incomplete_inspection(self):
        result = self.run_html('<source src="/missing.m3u8"><source src="/missing.m3u8">')
        self.assertEqual(len(result['errors']), 1)
        with self.assertRaises(ValueError):
            runner.inspect_playlist('<html>error</html>', 'https://example.com/a')
        _, result = runner.inspect_playlist('#EXTM3U\n#EXT-X-DEFINE:NAME="host",VALUE="example.com"\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttps://{$host}/a.m3u8', 'https://example.com/a')
        self.assertEqual(len(result['warnings']), 2)

    def test_script_strings_and_invalid_base(self):
        result = self.run_html('''<base href="javascript:bad"><base href="https://cdn.example.com/"><script>const x = 'path\\u002fmaster.m3u8';</script>''')
        self.assertEqual(result['errors'][0]['url'], 'https://cdn.example.com/path/master.m3u8')


if __name__ == '__main__':
    unittest.main()
