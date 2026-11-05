#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
namespace streamgallery {
GtkWidget* createGalleryView(std::function<void(const std::string&)> openPage = {}, bool canDiscover = true, bool recentOnly = false);
// Share one Undo slot and conflicting-action guards across Library and Recent.
void shareGallerySession(GtkWidget* gallery, GtkWidget* other);
// Keeps gallery state alive; safely does nothing after the widget is destroyed.
std::function<void()> galleryRefreshCallback(GtkWidget* gallery);
}
