#pragma once
#include <cstdint>
namespace opennow::video {
class FrameSubmissionTracker {
public:
    struct Result {bool fresh;bool deferred;};
    Result observe(std::uint64_t offered,std::uint64_t submitted) {
        const bool fresh=submitted!=last_;last_=submitted;
        return {fresh,offered!=submitted};
    }
private:
    std::uint64_t last_=0;
};
}
