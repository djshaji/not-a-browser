#include "ui/FilesView.hpp"
#include <utility>
#include <memory>
#ifdef HAVE_LIBRARY
#include "services/BookmarkService.hpp"
#endif

namespace streamgallery {
namespace {
struct FilesView : std::enable_shared_from_this<FilesView> {
    GtkWidget* chooser;
    GtkWidget* status;
    std::function<void(const std::string&)> openFile;
    std::string folder;
    std::string selection;
    std::function<void()> libraryChanged;
    GtkWidget* favoriteButton = nullptr;
    unsigned generation = 0;
    bool closed = false, saving = false;
#ifdef HAVE_LIBRARY
    void favorite() {
        if (saving) return;
        auto* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (!filename) { gtk_label_set_text(GTK_LABEL(status),"Select a local file to favorite."); return; }
        const auto selectedGeneration = generation;
        saving = true;
        gtk_widget_set_sensitive(favoriteButton,FALSE);
        gtk_label_set_text(GTK_LABEL(status),"Saving favorite…");
        favoriteLocalFileAsync(filename,[self = shared_from_this(),selectedGeneration](bool, const std::string& error) {
            self->saving = false;
            if (error.empty() && self->libraryChanged) self->libraryChanged();
            if (self->closed) return;
            gtk_widget_set_sensitive(self->favoriteButton,TRUE);
            if (selectedGeneration == self->generation)
                gtk_label_set_text(GTK_LABEL(self->status),error.empty() ? "Saved to Library. Filter Tag by Favorites to find it." : error.c_str());
        });
        g_free(filename);
    }
#endif

    void remember() {
        gchar* currentFolder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(chooser));
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (currentFolder) folder = currentFolder;
        selection = filename ? filename : "";
        g_free(currentFolder);
        g_free(filename);
    }

    void restore() {
        if (!selection.empty())
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), selection.c_str());
        else if (!folder.empty())
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), folder.c_str());
    }

    void open() {
        gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (!filename) {
            gtk_label_set_text(GTK_LABEL(status), "Select a local video to play.");
            return;
        }
        // GTK navigates activated directories itself; only files go to the player.
        if (g_file_test(filename, G_FILE_TEST_IS_REGULAR) && openFile) {
            gtk_label_set_text(GTK_LABEL(status), "");
            openFile(filename);
        }
        g_free(filename);
    }
};
}

GtkWidget* createFilesView(std::function<void(const std::string&)> openFile, std::function<void()> libraryChanged) {
    auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_name(root, "files-view");
    auto* chooser = gtk_file_chooser_widget_new(GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_widget_set_name(chooser, "files-chooser");
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), FALSE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), g_get_home_dir());
    auto* videos = gtk_file_filter_new();
    gtk_file_filter_set_name(videos, "Videos");
    gtk_file_filter_add_mime_type(videos, "video/*");
    for (const char* pattern : {"*.mkv", "*.mp4", "*.webm", "*.avi", "*.mov", "*.m4v", "*.ts", "*.m3u8"})
        gtk_file_filter_add_pattern(videos, pattern);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), videos);
    auto* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);
    gtk_box_pack_start(GTK_BOX(root), chooser, TRUE, TRUE, 0);
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(actions, 16);
    gtk_widget_set_margin_end(actions, 16);
    gtk_widget_set_margin_bottom(actions, 12);
    auto* status = gtk_label_new(openFile ? "Select a video, then click Play or double-click the file." : "Playback is unavailable in this build.");
    gtk_widget_set_name(status,"files-status");
    gtk_label_set_xalign(GTK_LABEL(status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
    auto* play = gtk_button_new_with_label("Play selected video");
    gtk_widget_set_name(play, "files-play");
    gtk_style_context_add_class(gtk_widget_get_style_context(play), "suggested-action");
    gtk_widget_set_sensitive(play, static_cast<bool>(openFile));
    gtk_box_pack_start(GTK_BOX(actions), status, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(actions), play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), actions, FALSE, FALSE, 0);
    auto state = std::make_shared<FilesView>();
    auto* view = state.get();
    view->chooser = chooser; view->status = status;
    view->openFile = std::move(openFile); view->libraryChanged = std::move(libraryChanged);
#ifdef HAVE_LIBRARY
    view->favoriteButton = gtk_button_new_with_label("Add to favorites");
    gtk_widget_set_name(view->favoriteButton,"files-favorite");
    gtk_box_pack_end(GTK_BOX(actions),view->favoriteButton,FALSE,FALSE,0);
    g_signal_connect(view->favoriteButton,"clicked",G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<FilesView*>(data)->favorite();
    }),view);
    g_signal_connect(chooser,"selection-changed",G_CALLBACK(+[](GtkFileChooser*, gpointer data) {
        auto* view = static_cast<FilesView*>(data);
        ++view->generation;
        gtk_label_set_text(GTK_LABEL(view->status),"");
    }),view);
#endif
    g_signal_connect(root,"destroy",G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<FilesView*>(data)->closed = true;
    }),view);
    // Snapshot before GTK's chooser unmap handler clears its browse state.
    // The parent is hidden before the chooser is unmapped.
    g_signal_connect(root, "unmap", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<FilesView*>(data)->remember();
    }), view);
    g_signal_connect_after(chooser, "map", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<FilesView*>(data)->restore();
    }), view);
    g_object_set_data_full(G_OBJECT(root), "files-controller", new std::shared_ptr<FilesView>(state),
        [](gpointer data) { delete static_cast<std::shared_ptr<FilesView>*>(data); });
    g_signal_connect(chooser, "file-activated", G_CALLBACK(+[](GtkFileChooser*, gpointer data) {
        static_cast<FilesView*>(data)->open();
    }), view);
    g_signal_connect(play, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<FilesView*>(data)->open();
    }), view);
    return root;
}
}
