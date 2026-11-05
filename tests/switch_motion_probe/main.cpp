#include "mihoyo/switch_motion_sampler.hpp"
#include "mihoyo/game_control.hpp"
#include "mihoyo/input_packet.hpp"
#include "mihoyo/gyro_stick_assist.hpp"
#include "mihoyo/genshin_input_experiment.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

using namespace cloudgenshin;

static std::int64_t MonotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW", 0777);
    auto* log = std::fopen("sdmc:/switch/SwitchNOW/motion-probe.log", "w");
    if (!log) return 2;
    std::fprintf(log, "MOTION_PROBE_BEGIN no_account_no_cloud_transport=1 synthetic_feedback=1\n");
    auto config=input::DefaultControllerMapping("hk4e");config.gyro_assist=true;
    const std::string home="sdmc:/switch/SwitchNOW";
    const bool stored=input::SaveControllerMapping(home,"hk4e",config) &&
        input::SaveControllerMapping(home,"endfield",input::DefaultControllerMapping("endfield"));
    const auto restored=input::LoadControllerMapping(home,"hk4e");
    const bool persistence=stored && restored.restored && restored.value==config &&
        !input::LoadControllerMapping(home,"endfield").value.gyro_assist;
    std::fprintf(log,"MAPPING_SD_ROUNDTRIP pass=%u profiles_isolated=1\n",persistence);
    // Credential-free SD test: the diagnostic harness refuses account vaults.
    // Exercise the same new preference and rollback code as the production UI.
    bool experiment_store=input::SaveGenshinInputMode(home,input::GenshinInputMode::AndroidPs5);
    const auto mode=input::LoadGenshinInputMode(home);
    const auto identity=input::ResolveSessionInputIdentity(input::ScopedGenshinInputMode("hk4e",mode),{10,0x045e,0x028e});
    experiment_store=experiment_store && mode==input::GenshinInputMode::AndroidPs5 &&
        identity.terminal==3 && identity.vendor==0x054c && identity.product==0x0ce6 &&
        !input::AndroidPs5Experiment("endfield",mode) && !input::UseLocalGyroAssist(true,true);
    experiment_store=input::SaveGenshinInputMode(home,input::GenshinInputMode::Standard) && experiment_store;
    experiment_store=experiment_store && input::LoadGenshinInputMode(home)==input::GenshinInputMode::Standard;
    std::fprintf(log,"ANDROID_PS5_PROFILE_SD_ROUNDTRIP pass=%u rolled_back=1 cloud_packets_sent=0\n",experiment_store);
    std::fflush(log);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState handheld{}, player{};
    padInitialize(&handheld, HidNpadIdType_Handheld);
    padInitialize(&player, HidNpadIdType_No1);

    // This standalone probe owns these handles. The production sampler still
    // borrows them, exactly as it borrows Borealis' handles in the real app.
    HidSixAxisSensorHandle handles[4]{};
    bool available[4]{};
    available[0] = R_SUCCEEDED(hidGetSixAxisSensorHandles(
        &handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
    available[1] = R_SUCCEEDED(hidGetSixAxisSensorHandles(
        &handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey));
    available[2] = available[3] = R_SUCCEEDED(hidGetSixAxisSensorHandles(
        &handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
    for (unsigned i = 0; i < 4; ++i) {
        const auto result = available[i] ? hidStartSixAxisSensor(handles[i]) : 1;
        std::fprintf(log, "SENSOR handle=%u available=%u start_result=%u\n", i, available[i], result);
    }
    std::fflush(log);

    input::SwitchMotionSampler sampler;
    input::MotionSendPolicy policy;
    input::GyroStickAssist assist;
    std::uint64_t local_aim=0,local_aim_errors=0;
    std::uint64_t samples = 0, fresh = 0, moving = 0, poses = 0, changed_poses = 0;
    std::uint64_t encoded = 0, encode_errors = 0, blocked = 0, gate_errors = 0, last_sequence = 0;
    std::optional<input::MotionQuaternion> previous_pose;
    float peak_gyro = 0;
    const auto started = MonotonicMs();
    auto next_log = started;
    bool was_enabled = false;
    bool completed = false;
    while (appletMainLoop()) {
        const auto now = MonotonicMs();
        const auto elapsed = now - started;
        if (elapsed >= 24000) {completed = true; break;}
        padUpdate(&handheld);
        padUpdate(&player);
        const bool use_handheld = padIsConnected(&handheld);
        const auto sample = sampler.sample(use_handheld ? 0 : 1, use_handheld ? &handheld : &player);
        ++samples;
        const bool valid = sample.valid && now >= sample.sampled_ms && now - sample.sampled_ms <= 100;
        if (valid && sample.sequence != last_sequence) {
            ++fresh;
            last_sequence = sample.sequence;
            peak_gyro = std::max(peak_gyro, input::MotionLength(sample.gyro));
            if (input::MotionLength(sample.gyro) > 0.01f) ++moving;
            if (sample.orientation) {
                ++poses;
                if (previous_pose && std::abs(input::QuaternionDot(*sample.orientation, *previous_pose)) < 0.999999f)
                    ++changed_poses;
                previous_pose = sample.orientation;
            }
        }
        // Exercise both gates using a clearly labelled LOCAL fixture, never a
        // claim that a cloud pod asked for motion. No packet is transmitted.
        const bool enabled = elapsed >= 4000 && elapsed < 20000;
        if (enabled != was_enabled) {
            const std::vector<std::uint8_t> feedback{8, 23, 122, 2, 8, static_cast<std::uint8_t>(enabled)};
            const auto decoded = control::DecodeImuFeedback(feedback);
            if (!decoded || *decoded != enabled) ++gate_errors;
            policy.enable(decoded.value_or(false));
            was_enabled = enabled;
        }
        const bool admitted = policy.admit(sample, now, 16);
        if (!enabled && valid && !admitted) ++blocked;
        if (!enabled && admitted) ++gate_errors;
        if (admitted) {
            const auto frame = input::EncodeInputPacket(input::EncodeImuChannel(sample));
            if (input::IsValidControlPacket(frame)) {++encoded; policy.sent(sample);}
            else ++encode_errors;
        }
        input::SwitchControllerState aim_fixture;aim_fixture.zl=true;
        const auto assisted=assist.apply(config,aim_fixture,sample,now,enabled);
        if(assist.active()) ++local_aim;
        if((!enabled || !valid) && (assisted.right_x || assisted.right_y)) ++local_aim_errors;
        if (now >= next_log) {
            const unsigned sensor = use_handheld ? 0 : (padGetStyleSet(&player) & HidNpadStyleTag_NpadJoyDual) ? 2 : 1;
            HidSixAxisSensorState raw{};
            const auto count = hidGetSixAxisSensorStates(handles[sensor], &raw, 1);
            std::fprintf(log, "RAW sensor=%u count=%zu attributes=%u sampling=%" PRIu64 " accel_g_length=%.4f gyro_turns_length=%.4f\n",
                sensor, count, raw.attributes, static_cast<std::uint64_t>(raw.sampling_number),
                input::MotionLength({raw.acceleration.x, raw.acceleration.y, raw.acceleration.z}),
                input::MotionLength({raw.angular_velocity.x, raw.angular_velocity.y, raw.angular_velocity.z}));
            std::fprintf(log, "SNAPSHOT t_ms=%lld source=%d connected=%u style=%u samples=%" PRIu64 " fresh=%" PRIu64 " moving=%" PRIu64 " poses=%" PRIu64 " pose_changes=%" PRIu64 " local_encoded=%" PRIu64 " encode_errors=%" PRIu64 " gate_off_blocked=%" PRIu64 " gate_errors=%" PRIu64 " peak_gyro_rad_s=%.4f\n",
                static_cast<long long>(elapsed), use_handheld ? 0 : 1,
                padIsConnected(use_handheld ? &handheld : &player),
                padGetStyleSet(use_handheld ? &handheld : &player),
                samples, fresh, moving, poses, changed_poses, encoded, encode_errors, blocked, gate_errors, peak_gyro);
            std::fflush(log);
            next_log = now + 1000;
        }
        svcSleepThread(16'000'000);
    }
    const bool passed = completed && persistence && experiment_store && local_aim>100 && !local_aim_errors &&
        fresh > 100 && moving > 100 && poses > 100 && changed_poses > 10 &&
        encoded > 100 && blocked > 100 && !encode_errors && !gate_errors;
    std::fprintf(log,"LOCAL_GYRO_ASSIST composed=%" PRIu64 " gate_errors=%" PRIu64 " cloud_packets_sent=0\n",local_aim,local_aim_errors);
    std::fprintf(log, "MOTION_PROBE_DONE pass=%u fresh=%" PRIu64 " moving=%" PRIu64 " poses=%" PRIu64 " pose_changes=%" PRIu64 " local_encoded=%" PRIu64 " encode_errors=%" PRIu64 " gate_off_blocked=%" PRIu64 " gate_errors=%" PRIu64 " cloud_packets_sent=0\n",
        passed, fresh, moving, poses, changed_poses, encoded, encode_errors, blocked, gate_errors);
    std::fclose(log);
    for (unsigned i = 0; i < 4; ++i) if (available[i]) hidStopSixAxisSensor(handles[i]);
    return passed ? 0 : 1;
}
