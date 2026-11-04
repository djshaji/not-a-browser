#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
namespace streamgallery {
GtkWidget* createGalleryView(std::function<void(const std::string&)> openPage = {});
// Keeps gallery state alive; safely does nothing after the widget is destroyed.
std::function<void()> galleryRefreshCallback(GtkWidget* gallery);
}
