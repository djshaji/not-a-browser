#include "ui/MainWindow.hpp"
#ifdef HAVE_LIBRARY
#include "ui/GalleryView.hpp"
#endif

#ifdef HAVE_PLAYER
#include "ui/PlayerView.hpp"
#include <clocale>
#endif

// Referencing the generated symbol also pulls resources from the static library.
extern "C" GResource* stream_gallery_get_resource();

namespace streamgallery {
namespace {
void themeChanged(GObject* toggle, GParamSpec*, gpointer) {
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                 gtk_switch_get_active(GTK_SWITCH(toggle)), nullptr);
}
}

GtkWidget* createMainWindow(GtkApplication* application) {
#ifdef HAVE_PLAYER
    // GTK adopts the desktop locale; libmpv requires C numeric formatting.
    // Do this once, before constructing gallery workers or the player.
    static const bool numericLocaleReady = [] { return std::setlocale(LC_NUMERIC, "C") != nullptr; }();
    (void)numericLocaleReady;
#endif
    (void)stream_gallery_get_resource();
    GtkBuilder* builder = gtk_builder_new_from_resource(
        "/org/streamgallery/app/ui/main-window.ui");
    auto* window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    gtk_window_set_application(GTK_WINDOW(window), application);
    g_signal_connect(window, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer) -> gboolean {
        constexpr guint reserved = GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_MOD3_MASK |
            GDK_MOD4_MASK | GDK_MOD5_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK | GDK_META_MASK;
        if ((event->state & reserved) || gdk_keyval_to_lower(event->keyval) != GDK_KEY_q) return FALSE;
        auto* app = gtk_window_get_application(GTK_WINDOW(widget));
        if (app) g_application_quit(G_APPLICATION(app));
        gtk_widget_destroy(widget);
        return TRUE;
    }), nullptr);
    auto* toggle = GTK_SWITCH(gtk_builder_get_object(builder, "theme_switch"));
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(toggle)), "Dark theme");
    gboolean dark = FALSE;
    g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &dark, nullptr);
    gtk_switch_set_active(toggle, dark);
    g_signal_connect(toggle, "notify::active", G_CALLBACK(themeChanged), nullptr);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/org/streamgallery/app/application.css");
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(window),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    // The screen retains the provider; release it when this window is destroyed.
    g_object_set_data_full(G_OBJECT(window), "application-css", css, [](gpointer data) {
        auto* provider = GTK_CSS_PROVIDER(data);
        gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider));
        g_object_unref(provider);
    });
#ifdef HAVE_LIBRARY
    auto* gallery = GTK_BOX(gtk_builder_get_object(builder, "gallery_page"));
    GList* children = gtk_container_get_children(GTK_CONTAINER(gallery));
    for (auto* child = children; child; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
    g_list_free(children);
    gtk_widget_set_halign(GTK_WIDGET(gallery), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(gallery), GTK_ALIGN_FILL);
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(gallery)), "empty-state");
    gtk_widget_set_margin_start(GTK_WIDGET(gallery), 16);
    gtk_widget_set_margin_end(GTK_WIDGET(gallery), 16);
    gtk_widget_set_margin_top(GTK_WIDGET(gallery), 16);
    gtk_widget_set_margin_bottom(GTK_WIDGET(gallery), 16);
    gtk_box_pack_start(gallery, createGalleryView(), TRUE, TRUE, 0);
#endif
#ifdef HAVE_PLAYER
    auto* player = GTK_BOX(gtk_builder_get_object(builder, "player_page"));
    GList* playerChildren = gtk_container_get_children(GTK_CONTAINER(player));
    for (auto* child = playerChildren; child; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
    g_list_free(playerChildren);
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(player)), "empty-state");
    gtk_widget_set_halign(GTK_WIDGET(player), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(player), GTK_ALIGN_FILL);
    auto* playerView = createPlayerView();
    gtk_box_pack_start(player, playerView, TRUE, TRUE, 0);
    // Run before GtkWindow forwards keys to the focused child.
    g_signal_connect_object(window, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer view) -> gboolean {
        if (event->keyval == GDK_KEY_Escape) {
            setPlayerFullscreen(GTK_WIDGET(view), false);
            return TRUE;
        }
        return handlePlayerKey(GTK_WIDGET(view), event);
    }), playerView, G_CONNECT_DEFAULT);
    g_signal_connect_object(window, "window-state-event", G_CALLBACK(+[](GtkWidget*, GdkEventWindowState* event, gpointer view) -> gboolean {
        if ((event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) &&
            !(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN))
            setPlayerFullscreen(GTK_WIDGET(view), false);
        return FALSE;
    }), playerView, G_CONNECT_DEFAULT);
#endif
    g_object_unref(builder);
    return window;
}
}
