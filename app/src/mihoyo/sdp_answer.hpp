#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace cloudgenshin::rtc {
struct NegotiatedAnswer {
    std::string sdp;
    int video_payload=-1, audio_payload=-1;
};
enum class AnswerIssue { None=0, InvalidDescription=1, LocalTransport=2,
    MediaLayout=3, Bundle=4, DataTransport=5, MediaTransport=6, Codec=7 };
// Bounded receive-only H264/Opus + SCTP answer. Local ICE credentials and
// certificate fingerprint come exclusively from libpeer's local description.
std::optional<NegotiatedAnswer> NegotiateAnswer(std::string_view offer,std::string_view local,AnswerIssue* issue=nullptr);
}
