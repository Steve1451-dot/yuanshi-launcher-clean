#pragma once

#include <nanovg.h>
#include <cstdint>

#define COLORSPACE_REC_601 1
#define COLORSPACE_REC_709 2
#define COLORSPACE_REC_2020 3
#define COLOR_RANGE_LIMITED 1
#define COLOR_RANGE_FULL 2

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

struct AVFrame;

struct VideoRenderStats {
    // NOT TO USE, INTERMEDIATE VALUES
    uint32_t rendered_frames;
    uint64_t total_render_time;

    float rendered_fps;
    float rendering_time;

    uint64_t measurement_start_timestamp;
    uint32_t retained_surfaces;
    uint32_t surface_mappings;
    // Last frame actually submitted by this renderer (not merely offered by
    // the display queue). This is not physical screen/GPU completion proof.
    uint64_t submitted_generation = 0;
    uint64_t command_busy=0,upload_busy=0,hardware_ref_failed=0,mapping_failed=0;
};

class IVideoRenderer {
  public:
    virtual ~IVideoRenderer(){};
    virtual void draw(NVGcontext* vg, int width, int height,
                      AVFrame* frame, int imageFormat) = 0;
    virtual void drawLatest(NVGcontext* vg, int width, int height,
                            AVFrame* frame, int imageFormat, uint64_t generation) {
        const auto before=video_render_stats()->rendered_frames;
        draw(vg, width, height, frame, imageFormat);
        if(video_render_stats()->rendered_frames!=before)
            video_render_stats()->submitted_generation=generation;
    }
    virtual VideoRenderStats* video_render_stats() = 0;
    // Before first draw. Native clients favor frame cadence over optional
    // multi-tap denoise/sharpen; other renderers may ignore this preference.
    virtual void setLowCostPresentation(bool) {}

    // Default implementations
    virtual int getDecoderColorspace() {
        // Rec 601 is default
        return COLORSPACE_REC_601;
    }

    virtual int getDecoderColorRange() {
        // Limited is the default
        return COLOR_RANGE_LIMITED;
    }

    virtual int getFrameColorspace(const AVFrame* frame) = 0;

    virtual bool isFrameFullRange(const AVFrame* frame) = 0;
};
