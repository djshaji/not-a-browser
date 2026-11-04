#include "ui/BrowserView.hpp"
#include "services/ThumbnailService.hpp"
#ifdef HAVE_LIBRARY
#include "services/BookmarkService.hpp"
#endif
#include <webkit2/webkit2.h>
#include <memory>
#include <utility>
#include <deque>
#include <set>

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
std::string stringProperty(JSCValue* object, const char* name) {
    if (!object || !jsc_value_is_object(object)) return {};
    JSCValue* value = jsc_value_object_get_property(object, name);
    gchar* text = value && jsc_value_is_string(value) ? jsc_value_to_string(value) : nullptr;
    std::string result = text ? text : "";
    g_free(text); g_clear_object(&value);
    return result;
}
struct Browser {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* address = gtk_entry_new();
    GtkWidget* status = gtk_label_new("Load a webpage to find an HLS stream in its DOM");
    GtkWidget* streams = gtk_combo_box_text_new();
    GtkWidget* play = gtk_button_new_with_label("Play stream");
    GtkWidget* poster = gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
    GtkWidget* details = gtk_label_new("");
    GtkWidget* pageTitle = gtk_label_new("");
#ifdef HAVE_LIBRARY
    GtkWidget* save = gtk_button_new_with_label("Save to Library");
    GtkWidget* saveStatus = gtk_label_new("");
    bool saving = false;
#endif
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
    std::function<void()> librarySaved;
    GCancellable* cancellation = nullptr;
    guint timer = 0;
    unsigned generation = 0;
    gint64 deadline = 0;
    bool closed = false;
    bool evaluating = false;
    bool autoPlay = true;
    bool failed = false;
    std::string script;
    DiscoveryResult result;
    std::set<std::string> requested;
    std::deque<std::string> pending;
    unsigned inFlight = 0;
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
        pending.clear(); requested.clear(); inFlight = 0;
    }
    void clearStreams() {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(streams));
        gtk_widget_set_sensitive(play, FALSE);
        result = {};
        gtk_image_set_from_icon_name(GTK_IMAGE(poster), "video-x-generic", GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_tooltip_text(poster, nullptr);
        gtk_label_set_text(GTK_LABEL(details), "");
        gtk_label_set_text(GTK_LABEL(pageTitle), "");
        gtk_widget_set_tooltip_text(pageTitle, nullptr);
#ifdef HAVE_LIBRARY
        gtk_widget_set_sensitive(save, FALSE);
        gtk_label_set_text(GTK_LABEL(saveStatus), "");
        gtk_widget_hide(saveStatus);
#endif
    }
#ifdef HAVE_LIBRARY
    void updateSaveButton() {
        const char* currentPage = webkit_web_view_get_uri(web);
        gtk_widget_set_sensitive(save, !saving && httpUrl(result.pageUrl) && currentPage && result.pageUrl == currentPage);
        gtk_button_set_label(GTK_BUTTON(save), saving ? "Saving…" : "Save to Library");
    }
    void saveMessage(const std::string& text) {
        gtk_label_set_text(GTK_LABEL(saveStatus), text.c_str());
        gtk_widget_set_tooltip_text(saveStatus, text.c_str());
        gtk_widget_show(saveStatus);
    }
    void saveBookmark() {
        updateSaveButton();
        if (!gtk_widget_get_sensitive(save)) return;
        // An explicit save finishes even if the user navigates away. Its metadata
        // is immutable, and only the matching page receives completion messages.
        saving = true;
        updateSaveButton();
        saveMessage("Saving to Library…");
        auto state = shared();
        const auto token = generation;
        saveBookmarkAsync(result.pageUrl, result.title, result.posterUrl, [state, token](bool created, const std::string& error) {
            if (!state->closed) {
                state->saving = false;
                state->updateSaveButton();
                if (token == state->generation) {
                    state->saveMessage(!error.empty() ? "Could not save to Library: " + error :
                        created ? "Saved to Library" : "Already in Library");
                }
                if (error.empty() && state->librarySaved) state->librarySaved();
            }
        });
    }
#endif
    void showSelection() {
        const char* selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(streams));
        if (!selected) return;
        for (const auto& playlist : result.playlists) {
            if (playlist.url != selected) continue;
            const auto links = playlist.referenceSummary == "relative-only" ? "relative" :
                playlist.referenceSummary == "absolute-only" ? "absolute" : "mixed";
            std::string text = playlist.master ? std::string("Master playlist: ") + links + " links" : "Media playlist";
            if (playlist.master) text += " (" + std::to_string(playlist.references.size()) +
                (playlist.references.size() == 1 ? " reference)" : " references)");
            for (const auto& warning : playlist.warnings) text += "; " + warning;
            gtk_label_set_text(GTK_LABEL(details), text.c_str());
            gtk_widget_set_tooltip_text(details, text.c_str());
            break;
        }
    }
    void finishInspection() {
        if (result.playlists.empty()) return;
        if (timer) { g_source_remove(timer); timer = 0; }
        if (!gtk_combo_box_get_active_id(GTK_COMBO_BOX(streams))) {
            const HlsPlaylist* selected = &result.playlists.front();
            for (const auto& playlist : result.playlists) if (playlist.master) { selected = &playlist; break; }
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(streams), selected->url.c_str());
        }
        gtk_widget_set_sensitive(play, TRUE);
        std::string text = "Verified " + std::to_string(result.playlists.size()) + " HLS playlist(s)";
        if (!result.errors.empty()) text += "; " + std::to_string(result.errors.size()) + " candidate(s) rejected";
        message(text.c_str());
        if (autoPlay) { autoPlay = false; playSelected(); }
    }
    void pump() {
        while (inFlight < 4 && !pending.empty()) {
            std::string url = std::move(pending.front()); pending.pop_front();
            ++inFlight;
            auto state = shared();
            const auto token = generation;
            const char* agent = webkit_settings_get_user_agent(webkit_web_view_get_settings(web));
            inspectHlsPlaylistAsync(url, result.pageUrl, agent ? agent : "StreamGallery/0.1", cancellation,
                [state, token, url](HlsPlaylist playlist) {
                    if (state->closed || token != state->generation) return;
                    --state->inFlight;
                    if (!playlist.error.empty()) {
                        state->result.errors.push_back(url + ": " + playlist.error);
                    } else {
                        bool duplicate = false;
                        for (const auto& existing : state->result.playlists)
                            if (existing.url == playlist.url) duplicate = true;
                        if (!duplicate) {
                            std::string label = (playlist.master ? "Master — " : "Media — ") + playlist.url;
                            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(state->streams), playlist.url.c_str(), label.c_str());
                            state->result.playlists.push_back(std::move(playlist));
                        }
                    }
                    state->pump();
                    if (!state->inFlight && state->pending.empty()) state->finishInspection();
                });
        }
    }
    void loadPoster(const std::string& url) {
        if (url.empty() || result.posterUrl == url) return;
        result.posterUrl = url;
        gtk_widget_set_tooltip_text(poster, url.c_str());
        struct PosterRequest { BrowserPtr state; unsigned generation; std::string url; };
        auto* request = new PosterRequest{shared(), generation, url};
        GTask* task = g_task_new(nullptr, cancellation, +[](GObject*, GAsyncResult* async, gpointer) {
            auto* task = G_TASK(async);
            auto* request = static_cast<PosterRequest*>(g_task_get_task_data(task));
            GError* error = nullptr;
            auto* image = static_cast<GdkPixbuf*>(g_task_propagate_pointer(task, &error));
            auto state = request->state;
            if (image && !state->closed && request->generation == state->generation && request->url == state->result.posterUrl)
                gtk_image_set_from_pixbuf(GTK_IMAGE(state->poster), image);
            g_clear_object(&image); g_clear_error(&error);
        }, nullptr);
        g_task_set_task_data(task, request, +[](gpointer data) { delete static_cast<PosterRequest*>(data); });
        g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancellation) {
            auto* request = static_cast<PosterRequest*>(data);
            auto* image = loadThumbnail(request->url, cancellation);
            g_task_return_pointer(task, image, +[](gpointer data) { if (data) g_object_unref(data); });
        });
        g_object_unref(task);
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
                    state->cancel();
                    state->message(error->message);
                    g_error_free(error); g_clear_object(&value); return;
                }
                state->result.pageUrl = stringProperty(value, "pageUrl");
                state->result.title = stringProperty(value, "title");
                if (state->result.title.empty()) state->result.title = state->result.pageUrl;
                gtk_label_set_text(GTK_LABEL(state->pageTitle), state->result.title.c_str());
                gtk_widget_set_tooltip_text(state->pageTitle, state->result.title.c_str());
#ifdef HAVE_LIBRARY
                state->updateSaveButton();
#endif
                if (value && jsc_value_is_object(value)) {
                    JSCValue* poster = jsc_value_object_get_property(value, "poster");
                    const auto posterUrl = stringProperty(poster, "url");
                    if (httpUrl(posterUrl)) state->loadPoster(posterUrl);
                    g_clear_object(&poster);
                    JSCValue* candidates = jsc_value_object_get_property(value, "playlistCandidates");
                    if (candidates && jsc_value_is_array(candidates)) {
                        JSCValue* length = jsc_value_object_get_property(candidates, "length");
                        const int count = length ? jsc_value_to_int32(length) : 0;
                        g_clear_object(&length);
                        for (int i = 0; i < count && i < 64 && state->requested.size() < 64; ++i) {
                            JSCValue* item = jsc_value_object_get_property_at_index(candidates, i);
                            gchar* text = item && jsc_value_is_string(item) ? jsc_value_to_string(item) : nullptr;
                            if (text && httpUrl(text) && state->requested.insert(text).second) state->pending.push_back(text);
                            g_free(text); g_clear_object(&item);
                        }
                    }
                    g_clear_object(&candidates);
                    state->pump();
                }
                g_clear_object(&value);
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
                state->finishInspection();
                if (state->cancellation) g_cancellable_cancel(state->cancellation);
                ++state->generation;
                state->evaluating = false;
                state->pending.clear(); state->inFlight = 0;
                if (state->result.playlists.empty()) {
                    std::string message = state->result.errors.empty() ?
                        "No .m3u8 URL found in the DOM. Start the website player, then try Extract & Play." :
                        "No verified HLS playlist. " + state->result.errors.back();
                    state->message(message.c_str());
                }
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
GtkWidget* createBrowserView(StreamReady onStream, std::function<void()> onLibrarySaved) {
    (void)stream_gallery_get_resource();
    auto state = std::make_shared<Browser>();
    state->ready = std::move(onStream);
    state->librarySaved = std::move(onLibrarySaved);
    g_object_set_data_full(G_OBJECT(state->root), "browser-state", new BrowserPtr(state),
        +[](gpointer data) { delete static_cast<BrowserPtr*>(data); });
    GBytes* script = g_resources_lookup_data("/org/streamgallery/app/scripts/extract-hls.js", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (script) {
        gsize size = 0;
        const char* text = static_cast<const char*>(g_bytes_get_data(script, &size));
        state->script.assign(text, size); g_bytes_unref(script);
    }
    GBytes* adapter = g_resources_lookup_data("/org/streamgallery/app/adapters/generic-hls.json", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (adapter) {
        gsize size = 0;
        const char* text = static_cast<const char*>(g_bytes_get_data(adapter, &size));
        state->script += "(" + std::string(text, size) + ");";
        g_bytes_unref(adapter);
    } else state->script.clear();
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
    gtk_widget_set_valign(state->streams, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state->play, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(state->poster, 192, 108);
    gtk_combo_box_set_popup_fixed_width(GTK_COMBO_BOX(state->streams), FALSE);
    GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(state->streams));
    for (auto* cell = cells; cell; cell = cell->next)
        if (GTK_IS_CELL_RENDERER_TEXT(cell->data))
            g_object_set(cell->data, "ellipsize", PANGO_ELLIPSIZE_END, "max-width-chars", 60, nullptr);
    g_list_free(cells);
    gtk_box_pack_start(GTK_BOX(results), state->streams, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(results), state->play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(results), state->poster, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), results, FALSE, FALSE, 0);
    auto* metadata = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_label_set_ellipsize(GTK_LABEL(state->pageTitle), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(state->pageTitle), 0);
    gtk_box_pack_start(GTK_BOX(metadata), state->pageTitle, TRUE, TRUE, 0);
#ifdef HAVE_LIBRARY
    gtk_widget_set_sensitive(state->save, FALSE);
    gtk_box_pack_start(GTK_BOX(metadata), state->save, FALSE, FALSE, 0);
    g_signal_connect(state->save, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<Browser*>(data)->saveBookmark();
    }), state.get());
    gtk_widget_set_name(state->save, "discovery-save");
#endif
    gtk_box_pack_start(GTK_BOX(state->root), metadata, FALSE, FALSE, 0);
    gtk_label_set_ellipsize(GTK_LABEL(state->details), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(state->root), state->details, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), GTK_WIDGET(state->web), TRUE, TRUE, 0);
    gtk_label_set_ellipsize(GTK_LABEL(state->status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(state->root), state->status, FALSE, FALSE, 0);
#ifdef HAVE_LIBRARY
    gtk_label_set_ellipsize(GTK_LABEL(state->saveStatus), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_no_show_all(state->saveStatus, TRUE);
    gtk_widget_set_name(state->saveStatus, "discovery-save-status");
    gtk_box_pack_start(GTK_BOX(state->root), state->saveStatus, FALSE, FALSE, 0);
#endif
    gtk_widget_set_sensitive(state->play, FALSE);
    g_signal_connect(state->streams, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer data) {
        static_cast<Browser*>(data)->showSelection();
    }), state.get());
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
    gtk_widget_set_name(state->poster, "discovery-poster");
    gtk_widget_set_name(state->details, "discovery-details");
    gtk_widget_set_name(state->pageTitle, "discovery-title");
    return state->root;
}
void openBrowserPage(GtkWidget* browser, const std::string& url) {
    if (auto* notebook = gtk_widget_get_ancestor(browser, GTK_TYPE_NOTEBOOK))
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), gtk_notebook_page_num(GTK_NOTEBOOK(notebook), browser));
    auto* state = static_cast<BrowserPtr*>(g_object_get_data(G_OBJECT(browser), "browser-state"));
    if (state && !(*state)->closed) (*state)->open(url);
}
WebKitWebContext* browserContext(GtkWidget* browser) {
    auto* state = static_cast<BrowserPtr*>(g_object_get_data(G_OBJECT(browser), "browser-state"));
    return state && !(*state)->closed ? webkit_web_view_get_context((*state)->web) : nullptr;
}
DiscoveryResult browserDiscoveryResult(GtkWidget* browser) {
    auto* state = static_cast<BrowserPtr*>(g_object_get_data(G_OBJECT(browser), "browser-state"));
    return state && !(*state)->closed ? (*state)->result : DiscoveryResult{};
}
}
