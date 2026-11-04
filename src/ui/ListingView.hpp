#pragma once
#include <webkit2/webkit2.h>
#include <functional>
#include <string>
#include <vector>
namespace streamgallery {
struct ListingVideo {
    std::string url, title, posterUrl;
};
struct ListingResult {
    std::vector<ListingVideo> items;
    unsigned pages = 0;
    bool busy = false;
    bool hasNext = false;
};
GtkWidget* createListingView(WebKitWebContext* context,
                            std::function<void(const std::string&)> openVideo,
                            std::function<void()> librarySaved = {});
void openListingPage(GtkWidget* listing, const std::string& url);
ListingResult listingResult(GtkWidget* listing);
}
