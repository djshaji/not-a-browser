#pragma once
#include "database/Database.hpp"
#include "models/Video.hpp"
#include <vector>
namespace streamgallery {
std::string canonicalizeUrl(const std::string& url);
enum class VideoSort { Newest, Oldest, Title };
struct VideoPage {
    std::vector<Video> items;
    std::int64_t total = 0;
};
struct BookmarkResult {
    Video video;
    bool created = false;
};
class LibraryRepository {
public:
    explicit LibraryRepository(Database& database) : db(database) {}
    Video bookmark(const std::string& url, const std::string& title, const std::string& thumbnailUrl = "");
    BookmarkResult bookmarkWithStatus(const std::string& url, const std::string& title, const std::string& thumbnailUrl = "");
    void updateMetadata(std::int64_t videoId, const std::string& title, const std::string& description);
    std::vector<Video> videos();
    VideoPage page(const std::string& search, VideoSort sort, int offset, int limit = 24);
    Folder createFolder(const std::string& name, std::int64_t parentId = 0);
    Tag createTag(const std::string& name);
    void addToFolder(std::int64_t videoId, std::int64_t folderId);
    void tagVideo(std::int64_t videoId, std::int64_t tagId);
private:
    Database& db;
};
}
