#pragma once
#include <gtk/gtk.h>
namespace streamgallery {
// Widgets own the asynchronous lookup state. Combo is optional for filter completion.
void installNameSuggestions(GtkWidget* entry, bool folders, GtkWidget* combo = nullptr);
void refreshNameSuggestions(GtkWidget* entry);
}
