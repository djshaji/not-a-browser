#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
#include "services/HlsDiscovery.hpp"
namespace streamgallery {
struct DiscoveryResult {
    std::string pageUrl;
    std::string posterUrl;
    std::vector<HlsPlaylist> playlists;
    std::vector<std::string> errors;
};
using StreamReady = std::function<void(const std::string& stream, const std::string& page,
                                      const std::string& userAgent)>;
GtkWidget* createBrowserView(StreamReady onStream);
void openBrowserPage(GtkWidget* browser, const std::string& url);
DiscoveryResult browserDiscoveryResult(GtkWidget* browser);
}
