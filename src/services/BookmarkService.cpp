#include "services/BookmarkService.hpp"
#include "database/LibraryRepository.hpp"
#include <gio/gio.h>
#include <filesystem>
#include <memory>
#include <utility>
namespace streamgallery {
namespace {
struct Request {
    std::string path, page, title, poster;
    std::function<void(bool, const std::string&)> complete;
};
}
void saveBookmarkAsync(const std::string& page, const std::string& title, const std::string& poster,
                       std::function<void(bool, const std::string&)> complete) {
    auto path = std::filesystem::path(g_get_user_data_dir()) / "stream-gallery" / "library.db";
    auto* request = new Request{path.string(), page, title.empty() ? page : title, poster, std::move(complete)};
    GTask* task = g_task_new(nullptr, nullptr, +[](GObject*, GAsyncResult* async, gpointer) {
        auto* task = G_TASK(async);
        auto* request = static_cast<Request*>(g_task_get_task_data(task));
        GError* error = nullptr;
        std::unique_ptr<BookmarkResult> result(static_cast<BookmarkResult*>(g_task_propagate_pointer(task, &error)));
        request->complete(result && result->created, error ? error->message : "");
        g_clear_error(&error);
    }, nullptr);
    g_task_set_task_data(task, request, +[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable*) {
        auto* request = static_cast<Request*>(data);
        try {
            std::filesystem::create_directories(std::filesystem::path(request->path).parent_path());
            Database database(request->path);
            LibraryRepository library(database);
            auto result = std::make_unique<BookmarkResult>(library.bookmarkWithStatus(request->page, request->title, request->poster));
            g_task_return_pointer(task, result.release(), +[](gpointer data) { delete static_cast<BookmarkResult*>(data); });
        } catch (const std::exception& error) {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", error.what());
        }
    });
    g_object_unref(task);
}
}
