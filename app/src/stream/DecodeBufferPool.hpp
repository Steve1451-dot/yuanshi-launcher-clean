#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace opennow::video {
// Caller holds the decode queue lock. Reuse compressed payload storage, not
// decoded/GPU surfaces. Strict retained-memory budget; large IDRs aren't cached.
class DecodeBufferPool {
public:
    static constexpr std::size_t kSlots=8,kMaximumBuffer=512*1024,kMaximumBytes=2*1024*1024;
    std::vector<std::uint8_t> take(std::size_t size,bool& reused) {
        std::vector<std::uint8_t> result;
        std::size_t selected=count_;
        for(std::size_t i=0;i<count_;++i)
            if(buffers_[i].capacity()>=size && (selected==count_ || buffers_[i].capacity()<buffers_[selected].capacity())) selected=i;
        if(selected<count_) {
            result=std::move(buffers_[selected]);bytes_-=result.capacity();
            if(selected+1<count_) buffers_[selected]=std::move(buffers_[count_-1]);
            --count_;
        }
        reused=result.capacity()>=size;result.reserve(size);return result;
    }
    void recycle(std::vector<std::uint8_t>&& buffer) {
        const auto capacity=buffer.capacity();
        if(!capacity || capacity>kMaximumBuffer || count_==kSlots || capacity>kMaximumBytes-bytes_) return;
        buffer.clear();bytes_+=capacity;buffers_[count_++]=std::move(buffer);
    }
    void clear() {
        for(auto& buffer:buffers_) std::vector<std::uint8_t>().swap(buffer);
        count_=bytes_=0;
    }
    std::size_t size() const {return count_;}
    std::size_t bytes() const {return bytes_;}
private:
    std::array<std::vector<std::uint8_t>,kSlots> buffers_;
    std::size_t count_=0,bytes_=0;
};
}
