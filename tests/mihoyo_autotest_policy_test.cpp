#include "mihoyo/autotest_policy.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>

int main() {
    using namespace cloudgenshin;
    AutoTestPolicy disabled(false,0);assert(disabled.poll(999999,true,false,false)==AutoTestAction::None);
    AutoTestPolicy login(true,0);
    assert(login.poll(1,false,true,false)==AutoTestAction::None);
    assert(login.poll(2,true,true,false)==AutoTestAction::None);
    assert(login.poll(3,true,false,false)==AutoTestAction::LaunchOnce);
    assert(login.poll(4,true,true,false)==AutoTestAction::None);
    assert(login.poll(5,true,false,false)==AutoTestAction::Finished);
    assert(login.poll(6,true,false,false)==AutoTestAction::None);
    AutoTestPolicy wait(true,0);assert(wait.poll(180000,false,true,false)==AutoTestAction::CancelTimeout);
    assert(wait.poll(180001,true,true,true)==AutoTestAction::None);
    assert(wait.poll(180002,true,false,true)==AutoTestAction::Finished);
    assert(wait.poll(180003,true,false,false)==AutoTestAction::None);
    AutoTestPolicy queue(true,0);assert(queue.poll(100,true,false,false)==AutoTestAction::LaunchOnce);
    assert(queue.poll(1800099,true,true,false)==AutoTestAction::None);
    assert(queue.poll(1800100,true,true,false)==AutoTestAction::CancelTimeout);
    AutoTestPolicy cancel(true,0);assert(cancel.poll(1,true,false,true)==AutoTestAction::Finished);
    assert(cancel.poll(2,true,false,false)==AutoTestAction::None);
    AutoTestPolicy stop(true,0);stop.stop();assert(stop.poll(1,true,false,false)==AutoTestAction::None);
    AutoTestPolicy handoff(true,0);
    assert(handoff.poll(1,true,false,false)==AutoTestAction::LaunchOnce);
    // Worker became idle after the UI's startup snapshot. The caller keeps
    // this frame busy, then consumes the prepared stream and stops the policy.
    const bool frame_working=true,worker_active=false;
    assert(handoff.poll(2,true,frame_working || worker_active,false)==AutoTestAction::None);
    handoff.stop();assert(handoff.poll(3,true,false,false)==AutoTestAction::None);
    const auto path=std::filesystem::temp_directory_path()/
        ("cg-autotest-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(!AutoTestEnabled(path.string()));
    assert(AutoTestStreamSeconds(path.string())==170);
    assert(!AutoTestUiProbeEnabled(path.string()));
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_SOAK_600_V1\n";}
    assert(AutoTestStreamSeconds(path.string())==600);
    assert(!AutoTestEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"extra\n";}
    assert(AutoTestStreamSeconds(path.string())==170);
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_UI_PROBE_V1\n";}
    assert(AutoTestUiProbeEnabled(path.string()));
    assert(!AutoTestEnabled(path.string()) && AutoTestStreamSeconds(path.string())==170);
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"extra\n";}
    assert(!AutoTestUiProbeEnabled(path.string()));
    assert(AutoTestUiProbePhase(-1)==0 && AutoTestUiProbePhase(99999)==0);
    assert(AutoTestUiProbePhase(100000)==1 && AutoTestUiProbePhase(102999)==1);
    assert(AutoTestUiProbePhase(103000)==2 && AutoTestUiProbePhase(132999)==11);
    assert(AutoTestUiProbePhase(133000)==12 && AutoTestUiProbePhase(999999999)==12);
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_AUTOTEST_V1\n";}
    assert(AutoTestEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"unknown-command\n";}
    assert(!AutoTestEnabled(path.string()));
    assert(!AutoTestEntryClickEnabled(path.string()));
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_ENDFIELD_ALT_NODE_V1\n";}
    assert(AutoTestEndfieldAlternateNodeEnabled(path.string()));
    assert(!AutoTestEnabled(path.string()) && !AutoTestEntryClickEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"extra\n";}
    assert(!AutoTestEndfieldAlternateNodeEnabled(path.string()));
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_PASSPORT_LOGIN_V1\n";}
    assert(AutoTestPassportLoginEnabled(path.string()));
    assert(!AutoTestEnabled(path.string()) && !AutoTestEntryClickEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"extra\n";}
    assert(!AutoTestPassportLoginEnabled(path.string()));
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_ENTRY_CLICK_V1\n";}
    assert(AutoTestEntryClickEnabled(path.string()));
    assert(!AutoTestEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"x\n";}
    assert(!AutoTestEntryClickEnabled(path.string()));
    std::filesystem::remove(path);
    {std::ofstream file(path,std::ios::binary);file<<"CGNX_ENDFIELD_CONFIRM_V1\n";}
    assert(AutoTestEndfieldConfirmEnabled(path.string()));
    assert(!AutoTestEnabled(path.string()) && !AutoTestEntryClickEnabled(path.string()));
    {std::ofstream file(path,std::ios::app|std::ios::binary);file<<"extra\n";}
    assert(!AutoTestEndfieldConfirmEnabled(path.string()));
    std::filesystem::remove(path);
    const auto long_home=path.string()+"-longqueue";
    assert(std::filesystem::create_directory(long_home));
    const auto long_marker=long_home+"/cloudgenshin-autotest.longqueue";
    const auto test_marker=long_home+"/cloudgenshin-autotest.txt";
    const auto pointer_marker=long_home+"/cloudgenshin-autotest.star-rail-pointer";
    const auto refresh_marker=long_home+"/cloudgenshin-autotest.star-rail-sdk-refresh";
    const auto web_marker=long_home+"/cloudgenshin-autotest.star-rail-web-resume";
    const auto game_token_marker=long_home+"/cloudgenshin-autotest.star-rail-game-token";
    {std::ofstream file(game_token_marker,std::ios::binary);file<<"CGNX_HKRPG_GAME_TOKEN_V1\n";}
    assert(!AutoTestStarRailWebGameTokenEnabled(long_home,"8"));
    {std::ofstream file(web_marker,std::ios::binary);file<<"CGNX_HKRPG_WEB_RESUME_V1\n";}
    assert(!AutoTestStarRailWebResumeEnabled(long_home,"8"));
    {std::ofstream file(refresh_marker,std::ios::binary);file<<"CGNX_HKRPG_SDK_REFRESH_V1\n";}
    assert(!AutoTestStarRailFreshSdkLoginEnabled(long_home,"8"));
    {std::ofstream file(pointer_marker,std::ios::binary);file<<"CGNX_HKRPG_POINTER_V1\n";}
    assert(!AutoTestStarRailPointerEnabled(long_home,"8"));
    assert(!AutoTestLongNormalQueueEnabled(long_home));
    {std::ofstream file(long_marker,std::ios::binary);file<<"CGNX_LONG_QUEUE_30M_V1\n";}
    assert(!AutoTestLongNormalQueueEnabled(long_home)); // Long marker alone cannot opt in.
    {std::ofstream file(test_marker,std::ios::binary);file<<"CGNX_AUTOTEST_V1\n";}
    assert(AutoTestStarRailWebResumeEnabled(long_home,"8"));
    assert(AutoTestStarRailWebGameTokenEnabled(long_home,"8"));
    assert(!AutoTestStarRailWebGameTokenEnabled(long_home,"4"));
    assert(!AutoTestStarRailWebResumeEnabled(long_home,"4"));
    {std::ofstream file(web_marker,std::ios::app|std::ios::binary);file<<"extra";}
    assert(!AutoTestStarRailWebResumeEnabled(long_home,"8"));
    assert(!AutoTestStarRailWebGameTokenEnabled(long_home,"8"));
    assert(AutoTestLongNormalQueueEnabled(long_home));
    assert(AutoTestStarRailPointerEnabled(long_home,"8"));
    assert(AutoTestStarRailFreshSdkLoginEnabled(long_home,"8"));
    for(const auto app:{"4","12","","08"}) assert(!AutoTestStarRailFreshSdkLoginEnabled(long_home,app));
    for(const auto& value:{std::string("CGNX_HKRPG_SDK_REFRESH_V1\nextra"),std::string("CGNX_HKRPG_SDK_REFRESH_V1\r\n"),std::string(49,'x')}) {
        {std::ofstream file(refresh_marker,std::ios::binary);file<<value;}
        assert(!AutoTestStarRailFreshSdkLoginEnabled(long_home,"8"));
    }
    for(const auto app:{"4","12","","08"}) assert(!AutoTestStarRailPointerEnabled(long_home,app));
    for(const auto& value:{std::string("CGNX_HKRPG_POINTER_V1\nextra"),std::string("CGNX_HKRPG_POINTER_V1\r\n"),std::string(49,'x')}) {
        {std::ofstream file(pointer_marker,std::ios::binary);file<<value;}
        assert(!AutoTestStarRailPointerEnabled(long_home,"8"));
    }
    for(const auto& value:{std::string("CGNX_LONG_QUEUE_30M_V2\n"),std::string("CGNX_LONG_QUEUE_30M_V1\r\n"),
        std::string("CGNX_LONG_QUEUE_30M_V1\nextra"),std::string(49,'x')}) {
        {std::ofstream file(long_marker,std::ios::binary);file<<value;}
        assert(!AutoTestLongNormalQueueEnabled(long_home));
    }
    {std::ofstream file(long_marker,std::ios::binary);file<<"CGNX_LONG_QUEUE_30M_V1\n";}
    {std::ofstream file(test_marker,std::ios::binary);file<<"CGNX_AUTOTEST_V1\nextra";}
    assert(!AutoTestLongNormalQueueEnabled(long_home));
    std::filesystem::remove_all(long_home); // Fresh, unique test-owned directory only.
}
