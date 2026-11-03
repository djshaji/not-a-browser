#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
namespace streamgallery {
using StreamReady = std::function<void(const std::string& stream, const std::string& page,
                                      const std::string& userAgent)>;
GtkWidget* createBrowserView(StreamReady onStream);
void openBrowserPage(GtkWidget* browser, const std::string& url);
}
