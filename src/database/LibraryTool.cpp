#include "database/LibraryRepository.hpp"
#include <filesystem>
#include <iostream>
#include <glib.h>
int main(int argc, char** argv) {
    try {
        const auto directory=std::filesystem::path(g_get_user_data_dir()) / "stream-gallery";
        std::filesystem::create_directories(directory);
        streamgallery::Database database((directory / "library.db").string());
        streamgallery::LibraryRepository library(database);
        if ((argc == 4 || argc == 5) && std::string(argv[1]) == "bookmark") {
            const auto video=library.bookmark(argv[2],argv[3],argc == 5 ? argv[4] : "");
            std::cout << "Saved bookmark " << video.id << '\n';
        } else if (argc == 2 && std::string(argv[1]) == "list") {
            for (const auto& video : library.videos())
                std::cout << video.id << '\t' << video.title << '\t' << video.canonicalUrl << '\n';
        } else {
            std::cerr << "Usage: stream-gallery-library bookmark URL TITLE [THUMBNAIL_URL] | list\n";
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "Library error: " << error.what() << '\n';
        return 1;
    }
}
