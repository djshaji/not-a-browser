#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
namespace streamgallery {
GtkWidget* createPlayerView();
void setPlayerPlaybackCallback(GtkWidget* player, std::function<void(const std::string&)> callback);
// Window-level playback shortcuts, including when a child widget has focus.
void setPlayerFullscreen(GtkWidget* player, bool fullscreen);
void openPlayerFile(GtkWidget* player, const char* filename);
void openPlayerStream(GtkWidget* player, const char* url, const char* referrer, const char* userAgent);
gboolean handlePlayerKey(GtkWidget* player, GdkEventKey* event);
}
