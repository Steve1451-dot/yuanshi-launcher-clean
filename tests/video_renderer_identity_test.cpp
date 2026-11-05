#include "stream/IVideoRenderer.hpp"
#include "stream/FrameSubmission.hpp"
#include <cassert>
class Renderer:public IVideoRenderer {
public:
    VideoRenderStats stats{};
    bool accept=true;
    void draw(NVGcontext*,int,int,AVFrame*,int) override {if(accept) ++stats.rendered_frames;}
    VideoRenderStats* video_render_stats() override {return &stats;}
    int getFrameColorspace(const AVFrame*) override {return 0;}
    bool isFrameFullRange(const AVFrame*) override {return false;}
};
int main() {
    Renderer renderer;
    renderer.drawLatest(nullptr,1280,720,nullptr,0,7);
    assert(renderer.stats.submitted_generation==7);
    renderer.accept=false;renderer.drawLatest(nullptr,1280,720,nullptr,0,8);
    assert(renderer.stats.submitted_generation==7);
    opennow::video::FrameSubmissionTracker tracker;
    auto r=tracker.observe(7,7);assert(r.fresh && !r.deferred);
    // GPU busy: the offered frame isn't the one actually submitted.
    r=tracker.observe(8,7);assert(!r.fresh && r.deferred);
    // Retry succeeds even though the display queue now labels it reused.
    r=tracker.observe(8,8);assert(r.fresh && !r.deferred);
    r=tracker.observe(8,8);assert(!r.fresh && !r.deferred);
}
