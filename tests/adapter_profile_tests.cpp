#include "services/AdapterProfiles.hpp"
#include <glib.h>
#include <filesystem>
#include <fstream>
#include <sstream>

std::string fixture(const char* name) {
    std::ifstream file(std::string(PROFILE_DIRECTORY) + "/" + name);
    std::ostringstream text; text << file.rdbuf(); return text.str();
}
int main() {
    gchar* temporary = g_dir_make_tmp("adapter-profiles-XXXXXX", nullptr);
    g_assert_nonnull(temporary); g_setenv("XDG_CONFIG_HOME", temporary, TRUE);
    const auto directory = std::filesystem::path(temporary) / "notabrowser";
    g_assert_true(streamgallery::loadListingProfiles().empty());
    g_assert_true(streamgallery::loadHlsProfile().empty());
    std::filesystem::create_directory(directory);
    auto write = [&](const char* name, const std::string& text) { std::ofstream(directory / name) << text; };
    write("z-button.json", fixture("listing-load-more.json"));
    write("a-links.json", fixture("listing-links.json"));
    write("generic-hls.json", fixture("generic-hls.json"));
    write("broken.json", "{}; throw new Error('configuration must not execute')");
    write("unsupported.json", "{\"interfaceVersion\":2}");
    write("oversized.json", std::string(128 * 1024 + 1, ' '));
    write("ignored.txt", fixture("listing-links.json"));
    auto profiles = streamgallery::loadListingProfiles();
    g_assert_cmpuint(profiles.size(), ==, 2);
    g_assert_false(profiles[0].button); g_assert_true(profiles[1].button);
    g_assert_cmpstr(profiles[0].name.c_str(), ==, "Cards with next-page links");
    g_assert_false(streamgallery::loadHlsProfile().empty());
    write("generic-hls.json", fixture("listing-links.json"));
    g_assert_true(streamgallery::loadHlsProfile().empty());
    std::filesystem::remove_all(temporary); g_free(temporary);
}
