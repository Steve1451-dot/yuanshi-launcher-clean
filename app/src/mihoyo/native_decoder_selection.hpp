#pragma once
#include "atomic_file_replace.hpp"
#include <cstdio>
#include <fstream>
#include <string>
namespace cloudgenshin {
// UI owner only. A failed/unclean hardware attempt must not cause a crash loop.
// This is an experimental preference, not proof of correct hardware pixels.
class NativeDecoderSelection {
public:
    NativeDecoderSelection()=default;
    NativeDecoderSelection(const NativeDecoderSelection&)=delete;
    NativeDecoderSelection& operator=(const NativeDecoderSelection&)=delete;
    bool begin(const std::string& home) {
        if(owned_) return true;
        if(home.empty()) {blocked_=true;return false;}
        home_=home;preferred_=load(home+"/cloudgenshin-decoder.txt");
        if(!preferred_) return false;
        marker_=home+"/cloudgenshin-nvdec-active.txt";
        if(std::ifstream(marker_).good()) {blocked_=true;return false;}
        std::ofstream out(marker_,std::ios::binary|std::ios::trunc);
        out<<"CGNX_NVDEC_ACTIVE_V1\n";out.flush();const bool good=out.good();out.close();
        owned_=good && !out.fail();blocked_=!owned_;return owned_;
    }
    void finish() {if(owned_) {std::remove(marker_.c_str());owned_=false;}}
    bool preferred() const {return preferred_;}
    bool blocked() const {return blocked_;}
    bool toggle() {
        if(home_.empty()) return false;
        const bool next=!preferred_;
        const auto path=home_+"/cloudgenshin-decoder.txt";
        std::ofstream out(path+".tmp",std::ios::binary|std::ios::trunc);
        out<<(next ? "hardware\n" : "software\n");out.flush();const bool good=out.good();out.close();
        if(!good || out.fail() || !opennow::storage::ReplaceWithTemporaryFile(path+".tmp",path)) return false;
        preferred_=next;
        // A deliberate software -> hardware choice rearms a previous unclean
        // attempt, but never removes a marker owned by this live session.
        if(next && !owned_) std::remove((home_+"/cloudgenshin-nvdec-active.txt").c_str());
        return true;
    }
    static bool load(const std::string& path) {
        bool present=false;
        for(const auto& file:{path,path+".bak"}) {
            std::ifstream in(file,std::ios::binary);if(!in) continue;
            present=true;char text[32]{};in.read(text,sizeof(text));
            if(!in.eof()) continue;
            const std::string value(text,static_cast<size_t>(in.gcount()));
            if(value=="hardware\n" || value=="hardware\r\n") return true;
            if(value=="software\n" || value=="software\r\n") return false;
        }
        return !present; // New install prefers hardware; damaged preference is safe software.
    }
private:
    std::string home_,marker_;
    bool preferred_=true,blocked_=false,owned_=false;
};
}
