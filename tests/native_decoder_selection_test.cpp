#include "mihoyo/native_decoder_selection.hpp"
#include <filesystem>
#include <chrono>
#include <cassert>
int main() {
    using cloudgenshin::NativeDecoderSelection;
    const auto dir=std::filesystem::temp_directory_path()/
        ("cgnx-decoder-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);const auto home=dir.string();
    NativeDecoderSelection first;assert(first.begin(home) && first.preferred());
    NativeDecoderSelection unclean;assert(!unclean.begin(home) && unclean.blocked());
    first.finish();assert(!std::filesystem::exists(dir/"cloudgenshin-nvdec-active.txt"));
    assert(unclean.toggle() && !unclean.preferred());
    NativeDecoderSelection software;assert(!software.begin(home) && !software.blocked());
    assert(software.toggle() && software.preferred());
    NativeDecoderSelection hardware;assert(hardware.begin(home));hardware.finish();
    // A crash leaves the active marker. A deliberate off/on choice can rearm it.
    {std::ofstream marker(dir/"cloudgenshin-nvdec-active.txt");marker<<"CGNX_NVDEC_ACTIVE_V1\n";}
    NativeDecoderSelection recover;assert(!recover.begin(home) && recover.blocked());
    assert(recover.toggle());assert(recover.toggle());assert(!std::filesystem::exists(dir/"cloudgenshin-nvdec-active.txt"));
    {std::ofstream damaged(dir/"cloudgenshin-decoder.txt");damaged<<std::string(100,'x');}
    assert(!NativeDecoderSelection::load((dir/"cloudgenshin-decoder.txt").string()));
    std::filesystem::remove(dir/"cloudgenshin-decoder.txt");
    std::filesystem::remove(dir/"cloudgenshin-nvdec-active.txt");
    std::filesystem::remove(dir);
}
