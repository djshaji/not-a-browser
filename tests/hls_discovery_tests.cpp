#include "services/HlsDiscovery.hpp"
#include <libsoup/soup.h>
#include <functional>
#include <string>

namespace {
void waitFor(const std::function<bool()>& predicate) {
    const auto deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!predicate() && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(1000);
    }
    g_assert_true(predicate());
}
void parserTests() {
    using namespace streamgallery;
    auto playlist = inspectHlsPlaylist(
        "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,NAME=\"English, stereo\",URI=\"audio/index.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1\nlow/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2\nhttps://cdn.example.com/high.m3u8\n"
        "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=1,URI=\"//cdn.example.com/iframe.m3u8\"\n",
        "https://example.com/hls/master.m3u8?token=one");
    g_assert_true(playlist.error.empty());
    g_assert_true(playlist.master);
    g_assert_cmpstr(playlist.referenceSummary.c_str(), ==, "mixed");
    g_assert_cmpuint(playlist.references.size(), ==, 4);
    g_assert_cmpstr(playlist.references[0].resolvedUrl.c_str(), ==, "https://example.com/hls/audio/index.m3u8");
    g_assert_cmpstr(playlist.references[3].kind.c_str(), ==, "scheme-relative");
    playlist = inspectHlsPlaylist("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n/root.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2\n?v=2\n",
        "https://example.com/hls/master.m3u8?token=one");
    g_assert_cmpstr(playlist.referenceSummary.c_str(), ==, "relative-only");
    g_assert_cmpstr(playlist.references[1].resolvedUrl.c_str(), ==, "https://example.com/hls/master.m3u8?v=2");
    playlist = inspectHlsPlaylist("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttps://example.com/a.m3u8\n", "https://example.com/master.m3u8");
    g_assert_cmpstr(playlist.referenceSummary.c_str(), ==, "absolute-only");
    for (const auto* body : {"<html>error</html>", "#EXTM3U\n", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n",
                            "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nfile:///tmp/a.m3u8\n",
                            "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nhttps://{$host}/a.m3u8\n"})
        g_assert_false(inspectHlsPlaylist(body, "https://example.com/master.m3u8").error.empty());
    playlist = inspectHlsPlaylist("#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nclip.ts\n#EXT-X-ENDLIST\n", "https://example.com/master.m3u8");
    g_assert_true(playlist.error.empty());
    g_assert_false(playlist.master);
    g_assert_true(resolveDiscoveryUrl("https://user:secret@example.com/a", "https://example.com/").empty());
    g_assert_true(resolveDiscoveryUrl("file:///tmp/a", "https://example.com/").empty());
}
}
int main() {
    parserTests();
    SoupServer* server = soup_server_new(nullptr, nullptr);
    soup_server_add_handler(server, nullptr, +[](SoupServer*, SoupServerMessage* message,
        const char* path, GHashTable*, gpointer) {
        if (g_str_equal(path, "/redirect.m3u8")) {
            soup_server_message_set_redirect(message, SOUP_STATUS_FOUND, "/final/master.m3u8?token=one"); return;
        }
        if (g_str_equal(path, "/loop.m3u8")) {
            soup_server_message_set_redirect(message, SOUP_STATUS_FOUND, "/loop.m3u8"); return;
        }
        if (g_str_equal(path, "/bad-redirect.m3u8")) {
            soup_server_message_set_redirect(message, SOUP_STATUS_FOUND, "file:///etc/passwd"); return;
        }
        if (g_str_equal(path, "/missing.m3u8")) {
            soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, nullptr); return;
        }
        auto* headers = soup_server_message_get_request_headers(message);
        g_assert_cmpstr(soup_message_headers_get_one(headers, "Referer"), ==, "https://example.com/page");
        g_assert_cmpstr(soup_message_headers_get_one(headers, "User-Agent"), ==, "FixtureAgent/1");
        std::string body = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100\nchild/index.m3u8\n";
        if (g_str_equal(path, "/invalid.m3u8")) body = "<html>Not HLS</html>";
        if (g_str_equal(path, "/large.m3u8")) body.assign(1024 * 1024 + 1, 'x');
        soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
        soup_server_message_set_response(message, "application/vnd.apple.mpegurl", SOUP_MEMORY_COPY, body.data(), body.size());
        if (g_str_equal(path, "/slow.m3u8")) {
            soup_server_message_pause(message);
            g_timeout_add_full(G_PRIORITY_DEFAULT, 300, +[](gpointer data) -> gboolean {
                soup_server_message_unpause(SOUP_SERVER_MESSAGE(data));
                return G_SOURCE_REMOVE;
            }, g_object_ref(message), g_object_unref);
        }
    }, nullptr, nullptr);
    GError* error = nullptr;
    g_assert_true(soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
    g_assert_no_error(error);
    GSList* uris = soup_server_get_uris(server);
    gchar* uri = g_uri_to_string(static_cast<GUri*>(uris->data));
    std::string base = uri;
    g_free(uri); g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
    auto inspect = [&](const char* path, GCancellable* cancel = nullptr) {
        bool done = false;
        streamgallery::HlsPlaylist result;
        streamgallery::inspectHlsPlaylistAsync(base + path, "https://example.com/page", "FixtureAgent/1", cancel,
            [&](streamgallery::HlsPlaylist value) { result = std::move(value); done = true; });
        waitFor([&] { return done; });
        return result;
    };
    auto result = inspect("redirect.m3u8");
    const auto finalUrl = base + "final/master.m3u8?token=one";
    const auto childUrl = base + "final/child/index.m3u8";
    g_assert_true(result.error.empty());
    g_assert_cmpstr(result.url.c_str(), ==, finalUrl.c_str());
    g_assert_cmpstr(result.references[0].resolvedUrl.c_str(), ==, childUrl.c_str());
    for (const auto* path : {"invalid.m3u8", "large.m3u8", "missing.m3u8", "loop.m3u8", "bad-redirect.m3u8"})
        g_assert_false(inspect(path).error.empty());
    GCancellable* cancellation = g_cancellable_new();
    g_timeout_add(50, +[](gpointer data) -> gboolean { g_cancellable_cancel(G_CANCELLABLE(data)); return G_SOURCE_REMOVE; }, cancellation);
    g_assert_false(inspect("slow.m3u8", cancellation).error.empty());
    g_object_unref(cancellation);
    const auto cleanup = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < cleanup) { while (g_main_context_iteration(nullptr, FALSE)) {} g_usleep(1000); }
    soup_server_disconnect(server); g_object_unref(server);
    return 0;
}
