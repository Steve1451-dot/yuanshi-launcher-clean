#pragma once
#include <string>
#include <string_view>

namespace cloudgenshin::ui {
// Display only, after translation. Never apply to stored profile ids, paths,
// authentication metadata, wire messages or the machine-readable event log.
inline std::string AbbreviateGameNames(std::string_view text) {
    struct Name {std::string_view original,alias;};
    static constexpr Name names[]={
        {"云·星穹铁道","bt"},{"崩坏：星穹铁道","bt"},
        {"云·终末地","zmd"},{"云·原神","ys"},
        {"Star Rail Cloud","bt"},{"Endfield Cloud","zmd"},{"Genshin Cloud","ys"},
        {"Star Rail","bt"},{"Endfield","zmd"},{"Genshin","ys"},
        {"原神","ys"},{"终末地","zmd"},{"星穹铁道","bt"},{"崩铁","bt"},{"星铁","bt"}
    };
    std::string result(text);
    for(const auto& name:names) {
        std::size_t at=0;
        while((at=result.find(name.original,at))!=result.npos) {
            result.replace(at,name.original.size(),name.alias);at+=name.alias.size();
        }
    }
    return result;
}
} // namespace cloudgenshin::ui
