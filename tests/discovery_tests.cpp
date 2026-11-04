#include "ui/BrowserView.hpp"
#include "ui/MainWindow.hpp"
#include <libsoup/soup.h>
#include <webkit2/webkit2.h>
#include <functional>
#include <filesystem>
#include <string>
#include <vector>

namespace {
GtkWidget* find(GtkWidget* root, GType type, const char* name = nullptr) {
    if ((name && g_strcmp0(gtk_widget_get_name(root), name) == 0) ||
        (!name && G_TYPE_CHECK_INSTANCE_TYPE(root, type))) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* result = nullptr;
    for (auto* child = children; child && !result; child = child->next)
        result = find(GTK_WIDGET(child->data), type, name);
    g_list_free(children);
    return result;
}
void waitFor(const std::function<bool()>& predicate) {
    const gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
    while (!predicate() && g_get_monotonic_time() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(1000);
    }
    g_assert_true(predicate());
}
}
int main(int argc, char** argv) {
    gchar* directory = g_dir_make_tmp("stream-gallery-discovery-XXXXXX", nullptr);
    g_assert_nonnull(directory);
    g_setenv("XDG_DATA_HOME", directory, TRUE);
    g_setenv("XDG_CACHE_HOME", directory, TRUE);
    g_setenv("XDG_CONFIG_HOME", directory, TRUE);
    if (!gtk_init_check(&argc, &argv)) {
        std::filesystem::remove_all(directory); g_free(directory); return 77;
    }
    struct DomFixture { bool slowRequested = false; } domFixture;
    SoupServer* domServer = soup_server_new(nullptr, nullptr);
    soup_server_add_handler(domServer, nullptr, +[](SoupServer*, SoupServerMessage* message,
        const char* path, GHashTable*, gpointer data) {
        std::string body;
        const char* type = "application/vnd.apple.mpegurl";
        if (g_str_equal(path, "/hls/master.m3u8")) {
            soup_server_message_set_redirect(message, SOUP_STATUS_FOUND, "/cdn/master.m3u8?token=one"); return;
        } else if (g_str_equal(path, "/cdn/master.m3u8")) {
            body = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100\n720p/index.m3u8\n";
        } else if (g_str_equal(path, "/invalid.m3u8")) {
            body = "<html>Not a playlist</html>";
        } else if (g_str_equal(path, "/poster.svg")) {
            type = "image/svg+xml";
            body = "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='36'><rect width='64' height='36' fill='red'/></svg>";
        } else {
            body = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nclip.ts\n#EXT-X-ENDLIST\n";
        }
        soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
        soup_server_message_set_response(message, type, SOUP_MEMORY_COPY, body.data(), body.size());
        if (g_str_equal(path, "/slow.m3u8")) {
            static_cast<DomFixture*>(data)->slowRequested = true;
            soup_server_message_pause(message);
            g_timeout_add_full(G_PRIORITY_DEFAULT, 300, +[](gpointer data) -> gboolean {
                soup_server_message_unpause(SOUP_SERVER_MESSAGE(data)); return G_SOURCE_REMOVE;
            }, g_object_ref(message), g_object_unref);
        }
    }, &domFixture, nullptr);
    GError* serverError = nullptr;
    g_assert_true(soup_server_listen_local(domServer, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &serverError));
    g_assert_no_error(serverError);
    GSList* domUris = soup_server_get_uris(domServer);
    gchar* domUri = g_uri_to_string(static_cast<GUri*>(domUris->data));
    const std::string domBase = domUri;
    g_free(domUri); g_slist_free_full(domUris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
    std::vector<std::string> played;
    auto* browser = streamgallery::createBrowserView([&](const std::string& stream, const std::string& page,
                                                       const std::string& agent) {
        g_assert_true(page.find(domBase) == 0);
        g_assert_false(agent.empty());
        played.push_back(stream);
    });
    auto* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_container_add(GTK_CONTAINER(window), browser);
    gtk_widget_show_all(window);
    auto* web = WEBKIT_WEB_VIEW(find(browser, WEBKIT_TYPE_WEB_VIEW));
    auto* status = GTK_LABEL(find(browser, GTK_TYPE_LABEL, "discovery-status"));
    g_assert_nonnull(web);
    streamgallery::openBrowserPage(browser, "file:///etc/passwd");
    g_assert_cmpstr(gtk_label_get_text(status), ==, "Enter an HTTP/HTTPS webpage URL without credentials");
    // Render JavaScript-generated DOM, resolve relative URLs, decode JSON escaping,
    // reject credentials/non-HTTP links, and deduplicate the same manifest.
    std::string html = R"HTML(
        <html><head><base href="BASEmedia/"></head><body>
        <a href="javascript:alert('x.m3u8')">invalid</a>
        <div data-src="https://user:secret@fixture.test/private.m3u8"></div>
        <video poster="../poster.svg"></video>
        <a href="../invalid.m3u8">Not HLS</a>
        <script>
        setTimeout(() => {
            const item = document.createElement('div');
            item.dataset.src = '../hls/master.m3u8?token=one&quality=best';
            document.body.appendChild(item);
            const script = document.createElement('script');
            script.type = 'application/json';
            script.textContent = '{"url":"BASEalternate.m3u8?x=1\\u0026y=2"}';
            document.body.appendChild(script);
            const duplicate = document.createElement('a');
            duplicate.href = item.dataset.src;
            document.body.appendChild(duplicate);
        }, 100);
        </script></body></html>
    )HTML";
    for (std::size_t pos = 0; (pos = html.find("BASE", pos)) != std::string::npos; pos += domBase.size()) html.replace(pos, 4, domBase);
    const auto page = domBase + "page";
    webkit_web_view_load_html(web, html.c_str(), page.c_str());
    waitFor([&] { return played.size() == 1; });
    // Inline scripts can expose candidates before the delayed DOM insertion.
    g_assert_true(played[0].find(".m3u8") != std::string::npos);
    auto* extract = GTK_BUTTON(find(browser, GTK_TYPE_BUTTON, "discovery-extract"));
    const gint64 settle = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < settle) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000); }
    gtk_button_clicked(extract);
    waitFor([&] { return played.size() == 2; });
    const auto master = domBase + "cdn/master.m3u8?token=one";
    g_assert_cmpstr(played.back().c_str(), ==, master.c_str());
    auto structured = streamgallery::browserDiscoveryResult(browser);
    g_assert_cmpuint(structured.playlists.size(), ==, 2);
    g_assert_cmpuint(structured.errors.size(), ==, 1);
    const auto poster = domBase + "poster.svg";
    g_assert_cmpstr(structured.posterUrl.c_str(), ==, poster.c_str());
    bool foundMaster = false;
    for (const auto& playlist : structured.playlists) {
        if (!playlist.master) continue;
        foundMaster = true;
        const auto child = domBase + "cdn/720p/index.m3u8";
        g_assert_cmpstr(playlist.references[0].resolvedUrl.c_str(), ==, child.c_str());
        g_assert_cmpstr(playlist.referenceSummary.c_str(), ==, "relative-only");
    }
    g_assert_true(foundMaster);
    if (const char* screenshot = g_getenv("STREAM_GALLERY_TEST_SCREENSHOT")) {
        auto* image = GTK_IMAGE(find(browser, GTK_TYPE_IMAGE, "discovery-poster"));
        waitFor([&] { return gtk_image_get_storage_type(image) == GTK_IMAGE_PIXBUF; });
        // Allow the image's queued redraw to reach the window before capture.
        const auto redraw = g_get_monotonic_time() + 200 * G_TIME_SPAN_MILLISECOND;
        while (g_get_monotonic_time() < redraw) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000); }
        auto* pixels = gdk_pixbuf_get_from_window(gtk_widget_get_window(window), 0, 0,
            gtk_widget_get_allocated_width(window), gtk_widget_get_allocated_height(window));
        g_assert_nonnull(pixels);
        GError* error = nullptr;
        g_assert_true(gdk_pixbuf_save(pixels, screenshot, "png", &error, nullptr));
        g_assert_no_error(error);
        g_object_unref(pixels);
    }
    auto* streamChoices = GTK_COMBO_BOX(find(browser, GTK_TYPE_COMBO_BOX));
    const auto alternate = domBase + "alternate.m3u8?x=1&y=2";
    g_assert_true(gtk_combo_box_set_active_id(streamChoices, alternate.c_str()));
    // Navigate away while playlist verification is in flight.
    const auto slowPage = domBase + "slow-page";
    webkit_web_view_load_html(web, "<div data-src='/slow.m3u8'></div>", slowPage.c_str());
    waitFor([&] { return domFixture.slowRequested; });
    // Navigation invalidates outstanding extraction and never replays stale URLs.
    webkit_web_view_load_html(web,
        "<html><script>setTimeout(() => { const item = document.createElement('div'); "
        "item.dataset.src = '/new.' + 'm3u8'; document.body.appendChild(item); }, 100);</script></html>",
        (domBase + "new-page").c_str());
    waitFor([&] { return played.size() == 3; });
    const auto newStream = domBase + "new.m3u8";
    g_assert_cmpstr(played.back().c_str(), ==, newStream.c_str());
    // Destroy while JavaScript evaluation is pending; callbacks must ignore it.
    gtk_button_clicked(extract);
    gtk_widget_destroy(window);
    const gint64 cleanup = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < cleanup) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000); }
    g_assert_cmpuint(played.size(), ==, 3);
    soup_server_disconnect(domServer); g_object_unref(domServer);
    // Exercise the complete page -> DOM -> HLS -> native player path locally.
    gchar* ffmpeg = g_find_program_in_path("ffmpeg");
    if (ffmpeg) {
        gchar* clipPath = g_build_filename(directory, "clip.ts", nullptr);
        GError* error = nullptr;
        GSubprocess* encoder = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &error,
            ffmpeg, "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=64x64:r=25:d=2",
            "-an", "-c:v", "mpeg2video", "-f", "mpegts", clipPath, nullptr);
        g_assert_no_error(error);
        g_assert_nonnull(encoder);
        g_assert_true(g_subprocess_wait_check(encoder, nullptr, &error));
        g_assert_no_error(error);
        g_object_unref(encoder);
        struct Fixture { std::string clip; std::string page; int manifests = 0; int segments = 0; } fixture;
        gchar* clip = nullptr;
        gsize length = 0;
        g_assert_true(g_file_get_contents(clipPath, &clip, &length, &error));
        g_assert_no_error(error);
        fixture.clip.assign(clip, length); g_free(clip);
        SoupServer* server = soup_server_new(nullptr, nullptr);
        soup_server_add_handler(server, nullptr, +[](SoupServer*, SoupServerMessage* message,
            const char* path, GHashTable*, gpointer data) {
            auto* fixture = static_cast<Fixture*>(data);
            std::string body;
            const char* type = "text/html";
            if (g_str_equal(path, "/page")) body = "<html><body><h1>Local HLS fixture</h1><div data-src='/stream.m3u8'></div></body></html>";
            else if (g_str_equal(path, "/stream.m3u8")) {
                ++fixture->manifests;
                auto* headers = soup_server_message_get_request_headers(message);
                g_assert_cmpstr(soup_message_headers_get_one(headers, "Referer"), ==, fixture->page.c_str());
                g_assert_nonnull(soup_message_headers_get_one(headers, "User-Agent"));
                type = "application/vnd.apple.mpegurl";
                body = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:2.0,\nclip.ts\n#EXT-X-ENDLIST\n";
            } else if (g_str_equal(path, "/clip.ts")) {
                ++fixture->segments; type = "video/mp2t"; body = fixture->clip;
            } else { soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, nullptr); return; }
            soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
            soup_server_message_set_response(message, type, SOUP_MEMORY_COPY, body.data(), body.size());
        }, &fixture, nullptr);
        g_assert_true(soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
        g_assert_no_error(error);
        GSList* uris = soup_server_get_uris(server);
        gchar* base = g_uri_to_string(static_cast<GUri*>(uris->data));
        fixture.page = std::string(base) + "page";
        g_free(base);
        g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
        auto* app = gtk_application_new("org.streamgallery.DiscoveryTest", G_APPLICATION_NON_UNIQUE);
        g_assert_true(g_application_register(G_APPLICATION(app), nullptr, &error));
        g_assert_no_error(error);
        auto* main = streamgallery::createMainWindow(app);
        gtk_widget_show_all(main);
        auto* stack = GTK_STACK(find(main, GTK_TYPE_STACK));
        gtk_stack_set_visible_child_name(stack, "browser");
        auto* discovery = find(main, GTK_TYPE_BOX, "discovery-browser");
        streamgallery::openBrowserPage(discovery, fixture.page);
        auto* playPause = GTK_BUTTON(find(main, GTK_TYPE_BUTTON, "player-play-pause"));
        const gint64 playbackStart = g_get_monotonic_time();
        bool reported = false;
        waitFor([&] {
            if (!reported && g_get_monotonic_time() - playbackStart > 12 * G_TIME_SPAN_SECOND) {
                reported = true;
                auto* playerStatus = GTK_LABEL(find(main, GTK_TYPE_LABEL, "player-status"));
                auto* browserStatus = GTK_LABEL(find(main, GTK_TYPE_LABEL, "discovery-status"));
                g_printerr("HLS playback diagnostic: player=%s; discovery=%s; manifests=%d; segments=%d\n",
                    gtk_label_get_text(playerStatus), gtk_label_get_text(browserStatus), fixture.manifests, fixture.segments);
            }
            return g_strcmp0(gtk_button_get_label(playPause), "Replay") == 0;
        });
        g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "player");
        g_assert_cmpint(fixture.manifests, >, 0);
        g_assert_cmpint(fixture.segments, >, 0);
        g_assert_true(gtk_widget_get_sensitive(find(main, GTK_TYPE_SCALE, "player-position")));
        gtk_widget_destroy(main);
        g_object_unref(app);
        soup_server_disconnect(server); g_object_unref(server);
        g_free(clipPath); g_free(ffmpeg);
    } else g_test_message("ffmpeg unavailable: local end-to-end HLS playback check skipped");
    std::filesystem::remove_all(directory);
    g_free(directory);
    return 0;
}
