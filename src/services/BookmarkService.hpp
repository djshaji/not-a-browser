#pragma once
#include <functional>
#include <string>
namespace streamgallery {
// Records existing bookmarks in a worker; refresh runs on the caller’s main context.
void recordPlaybackAsync(const std::string& page, std::function<void()> refresh);
// Explicit saves finish across navigation. Completion runs on the caller's
// main context; an empty error indicates success.
void saveBookmarkAsync(const std::string& page, const std::string& title, const std::string& poster,
                       std::function<void(bool created, const std::string& error)> complete);
}
