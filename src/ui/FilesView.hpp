#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>

namespace streamgallery {
// The native chooser handles folder navigation and asynchronous directory loading.
GtkWidget* createFilesView(std::function<void(const std::string&)> openFile = {});
}
