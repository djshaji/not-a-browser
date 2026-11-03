#include "ui/MainWindow.hpp"
#include <initializer_list>
#ifdef HAVE_LIBRARY_TESTS
#include "database/LibraryRepository.hpp"
#include <filesystem>
#include <functional>
#include <string>
#endif

namespace {
GtkWidget* findWidget(GtkWidget* root, GType type) {
    if (G_TYPE_CHECK_INSTANCE_TYPE(root, type)) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* found = nullptr;
    for (GList* child = children; child && !found; child = child->next) {
        found = findWidget(GTK_WIDGET(child->data), type);
    }
    g_list_free(children);
    return found;
}
#if defined(HAVE_LIBRARY_TESTS) || defined(HAVE_PLAYER_TESTS)
GtkWidget* findNamed(GtkWidget* root, const char* name) {
    if (g_strcmp0(gtk_widget_get_name(root),name)==0) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* found = nullptr;
    for (auto* child = children; child && !found; child = child->next)
        found = findNamed(GTK_WIDGET(child->data),name);
    g_list_free(children);
    return found;
}
#endif
#ifdef HAVE_LIBRARY_TESTS
void waitFor(const std::function<bool()>& predicate) {
    const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (!predicate() && g_get_monotonic_time() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(1000);
    }
    g_assert_true(predicate());
}
#endif
}
int main(int argc, char** argv) {
    // Keep direct test runs independent of third-party desktop theme warnings.
    g_setenv("GTK_THEME", "Adwaita", TRUE);
#ifdef HAVE_LIBRARY_TESTS
    gchar* testDirectory = g_dir_make_tmp("stream-gallery-ui-XXXXXX",nullptr);
    g_assert_nonnull(testDirectory);
    g_setenv("XDG_DATA_HOME",testDirectory,TRUE);
    g_setenv("XDG_CACHE_HOME",testDirectory,TRUE);
#endif
    if (!gtk_init_check(&argc, &argv)) {
#ifdef HAVE_LIBRARY_TESTS
        std::filesystem::remove_all(testDirectory);
        g_free(testDirectory);
#endif
        return 77;
    }
#ifdef HAVE_LIBRARY_TESTS
    const auto libraryDirectory = std::filesystem::path(testDirectory) / "stream-gallery";
    std::filesystem::create_directories(libraryDirectory);
    {
        streamgallery::Database db((libraryDirectory / "library.db").string());
        streamgallery::LibraryRepository library(db);
        for (int index = 0; index < 30; ++index)
            library.bookmark("https://example.com/" + std::to_string(index),
                index == 0 ? "Unique concert" : "Video " + std::to_string(index));
    }
#endif
    auto* app = gtk_application_new("org.streamgallery.UiTest", G_APPLICATION_NON_UNIQUE);
    GError* error = nullptr;
    g_assert_true(g_application_register(G_APPLICATION(app), nullptr, &error));
    g_assert_no_error(error);
    for (int iteration = 0; iteration < 2; ++iteration) {
        GtkWidget* window = streamgallery::createMainWindow(app);
        gtk_widget_show_all(window);
        auto* stack = GTK_STACK(findWidget(window, GTK_TYPE_STACK));
        auto* sidebar = GTK_STACK_SIDEBAR(findWidget(window, GTK_TYPE_STACK_SIDEBAR));
        g_assert_nonnull(stack);
        g_assert_true(gtk_stack_sidebar_get_stack(sidebar) == stack);
        g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "gallery");
        for (const char* page : {"browser", "player", "gallery"}) {
            g_assert_nonnull(gtk_stack_get_child_by_name(stack, page));
            gtk_stack_set_visible_child_name(stack, page);
            g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, page);
        }
#ifdef HAVE_LIBRARY_TESTS
        auto* status = GTK_LABEL(findNamed(window,"library-status"));
        auto* flow = GTK_CONTAINER(findNamed(window,"library-videos"));
        g_assert_nonnull(status); g_assert_nonnull(flow);
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"1–24 of 30 videos")==0; });
        auto childCount = [&] {
            GList* children = gtk_container_get_children(flow);
            const guint count = g_list_length(children); g_list_free(children); return count;
        };
        g_assert_cmpuint(childCount(),==,24);
        gtk_button_clicked(GTK_BUTTON(findNamed(window,"library-next")));
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"25–30 of 30 videos")==0; });
        g_assert_cmpuint(childCount(),==,6);
        gtk_combo_box_set_active(GTK_COMBO_BOX(findNamed(window,"library-mode")),1);
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"25–30 of 30 videos")==0; });
        g_assert_cmpuint(gtk_flow_box_get_max_children_per_line(GTK_FLOW_BOX(flow)),==,1);
        gtk_entry_set_text(GTK_ENTRY(findNamed(window,"library-search")),"Unique");
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"1–1 of 1 videos")==0; });
        g_assert_cmpuint(childCount(),==,1);
#endif
#ifdef HAVE_PLAYER_TESTS
        g_assert_nonnull(findWidget(window, GTK_TYPE_GL_AREA));
        auto* url = GTK_ENTRY(findNamed(window, "player-url"));
        auto* openUrl = GTK_BUTTON(findNamed(window, "player-open-url"));
        auto* playerStatus = GTK_LABEL(findNamed(window, "player-status"));
        g_assert_nonnull(url);
        g_assert_nonnull(openUrl);
        for (const char* invalid : {"file:///etc/passwd", "https://user:password@example.com/video", "not a URL"}) {
            gtk_entry_set_text(url, invalid);
            gtk_button_clicked(openUrl);
            g_assert_cmpstr(gtk_label_get_text(playerStatus), ==,
                "Enter an HTTP/HTTPS media URL without credentials");
        }
#endif
        auto* toggle = GTK_SWITCH(findWidget(window, GTK_TYPE_SWITCH));
        g_assert_nonnull(toggle);
        g_assert_cmpstr(atk_object_get_name(gtk_widget_get_accessible(GTK_WIDGET(toggle))),
                        ==, "Dark theme");
        for (gboolean dark : {1, 0}) {
            gtk_switch_set_active(toggle, dark);
            gboolean actual = !dark;
            g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &actual, nullptr);
            g_assert_true(actual == dark);
        }
        while (gtk_events_pending()) gtk_main_iteration();
        gtk_widget_destroy(window);
    }
    g_object_unref(app);
#ifdef HAVE_LIBRARY_TESTS
    std::filesystem::remove_all(testDirectory);
    g_free(testDirectory);
#endif
    return 0;
}
