#include "endfield/stream_settings.hpp"
#include "mihoyo/controller_capture_policy.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main() {
    using namespace cloudgenshin::endfield;
    const auto path=(std::filesystem::temp_directory_path()/
        ("endfield-stream-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    auto settings=LoadStreamSettings(path);
    assert(!settings.valid && settings.tier==2 && settings.fps==60);
    assert(kStreamBitrates[settings.tier].minimum==3000 && kStreamBitrates[settings.tier].maximum==4000);
    assert(kDispatchFps==60 && kDefaultStreamFps==60);
    assert(!SaveStreamSettings(path,-1,30) && !SaveStreamSettings(path,3,30));
    assert(!SaveStreamSettings(path,2,0) && !SaveStreamSettings(path,2,120));
    assert(!std::filesystem::exists(path));
    assert(SaveStreamSettings(path,2,30));
    settings=LoadStreamSettings(path);assert(settings.valid && settings.tier==2 && settings.fps==30);
    assert(SaveStreamSettings(path,0,60));
    settings=LoadStreamSettings(path);assert(settings.valid && settings.tier==0 && settings.fps==60);
    // Recover a valid backup left by an interrupted replacement. A successful
    // atomic replacement normally removes its temporary backup.
    std::filesystem::copy_file(path,path+".bak");
    {std::ofstream file(path,std::ios::trunc);file<<"99 120";}
    settings=LoadStreamSettings(path);assert(settings.valid && settings.tier==0 && settings.fps==60);
    std::filesystem::remove(path+".bak");
    for(const auto* invalid:{"", "0", "3 30", "2 120", "2 30 trailing", "2 30 0"}) {
        {std::ofstream file(path,std::ios::trunc);file<<invalid;}
        settings=LoadStreamSettings(path);assert(!settings.valid && settings.tier==2 && settings.fps==60);
    }
    {std::ofstream file(path,std::ios::trunc);file<<"0 60"<<std::string(1000,' ');}
    settings=LoadStreamSettings(path);assert(!settings.valid && settings.tier==2 && settings.fps==60);
    {std::ofstream file(path,std::ios::trunc);file<<"0 30";}
    settings=LoadStreamSettings(path);assert(settings.valid && settings.tier==2 && settings.fps==60);
    assert(SaveStreamSettings(path,1,30));
    settings=LoadStreamSettings(path);assert(settings.valid && settings.tier==1 && settings.fps==30);
    std::filesystem::remove(path);

    const auto commands=BitrateCommands(2,0);
    assert(commands[0].cmd=="ResetVideoBitrate" && commands[0].arg=="4000");
    assert(commands[1].cmd=="autoBitrateAdjust" && commands[1].arg=="3000");
    const auto lower_first=BitrateCommands(2,4000);
    assert(lower_first[0].cmd=="autoBitrateAdjust" && lower_first[1].cmd=="ResetVideoBitrate");
    for(int tier=0;tier<3;++tier) for(const auto& cmd:BitrateCommands(tier,0)) {
        const auto parsed=SimpleCmd::Parse(cmd.Serialize());
        assert(parsed && parsed->cmd==cmd.cmd && parsed->arg==cmd.arg && !parsed->data && !parsed->arg1);
    }
    assert(BitrateCommands(-1,0)[0].cmd.empty() && BitrateCommands(3,0)[0].cmd.empty());
    assert(BitrateRangeCommands({1000,3000},1000)[0].arg=="3000");
    assert(BitrateRangeCommands({1000,1000},4000)[0].cmd=="autoBitrateAdjust");
    assert(BitrateRangeCommands({0,3000},0)[0].cmd.empty());
    assert(BitrateRangeCommands({4000,3000},0)[0].cmd.empty());
    assert(BitrateRangeCommands({1000,12001},0)[0].cmd.empty());
    int previous_minimum=4000,writes=0;
    auto send=[&](const auto& bytes) {
        const auto packet=SimpleCmd::Parse(bytes);assert(packet);
        assert(packet->cmd==(writes%2==0 ? "autoBitrateAdjust" : "ResetVideoBitrate"));
        return ++writes!=2;
    };
    assert(!SendBitrateTier(2,previous_minimum,send) && previous_minimum==4000 && writes==2);
    assert(SendBitrateTier(2,previous_minimum,send) && previous_minimum==3000 && writes==4);
    assert(!SendBitrateTier(3,previous_minimum,send) && writes==4);
    for(const auto fps:{30,60}) {
        const auto parsed=SimpleCmd::Parse(StreamFpsFrame(fps));
        assert(parsed && parsed->cmd=="SetFps" && parsed->arg==std::to_string(fps) && !parsed->data);
    }
    assert(StreamFpsFrame(0).empty() && StreamFpsFrame(120).empty());

    cloudgenshin::overlay::SettingRequest request;
    SettingSendBudget budget;
    request.request(30);const auto old=*request.pending();
    for(int attempt=0;attempt<20;++attempt) {
        assert(budget.try_write(old,attempt*500));
        assert(!budget.try_write(old,attempt*500+1));
    }
    assert(budget.exhausted() && !budget.try_write(old,100000));
    request.complete(old,false);request.request(60);
    const auto next=*request.pending();assert(budget.try_write(next,100001) && !budget.exhausted());
    request.complete(old,true);assert(request.pending()->value()==60);
    request.complete(next,true);assert(!request.pending());

    // Shared touch ball / generation capture: opening is touch-only, while
    // L+R stay gameplay buttons. A menu press cannot leak on close.
    using namespace cloudgenshin;
    overlay::FloatingBall ball;const overlay::Rect bounds{0,0,1280,720};
    input::ControllerCapture capture;input::SwitchControllerState raw;
    capture.update(raw,0,false);
    raw.l=raw.r=true;
    auto frame=capture.update(raw,0,false);assert(frame.state.l && frame.state.r);
    const auto centre=ball.centre(bounds);
    assert(!ball.update(true,1,centre,bounds) && ball.captured());
    const auto generation=capture.arm().generation;
    frame=capture.update(raw,0,true);assert(!frame.state.l && !frame.state.r);
    assert(ball.update(false,0,{},bounds));
    raw.a=true;capture.update(raw,0,true);capture.arm();
    frame=capture.update(raw,0,false);assert(!frame.state.a && frame.generation>generation);
    raw={};capture.update(raw,0,false);raw.a=true;
    assert(capture.update(raw,0,false).state.a);
}
