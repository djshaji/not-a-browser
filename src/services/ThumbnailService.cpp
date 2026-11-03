#include "services/ThumbnailService.hpp"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <mutex>
#include <memory>
#ifdef HAVE_SOUP
#include <libsoup/soup.h>
#endif
namespace streamgallery {
namespace {
constexpr gsize maxBytes = 5 * 1024 * 1024;
std::mutex cacheMutex;
void scaleImage(GdkPixbufLoader* loader, int width, int height, gpointer) {
    if (width <= 0 || height <= 0) return;
    const double ratio = std::min({192.0 / width, 108.0 / height, 1.0});
    gdk_pixbuf_loader_set_size(loader, std::max(1, static_cast<int>(width * ratio)),
        std::max(1, static_cast<int>(height * ratio)));
}
void trimCache(const std::filesystem::path& directory) {
    std::vector<std::filesystem::directory_entry> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file()) files.push_back(entry);
    if (files.size() <= 256) return;
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) {
        return a.last_write_time() < b.last_write_time();
    });
    for (std::size_t i = 0; i < files.size() - 256; ++i) std::filesystem::remove(files[i]);
}
}
GdkPixbuf* loadThumbnail(const std::string& uri, GCancellable* cancellable) {
    if (uri.empty() || g_cancellable_is_cancelled(cancellable)) return nullptr;
    try {
        gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256,uri.c_str(),-1);
        const auto directory = std::filesystem::path(g_get_user_cache_dir()) / "stream-gallery" / "thumbnails";
        const auto path = directory / (std::string(digest) + ".png");
        g_free(digest);
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (std::filesystem::exists(path)) {
                if (auto* cached = gdk_pixbuf_new_from_file_at_scale(path.c_str(),192,108,TRUE,nullptr)) return cached;
            }
        }
        GInputStream* stream = nullptr;
#ifdef HAVE_SOUP
        SoupSession* session = nullptr;
        SoupMessage* message = nullptr;
        if (g_str_has_prefix(uri.c_str(), "https://") || g_str_has_prefix(uri.c_str(), "http://")) {
            session = soup_session_new_with_options("timeout", 10, "user-agent", "StreamGallery/0.1", nullptr);
            message = soup_message_new("GET",uri.c_str());
            if (message) stream = soup_session_send(session,message,cancellable,nullptr);
            if (message && (soup_message_get_status(message) < 200 || soup_message_get_status(message) >= 300))
                g_clear_object(&stream);
        } else
#endif
        if (g_str_has_prefix(uri.c_str(), "file://")) {
            GFile* file = g_file_new_for_uri(uri.c_str());
            stream = G_INPUT_STREAM(g_file_read(file,cancellable,nullptr));
            g_object_unref(file);
        }
        GdkPixbuf* result = nullptr;
        if (stream) {
            GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
            g_signal_connect(loader,"size-prepared",G_CALLBACK(scaleImage),nullptr);
            gsize total = 0;
            gssize size = 0;
            guchar buffer[16384];
            bool valid = true;
            while ((size = g_input_stream_read(stream,buffer,sizeof(buffer),cancellable,nullptr)) > 0) {
                total += static_cast<gsize>(size);
                if (total > maxBytes || !gdk_pixbuf_loader_write(loader,buffer,size,nullptr)) { valid = false; break; }
            }
            const bool complete = gdk_pixbuf_loader_close(loader,nullptr);
            if (valid && size == 0 && complete && !g_cancellable_is_cancelled(cancellable)) {
                result = gdk_pixbuf_loader_get_pixbuf(loader);
                if (result) g_object_ref(result);
            }
            g_object_unref(loader);
            g_object_unref(stream);
        }
#ifdef HAVE_SOUP
        g_clear_object(&message);
        g_clear_object(&session);
#endif
        if (result) {
            // A read-only/unavailable cache must not prevent displaying the image.
            try {
                std::lock_guard<std::mutex> lock(cacheMutex);
                std::filesystem::create_directories(directory);
                gdk_pixbuf_save(result,path.c_str(),"png",nullptr,nullptr);
                trimCache(directory);
            } catch (const std::filesystem::filesystem_error&) {}
        }
        return result;
    } catch (const std::filesystem::filesystem_error&) { return nullptr; }
}
}
