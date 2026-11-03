#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
namespace streamgallery {
GtkWidget* createGalleryView(std::function<void(const std::string&)> openPage = {});
}
