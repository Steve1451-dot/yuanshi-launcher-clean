#include "stream/DecodeBufferPool.hpp"
#include <cassert>
int main() {
    using opennow::video::DecodeBufferPool;DecodeBufferPool pool;bool reused=false;
    auto first=pool.take(64000,reused);assert(!reused);auto* original=first.data();pool.recycle(std::move(first));
    for(int i=0;i<10000;++i) {
        auto next=pool.take(32000,reused);assert(reused && next.data()==original && next.capacity()>=32000);
        next.assign(32000,0x65);pool.recycle(std::move(next));
        assert(pool.size()==1 && pool.bytes()<=DecodeBufferPool::kMaximumBytes);
    }
    auto large=pool.take(DecodeBufferPool::kMaximumBuffer+1,reused);assert(!reused);
    pool.recycle(std::move(large));assert(pool.size()==1);
    for(int i=0;i<32;++i) {std::vector<std::uint8_t> buffer(512*1024);pool.recycle(std::move(buffer));}
    assert(pool.size()<=DecodeBufferPool::kSlots && pool.bytes()<=DecodeBufferPool::kMaximumBytes);
    pool.clear();assert(pool.size()==0 && pool.bytes()==0);
    auto next=pool.take(32000,reused);assert(!reused && next.capacity()>=32000);
}
