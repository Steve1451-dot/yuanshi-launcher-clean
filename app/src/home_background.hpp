#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace cloudgenshin::background {

// These are filenames, never values read from configuration or a remote API.
inline constexpr std::array<std::string_view,3> kNames={"genshin","starrail","endfield"};
inline constexpr std::size_t kMaximumFileBytes=8*1024*1024;
inline constexpr std::uint64_t kMaximumPixels=4*1024*1024;

inline bool AllowedDimensions(int width,int height) {
    return width>0 && height>0 && width<=4096 && height<=4096 &&
        static_cast<std::uint64_t>(width)*static_cast<unsigned>(height)<=kMaximumPixels;
}

inline bool AllowedSignature(const std::vector<unsigned char>& data) {
    constexpr unsigned char png[]={137,80,78,71,13,10,26,10};
    bool is_png=data.size()>=sizeof(png);
    for(unsigned i=0;is_png && i<sizeof(png);++i) is_png=data[i]==png[i];
    return is_png || (data.size()>=3 && data[0]==0xff && data[1]==0xd8 && data[2]==0xff);
}

inline std::array<std::string,3> Paths(const std::string& home,unsigned game) {
    if(game>=kNames.size()) return {};
    const std::string base=home+"/backgrounds/"+std::string(kNames[game]);
    return {base+".png",base+".jpg",base+".jpeg"};
}

inline std::vector<unsigned char> ReadBounded(const std::string& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in) return {};
    const auto length=in.tellg();
    if(length<=0 || length>static_cast<std::streamoff>(kMaximumFileBytes)) return {};
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) return {};
    // Reject a concurrently replaced/growing file; probe and decode use this
    // same bounded snapshot, not a second open of an unvalidated path.
    if(in.peek()!=std::char_traits<char>::eof()) return {};
    return bytes;
}

// Probe must inspect the actual encoded image dimensions before decode/upload.
// Create returns a positive texture handle on success, zero otherwise. The UI
// owns that handle and retains at most one background texture at a time.
template<class Probe,class Create>
int Load(const std::string& home,unsigned game,Probe probe,Create create) {
    if(game>=kNames.size()) return 0;
    for(const auto& path:Paths(home,game)) {
        auto bytes=ReadBounded(path);
        if(!AllowedSignature(bytes)) continue;
        int width=0,height=0;
        if(!probe(bytes,width,height) || !AllowedDimensions(width,height)) continue;
        const int handle=create(bytes);
        if(handle>0) return handle;
    }
    return 0; // No retry every frame: draw the procedural original instead.
}

// Original geometric palette shared with the optional PNG pack generator.
struct Palette {unsigned char red,green,blue;};
inline constexpr std::array<Palette,3> kPalettes={{{65,190,175},{109,129,230},{94,174,209}}};

} // namespace cloudgenshin::background
