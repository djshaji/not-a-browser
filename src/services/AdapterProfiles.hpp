#pragma once
#include <string>
#include <vector>

namespace streamgallery {
struct AdapterProfile {
    std::string name;
    std::string json;
    bool button = false;
    std::string filename;
};
// Reads bounded JSON files from $XDG_CONFIG_HOME/notabrowser at view creation or explicit reload.
std::vector<AdapterProfile> loadListingProfiles();
std::string loadHlsProfile();
}
