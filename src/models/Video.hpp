#pragma once
#include <cstdint>
#include <string>
namespace streamgallery {
struct Video {
    std::int64_t id = 0;
    std::string canonicalUrl;
    std::string title;
    std::string thumbnailUrl;
    std::string description;
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
