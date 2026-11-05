#pragma once
#include "mihoyo/safe_event_log.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
namespace cloudgenshin {
// Only these fixed app-owned segment names are managed. No recursive cleanup,
// credentials, arbitrary directory enumeration, or following symlink targets.
class SafeLogArchive {
public:
    static constexpr std::size_t kSegments=16,kSegmentBytes=512*1024;
    static constexpr std::size_t kTotalBytes=kSegments*kSegmentBytes;
    explicit SafeLogArchive(std::size_t segment_bytes=kSegmentBytes)
        :limit_(std::max<std::size_t>(segment_bytes,1024)) {}
    bool save(const SafeEventLog& log,const std::string& directory) {
        std::lock_guard lock(mutex_);
        if(directory.empty()) return false;
        if(directory_!=directory) {directory_=directory;active_.clear();cursor_=0;}
        std::error_code error;
        auto status=std::filesystem::symlink_status(directory_,error);
        if(!error && std::filesystem::is_symlink(status)) return false;
        error.clear();
        std::filesystem::create_directory(directory_,error);
        if(error || !std::filesystem::is_directory(std::filesystem::symlink_status(directory_,error)) || error) return false;
        const auto batch=log.since(cursor_);
        if(batch.last<=cursor_) return true;
        std::string_view remaining=batch.text;
        while(!remaining.empty()) {
            if(active_.empty() && !open_segment(batch.header)) return false;
            std::uintmax_t size=0;if(!owned(active_,size)) return false;
            if(size>=limit_) {active_.clear();continue;}
            const auto available=limit_-static_cast<std::size_t>(size);
            std::size_t length=std::min(available,remaining.size());
            if(length<remaining.size()) {
                const auto newline=remaining.rfind('\n',length-1);
                if(newline==std::string_view::npos) {active_.clear();continue;}
                length=newline+1;
            }
            std::ofstream out(active_,std::ios::binary|std::ios::app);
            out.write(remaining.data(),static_cast<std::streamsize>(length));out.flush();
            const bool good=out.good();out.close();
            if(!good || out.fail()) return false;
            remaining.remove_prefix(length);
        }
        cursor_=batch.last;return true;
    }
private:
    std::filesystem::path path(std::size_t index) const {
        char name[32]{};std::snprintf(name,sizeof(name),"stream-safe-%02u.log",static_cast<unsigned>(index));
        return std::filesystem::path(directory_)/name;
    }
    bool owned(const std::filesystem::path& file,std::uintmax_t& size) const {
        std::error_code error;const auto status=std::filesystem::symlink_status(file,error);
        if(error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) return false;
        std::ifstream input(file,std::ios::binary);char bytes[160]{};input.getline(bytes,sizeof(bytes));
        const std::string_view first(bytes);
        // Old app-owned segments remain eligible for the same bounded ring;
        // rebranding must not strand the user's existing log budget.
        if(!input || (!first.starts_with("yuanshi launcher native alpha ") &&
            !first.starts_with("CloudGenshin-NX native alpha ")) ||
           first.find(" / safe events only")==std::string::npos) return false;
        size=std::filesystem::file_size(file,error);return !error;
    }
    bool open_segment(const std::string& header) {
        if(header.size()+256>=limit_) return false;
        std::optional<std::size_t> chosen;
        bool empty_slot=false;
        auto oldest=std::filesystem::file_time_type::max();
        // Only validated slots may be replaced. Foreign/truncated files are
        // quarantined in place, rather than disabling all remaining slots.
        for(std::size_t i=0;i<kSegments;++i) {
            std::error_code error;const auto file=path(i);
            const auto status=std::filesystem::symlink_status(file,error);
            if(status.type()==std::filesystem::file_type::not_found) {
                if(!empty_slot) {chosen=i;empty_slot=true;}
                continue;
            }
            std::uintmax_t size=0;if(!owned(file,size)) continue;
            if(size>limit_) continue; // Never overwrite an unexpected oversized file.
            auto modified=std::filesystem::last_write_time(file,error);
            if(error) modified={}; // Some SD backends cannot return timestamps.
            const auto distance=[&](std::size_t slot) {return (slot+kSegments-next_slot_)%kSegments;};
            if(!empty_slot && (!chosen || modified<oldest ||
                (modified==oldest && distance(i)<distance(*chosen)))) {oldest=modified;chosen=i;}
        }
        if(!chosen) return false;
        const auto selected=path(*chosen);
        std::ofstream output(selected,std::ios::binary|std::ios::trunc);
        output<<header;output.flush();const bool good=output.good();output.close();
        if(!good || output.fail()) return false;
        active_=selected;next_slot_=(*chosen+1)%kSegments;return true;
    }
    std::mutex mutex_;
    std::string directory_;
    std::filesystem::path active_;
    unsigned cursor_=0;
    std::size_t limit_;
    std::size_t next_slot_=0;
};
}
