#include "services/AdapterProfiles.hpp"
#include <jsc/jsc.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <utility>

namespace streamgallery {
namespace {
std::string directory() {
    gchar* path = g_build_filename(g_get_user_config_dir(), "notabrowser", nullptr);
    std::string result(path); g_free(path); return result;
}
std::string field(JSCValue* object, const char* key) {
    auto* value = jsc_value_object_get_property(object, key);
    gchar* text = jsc_value_is_string(value) ? jsc_value_to_string(value) : nullptr;
    std::string result = text ? text : "";
    g_free(text); g_object_unref(value); return result;
}
bool read(const std::string& filename, bool listing, AdapterProfile& profile) {
    const auto path = directory() + "/" + filename;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    constexpr std::size_t limit = 128 * 1024;
    std::string text(limit + 1, '\0');
    file.read(text.data(), text.size()); text.resize(file.gcount());
    if (text.size() > limit || file.bad()) {
        g_printerr("Ignoring profile %s: unreadable or larger than 128 KiB\n", path.c_str()); return false;
    }
    auto* context = jsc_context_new();
    auto* value = jsc_value_new_from_json(context, text.c_str());
    bool valid = value && !jsc_context_get_exception(context);
    if (valid) {
        jsc_context_set_value(context, "profile", value);
        const char* common = R"JS((() => {
            const p = profile;
            const str = x => typeof x === 'string' && x.length > 0 && x.length <= 4096;
            const positive = x => Number.isFinite(x) && x > 0;
            const sources = x => Array.isArray(x) && x.length > 0 && x.length <= 64 &&
                x.every(s => s && str(s.selector) && (s.text === true || str(s.attribute)));
            if (!p || p.interfaceVersion !== 1 || !str(p.name)) return false;
)JS";
        std::string check = common;
        check += listing ? R"JS(
            return p.selectors && ['card','link','title','poster'].every(k => str(p.selectors[k])) &&
                p.pagination && ['link','button'].includes(p.pagination.type) && str(p.pagination.selector) &&
                p.limits && positive(p.limits.maxItemsPerPage) && positive(p.limits.waitMilliseconds);
        })())JS" : R"JS(
            const d = p.discovery;
            return p.id === 'generic-html-hls' && d && d.playlistCandidates &&
                sources(d.playlistCandidates.sources) && str(d.playlistCandidates.urlPathExtension) &&
                positive(d.playlistCandidates.maxCandidates) && d.poster && sources(d.poster.sources) &&
                (!d.title || (sources(d.title.sources) && positive(d.title.maxLength)));
        })())JS";
        auto* result = jsc_context_evaluate(context, check.c_str(), check.size());
        valid = !jsc_context_get_exception(context) && jsc_value_to_boolean(result);
        g_object_unref(result);
    }
    if (valid) {
        profile.filename = filename;
        profile.name = field(value, "name");
        // Canonical JSON prevents configuration text from becoming executable script.
        gchar* json = jsc_value_to_json(value, 0); profile.json = json ? json : ""; g_free(json);
        if (listing) {
            auto* pagination = jsc_value_object_get_property(value, "pagination");
            profile.button = field(pagination, "type") == "button"; g_object_unref(pagination);
        }
    } else g_printerr("Ignoring profile %s: invalid JSON or unsupported profile schema\n", path.c_str());
    g_clear_object(&value); g_object_unref(context); return valid;
}
}
std::vector<AdapterProfile> loadListingProfiles() {
    std::vector<AdapterProfile> profiles;
    GDir* dir = g_dir_open(directory().c_str(), 0, nullptr);
    if (!dir) return profiles;
    std::vector<std::string> names;
    while (const char* name = g_dir_read_name(dir)) {
        if (g_str_has_suffix(name, ".json") && std::string(name) != "generic-hls.json") names.emplace_back(name);
    }
    g_dir_close(dir); std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        if (profiles.size() == 64) break;
        AdapterProfile profile;
        if (read(name, true, profile)) profiles.push_back(std::move(profile));
    }
    return profiles;
}
std::string loadHlsProfile() {
    AdapterProfile profile;
    return read("generic-hls.json", false, profile) ? profile.json : "";
}
}
