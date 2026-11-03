#include "app/Application.hpp"
#include "ui/MainWindow.hpp"

namespace streamgallery {
namespace {
void activate(GtkApplication* application, gpointer) {
    GtkWindow* window = gtk_application_get_active_window(application);
    if (!window) {
        window = GTK_WINDOW(createMainWindow(application));
        gtk_widget_show_all(GTK_WIDGET(window));
    }
    gtk_window_present(window);
}
}
int runApplication(int argc, char** argv) {
    GtkApplication* application = gtk_application_new("org.streamgallery.App", static_cast<GApplicationFlags>(0));
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
    const int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
}
