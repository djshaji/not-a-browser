#include "ui/ListingView.hpp"
#include "services/HlsDiscovery.hpp"
#include "services/ThumbnailService.hpp"
#ifdef HAVE_LIBRARY
#include "services/BookmarkService.hpp"
#endif
#include <memory>
#include <set>
#include <utility>

extern "C" GResource* stream_gallery_get_resource();
namespace streamgallery {
namespace {
constexpr unsigned maxPages = 5, maxItems = 120;
constexpr const char* world = "stream-gallery-listing";
std::string resource(const char* path) {
    GBytes* bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
    if (!bytes) return {};
    gsize size = 0;
    const char* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
    std::string result(data, size); g_bytes_unref(bytes); return result;
}
std::string property(JSCValue* object, const char* name) {
    if (!object || !jsc_value_is_object(object)) return {};
    JSCValue* value = jsc_value_object_get_property(object, name);
    gchar* text = value && jsc_value_is_string(value) ? jsc_value_to_string(value) : nullptr;
    std::string result = text ? text : "";
    g_free(text); g_clear_object(&value); return result;
}
bool booleanProperty(JSCValue* object, const char* name) {
    if (!object || !jsc_value_is_object(object)) return false;
    JSCValue* value = jsc_value_object_get_property(object, name);
    const bool result = value && jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
    g_clear_object(&value); return result;
}
bool sameOrigin(const std::string& a, const std::string& b) {
    GUri* first = g_uri_parse(a.c_str(), G_URI_FLAGS_ENCODED, nullptr);
    GUri* second = g_uri_parse(b.c_str(), G_URI_FLAGS_ENCODED, nullptr);
    const bool result = first && second && g_strcmp0(g_uri_get_scheme(first), g_uri_get_scheme(second)) == 0 &&
        g_strcmp0(g_uri_get_host(first), g_uri_get_host(second)) == 0 && g_uri_get_port(first) == g_uri_get_port(second);
    if (first) g_uri_unref(first);
    if (second) g_uri_unref(second);
    return result;
}
struct Listing;
using ListingPtr = std::shared_ptr<Listing>;
struct Action { std::weak_ptr<Listing> state; unsigned session; std::size_t index; };
struct Listing {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* address = gtk_entry_new();
    GtkWidget* profile = gtk_combo_box_text_new();
    GtkWidget* extract = gtk_button_new_with_label("Find videos");
    GtkWidget* next = gtk_button_new_with_label("Next page");
    GtkWidget* stop = gtk_button_new_with_label("Stop");
    GtkWidget* flow = gtk_flow_box_new();
    GtkWidget* status = gtk_label_new("Load a listing page to find video cards");
    WebKitWebView* web;
    std::function<void(const std::string&)> openVideo;
    std::function<void()> librarySaved;
    ListingResult result;
    std::set<std::string> seen, visited;
    std::string nextUrl;
    bool buffered = false, more = false, stopped = false, expectedNavigation = false, closed = false;
    bool awaitingFinish = false;
    unsigned generation = 0, session = 0;
    guint timer = 0;
    GCancellable* cancellation = nullptr;
    GCancellable* images = g_cancellable_new();
    explicit Listing(WebKitWebContext* context) : web(WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context))) {}
    ~Listing() { g_clear_object(&cancellation); g_clear_object(&images); }
    ListingPtr shared() { return *static_cast<ListingPtr*>(g_object_get_data(G_OBJECT(root), "listing-state")); }
    void message(const std::string& text) {
        gtk_label_set_text(GTK_LABEL(status), text.c_str()); gtk_widget_set_tooltip_text(status, text.c_str());
    }
    void controls() {
        const char* uri = webkit_web_view_get_uri(web);
        const bool loaded = uri && !resolveDiscoveryUrl(uri, uri).empty();
        result.hasNext = !stopped && result.pages < maxPages && result.items.size() < maxItems &&
            (buffered || more || !nextUrl.empty());
        gtk_widget_set_sensitive(next, !result.busy && result.hasNext);
        gtk_widget_set_sensitive(extract, !result.busy && loaded);
        gtk_widget_set_sensitive(stop, result.busy);
        gtk_button_set_label(GTK_BUTTON(next), gtk_combo_box_get_active(GTK_COMBO_BOX(profile)) == 1 ? "Load more" : "Next page");
    }
    void cancel() {
        const auto cancelledToken = std::to_string(generation);
        ++generation;
        if (timer) { g_source_remove(timer); timer = 0; }
        if (cancellation) g_cancellable_cancel(cancellation);
        g_clear_object(&cancellation);
        const auto script = "document.dispatchEvent(new CustomEvent('stream-gallery-listing-cancel',{detail:'" + cancelledToken + "'}));";
        webkit_web_view_evaluate_javascript(web, script.c_str(), script.size(),
            world, nullptr, nullptr, nullptr, nullptr);
        result.busy = false;
    }
    void reset(bool stopNavigation = true) {
        stopped = true;
        awaitingFinish = false;
        cancel(); if (stopNavigation) webkit_web_view_stop_loading(web);
        ++session;
        g_cancellable_cancel(images); g_clear_object(&images); images = g_cancellable_new();
        GList* children = gtk_container_get_children(GTK_CONTAINER(flow));
        for (auto* child = children; child; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
        g_list_free(children);
        result = {}; seen.clear(); visited.clear(); nextUrl.clear(); more = false; buffered = false;
        stopped = false;
        controls();
    }
    void stopWork() {
        awaitingFinish = false;
        stopped = true; cancel(); webkit_web_view_stop_loading(web); controls();
        message("Listing stopped — collected videos are still available");
    }
    void thumbnail(GtkWidget* image, const std::string& url) {
        if (url.empty()) return;
        struct ImageRequest { ListingPtr state; unsigned session; GtkWidget* image; std::string url; };
        auto* task = g_task_new(nullptr, images, +[](GObject*, GAsyncResult* async, gpointer) {
            auto* task = G_TASK(async);
            auto* request = static_cast<ImageRequest*>(g_task_get_task_data(task));
            GError* error = nullptr;
            auto* image = static_cast<GdkPixbuf*>(g_task_propagate_pointer(task, &error));
            if (image && !request->state->closed && request->session == request->state->session)
                gtk_image_set_from_pixbuf(GTK_IMAGE(request->image), image);
            g_clear_object(&image); g_clear_error(&error);
        }, nullptr);
        g_task_set_task_data(task, new ImageRequest{shared(), session, image, url}, +[](gpointer data) { delete static_cast<ImageRequest*>(data); });
        g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancellation) {
            auto* request = static_cast<ImageRequest*>(data);
            g_task_return_pointer(task, loadThumbnail(request->url, cancellation), +[](gpointer image) { if (image) g_object_unref(image); });
        });
        g_object_unref(task);
    }
    void addCard(const ListingVideo& video) {
        const auto index = result.items.size();
        result.items.push_back(video);
        auto* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        for (auto set : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) set(card, 8);
        auto* image = gtk_image_new_from_icon_name("video-x-generic", GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_size_request(image, 192, 108);
        gtk_box_pack_start(GTK_BOX(card), image, FALSE, FALSE, 0);
        auto* title = gtk_label_new(video.title.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END); gtk_label_set_max_width_chars(GTK_LABEL(title), 24);
        gtk_widget_set_tooltip_text(card, (video.title + "\n" + video.url).c_str());
        gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 0);
        auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        auto* play = gtk_button_new_with_label("Play");
        gtk_widget_set_name(play, ("listing-play-" + std::to_string(index)).c_str());
        gtk_box_pack_start(GTK_BOX(actions), play, TRUE, TRUE, 0);
        g_signal_connect_data(play, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* action = static_cast<Action*>(data); auto state = action->state.lock();
            if (!state || state->closed || action->session != state->session || action->index >= state->result.items.size()) return;
            const auto url = state->result.items[action->index].url;
            state->stopWork();
            if (state->openVideo) state->openVideo(url);
        }), new Action{shared(), session, index}, +[](gpointer data, GClosure*) { delete static_cast<Action*>(data); }, G_CONNECT_DEFAULT);
#ifdef HAVE_LIBRARY
        auto* save = gtk_button_new_with_label("Save");
        gtk_widget_set_name(save, ("listing-save-" + std::to_string(index)).c_str());
        gtk_box_pack_start(GTK_BOX(actions), save, TRUE, TRUE, 0);
        g_signal_connect_data(save, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
            auto* action = static_cast<Action*>(data); auto state = action->state.lock();
            if (!state || state->closed || action->session != state->session || action->index >= state->result.items.size() ||
                !gtk_widget_get_sensitive(GTK_WIDGET(button))) return;
            const auto item = state->result.items[action->index]; const auto token = state->session;
            gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE); gtk_button_set_label(button, "Saving…");
            saveBookmarkAsync(item.url, item.title, item.posterUrl, [state, token, button](bool created, const std::string& error) {
                if (state->closed) return;
                if (token == state->session) {
                    gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
                    gtk_button_set_label(button, error.empty() ? (created ? "Saved" : "Already saved") : "Retry save");
                    if (!error.empty()) state->message("Could not save: " + error);
                }
                if (error.empty() && state->librarySaved) state->librarySaved();
            });
        }), new Action{shared(), session, index}, +[](gpointer data, GClosure*) { delete static_cast<Action*>(data); }, G_CONNECT_DEFAULT);
#endif
        gtk_box_pack_start(GTK_BOX(card), actions, FALSE, FALSE, 0);
        gtk_flow_box_insert(GTK_FLOW_BOX(flow), card, -1); gtk_widget_show_all(card);
        // GtkFlowBox wraps the inserted card in an initially hidden child.
        gtk_widget_show(gtk_widget_get_parent(card));
        thumbnail(image, video.posterUrl);
    }
    void collect(bool loadMore) {
        if (closed || result.busy) return;
        cancel(); stopped = false;
        result.busy = true; controls(); message(loadMore ? "Waiting for more video cards…" : "Finding video cards…");
        cancellation = g_cancellable_new();
        const bool button = gtk_combo_box_get_active(GTK_COMBO_BOX(profile)) == 1;
        const auto json = resource(button ? "/org/streamgallery/app/adapters/listing-load-more.json" : "/org/streamgallery/app/adapters/listing-links.json");
        const auto script = "const adapter = " + json + ";\n" + resource("/org/streamgallery/app/scripts/extract-listing.js");
        struct Evaluation { ListingPtr state; unsigned generation; };
        auto* request = new Evaluation{shared(), generation};
        GVariantDict arguments; g_variant_dict_init(&arguments, nullptr);
        g_variant_dict_insert(&arguments, "operation", "s", loadMore ? "more" : "extract");
        g_variant_dict_insert(&arguments, "requestToken", "s", std::to_string(generation).c_str());
        GVariantBuilder known; g_variant_builder_init(&known, G_VARIANT_TYPE_VARDICT);
        for (const auto& url : seen) g_variant_builder_add(&known, "{sv}", url.c_str(), g_variant_new_string("1"));
        g_variant_dict_insert_value(&arguments, "knownUrls", g_variant_builder_end(&known));
        GVariant* values = g_variant_ref_sink(g_variant_dict_end(&arguments));
        webkit_web_view_call_async_javascript_function(web, script.c_str(), script.size(), values, world, nullptr, cancellation,
            +[](GObject* source, GAsyncResult* async, gpointer data) {
                std::unique_ptr<Evaluation> request(static_cast<Evaluation*>(data)); auto state = request->state;
                GError* error = nullptr;
                JSCValue* value = webkit_web_view_call_async_javascript_function_finish(WEBKIT_WEB_VIEW(source), async, &error);
                if (state->closed || request->generation != state->generation) { g_clear_object(&value); g_clear_error(&error); return; }
                state->result.busy = false;
                if (error) { state->message(error->message); state->stopped = true; g_clear_error(&error); }
                else if (value && jsc_value_is_object(value)) {
                    const auto page = property(value, "pageUrl");
                    JSCValue* items = jsc_value_object_get_property(value, "items");
                    unsigned added = 0;
                    if (items && jsc_value_is_array(items)) {
                        JSCValue* length = jsc_value_object_get_property(items, "length");
                        const int count = length ? jsc_value_to_int32(length) : 0; g_clear_object(&length);
                        for (int i = 0; i < count && i < 24 && state->result.items.size() < maxItems; ++i) {
                            JSCValue* item = jsc_value_object_get_property_at_index(items, i);
                            const auto raw = property(item, "url"); const auto url = resolveDiscoveryUrl(raw, page);
                            if (!url.empty() && state->seen.insert(url).second) {
                                const auto title = property(item, "title"); const auto poster = property(item, "posterUrl");
                                state->addCard({url, title.empty() ? url : title, resolveDiscoveryUrl(poster, page)}); ++added;
                            }
                            g_clear_object(&item);
                        }
                    }
                    g_clear_object(&items);
                    const auto next = property(value, "nextUrl");
                    state->nextUrl = !next.empty() && sameOrigin(next, page) && !state->visited.count(next) ? resolveDiscoveryUrl(next, page) : "";
                    state->more = booleanProperty(value, "more"); state->buffered = booleanProperty(value, "buffered");
                    if (!added && state->nextUrl.empty()) state->more = false;
                    std::string text = std::to_string(state->result.items.size()) + " videos from " + std::to_string(state->result.pages) + " page(s)";
                    if (state->result.pages >= maxPages || state->result.items.size() >= maxItems) text += " — listing limit reached";
                    else if (!state->more && state->nextUrl.empty() && !state->buffered) text += " — no more results";
                    state->message(text);
                }
                g_clear_object(&value); state->controls();
            }, request);
        g_variant_unref(values);
    }
    void open(const std::string& url, bool append = false) {
        const auto valid = resolveDiscoveryUrl(url, url);
        if (valid.empty()) { message("Enter an HTTP/HTTPS listing URL without credentials"); return; }
        if (!append) reset(); else cancel();
        stopped = false; expectedNavigation = true; awaitingFinish = false; result.busy = true; controls();
        gtk_entry_set_text(GTK_ENTRY(address), valid.c_str()); webkit_web_view_load_uri(web, valid.c_str());
    }
};
}

GtkWidget* createListingView(WebKitWebContext* context, std::function<void(const std::string&)> openVideo,
                            std::function<void()> librarySaved) {
    (void)stream_gallery_get_resource();
    auto state = std::make_shared<Listing>(context);
    state->openVideo = std::move(openVideo); state->librarySaved = std::move(librarySaved);
    g_object_set_data_full(G_OBJECT(state->root), "listing-state", new ListingPtr(state), +[](gpointer data) { delete static_cast<ListingPtr*>(data); });
    auto* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->profile), "Next-page links");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->profile), "Load-more button");
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->profile), 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->address), "Listing page URL"); gtk_entry_set_max_length(GTK_ENTRY(state->address), 8192);
    auto* load = gtk_button_new_with_label("Load listing");
    gtk_box_pack_start(GTK_BOX(toolbar), state->address, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), load, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), state->stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), toolbar, FALSE, FALSE, 0);
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (auto* widget : {state->profile, state->extract, state->next}) gtk_box_pack_start(GTK_BOX(actions), widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(state->root), actions, FALSE, FALSE, 0);
    auto* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(state->flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->flow), 3);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(state->flow), TRUE);
    gtk_widget_set_valign(state->flow, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(scroll), state->flow);
    gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), GTK_WIDGET(state->web), TRUE, FALSE); gtk_paned_set_position(GTK_PANED(paned), 320);
    gtk_box_pack_start(GTK_BOX(state->root), paned, TRUE, TRUE, 0);
    gtk_label_set_ellipsize(GTK_LABEL(state->status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(state->root), state->status, FALSE, FALSE, 0);
    webkit_web_view_set_is_muted(state->web, TRUE);
    g_signal_connect(load, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Listing*>(data); state->open(gtk_entry_get_text(GTK_ENTRY(state->address)));
    }), state.get());
    g_signal_connect(state->address, "activate", G_CALLBACK(+[](GtkEntry* entry, gpointer data) {
        static_cast<Listing*>(data)->open(gtk_entry_get_text(entry));
    }), state.get());
    g_signal_connect(state->profile, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer data) {
        auto* state = static_cast<Listing*>(data); state->reset(); state->message("Profile changed — load a listing or choose Find videos");
    }), state.get());
    g_signal_connect(state->stop, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Listing*>(data)->stopWork(); }), state.get());
    g_signal_connect(state->extract, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Listing*>(data);
        if (!state->result.pages) state->result.pages = 1;
        state->collect(false);
    }), state.get());
    g_signal_connect(state->next, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Listing*>(data);
        state->controls(); if (state->result.busy || !state->result.hasNext) return;
        if (state->buffered || state->more) { ++state->result.pages; state->collect(true); }
        else state->open(state->nextUrl, true);
    }), state.get());
    g_signal_connect(state->web, "load-changed", G_CALLBACK(+[](WebKitWebView* web, WebKitLoadEvent event, gpointer data) {
        auto* state = static_cast<Listing*>(data); if (state->closed) return;
        if (event == WEBKIT_LOAD_STARTED) {
            if (!state->expectedNavigation) state->reset(false); else state->cancel();
            state->awaitingFinish = true;
            state->expectedNavigation = false; state->stopped = false; state->result.busy = true; state->controls(); state->message("Loading listing…");
            state->timer = g_timeout_add_seconds(30, +[](gpointer data) -> gboolean {
                auto* state = static_cast<Listing*>(data); state->timer = 0; state->stopWork(); state->message("Listing page loading timed out"); return G_SOURCE_REMOVE;
            }, state);
        } else if (event == WEBKIT_LOAD_FINISHED && !state->stopped && !state->expectedNavigation && state->awaitingFinish) {
            state->awaitingFinish = false;
            state->cancel();
            const char* uri = webkit_web_view_get_uri(web); const auto url = uri ? resolveDiscoveryUrl(uri, uri) : "";
            if (url.empty() || state->visited.count(url) || state->result.pages >= maxPages) {
                state->stopped = true; state->controls(); state->message("Listing pagination stopped at a repeated or unsupported page"); return;
            }
            state->visited.insert(url); ++state->result.pages;
            gtk_entry_set_text(GTK_ENTRY(state->address), url.c_str()); state->collect(false);
        }
    }), state.get());
    g_signal_connect(state->web, "load-failed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent, const char*, GError* error, gpointer data) -> gboolean {
        auto* state = static_cast<Listing*>(data);
        if (!state->closed && !state->stopped && !g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)) {
            state->stopped = true; state->cancel(); state->controls(); state->message(error->message);
        }
        return TRUE;
    }), state.get());
    g_signal_connect(state->web, "web-process-terminated", G_CALLBACK(+[](WebKitWebView*, WebKitWebProcessTerminationReason, gpointer data) {
        auto* state = static_cast<Listing*>(data); if (state->closed) return;
        state->stopWork(); state->message("Listing browser stopped — load the page again");
    }), state.get());
    g_signal_connect(state->root, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* state = static_cast<Listing*>(data); state->closed = true; state->cancel();
        g_cancellable_cancel(state->images); g_signal_handlers_disconnect_by_data(state->web, state); webkit_web_view_stop_loading(state->web);
    }), state.get());
    gtk_widget_set_name(state->root, "listing-browser"); gtk_widget_set_name(state->profile, "listing-profile");
    gtk_widget_set_name(GTK_WIDGET(state->web), "listing-web");
    gtk_widget_set_name(state->flow, "listing-videos"); gtk_widget_set_name(state->next, "listing-next");
    gtk_widget_set_name(state->stop, "listing-stop"); gtk_widget_set_name(state->status, "listing-status");
    gtk_widget_set_name(state->extract, "listing-extract"); gtk_widget_set_name(state->address, "listing-address");
    state->controls(); return state->root;
}
void openListingPage(GtkWidget* listing, const std::string& url) {
    auto* state = static_cast<ListingPtr*>(g_object_get_data(G_OBJECT(listing), "listing-state"));
    if (state && !(*state)->closed) (*state)->open(url);
}
ListingResult listingResult(GtkWidget* listing) {
    auto* state = static_cast<ListingPtr*>(g_object_get_data(G_OBJECT(listing), "listing-state"));
    return state && !(*state)->closed ? (*state)->result : ListingResult{};
}
}
