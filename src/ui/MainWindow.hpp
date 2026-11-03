#pragma once
#include <gtk/gtk.h>

namespace streamgallery {
// The application owns the returned window and all its child widgets.
GtkWidget* createMainWindow(GtkApplication* application);
}
