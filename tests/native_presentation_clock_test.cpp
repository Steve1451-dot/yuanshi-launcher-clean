#include "stream/ffmpeg/NativePresentationClock.hpp"
#include <cassert>
int main() {
    using namespace opennow::video;
    NativePresentationClock clock;
    assert(clock.target(0,90000,93000,2,0)==90000);
    auto target=clock.target(16667,93000,93000,2,0);
    assert(DecideFrame(93000,false,0,true,target)==FrameDecision::HoldPrevious);
    target=clock.target(33334,93000,93000,2,0);
    assert(DecideFrame(93000,false,0,true,target)==FrameDecision::Present);
    assert(clock.rebases()==0);
    assert(clock.target(500000,135000,135000,3,0)==135000 && clock.rebases()==1);
    // Same generation during a network stall must not keep resetting time.
    for(int i=1;i<=60;++i) clock.target(500000+i*16667,135000,135000,3,0);
    assert(clock.rebases()==1);
    assert(clock.target(1516687,136500,136500,4,0)==136500 && clock.rebases()==2);
    assert(clock.target(1510000,138000,138000,5,0)==138000 && clock.rebases()==3);
    // A source/decoder reset with an unrelated RTP epoch cannot freeze display.
    assert(clock.target(1520000,1000,2500,6,1)==1000 && clock.rebases()==3);
    assert(clock.target(1536667,2500,2500,6,1)==2500);
    NativePresentationClock wrapped;
    assert(wrapped.target(1000000,0xffffff00u,0xffffff00u,1,0)==0xffffff00u);
    assert(wrapped.target(1010000,644,644,2,0)==644);
    assert(wrapped.rebases()==0);
    NativePresentationClock hz60;
    for(int i=0;i<600;++i) {
        const auto pts=static_cast<uint32_t>(90000+i*1500);
        const auto now=static_cast<int64_t>(i)*1000000/60;
        const auto due=hz60.target(now,pts,pts+1500,i+2,0);
        assert(DecideFrame(pts,true,pts+1500,i!=0,due)==FrameDecision::Present);
    }
    assert(hz60.rebases()==0);
}
