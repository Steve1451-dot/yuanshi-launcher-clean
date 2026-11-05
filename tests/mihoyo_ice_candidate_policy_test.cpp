#include "mihoyo/ice_candidate_policy.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::rtc;
    assert(ClassifyCandidate("candidate:1 1 udp 123 192.0.2.1 1234 typ host")==CandidateKind::Udp);
    assert(ClassifyCandidate("a=candidate:1 1 UDP 123 2001:db8::1 1234 typ relay\r\n")==CandidateKind::Udp);
    assert(ClassifyCandidate("candidate:1 1 tcp 123 192.0.2.1 9 typ host tcptype passive")==CandidateKind::Tcp);
    assert(ClassifyCandidate("candidate:1 2 udp 123 192.0.2.1 1234 typ host")==CandidateKind::Component);
    assert(ClassifyCandidate("candidate:1 1 udp 123 192.0.2.1 1234 typ prflx")==CandidateKind::Type);
    assert(ClassifyCandidate("candidate:1 1 udp 123 192.0.2.1 70000 typ host")==CandidateKind::Invalid);
    assert(ClassifyCandidate("candidate:123456789012345678901234567890123 1 udp 1 192.0.2.1 9 typ host")==CandidateKind::Invalid);
    assert(ClassifyCandidate("candidate:1 1 udp")==CandidateKind::Invalid);
}
