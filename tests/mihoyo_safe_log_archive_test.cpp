#include "mihoyo/safe_log_archive.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
static std::string read(const fs::path& p) {std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main() {
    const auto root=fs::temp_directory_path()/("cgnx-archive-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    cloudgenshin::SafeEventLog log("test");cloudgenshin::SafeLogArchive archive(1024);
    log.append("STREAM_ENTRY");assert(archive.save(log,(root/"logs").string()));
    const auto first=read(root/"logs"/"stream-safe-00.log");
    assert(first.find("STREAM_ENTRY")!=std::string::npos);
    assert(first.starts_with("yuanshi launcher native alpha "));
    assert(archive.save(log,(root/"logs").string()));assert(first==read(root/"logs"/"stream-safe-00.log"));
    for(int i=0;i<1000;++i) {log.append("VIDEO_DECODED_FRAMES",i);assert(archive.save(log,(root/"logs").string()));}
    std::size_t count=0,total=0;bool latest=false;
    for(const auto& item:fs::directory_iterator(root/"logs")) {
        ++count;const auto size=fs::file_size(item);assert(size<=1024);total+=size;
        if(read(item).find("code=999")!=std::string::npos) latest=true;
    }
    assert(count==16 && total<=16*1024 && latest);
    cloudgenshin::SafeEventLog next("next");next.append("NEXT_RUN");cloudgenshin::SafeLogArchive next_archive(1024);
    assert(next_archive.save(next,(root/"logs").string()));
    // Unknown files, credentials and settings are never cleanup targets.
    {std::ofstream keep(root/"logs"/"login.vault");keep<<"KEEP_PRIVATE_FIXTURE";}
    assert(next_archive.save(next,(root/"logs").string()));
    assert(read(root/"logs"/"login.vault")=="KEEP_PRIVATE_FIXTURE");
    // A full old-name ring must rotate, rather than strand 8 MiB forever.
    fs::create_directory(root/"legacy");
    for(unsigned i=0;i<16;++i) {
        char name[32]{};std::snprintf(name,sizeof(name),"stream-safe-%02u.log",i);
        std::ofstream old(root/"legacy"/name);
        old<<"CloudGenshin-NX native alpha 0.2.109 / safe events only\nrun_started_unix_seconds=1\n";
    }
    cloudgenshin::SafeLogArchive legacy(1024);assert(legacy.save(next,(root/"legacy").string()));
    bool migrated=false;
    for(const auto& item:fs::directory_iterator(root/"legacy")) {
        migrated=migrated || read(item).starts_with("yuanshi launcher native alpha ");
        fs::remove(item.path());
    }
    assert(migrated);fs::remove(root/"legacy");
    fs::create_directory(root/"blocked");
    {std::ofstream unknown(root/"blocked"/"stream-safe-00.log");unknown<<"NOT_AN_APP_LOG";}
    cloudgenshin::SafeLogArchive blocked(1024);assert(blocked.save(log,(root/"blocked").string()));
    assert(read(root/"blocked"/"stream-safe-00.log")=="NOT_AN_APP_LOG");
    std::error_code ec;fs::create_directory_symlink(root/"logs",root/"link",ec);
    if(!ec) {cloudgenshin::SafeLogArchive link(1024);assert(!link.save(log,(root/"link").string()));fs::remove(root/"link");}
    for(const auto& item:fs::directory_iterator(root/"blocked")) fs::remove(item.path());
    fs::remove(root/"blocked");
    for(const auto& item:fs::directory_iterator(root/"logs")) fs::remove(item.path());
    fs::remove(root/"logs");fs::remove(root);
}
