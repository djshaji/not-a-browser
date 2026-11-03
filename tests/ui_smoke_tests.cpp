#include "ui/MainWindow.hpp"
#include <initializer_list>

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
}
int main(int argc, char** argv) {
    if (!gtk_init_check(&argc, &argv)) return 77;
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
    return 0;
}
