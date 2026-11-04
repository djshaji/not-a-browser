#pragma once
#include <gio/gio.h>
#include <functional>
#include <string>
#include <vector>

namespace streamgallery {
struct HlsReference {
    std::string tag;
    std::string raw;
    std::string kind;
    std::string resolvedUrl;
};
struct HlsPlaylist {
    std::string url;
    bool master = false;
    std::string referenceSummary = "none";
    std::vector<HlsReference> references;
    std::vector<std::string> warnings;
    std::string error;
};
// Empty return value means an invalid or unsupported URL.
std::string resolveDiscoveryUrl(const std::string& raw, const std::string& base);
HlsPlaylist inspectHlsPlaylist(const std::string& text, const std::string& finalUrl);
// Completion is delivered on the caller's main context. Fetching and parsing run
// in a worker, with a 1 MiB body limit, validated redirects, and cancellation.
void inspectHlsPlaylistAsync(const std::string& url, const std::string& page,
                            const std::string& userAgent, GCancellable* cancellable,
                            std::function<void(HlsPlaylist)> complete);
}
