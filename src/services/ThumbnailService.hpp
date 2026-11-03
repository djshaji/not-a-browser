#pragma once
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string>
namespace streamgallery {
// Worker-thread API. Caller owns the returned pixbuf; missing images return null.
GdkPixbuf* loadThumbnail(const std::string& uri, GCancellable* cancellable);
}
