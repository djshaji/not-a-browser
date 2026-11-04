#include "ui/FilesView.hpp"
#include <utility>

namespace streamgallery {
namespace {
struct FilesView {
    GtkWidget* chooser;
    GtkWidget* status;
    std::function<void(const std::string&)> openFile;
    std::string folder;
    std::string selection;

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

GtkWidget* createFilesView(std::function<void(const std::string&)> openFile) {
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
    gtk_label_set_xalign(GTK_LABEL(status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
    auto* play = gtk_button_new_with_label("Play selected video");
    gtk_widget_set_name(play, "files-play");
    gtk_style_context_add_class(gtk_widget_get_style_context(play), "suggested-action");
    gtk_widget_set_sensitive(play, static_cast<bool>(openFile));
    gtk_box_pack_start(GTK_BOX(actions), status, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(actions), play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), actions, FALSE, FALSE, 0);
    auto* view = new FilesView{chooser, status, std::move(openFile), {}, {}};
    // Snapshot before GTK's chooser unmap handler clears its browse state.
    // The parent is hidden before the chooser is unmapped.
    g_signal_connect(root, "unmap", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<FilesView*>(data)->remember();
    }), view);
    g_signal_connect_after(chooser, "map", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<FilesView*>(data)->restore();
    }), view);
    g_object_set_data_full(G_OBJECT(root), "files-controller", view, [](gpointer data) { delete static_cast<FilesView*>(data); });
    g_signal_connect(chooser, "file-activated", G_CALLBACK(+[](GtkFileChooser*, gpointer data) {
        static_cast<FilesView*>(data)->open();
    }), view);
    g_signal_connect(play, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<FilesView*>(data)->open();
    }), view);
    return root;
}
}
