#include "services/HlsDiscovery.hpp"
#include <libsoup/soup.h>
#include <memory>
#include <sstream>
#include <utility>

namespace streamgallery {
namespace {
constexpr std::size_t maxBytes = 1024 * 1024;
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \r\n\t");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \r\n\t") - first + 1);
}
// HLS attribute lists may contain commas inside quoted values.
std::string uriAttribute(const std::string& line) {
    auto start = line.find(':');
    if (start == std::string::npos) return {};
    ++start;
    while (start < line.size()) {
        auto end = start;
        bool quoted = false;
        for (; end < line.size(); ++end) {
            if (line[end] == '"') quoted = !quoted;
            if (line[end] == ',' && !quoted) break;
        }
        const auto attribute = trim(line.substr(start, end - start));
        if (attribute.compare(0, 4, "URI=") == 0 && attribute.size() >= 6 &&
            attribute[4] == '"' && attribute.back() == '"')
            return attribute.substr(5, attribute.size() - 6);
        start = end + 1;
    }
    return {};
}
template<typename T> struct Unref { void operator()(T* object) const { if (object) g_object_unref(object); } };
template<typename T> using Object = std::unique_ptr<T, Unref<T>>;
struct Request {
    std::string url, page, userAgent;
    std::function<void(HlsPlaylist)> complete;
};
HlsPlaylist fetch(const Request& request, GCancellable* cancellable) {
    HlsPlaylist result;
    result.url = resolveDiscoveryUrl(request.url, request.url);
    if (result.url.empty()) { result.error = "Invalid playlist URL"; return result; }
    Object<SoupSession> session(soup_session_new_with_options("timeout", 10, "user-agent", request.userAgent.c_str(), nullptr));
    for (unsigned redirects = 0; redirects <= 5; ++redirects) {
        Object<SoupMessage> message(soup_message_new("GET", result.url.c_str()));
        if (!message) { result.error = "Invalid playlist URL"; return result; }
        soup_message_add_flags(message.get(), SOUP_MESSAGE_NO_REDIRECT);
        if (!resolveDiscoveryUrl(request.page, request.page).empty())
            soup_message_headers_replace(soup_message_get_request_headers(message.get()), "Referer", request.page.c_str());
        GError* error = nullptr;
        Object<GInputStream> stream(soup_session_send(session.get(), message.get(), cancellable, &error));
        if (!stream) {
            result.error = error ? error->message : "Playlist request failed";
            g_clear_error(&error); return result;
        }
        const auto status = soup_message_get_status(message.get());
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            const char* location = soup_message_headers_get_one(soup_message_get_response_headers(message.get()), "Location");
            const auto next = location ? resolveDiscoveryUrl(location, result.url) : std::string();
            if (next.empty() || redirects == 5) { result.error = "Invalid redirect or redirect limit exceeded"; return result; }
            result.url = next;
            continue;
        }
        if (status < 200 || status >= 300) { result.error = "Playlist HTTP status " + std::to_string(status); return result; }
        std::string body;
        char buffer[16384];
        gssize count;
        while ((count = g_input_stream_read(stream.get(), buffer, sizeof(buffer), cancellable, &error)) > 0) {
            if (body.size() + static_cast<std::size_t>(count) > maxBytes) {
                result.error = "Playlist exceeds 1 MiB limit"; return result;
            }
            body.append(buffer, count);
        }
        if (count < 0) {
            result.error = error ? error->message : "Playlist read failed";
            g_clear_error(&error); return result;
        }
        return inspectHlsPlaylist(body, result.url);
    }
    result.error = "Redirect limit exceeded";
    return result;
}
}

std::string resolveDiscoveryUrl(const std::string& raw, const std::string& base) {
    if (raw.empty() || raw.size() > 8192 || raw.find_first_of("\r\n\t \"<>\\") != std::string::npos || raw.find('\0') != std::string::npos) return {};
    gchar* resolved = g_uri_resolve_relative(base.c_str(), raw.c_str(), G_URI_FLAGS_ENCODED, nullptr);
    if (!resolved) return {};
    GUri* uri = g_uri_parse(resolved, G_URI_FLAGS_ENCODED, nullptr);
    g_free(resolved);
    if (!uri) return {};
    const char* scheme = g_uri_get_scheme(uri);
    const bool valid = scheme && (g_ascii_strcasecmp(scheme, "http") == 0 || g_ascii_strcasecmp(scheme, "https") == 0) &&
        g_uri_get_host(uri) && *g_uri_get_host(uri) && !g_uri_get_userinfo(uri);
    gchar* value = valid ? g_uri_to_string_partial(uri, G_URI_HIDE_FRAGMENT) : nullptr;
    std::string result = value ? value : "";
    g_free(value); g_uri_unref(uri);
    return result;
}

HlsPlaylist inspectHlsPlaylist(const std::string& text, const std::string& finalUrl) {
    HlsPlaylist result;
    result.url = resolveDiscoveryUrl(finalUrl, finalUrl);
    if (result.url.empty()) { result.error = "Invalid playlist URL"; return result; }
    if (text.size() > maxBytes || text.find('\0') != std::string::npos || !g_utf8_validate(text.data(), text.size(), nullptr)) {
        result.error = "Invalid or oversized playlist text"; return result;
    }
    std::istringstream input(text.compare(0, 3, "\xef\xbb\xbf") == 0 ? text.substr(3) : text);
    std::string line;
    if (!std::getline(input, line) || trim(line) != "#EXTM3U") { result.error = "Missing #EXTM3U header"; return result; }
    bool pending = false, targetDuration = false, segment = false, absolute = false, relative = false;
    unsigned variants = 0;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::string tag = line.substr(0, line.find(':'));
        if (tag == "#EXT-X-STREAM-INF") {
            if (pending) { result.error = "Missing variant URI"; return result; }
            pending = true; result.master = true; continue;
        }
        if (tag == "#EXT-X-I-FRAME-STREAM-INF") result.master = true;
        if (tag == "#EXT-X-TARGETDURATION") targetDuration = true;
        std::string raw;
        if (line.front() != '#') {
            if (pending) { raw = line; tag = "#EXT-X-STREAM-INF"; pending = false; ++variants; }
            else segment = true;
        } else if (tag == "#EXT-X-I-FRAME-STREAM-INF" || tag == "#EXT-X-MEDIA" ||
                   tag == "#EXT-X-SESSION-KEY" || tag == "#EXT-X-SESSION-DATA") {
            raw = uriAttribute(line);
            if (tag == "#EXT-X-I-FRAME-STREAM-INF") {
                if (raw.empty()) { result.error = "Missing I-frame URI"; return result; }
                ++variants;
            }
        } else if (tag == "#EXT-X-DEFINE" || tag == "#EXT-X-CONTENT-STEERING") {
            result.warnings.push_back("Uninspected tag: " + tag);
        }
        if (raw.empty()) continue;
        if (raw.find("{$") != std::string::npos) { result.error = "HLS variables are not supported"; return result; }
        const auto resolved = resolveDiscoveryUrl(raw, result.url);
        if (resolved.empty()) { result.error = "Invalid playlist reference"; return result; }
        const char* scheme = g_uri_peek_scheme(raw.c_str());
        std::string kind = scheme ? "absolute" : raw.compare(0, 2, "//") == 0 ? "scheme-relative" :
            raw.front() == '/' ? "root-relative" : raw.front() == '?' ? "query-relative" : "path-relative";
        if (scheme) absolute = true; else relative = true;
        result.references.push_back({tag, raw, kind, resolved});
        if (result.references.size() > 4096) { result.error = "Too many playlist references"; return result; }
    }
    if (pending || (result.master && !variants)) result.error = "Missing variant URI";
    else if (!result.master && !(targetDuration && segment)) result.error = "Not a master or media playlist";
    result.referenceSummary = absolute && relative ? "mixed" : absolute ? "absolute-only" : relative ? "relative-only" : "none";
    return result;
}

void inspectHlsPlaylistAsync(const std::string& url, const std::string& page,
                            const std::string& userAgent, GCancellable* cancellable,
                            std::function<void(HlsPlaylist)> complete) {
    auto* request = new Request{url, page, userAgent, std::move(complete)};
    GTask* task = g_task_new(nullptr, cancellable, +[](GObject*, GAsyncResult* async, gpointer) {
        auto* task = G_TASK(async);
        auto* request = static_cast<Request*>(g_task_get_task_data(task));
        GError* error = nullptr;
        std::unique_ptr<HlsPlaylist> result(static_cast<HlsPlaylist*>(g_task_propagate_pointer(task, &error)));
        if (!result) {
            result = std::make_unique<HlsPlaylist>(); result->url = request->url;
            result->error = error ? error->message : "Playlist inspection failed";
        }
        g_clear_error(&error);
        request->complete(std::move(*result));
    }, nullptr);
    g_task_set_task_data(task, request, +[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancellation) {
        auto result = std::make_unique<HlsPlaylist>(fetch(*static_cast<Request*>(data), cancellation));
        g_task_return_pointer(task, result.release(), +[](gpointer data) { delete static_cast<HlsPlaylist*>(data); });
    });
    g_object_unref(task);
}
}
