#include "ui/GalleryView.hpp"
#include "database/LibraryRepository.hpp"
#include "services/ThumbnailService.hpp"
#include <filesystem>
#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace streamgallery {
namespace {
constexpr int pageSize = 24;
struct Gallery {
    GtkWidget *root, *search, *sort, *mode, *flow, *status, *selection, *previous, *next, *refresh;
    GtkWidget* discover = nullptr;
    std::function<void(const std::string&)> openPage;
    GCancellable* cancellable = nullptr;
    guint debounce = 0;
    unsigned generation = 0;
    int offset = 0;
    bool closed = false;
    VideoPage page;
    ~Gallery() { g_clear_object(&cancellable); }
};
using GalleryPtr = std::shared_ptr<Gallery>;
struct Query {
    GalleryPtr gallery;
    unsigned generation;
    std::string path, search;
    VideoSort sort;
    int offset;
};
struct Thumbnail {
    GalleryPtr gallery;
    unsigned generation;
    GtkWidget* image; // Only accessed on the main thread after checking generation.
    std::string uri;
};
GalleryPtr shared(Gallery* state) {
    return *static_cast<GalleryPtr*>(g_object_get_data(G_OBJECT(state->root),"gallery-state"));
}
void loadPage(const GalleryPtr& state);
void thumbnailWorker(GTask* task, gpointer, gpointer data, GCancellable* cancellable) {
    auto* request = static_cast<Thumbnail*>(data);
    auto* image = loadThumbnail(request->uri,cancellable);
    g_task_return_pointer(task,image,[](gpointer object) { if (object) g_object_unref(object); });
}
void thumbnailReady(GObject*, GAsyncResult* result, gpointer) {
    auto* task = G_TASK(result);
    auto* request = static_cast<Thumbnail*>(g_task_get_task_data(task));
    GError* error = nullptr;
    auto* image = static_cast<GdkPixbuf*>(g_task_propagate_pointer(task,&error));
    if (image && !request->gallery->closed && request->generation == request->gallery->generation)
        gtk_image_set_from_pixbuf(GTK_IMAGE(request->image),image);
    if (image) g_object_unref(image);
    g_clear_error(&error);
}
void render(const GalleryPtr& state) {
    // Invalidate image callbacks before destroying their target widgets.
    ++state->generation;
    GList* children = gtk_container_get_children(GTK_CONTAINER(state->flow));
    for (auto* child = children; child; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
    g_list_free(children);
    if (state->discover) gtk_widget_set_sensitive(state->discover, FALSE);
    gtk_label_set_text(GTK_LABEL(state->selection),"Select a bookmark to see its page URL.");
    const bool list = gtk_combo_box_get_active(GTK_COMBO_BOX(state->mode)) == 1;
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->flow),list ? 1 : 4);
    for (const auto& video : state->page.items) {
        GtkWidget* card = gtk_box_new(list ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL,8);
        gtk_widget_set_margin_start(card,8); gtk_widget_set_margin_end(card,8);
        gtk_widget_set_margin_top(card,8); gtk_widget_set_margin_bottom(card,8);
        auto* image = gtk_image_new_from_icon_name("video-x-generic-symbolic",GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_size_request(image,list ? 96 : 192,108);
        gtk_box_pack_start(GTK_BOX(card),image,FALSE,FALSE,0);
        auto* label = gtk_label_new(video.title.empty() ? video.canonicalUrl.c_str() : video.title.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label),list ? 60 : 24);
        gtk_label_set_xalign(GTK_LABEL(label),0);
        gtk_box_pack_start(GTK_BOX(card),label,TRUE,TRUE,0);
        gtk_widget_set_tooltip_text(card,video.canonicalUrl.c_str());
        gtk_flow_box_insert(GTK_FLOW_BOX(state->flow),card,-1);
        if (!video.thumbnailUrl.empty()) {
            auto* task = g_task_new(nullptr,state->cancellable,thumbnailReady,nullptr);
            g_task_set_task_data(task,new Thumbnail{state,state->generation,image,video.thumbnailUrl},
                [](gpointer data) { delete static_cast<Thumbnail*>(data); });
            g_task_run_in_thread(task,thumbnailWorker);
            g_object_unref(task);
        }
    }
    gtk_widget_show_all(state->flow);
    std::string message;
    if (state->page.total == 0) message = "No videos found. Add bookmarks with stream-gallery-library, then Refresh.";
    else message = std::to_string(state->offset + 1) + "–" + std::to_string(state->offset + state->page.items.size()) +
        " of " + std::to_string(state->page.total) + " videos";
    gtk_label_set_text(GTK_LABEL(state->status),message.c_str());
    gtk_widget_set_sensitive(state->previous,state->offset > 0);
    gtk_widget_set_sensitive(state->next,state->offset + pageSize < state->page.total);
}
void queryWorker(GTask* task, gpointer, gpointer data, GCancellable* cancellable) {
    auto* query = static_cast<Query*>(data);
    try {
        if (g_task_return_error_if_cancelled(task)) return;
        std::filesystem::create_directories(std::filesystem::path(query->path).parent_path());
        Database database(query->path);
        LibraryRepository library(database);
        auto result = std::make_unique<VideoPage>(library.page(query->search,query->sort,query->offset,pageSize));
        if (g_cancellable_is_cancelled(cancellable)) { g_task_return_error_if_cancelled(task); return; }
        g_task_return_pointer(task,result.release(),[](gpointer data) { delete static_cast<VideoPage*>(data); });
    } catch (const std::exception& error) {
        g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_FAILED,"%s",error.what());
    }
}
void queryReady(GObject*, GAsyncResult* result, gpointer) {
    auto* task = G_TASK(result);
    auto* query = static_cast<Query*>(g_task_get_task_data(task));
    auto state = query->gallery;
    GError* error = nullptr;
    std::unique_ptr<VideoPage> page(static_cast<VideoPage*>(g_task_propagate_pointer(task,&error)));
    if (state->closed || query->generation != state->generation) { g_clear_error(&error); return; }
    if (error) {
        gtk_label_set_text(GTK_LABEL(state->status),error->message);
        g_error_free(error);
        return;
    }
    if (page->total && state->offset >= page->total) { state->offset = 0; loadPage(state); return; }
    state->page = std::move(*page);
    render(state);
}
void loadPage(const GalleryPtr& state) {
    if (state->closed) return;
    if (state->cancellable) g_cancellable_cancel(state->cancellable);
    g_clear_object(&state->cancellable);
    state->cancellable = g_cancellable_new();
    ++state->generation;
    gtk_widget_set_sensitive(state->previous,FALSE); gtk_widget_set_sensitive(state->next,FALSE);
    gtk_label_set_text(GTK_LABEL(state->status),"Loading library…");
    auto path = std::filesystem::path(g_get_user_data_dir()) / "stream-gallery" / "library.db";
    auto* query = new Query{state,state->generation,path.string(),
        gtk_entry_get_text(GTK_ENTRY(state->search)),
        static_cast<VideoSort>(gtk_combo_box_get_active(GTK_COMBO_BOX(state->sort))),state->offset};
    auto* task = g_task_new(nullptr,state->cancellable,queryReady,nullptr);
    g_task_set_task_data(task,query,[](gpointer data) { delete static_cast<Query*>(data); });
    g_task_run_in_thread(task,queryWorker);
    g_object_unref(task);
}
void searchChanged(GtkSearchEntry*, gpointer data) {
    auto* state = static_cast<Gallery*>(data);
    if (state->debounce) g_source_remove(state->debounce);
    // Superseded results cannot overwrite the pending search.
    ++state->generation;
    if (state->cancellable) g_cancellable_cancel(state->cancellable);
    state->debounce = g_timeout_add(250,[](gpointer data) -> gboolean {
        auto* state = static_cast<Gallery*>(data);
        state->debounce = 0; state->offset = 0;
        loadPage(shared(state));
        return G_SOURCE_REMOVE;
    },state);
}
void selectionChanged(GtkFlowBox* flow, gpointer data) {
    auto* state = static_cast<Gallery*>(data);
    GList* selected = gtk_flow_box_get_selected_children(flow);
    if (selected) {
        const int index = gtk_flow_box_child_get_index(GTK_FLOW_BOX_CHILD(selected->data));
        if (index >= 0 && static_cast<std::size_t>(index) < state->page.items.size()) {
            const auto& video = state->page.items[index];
            gtk_label_set_text(GTK_LABEL(state->selection),video.canonicalUrl.c_str());
            if (state->discover) gtk_widget_set_sensitive(state->discover, TRUE);
        }
    }
    g_list_free(selected);
}
}
GtkWidget* createGalleryView(std::function<void(const std::string&)> openPage) {
    auto state = std::make_shared<Gallery>();
    state->openPage = std::move(openPage);
    state->root = gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    g_object_set_data_full(G_OBJECT(state->root),"gallery-state",new GalleryPtr(state),
        [](gpointer data) { delete static_cast<GalleryPtr*>(data); });
    auto* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    state->search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->search),"Search titles and descriptions");
    state->sort = gtk_combo_box_text_new();
    for (const char* label : {"Newest first", "Oldest first", "Title A–Z"})
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->sort),label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->sort),0);
    state->mode = gtk_combo_box_text_new();
    for (const char* label : {"Grid", "List"}) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->mode),label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->mode),0);
    state->refresh = gtk_button_new_with_label("Refresh");
    gtk_box_pack_start(GTK_BOX(controls),state->search,TRUE,TRUE,0);
    for (auto* widget : {state->sort,state->mode,state->refresh}) gtk_box_pack_start(GTK_BOX(controls),widget,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(state->root),controls,FALSE,FALSE,0);
    auto* scroll = gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    state->flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(state->flow),GTK_SELECTION_SINGLE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(state->flow),TRUE);
    gtk_widget_set_valign(state->flow,GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(scroll),state->flow);
    gtk_box_pack_start(GTK_BOX(state->root),scroll,TRUE,TRUE,0);
    state->status = gtk_label_new("Loading library…");
    gtk_label_set_line_wrap(GTK_LABEL(state->status),TRUE);
    gtk_box_pack_start(GTK_BOX(state->root),state->status,FALSE,FALSE,0);
    state->selection = gtk_label_new("Select a bookmark to see its page URL.");
    gtk_label_set_selectable(GTK_LABEL(state->selection),TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(state->selection),PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(state->root),state->selection,FALSE,FALSE,0);
    if (state->openPage) {
        state->discover = gtk_button_new_with_label("Discover & Play selected");
        gtk_widget_set_name(state->discover, "library-discover");
        gtk_widget_set_sensitive(state->discover, FALSE);
        gtk_box_pack_start(GTK_BOX(state->root), state->discover, FALSE, FALSE, 0);
        g_signal_connect(state->discover, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* state = static_cast<Gallery*>(data);
            GList* selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(state->flow));
            if (selected) {
                int index = gtk_flow_box_child_get_index(GTK_FLOW_BOX_CHILD(selected->data));
                if (index >= 0 && static_cast<std::size_t>(index) < state->page.items.size())
                    state->openPage(state->page.items[index].canonicalUrl);
            }
            g_list_free(selected);
        }), state.get());
    }
    auto* paging = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    state->previous = gtk_button_new_with_label("Previous");
    state->next = gtk_button_new_with_label("Next");
    gtk_box_pack_start(GTK_BOX(paging),state->previous,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(paging),state->next,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(state->root),paging,FALSE,FALSE,0);
    g_signal_connect(state->search,"search-changed",G_CALLBACK(searchChanged),state.get());
    g_signal_connect(state->flow,"selected-children-changed",G_CALLBACK(selectionChanged),state.get());
    g_signal_connect(state->sort,"changed",G_CALLBACK(+[](GtkComboBox*, gpointer data) {
        auto* state = static_cast<Gallery*>(data); state->offset = 0; loadPage(shared(state));
    }),state.get());
    g_signal_connect(state->mode,"changed",G_CALLBACK(+[](GtkComboBox*, gpointer data) {
        loadPage(shared(static_cast<Gallery*>(data)));
    }),state.get());
    g_signal_connect(state->refresh,"clicked",G_CALLBACK(+[](GtkButton*, gpointer data) {
        loadPage(shared(static_cast<Gallery*>(data)));
    }),state.get());
    g_signal_connect(state->previous,"clicked",G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Gallery*>(data); state->offset = std::max(0,state->offset-pageSize); loadPage(shared(state));
    }),state.get());
    g_signal_connect(state->next,"clicked",G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* state = static_cast<Gallery*>(data); state->offset += pageSize; loadPage(shared(state));
    }),state.get());
    g_signal_connect(state->root,"destroy",G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* state = static_cast<Gallery*>(data); state->closed = true;
        if (state->debounce) { g_source_remove(state->debounce); state->debounce = 0; }
        if (state->cancellable) g_cancellable_cancel(state->cancellable);
    }),state.get());
    gtk_widget_set_name(state->root,"library-gallery");
    gtk_widget_set_name(state->search,"library-search");
    gtk_widget_set_name(state->sort,"library-sort");
    gtk_widget_set_name(state->mode,"library-mode");
    gtk_widget_set_name(state->flow,"library-videos");
    gtk_widget_set_name(state->status,"library-status");
    gtk_widget_set_name(state->previous,"library-previous");
    gtk_widget_set_name(state->next,"library-next");
    for (auto* widget : {state->search,state->sort,state->mode}) {
        const char* name = widget == state->search ? "Search library" : widget == state->sort ? "Sort videos" : "Gallery view mode";
        atk_object_set_name(gtk_widget_get_accessible(widget),name);
    }
    loadPage(state);
    return state->root;
}
}
