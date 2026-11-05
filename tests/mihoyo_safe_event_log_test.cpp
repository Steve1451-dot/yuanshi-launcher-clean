#include "mihoyo/safe_event_log.hpp"
#include <cassert>
#include <filesystem>
#include <thread>
#include <vector>

std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main() {
    using cloudgenshin::SafeEventLog;
    SafeEventLog log("0.2.3");
    log.append("LOGIN_OK",0,200);
    log.append("VIDEO_DECODE_ERRORS",-42);
    log.append("wss://example.invalid/secret");
    log.append("QR=secret\nCookie: secret");
    assert(log.snapshot().find("native alpha 0.2.3")!=std::string::npos);
    assert(log.snapshot().starts_with("yuanshi launcher native alpha 0.2.3 / safe events only\n"));
    assert(log.since(0).header.starts_with("yuanshi launcher native alpha 0.2.3 / safe events only\n"));
    assert(log.snapshot().find("run_started_unix_seconds=")!=std::string::npos);
    assert(log.snapshot().find("secret")==std::string::npos);
    assert(log.snapshot().find("VIDEO_DECODE_ERRORS code=-42")!=std::string::npos);
    assert(log.recent(0).empty());
    assert(log.recent(1).find("UNSAFE_EVENT_TEXT_SUPPRESSED")!=std::string::npos);
    std::vector<std::thread> workers;
    for (int i=0;i<4;++i) workers.emplace_back([&log] {
        for (int j=0;j<200;++j) log.append("VIDEO_UNITS_RECEIVED",j);
    });
    for (auto& worker:workers) worker.join();
    const auto lines=log.recent(3000);
    assert(std::count(lines.begin(),lines.end(),'\n')==804);
    const auto full=log.snapshot();
    assert(full.find("LOGIN_OK")!=std::string::npos && lines.find("LOGIN_OK")!=std::string::npos);
    assert(full.find("LOGIN_OK")==full.rfind("LOGIN_OK") && full.find("secret")==std::string::npos);
    assert(std::count(full.begin(),full.end(),'\n')==806);
    SafeEventLog overlapping("test");
    for(int i=0;i<2500;++i) overlapping.append("COUNTER",i);
    const auto overlap=overlapping.snapshot();
    assert(std::count(overlap.begin(),overlap.end(),'\n')==128+2048+3);
    assert(overlap.find("omitted_event_count=")!=std::string::npos);
    SafeEventLog verbose("test");verbose.append("LOGIN_RESTORED");
    for(int i=0;i<3000;++i) verbose.append(std::string(96,'A'),i);
    const auto bounded=verbose.snapshot();assert(bounded.size()<=128*1024);
    assert(bounded.find("LOGIN_RESTORED")!=std::string::npos && bounded.find("code=2999")!=std::string::npos);
    assert(bounded.find("omitted_event_count=")!=std::string::npos);
    const auto directory=std::filesystem::temp_directory_path()/
        ("cloudgenshin-safe-log-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path=directory/"safe.log";
    assert(log.save(path.string()));
    assert(read(path)==log.snapshot());
    log.append("STREAM_DIAGNOSTICS_SNAPSHOT");
    assert(log.save(path.string()));
    assert(read(path)==log.snapshot());
    assert(!log.save((directory/"missing"/"safe.log").string()));
    assert(!log.recent(1).empty());
    // Failed installation must not destroy the previous complete file.
    assert(!opennow::storage::ReplaceWithTemporaryFile((directory/"missing.tmp").string(),path.string()));
    assert(read(path)==log.snapshot());
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
}
