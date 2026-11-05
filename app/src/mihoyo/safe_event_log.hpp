#pragma once
#include "atomic_file_replace.hpp"
#include "app_version.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace cloudgenshin {
// Only call with source-defined event names and numeric codes/counters. Never
// pass server bodies, identifiers, credentials, endpoints, QR content or SDP.
class SafeEventLog {
public:
    explicit SafeEventLog(std::string version) : version_(std::move(version)) {}
    void append(std::string_view event, int code=0, long http=0) {
        if (event.empty() || event.size()>96 || !std::all_of(event.begin(),event.end(),[](char c) {
            return (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';
        })) event="UNSAFE_EVENT_TEXT_SUPPRESSED";
        const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-started_).count();
        std::lock_guard lock(mutex_);
        std::string line=std::to_string(++sequence_)+" t="+std::to_string(seconds)+"s "+std::string(event);
        line+=" code="+std::to_string(code);
        if (http) line+=" http="+std::to_string(http);
        // Preserve bounded boot/auth/transport evidence even after several
        // minutes of counters evict it from the recent-event ring.
        if(sequence_<=128) startup_.push_back(line);
        // RAM ring is bounded by event count; snapshots also enforce the
        // monitor's byte budget. The archive preserves incremental history.
        if (events_.size()==2048) events_.pop_front();
        events_.push_back(std::move(line));
    }
    std::string recent(std::size_t count=8) const {
        std::lock_guard lock(mutex_);
        std::string result;
        for (auto i=events_.size()>count ? events_.size()-count : 0; i<events_.size(); ++i)
            result+=events_[i]+'\n';
        return result;
    }
    std::string snapshot() const {
        std::lock_guard lock(mutex_);
        std::string result=header();
        result+="run_started_unix_seconds="+std::to_string(run_started_)+"\n";
        constexpr std::size_t kSnapshotBytes=128*1024;
        const auto first_recent=sequence_-static_cast<unsigned>(events_.size())+1;
        std::size_t from=0;
        while(from<events_.size() && first_recent+from<=startup_.size()) ++from;
        std::size_t bytes=result.size()+64;
        for(const auto& line:startup_) bytes+=line.size()+1;
        for(std::size_t i=from;i<events_.size();++i) bytes+=events_[i].size()+1;
        while(bytes>kSnapshotBytes && from<events_.size()) {bytes-=events_[from].size()+1;++from;}
        result.reserve(std::min(bytes,kSnapshotBytes));
        for(const auto& line:startup_) result+=line+'\n';
        const auto first_tail=first_recent+from;
        if(first_tail>startup_.size()+1)
            result+="omitted_event_count="+std::to_string(first_tail-startup_.size()-1)+'\n';
        for(std::size_t i=from;i<events_.size();++i) result+=events_[i]+'\n';
        return result;
    }
    struct Batch {std::string header,text;unsigned last=0;};
    Batch since(unsigned cursor) const {
        std::lock_guard lock(mutex_);
        Batch batch{header()+
            "run_started_unix_seconds="+std::to_string(run_started_)+"\n",{},sequence_};
        const auto first_recent=sequence_-static_cast<unsigned>(events_.size())+1;
        for(unsigned i=0;i<startup_.size() && i+1<first_recent;++i)
            if(i+1>cursor) batch.text+=startup_[i]+'\n';
        if(first_recent>std::max<unsigned>(cursor+1,static_cast<unsigned>(startup_.size())+1))
            batch.text+="omitted_event_count="+std::to_string(first_recent-
                std::max<unsigned>(cursor+1,static_cast<unsigned>(startup_.size())+1))+'\n';
        for(unsigned i=0;i<events_.size();++i)
            if(first_recent+i>cursor) batch.text+=events_[i]+'\n';
        return batch;
    }
    bool save(const std::string& path,std::string (*formatter)(const std::string&)=nullptr) const {
        // Serialize saves independently of event and peer locks. A failed SD
        // write must leave the previous complete file and the RAM log usable.
        std::lock_guard lock(file_mutex_);
        const auto raw=snapshot();
        const auto text=formatter ? formatter(raw) : raw;
        const auto temporary=path+".tmp";
        std::ofstream file(temporary,std::ios::trunc|std::ios::binary);
        if (!file) return false;
        file<<text; file.flush();
        const bool flushed=file.good();
        file.close();
        if (!flushed || file.fail()) {std::remove(temporary.c_str());return false;}
        return opennow::storage::ReplaceWithTemporaryFile(temporary,path);
    }
private:
    std::string header() const {
        return std::string(opennow::kNativeAppName)+" native alpha "+version_+" / safe events only\n";
    }
    using Clock=std::chrono::steady_clock;
    const std::string version_;
    const Clock::time_point started_=Clock::now();
    const long long run_started_=std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    mutable std::mutex mutex_,file_mutex_;
    std::deque<std::string> events_,startup_;
    unsigned sequence_=0;
};
} // namespace cloudgenshin
