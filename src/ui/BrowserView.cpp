#include "ui/BrowserView.hpp"
#include <webkit2/webkit2.h>
#include <memory>
#include <utility>

extern "C" GResource* stream_gallery_get_resource();

namespace streamgallery {
namespace {
bool httpUrl(const std::string& value) {
    if (value.empty() || value.size() > 8192 || value.find_first_of("\r\n\t") != std::string::npos) return false;
    GUri* uri = g_uri_parse(value.c_str(), G_URI_FLAGS_NONE, nullptr);
    if (!uri) return false;
    const char* scheme = g_uri_get_scheme(uri);
    bool valid = scheme && (g_ascii_strcasecmp(scheme, "http") == 0 || g_ascii_strcasecmp(scheme, "https") == 0)
        && g_uri_get_host(uri) && *g_uri_get_host(uri) && !g_uri_get_userinfo(uri);
    g_uri_unref(uri);
    return valid;
}
struct Browser;
using BrowserPtr = std::shared_ptr<Browser>;
struct Evaluation { BrowserPtr state; unsigned generation; };
struct Browser {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* address = gtk_entry_new();
    GtkWidget* status = gtk_label_new("Load a webpage to find an HLS stream in its DOM");
    GtkWidget* streams = gtk_combo_box_text_new();
    GtkWidget* play = gtk_button_new_with_label("Play stream");
    WebKitWebView* web = nullptr;
    Browser() {
        gchar* data = g_build_filename(g_get_user_data_dir(), "stream-gallery", "browser", nullptr);
        gchar* cache = g_build_filename(g_get_user_cache_dir(), "stream-gallery", "browser", nullptr);
        auto* manager = webkit_website_data_manager_new("base-data-directory", data, "base-cache-directory", cache, nullptr);
        auto* context = webkit_web_context_new_with_website_data_manager(manager);
        web = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
        g_object_unref(context); g_object_unref(manager);
        g_free(data); g_free(cache);
    }
    StreamReady ready;
    GCancellable* cancellation = nullptr;
    guint timer = 0;
    unsigned generation = 0;
    gint64 deadline = 0;
    bool closed = false;
    bool evaluating = false;
    bool autoPlay = true;
    bool failed = false;
    std::string script;
    BrowserPtr shared() { return *static_cast<BrowserPtr*>(g_object_get_data(G_OBJECT(root), "browser-state")); }
    ~Browser() { g_clear_object(&cancellation); }
    void message(const char* text) {
        gtk_label_set_text(GTK_LABEL(status), text);
        gtk_widget_set_tooltip_text(status, text);
    }
    void cancel() {
        ++generation;
        if (timer) { g_source_remove(timer); timer = 0; }
        if (cancellation) g_cancellable_cancel(cancellation);
        g_clear_object(&cancellation);
        evaluating = false;
    }
    void clearStreams() {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(streams));
        gtk_widget_set_sensitive(play, FALSE);
    }
    void playSelected() {
        const char* stream = gtk_combo_box_get_active_id(GTK_COMBO_BOX(streams));
        const char* page = webkit_web_view_get_uri(web);
        if (!stream || !page || !httpUrl(stream) || !httpUrl(page)) return;
        std::string selected(stream), source(page);
        std::string agent = webkit_settings_get_user_agent(webkit_web_view_get_settings(web));
        // Pause page media to avoid simultaneous browser and native playback.
        webkit_web_view_evaluate_javascript(web,
            "document.querySelectorAll('video,audio').forEach(media => media.pause());", -1,
            nullptr, nullptr, nullptr, nullptr, nullptr);
        if (ready) ready(selected, source, agent);
    }
    void extract() {
        if (closed || evaluating || script.empty()) return;
        if (!cancellation) cancellation = g_cancellable_new();
        evaluating = true;
        auto* request = new Evaluation{shared(), generation};
        webkit_web_view_evaluate_javascript(web, script.c_str(), script.size(), "stream-gallery-discovery", nullptr,
            cancellation, +[](GObject* source, GAsyncResult* result, gpointer data) {
                std::unique_ptr<Evaluation> request(static_cast<Evaluation*>(data));
                auto state = request->state;
                GError* error = nullptr;
                JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
                if (state->closed || request->generation != state->generation) {
                    g_clear_object(&value); g_clear_error(&error); return;
                }
                state->evaluating = false;
                if (error) {
                    state->message(error->message);
                    if (state->timer) { g_source_remove(state->timer); state->timer = 0; }
                    g_error_free(error); g_clear_object(&value); return;
                }
                gchar* text = value ? jsc_value_to_string(value) : nullptr;
                if (text && *text) {
                    state->clearStreams();
                    gchar** candidates = g_strsplit(text, "\n", 65);
                    int count = 0;
                    for (gchar** item = candidates; *item && count < 64; ++item) {
                        if (httpUrl(*item)) {
                            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state->streams), *item, *item);
                            ++count;
                        }
                    }
                    g_strfreev(candidates);
                    if (count) {
                        gtk_combo_box_set_active(GTK_COMBO_BOX(state->streams), 0);
                        gtk_widget_set_sensitive(state->play, TRUE);
                        state->message(("Found " + std::to_string(count) + " HLS stream(s)").c_str());
                        if (state->timer) { g_source_remove(state->timer); state->timer = 0; }
                        if (state->autoPlay) { state->autoPlay = false; state->playSelected(); }
                    }
                }
                g_free(text); g_clear_object(&value);
            }, request);
    }
    void startExtraction() {
        cancel();
        clearStreams();
        autoPlay = true;
        deadline = g_get_monotonic_time() + 20 * G_TIME_SPAN_SECOND;
        message("Looking for an HLS stream in the page DOM…");
        timer = g_timeout_add(500, +[](gpointer data) -> gboolean {
            auto* state = static_cast<Browser*>(data);
            if (g_get_monotonic_time() >= state->deadline) {
                state->timer = 0;
                if (state->cancellation) g_cancellable_cancel(state->cancellation);
                ++state->generation;
                state->evaluating = false;
                state->message("No .m3u8 URL found in the DOM. Start the website player, then try Extract & Play.");
                return G_SOURCE_REMOVE;
            }
            state->extract();
            return G_SOURCE_CONTINUE;
        }, this);
        extract();
    }
    void open(const std::string& url) {
        if (!httpUrl(url)) { message("Enter an HTTP/HTTPS webpage URL without credentials"); return; }
        cancel(); clearStreams();
        gtk_entry_set_text(GTK_ENTRY(address), url.c_str());
        webkit_web_view_load_uri(web, url.c_str());
    }
};
}
GtkWidget* createBrowserView(StreamReady onStream) {
    (void)stream_gallery_get_resource();
    auto state = std::make_shared<Browser>();
    state->ready = std::move(onStream);
    g_object_set_data_full(G_OBJECT(state->root), "browser-state", new BrowserPtr(state),
        +[](gpointer data) { delete static_cast<BrowserPtr*>(data); });
    GBytes* script = g_resources_lookup_data("/org/streamgallery/app/scripts/extract-hls.js", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (script) {
        gsize size = 0;
        const char* text = static_cast<const char*>(g_bytes_get_data(script, &size));
        state->script.assign(text, size); g_bytes_unref(script);
    }
    auto* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    auto* load = gtk_button_new_with_label("Load & Play");
    auto* stop = gtk_button_new_with_label("Stop loading");
    auto* extract = gtk_button_new_with_label("Extract & Play");
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->address), "Webpage URL");
    gtk_entry_set_max_length(GTK_ENTRY(state->address), 8192);
    gtk_box_pack_start(GTK_BOX(toolbar), state->address, TRUE, TRUE, 0);
    for (auto* widget : {load, stop, extract}) gtk_box_pack_start(GTK_BOX(toolbar), widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), toolbar, FALSE, FALSE, 0);
    auto* results = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(state->streams, TRUE);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(state->streams), FALSE);
    GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(state->streams));
    for (auto* cell = cells; cell; cell = cell->next)
        if (GTK_IS_CELL_RENDERER_TEXT(cell->data))
            g_object_set(cell->data, "ellipsize", PANGO_ELLIPSIZE_END, "max-width-chars", 60, nullptr);
    g_list_free(cells);
    gtk_box_pack_start(GTK_BOX(results), state->streams, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(results), state->play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), results, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), GTK_WIDGET(state->web), TRUE, TRUE, 0);
    gtk_label_set_ellipsize(GTK_LABEL(state->status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(state->root), state->status, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(state->play, FALSE);
    // Keep browser media silent; the native player owns audio playback.
    webkit_web_view_set_is_muted(state->web, TRUE);
    g_signal_connect(load, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Browser*>(data); state->open(gtk_entry_get_text(GTK_ENTRY(state->address)));
    }), state.get());
    g_signal_connect(state->address, "activate", G_CALLBACK(+[](GtkEntry* entry, gpointer data) {
        static_cast<Browser*>(data)->open(gtk_entry_get_text(entry));
    }), state.get());
    g_signal_connect(stop, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Browser*>(data); state->cancel(); state->failed = true;
        webkit_web_view_stop_loading(state->web); state->message("Page loading and extraction stopped");
    }), state.get());
    g_signal_connect(extract, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<Browser*>(data)->startExtraction();
    }), state.get());
    g_signal_connect(state->play, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<Browser*>(data)->playSelected();
    }), state.get());
    g_signal_connect(state->web, "load-changed", G_CALLBACK(+[](WebKitWebView* web, WebKitLoadEvent event, gpointer data) {
        auto* state = static_cast<Browser*>(data);
        if (state->closed) return;
        if (event == WEBKIT_LOAD_STARTED) {
            state->cancel(); state->clearStreams(); state->failed = false;
            state->message("Loading webpage…");
            state->timer = g_timeout_add_seconds(30, +[](gpointer data) -> gboolean {
                auto* state = static_cast<Browser*>(data);
                state->timer = 0; state->failed = true;
                webkit_web_view_stop_loading(state->web);
                state->message("Page loading timed out. Try loading the page again.");
                return G_SOURCE_REMOVE;
            }, state);
        } else if (event == WEBKIT_LOAD_FINISHED && !state->failed) {
            const char* uri = webkit_web_view_get_uri(web);
            if (uri && httpUrl(uri)) {
                gtk_entry_set_text(GTK_ENTRY(state->address), uri); state->startExtraction();
            }
        }
    }), state.get());
    g_signal_connect(state->web, "load-failed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent, const gchar*, GError* error, gpointer data) -> gboolean {
        auto* state = static_cast<Browser*>(data);
        state->cancel(); state->failed = true; state->message(error->message); return TRUE;
    }), state.get());
    g_signal_connect(state->web, "web-process-terminated", G_CALLBACK(+[](WebKitWebView*, WebKitWebProcessTerminationReason, gpointer data) {
        auto* state = static_cast<Browser*>(data); state->cancel();
        state->message("Webpage process stopped. Reload the page to try again.");
    }), state.get());
    g_signal_connect(state->root, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* state = static_cast<Browser*>(data); state->closed = true; state->cancel();
        g_signal_handlers_disconnect_by_data(state->web, state);
        webkit_web_view_stop_loading(state->web);
    }), state.get());
    gtk_widget_set_name(state->root, "discovery-browser");
    gtk_widget_set_name(state->address, "discovery-address");
    gtk_widget_set_name(load, "discovery-load");
    gtk_widget_set_name(extract, "discovery-extract");
    gtk_widget_set_name(state->status, "discovery-status");
    return state->root;
}
void openBrowserPage(GtkWidget* browser, const std::string& url) {
    auto* state = static_cast<BrowserPtr*>(g_object_get_data(G_OBJECT(browser), "browser-state"));
    if (state && !(*state)->closed) (*state)->open(url);
}
}
