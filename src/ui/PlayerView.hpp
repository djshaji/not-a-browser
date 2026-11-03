#pragma once
#include <gtk/gtk.h>
namespace streamgallery {
GtkWidget* createPlayerView();
// Window-level playback shortcuts, including when a child widget has focus.
void setPlayerFullscreen(GtkWidget* player, bool fullscreen);
void openPlayerFile(GtkWidget* player, const char* filename);
gboolean handlePlayerKey(GtkWidget* player, GdkEventKey* event);
}
