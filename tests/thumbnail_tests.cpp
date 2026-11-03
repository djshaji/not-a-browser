#include "services/ThumbnailService.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
struct ImageSandboxUnavailable : std::runtime_error { using std::runtime_error::runtime_error; };
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main() {
    gchar* directory = g_dir_make_tmp("stream-gallery-thumbnails-XXXXXX",nullptr);
    if (!directory) return 1;
    const std::filesystem::path root(directory);
    g_free(directory);
    g_setenv("XDG_CACHE_HOME",root.c_str(),TRUE);
    int status = 0;
    try {
        auto source = root / "source.png";
        auto* original = gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,800,400);
        gdk_pixbuf_fill(original,0x336699ff);
        GError* saveError = nullptr;
        if (!gdk_pixbuf_save(original,source.c_str(),"png",&saveError,nullptr)) {
            const std::string message = saveError->message;
            g_error_free(saveError); g_object_unref(original);
            if (message.find("Operation not permitted") != std::string::npos ||
                message.find("Permission denied") != std::string::npos)
                throw ImageSandboxUnavailable("Image codec sandbox unavailable: " + message);
            throw std::runtime_error(message);
        }
        g_object_unref(original);
        gchar* uri = g_filename_to_uri(source.c_str(),nullptr,nullptr);
        auto* cancel = g_cancellable_new();
        auto* scaled = streamgallery::loadThumbnail(uri,cancel);
        check(scaled != nullptr,"Cannot decode source PNG");
        check(gdk_pixbuf_get_width(scaled)==192 && gdk_pixbuf_get_height(scaled)==96,"Incorrect thumbnail dimensions");
        g_object_unref(scaled);
        std::filesystem::remove(source);
        // A second request must work from the cache after the source is gone.
        scaled = streamgallery::loadThumbnail(uri,cancel);
        check(scaled != nullptr,"Disk cache was not reused"); g_object_unref(scaled);
        g_cancellable_cancel(cancel);
        check(streamgallery::loadThumbnail(uri,cancel)==nullptr,"Cancellation ignored");
        g_cancellable_reset(cancel);
        check(streamgallery::loadThumbnail("file:///nonexistent-thumbnail.png",cancel)==nullptr,"Missing image was not handled");
        check(streamgallery::loadThumbnail("",cancel)==nullptr,"Empty URI was not handled");
        check(streamgallery::loadThumbnail("javascript:alert(1)",cancel)==nullptr,"Unsupported URI accepted");
        g_free(uri); g_object_unref(cancel);
    } catch(const ImageSandboxUnavailable& error) { std::cerr << error.what() << '\n'; status=77; }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; status=1; }
    std::filesystem::remove_all(root);
    return status;
}
