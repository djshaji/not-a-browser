#include "ui/MainWindow.hpp"

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
    (void)stream_gallery_get_resource();
    GtkBuilder* builder = gtk_builder_new_from_resource(
        "/org/streamgallery/app/ui/main-window.ui");
    auto* window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    gtk_window_set_application(GTK_WINDOW(window), application);
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
    g_object_unref(builder);
    return window;
}
}
