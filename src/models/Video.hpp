#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace streamgallery {
struct Video {
    std::int64_t id = 0;
    std::string canonicalUrl;
    std::string title;
    std::string thumbnailUrl;
    std::string description;
    std::vector<std::string> tags = {};
    std::vector<std::string> rootFolders = {};
    std::int64_t lastPlayedAt = 0; // UTC microseconds; zero means never played.
};
struct Folder {
    std::int64_t id = 0;
    std::string name;
    std::int64_t parentId = 0;
};
struct Tag {
    std::int64_t id = 0;
    std::string name;
};
}
