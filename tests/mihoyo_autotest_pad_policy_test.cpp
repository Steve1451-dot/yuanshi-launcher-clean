#include "mihoyo/autotest_pad_policy.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>

int main() {
    using namespace cloudgenshin;
    AutoTestPadProbe probe;
    assert(probe.sample(999999).phase==0);
    probe.arm(1000);probe.arm(9000); // First game-enter only, no rearming.
    assert(probe.sample(999).phase==0 && probe.sample(15999).phase==0);
    assert(probe.sample(16000).state.right_x==20000);
    assert(probe.sample(18000).phase==2);
    assert(probe.sample(21000).state.plus && probe.sample(21199).state.plus);
    assert(!probe.sample(21200).state.plus && probe.sample(21200).phase==4);
    assert(probe.sample(27000).phase==5 && !probe.sample(27000).state.a && !probe.sample(27000).state.b);
    assert(probe.sample(27200).phase==6 && !probe.sample(27200).state.plus);
    const auto path=std::filesystem::temp_directory_path()/
        ("cg-pad-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(!AutoTestPadEnabled(path.string()));
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_PAD_PROBE_V1\n";}
    assert(AutoTestPadEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"extra\n";}
    assert(!AutoTestPadEnabled(path.string()));
    std::filesystem::remove(path);
}
