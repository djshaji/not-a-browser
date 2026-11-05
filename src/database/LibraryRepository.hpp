#pragma once
#include "database/Database.hpp"
#include "models/Video.hpp"
#include <vector>
#include <optional>
namespace streamgallery {
std::string canonicalizeUrl(const std::string& url);
enum class VideoSort { Newest, Oldest, Title, Recent };
struct VideoPage {
    std::vector<Video> items;
    std::int64_t total = 0;
};
struct BookmarkResult {
    Video video;
    bool created = false;
};
struct RemovedBookmark {
    Video video;
    std::string addedAt;
    std::vector<std::int64_t> folderIds, tagIds;
};
class LibraryRepository {
public:
    explicit LibraryRepository(Database& database) : db(database) {}
    Video bookmark(const std::string& url, const std::string& title, const std::string& thumbnailUrl = "");
    BookmarkResult bookmarkWithStatus(const std::string& url, const std::string& title, const std::string& thumbnailUrl = "");
    void updateMetadata(std::int64_t videoId, const std::string& title, const std::string& description);
    void updateBookmark(std::int64_t videoId, const std::string& title, const std::string& description,
                        const std::vector<std::string>& tags,
                        const std::optional<std::vector<std::string>>& rootFolders = std::nullopt);
    std::vector<std::string> videoTags(std::int64_t videoId);
    std::vector<std::string> videoRootFolders(std::int64_t videoId);
    // Distinct literal-prefix suggestions; folders include roots only.
    std::vector<std::string> assignmentNames(bool folders, const std::string& prefix = "", int limit = 50);
    RemovedBookmark removeBookmark(std::int64_t videoId);
    void restoreBookmark(const RemovedBookmark& removed);
    // Updates an existing durable page only; never creates a bookmark.
    bool recordPlayback(const std::string& pageUrl, std::int64_t playedAt);
    std::vector<Video> videos();
    VideoPage page(const std::string& search, VideoSort sort, int offset, int limit = 24, const std::string& tag = "",
                   const std::string& folder = "");
    Folder createFolder(const std::string& name, std::int64_t parentId = 0);
    Tag createTag(const std::string& name);
    void addToFolder(std::int64_t videoId, std::int64_t folderId);
    void tagVideo(std::int64_t videoId, std::int64_t tagId);
private:
    Database& db;
};
}
