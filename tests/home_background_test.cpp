#include "home_background.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace cloudgenshin::background;
namespace fs=std::filesystem;

static std::vector<unsigned char> PixelPng() {
    // A locally constructed 1x1 RGBA image, including an uncompressed zlib
    // block. PNG CRC words are unused by stb; no external image fixture.
    std::vector<unsigned char> bytes={137,80,78,71,13,10,26,10};
    auto word=[&](std::uint32_t value) {
        for(int shift=24;shift>=0;shift-=8) bytes.push_back(static_cast<unsigned char>(value>>shift));
    };
    auto chunk=[&](const char* name,const std::vector<unsigned char>& body) {
        word(static_cast<std::uint32_t>(body.size()));
        bytes.insert(bytes.end(),name,name+4);bytes.insert(bytes.end(),body.begin(),body.end());word(0);
    };
    chunk("IHDR",{0,0,0,1,0,0,0,1,8,6,0,0,0});
    const std::vector<unsigned char> raw={0,17,34,51,255};
    std::uint32_t a=1,b=0;
    for(auto value:raw) {a=(a+value)%65521;b=(b+a)%65521;}
    std::vector<unsigned char> compressed={0x78,1,1,5,0,0xfa,0xff};
    compressed.insert(compressed.end(),raw.begin(),raw.end());
    for(int shift=24;shift>=0;shift-=8) compressed.push_back(static_cast<unsigned char>(((b<<16)|a)>>shift));
    chunk("IDAT",compressed);chunk("IEND",{});return bytes;
}
static void Write(const fs::path& path,const std::vector<unsigned char>& bytes) {
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    assert(out.good());
}
int main(int argc,char** argv) {
    assert(AllowedDimensions(1280,720) && AllowedDimensions(1920,1080));
    assert(AllowedDimensions(2048,2048) && !AllowedDimensions(4096,4096));
    assert(!AllowedDimensions(4097,1) && !AllowedDimensions(0,720) && !AllowedDimensions(-1,720));
    assert(!AllowedSignature({}) && !AllowedSignature({137,80,78}));
    assert(!AllowedSignature({'G','I','F','8','9','a'}));
    assert(AllowedSignature({0xff,0xd8,0xff}));
    assert(Paths("test-root",0)[0]=="test-root/backgrounds/genshin.png");
    assert(Paths("test-root",1)[1]=="test-root/backgrounds/starrail.jpg");
    assert(Paths("test-root",2)[0]=="test-root/backgrounds/endfield.png");
    assert(Paths("test-root",3)[0].empty());
    assert(Paths("test-root",4)[0].empty());
    const auto root=fs::temp_directory_path()/("cg-background-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"backgrounds");
    const auto paths=Paths(root.string(),0);
    unsigned probes=0,uploads=0;
    auto probe=[&](const auto& bytes,int& width,int& height) {
        ++probes;int channels=0;return stbi_info_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&channels)!=0;
    };
    auto create=[&](auto& bytes) {
        ++uploads;int width=0,height=0,channels=0;
        auto* pixels=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&channels,4);
        if(!pixels) return 0;
        assert(width==1 && height==1 && pixels[0]==17 && pixels[3]==255);
        stbi_image_free(pixels);return 7;
    };
    assert(Load(root.string(),0,probe,create)==0 && probes==0 && uploads==0); // Missing -> vector fallback.
    const auto valid=PixelPng();Write(paths[0],valid);
    assert(Load(root.string(),0,probe,create)==7 && probes==1 && uploads==1); // Actual stb probe + decode.
    // Failed PNG must try the next supported extension, not a live URL or a
    // different game's image. Contents, not extension, select the decoder.
    Write(paths[0],{137,80,78,71,13,10,26,10,0});Write(paths[1],valid);
    assert(Load(root.string(),0,probe,create)==7 && probes==3 && uploads==2);
    auto enormous=valid;enormous[16]=0;enormous[17]=0;enormous[18]=16;enormous[19]=1;
    Write(paths[0],enormous);
    assert(Load(root.string(),0,probe,create)==7 && probes==5 && uploads==3); // Header rejected before allocation.
    fs::remove(paths[1]);Write(paths[0],std::vector<unsigned char>(kMaximumFileBytes+1,0));
    assert(Load(root.string(),0,probe,create)==0 && probes==5 && uploads==3);
    assert(Load(root.string(),2,probe,create)==0); // No cross-game fallback to Genshin.
    assert(Load(root.string(),999,probe,create)==0 && probes==5);
    Write(paths[0],valid);Write(paths[2],valid);
    unsigned attempts=0;
    assert(Load(root.string(),0,probe,[&](auto&) {return ++attempts==1?0:9;})==9 && attempts==2); // GPU failure tries JPEG slot.
    fs::remove_all(root); // Only this test's newly created directory.
    // Optional release validation: decode the actual three-file image pack
    // with the same stb implementation used by NanoVG, without a window/GPU.
    for(int argument=1;argument<argc;++argument) {
        for(unsigned game=0;game<kNames.size();++game) {
            const int loaded=Load(argv[argument],game,probe,[](auto& bytes) {
                int width=0,height=0,channels=0;
                auto* pixels=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&width,&height,&channels,4);
                if(!pixels) return 0;
                const bool good=width==1280 && height==720;
                stbi_image_free(pixels);return good?1:0;
            });
            assert(loaded==1);
        }
        std::cout<<"PASS actual release backgrounds decoded, all three 1280x720\n";
    }
    std::cout<<"PASS fixed background paths, PNG/JPEG gate, bounded snapshot, real stb decode and fallback\n";
}
