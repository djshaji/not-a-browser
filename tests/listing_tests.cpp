#include "ui/BrowserView.hpp"
#include "ui/ListingView.hpp"
#include "ui/MainWindow.hpp"
#ifdef HAVE_LIBRARY_TESTS
#include "database/LibraryRepository.hpp"
#endif
#include <libsoup/soup.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <cstdlib>

namespace {
GtkWidget* observedListing = nullptr;
GtkWidget* find(GtkWidget* root, const char* name) {
    if (g_strcmp0(gtk_widget_get_name(root), name) == 0) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget* result = nullptr;
    for (auto* child = children; child && !result; child = child->next) result = find(GTK_WIDGET(child->data), name);
    g_list_free(children); return result;
}
void waitUntil(int line, const std::function<bool()>& predicate) {
    const auto deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
    while (!predicate() && g_get_monotonic_time() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000);
    }
    if (!predicate()) {
        auto result = streamgallery::listingResult(observedListing);
        g_printerr("Listing check at line %d failed: items=%zu pages=%u busy=%d next=%d; status=%s\n", line,
            result.items.size(), result.pages, result.busy, result.hasNext,
            gtk_label_get_text(GTK_LABEL(find(observedListing, "listing-status"))));
        bool inspected = false;
        webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(find(observedListing, "listing-web")),
            "JSON.stringify({body:document.body.innerHTML,batch:typeof batch==='undefined'?'none':batch})", -1,
            nullptr, nullptr, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
                GError* error = nullptr;
                JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
                gchar* text = value ? jsc_value_to_string(value) : nullptr;
                g_printerr("Listing DOM: %s; error: %s\n", text ? text : "", error ? error->message : "");
                g_free(text); g_clear_object(&value); g_clear_error(&error); *static_cast<bool*>(data) = true;
            }, &inspected);
        const auto end = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
        while (!inspected && g_get_monotonic_time() < end) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000); }
        std::exit(1);
    }
}
#define waitFor(...) waitUntil(__LINE__, __VA_ARGS__)
std::string fixture(const char* file) {
    std::ifstream input(std::filesystem::path(FIXTURE_DIRECTORY) / file);
    g_assert_true(input.good()); std::ostringstream content; content << input.rdbuf(); return content.str();
}
void click(GtkWidget* root, const char* name) {
    auto* button = find(root, name); g_assert_nonnull(button); g_assert_true(gtk_widget_get_sensitive(button));
    gtk_button_clicked(GTK_BUTTON(button));
}
}
int main(int argc, char** argv) {
    gchar* directory = g_dir_make_tmp("stream-gallery-listings-XXXXXX", nullptr); g_assert_nonnull(directory);
    for (const auto* variable : {"XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME"}) g_setenv(variable, directory, TRUE);
    if (!gtk_init_check(&argc, &argv)) { std::filesystem::remove_all(directory); g_free(directory); return 77; }
    std::map<std::string, unsigned> requests;
    auto* server = soup_server_new(nullptr, nullptr);
    soup_server_add_handler(server, nullptr, +[](SoupServer*, SoupServerMessage* message, const char* path, GHashTable*, gpointer data) {
        ++(*static_cast<std::map<std::string, unsigned>*>(data))[path];
        std::string body; const char* type = "text/html";
        if (g_str_equal(path, "/catalog/one")) body = fixture("links-one.html");
        else if (g_str_equal(path, "/catalog/two")) body = fixture("links-two.html");
        else if (g_str_equal(path, "/dynamic")) body = fixture("load-more.html");
        else if (g_str_has_prefix(path, "/limit/")) {
            const auto page = std::stoi(std::string(path).substr(7));
            body = "<div class='video-card'><a class='video-link' href='/watch/limit" + std::to_string(page) +
                "'><span class='video-title'>Limited video</span></a></div><a rel='next' href='/limit/" + std::to_string(page + 1) + "'>Next</a>";
        } else if (g_str_equal(path, "/delayed")) {
            body = "<script>setTimeout(() => document.body.insertAdjacentHTML('beforeend', \"<div class='video-card'><a class='video-link' href='/watch/late'>Late video</a></div>\"), 800);</script>";
        } else if (g_str_has_suffix(path, ".svg")) {
            type = "image/svg+xml"; body = "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='54'><rect width='96' height='54' fill='steelblue'/></svg>";
        } else if (g_str_has_prefix(path, "/watch/")) body = "<title>Watch video</title><div data-src='/media.m3u8'></div>";
        else if (g_str_equal(path, "/media.m3u8")) {
            type = "application/vnd.apple.mpegurl"; body = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2,\nmissing.ts\n#EXT-X-ENDLIST\n";
        } else { soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, nullptr); return; }
        soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
        soup_server_message_set_response(message, type, SOUP_MEMORY_COPY, body.data(), body.size());
    }, &requests, nullptr);
    GError* error = nullptr;
    g_assert_true(soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error)); g_assert_no_error(error);
    GSList* uris = soup_server_get_uris(server); gchar* uri = g_uri_to_string(static_cast<GUri*>(uris->data));
    const std::string base = uri; g_free(uri); g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
    auto* app = gtk_application_new("org.streamgallery.ListingTest", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), nullptr, &error)); g_assert_no_error(error);
    auto* window = streamgallery::createMainWindow(app); gtk_widget_show_all(window);
    auto* listing = find(window, "listing-browser"); auto* browser = find(window, "discovery-browser");
    observedListing = listing;
    g_assert_nonnull(listing); g_assert_nonnull(browser);
    auto* tabs = GTK_NOTEBOOK(gtk_widget_get_ancestor(listing, GTK_TYPE_NOTEBOOK));
    gtk_notebook_set_current_page(tabs, 1);
    auto* stack = GTK_STACK(gtk_widget_get_ancestor(GTK_WIDGET(tabs), GTK_TYPE_STACK)); gtk_stack_set_visible_child_name(stack, "browser");
    streamgallery::openListingPage(listing, "file:///etc/passwd");
    g_assert_true(streamgallery::listingResult(listing).items.empty());
    streamgallery::openListingPage(listing, base + "catalog/one");
    waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.items.size() == 2; });
    auto result = streamgallery::listingResult(listing);
    const auto one = base + "watch/one"; const auto poster = base + "catalog/images/one.svg";
    g_assert_cmpstr(result.items[0].url.c_str(), ==, one.c_str());
    g_assert_cmpstr(result.items[0].title.c_str(), ==, "First & best");
    g_assert_cmpstr(result.items[0].posterUrl.c_str(), ==, poster.c_str());
    const auto untitled = base + "watch/untitled";
    g_assert_cmpstr(result.items[1].title.c_str(), ==, untitled.c_str());
#ifdef HAVE_LIBRARY_TESTS
    click(listing, "listing-save-0");
    auto* save = GTK_BUTTON(find(listing, "listing-save-0"));
    waitFor([&] { return g_strcmp0(gtk_button_get_label(save), "Saved") == 0; });
    click(listing, "listing-save-0");
    waitFor([&] { return g_strcmp0(gtk_button_get_label(save), "Already saved") == 0; });
    waitFor([&] { return std::string(gtk_label_get_text(GTK_LABEL(find(window, "library-status")))).find("of 1 videos") != std::string::npos; });
    {
        streamgallery::Database database((std::filesystem::path(directory) / "stream-gallery/library.db").string());
        streamgallery::LibraryRepository library(database); const auto videos = library.videos();
        g_assert_cmpuint(videos.size(), ==, 1); g_assert_cmpstr(videos[0].canonicalUrl.c_str(), ==, one.c_str());
        g_assert_cmpstr(videos[0].title.c_str(), ==, "First & best"); g_assert_cmpstr(videos[0].thumbnailUrl.c_str(), ==, poster.c_str());
    }
#else
    g_assert_null(find(listing, "listing-save-0"));
#endif
    if (const char* screenshot = g_getenv("STREAM_GALLERY_LISTING_SCREENSHOT")) {
        const auto redraw = g_get_monotonic_time() + 500 * G_TIME_SPAN_MILLISECOND;
        while (g_get_monotonic_time() < redraw) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000); }
        auto* pixels = gdk_pixbuf_get_from_window(gtk_widget_get_window(window), 0, 0,
            gtk_widget_get_allocated_width(window), gtk_widget_get_allocated_height(window));
        g_assert_nonnull(pixels); g_assert_true(gdk_pixbuf_save(pixels, screenshot, "png", &error, nullptr));
        g_assert_no_error(error); g_object_unref(pixels);
    }
    click(listing, "listing-next");
    waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.pages == 2 && result.items.size() == 3; });
    g_assert_false(streamgallery::listingResult(listing).hasNext);
    g_assert_cmpuint(requests["/catalog/one"], ==, 1); // Pagination loop was rejected.
    click(listing, "listing-play-0");
    waitFor([&] { auto result = streamgallery::browserDiscoveryResult(browser); return result.pageUrl == one && !result.playlists.empty(); });
    g_assert_cmpint(gtk_notebook_get_current_page(tabs), ==, 0);
    gtk_stack_set_visible_child_name(stack, "browser"); gtk_notebook_set_current_page(tabs, 1);
    auto* profile = GTK_COMBO_BOX(find(listing, "listing-profile")); gtk_combo_box_set_active(profile, 1);
    streamgallery::openListingPage(listing, base + "dynamic");
    waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.items.size() == 2; });
    click(listing, "listing-next");
    waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.items.size() == 3; });
    click(listing, "listing-next");
    waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.items.size() == 4; });
    g_assert_false(streamgallery::listingResult(listing).hasNext);
    g_assert_cmpuint(requests["/dynamic"], ==, 1); // Button pagination stays in the same document.
    gtk_combo_box_set_active(profile, 0); streamgallery::openListingPage(listing, base + "limit/1");
    waitFor([&] { return !streamgallery::listingResult(listing).busy && streamgallery::listingResult(listing).items.size() == 1; });
    for (unsigned page = 2; page <= 5; ++page) {
        click(listing, "listing-next");
        waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.pages == page && result.items.size() == page; });
    }
    g_assert_false(streamgallery::listingResult(listing).hasNext); g_assert_cmpuint(requests["/limit/6"], ==, 0);
    streamgallery::openListingPage(listing, base + "delayed");
    waitFor([&] { return !streamgallery::listingResult(listing).busy && streamgallery::listingResult(listing).items.size() == 1; });
    // Cancel a pending observer, then navigate before the old page inserts cards.
    streamgallery::openListingPage(listing, base + "delayed");
    waitFor([&] { return requests["/delayed"] >= 2 && streamgallery::listingResult(listing).busy; });
    click(listing, "listing-stop"); streamgallery::openListingPage(listing, base + "catalog/one");
    waitFor([&] { auto result = streamgallery::listingResult(listing); return !result.busy && result.items.size() == 2; });
    const auto finalResult = streamgallery::listingResult(listing);
    g_assert_cmpstr(finalResult.items[0].url.c_str(), ==, one.c_str());
    streamgallery::openListingPage(listing, base + "delayed");
    waitFor([&] { return requests["/delayed"] >= 3 && streamgallery::listingResult(listing).busy; });
    gtk_widget_destroy(window);
    const auto cleanup = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < cleanup) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(1000); }
    g_object_unref(app); soup_server_disconnect(server); g_object_unref(server);
    std::filesystem::remove_all(directory); g_free(directory); return 0;
}
