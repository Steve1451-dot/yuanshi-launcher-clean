#include "endfield/vlink_proto.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cctype>
#include <cstddef>
#include <vector>

namespace cloudgenshin::endfield {
namespace {

using Json = nlohmann::json;

std::string JsonText(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned())
        return std::to_string(value.get<unsigned long long>());
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (number == static_cast<double>(static_cast<long long>(number)))
            return std::to_string(static_cast<long long>(number));
        return std::to_string(number);
    }
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    return {};
}

std::optional<long long> JsonInt(const Json& value) {
    if (value.is_number_integer()) return value.get<long long>();
    if (value.is_number_unsigned())
        return static_cast<long long>(value.get<unsigned long long>());
    if (value.is_number_float()) return static_cast<long long>(value.get<double>());
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool IsDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

// Presence-only fingerprint of a packedData object. The values are credentials
// and are never inspected or logged; only which ClientFirstOnline fields exist.
std::string JsonDumpOrEmpty(const Json& value) {
    return value.is_null() ? std::string("\"\"") : value.dump();
}

// JS Number()/truthiness for sdk_msg.playerRole. Number("0") is 0 but the
// string itself is truthy, which is why the official page still builds
// playerInfo and then emits an empty SDPInfo.user.r.
void ReadPlayerRole(const Json& root, VlinkSdkData& data) {
    const auto role = root.find("playerRole");
    if (role == root.end() || role->is_null()) return;
    bool truthy = false;
    bool numeric = false;
    long long number = 0;
    if (role->is_string()) {
        const std::string text = role->get<std::string>();
        truthy = !text.empty();
        try {
            std::size_t used = 0;
            number = std::stoll(text, &used);
            numeric = used == text.size();
        } catch (...) {
            numeric = false;
        }
    } else if (role->is_number_integer() || role->is_number_unsigned()) {
        number = JsonInt(*role).value_or(0);
        truthy = number != 0;
        numeric = true;
    } else if (role->is_number_float()) {
        const double value = role->get<double>();
        truthy = value != 0.0;
        number = static_cast<long long>(value);
        numeric = true;
    } else if (role->is_boolean()) {
        const bool value = role->get<bool>();
        truthy = value;
        number = value ? 1 : 0;
        numeric = true;
    }
    data.player_info_valid = truthy;
    data.player_role_number = numeric ? number : 0;
}

unsigned PackedKeyMask(const Json& value) {
    if (!value.is_object()) return 0;
    unsigned mask = 0;
    unsigned bit = 1;
    for (const char* key : {"video", "gsm_id", "token", "gsm_token", "device_uuid",
                            "client_ver", "account_id", "sdk_info", "game_id_str",
                            "instance_id", "cmd_line"}) {
        if (value.find(key) != value.end()) mask |= bit;
        bit <<= 1;
    }
    return mask;
}

// One octet of the official IPv4 regex: "0" or [1-9]\d{0,2} with value <= 255.
std::size_t OctetLengthAt(const std::string& text, std::size_t start) {
    if (start >= text.size() || !IsDigit(text[start])) return 0;
    if (text[start] == '0' && (start + 1 >= text.size() || !IsDigit(text[start + 1])))
        return 1;
    if (text[start] == '0') return 0;
    std::size_t pos = start;
    int value = 0;
    int digits = 0;
    while (digits < 3 && pos < text.size() && IsDigit(text[pos])) {
        value = value * 10 + (text[pos] - '0');
        ++digits;
        ++pos;
    }
    if (value > 255) return 0;
    // A following digit means this run cannot be a whole octet.
    if (pos < text.size() && IsDigit(text[pos])) return 0;
    return pos - start;
}

// Match of the official IPv4 regex at `start`, or 0.
std::size_t Ipv4LengthAt(const std::string& text, std::size_t start) {
    std::size_t pos = start;
    for (int octet = 0; octet < 4; ++octet) {
        const auto length = OctetLengthAt(text, pos);
        if (length == 0) return 0;
        pos += length;
        if (octet < 3) {
            if (pos >= text.size() || text[pos] != '.') return 0;
            ++pos;
        }
    }
    return pos - start;
}

std::string ReplaceAllIpv4(const std::string& line, const std::string& ip) {
    std::string out;
    out.reserve(line.size());
    std::size_t pos = 0;
    while (pos < line.size()) {
        const std::size_t length = IsDigit(line[pos]) ? Ipv4LengthAt(line, pos) : 0;
        if (length > 0) {
            out += ip;
            pos += length;
        } else {
            out.push_back(line[pos]);
            ++pos;
        }
    }
    return out;
}

}  // namespace

std::optional<VlinkSdkData> ParseVlinkSdkMsg(const std::string& sdk_msg_json) {
    const Json root = Json::parse(sdk_msg_json, nullptr, false);
    if (!root.is_object()) return std::nullopt;

    VlinkSdkData data;
    int line_port = 0;
    const auto take_line = [&](const Json& entry) {
        if (!entry.is_object()) return;
        data.gs_ip = JsonText(entry.value("lineIp", Json()));
        if (const auto port = entry.find("port"); port != entry.end()) {
            if (const auto value = JsonInt(*port); value && *value > 0)
                line_port = static_cast<int>(*value);
        }
    };
    const auto gs_ip = root.find("gsIp");
    if (gs_ip != root.end()) {
        if (gs_ip->is_array()) {
            // Multi-line: [{lineIp, port, type}] — the first entry wins.
            for (const auto& entry : *gs_ip) {
                take_line(entry);
                break;
            }
        } else if (gs_ip->is_string()) {
            // The official client stores the multi-line list as a JSON string
            // and checks for "type" before JSON.parse (player module @1976029).
            const std::string text = gs_ip->get<std::string>();
            if (!text.empty() && text.front() == '[') {
                const Json lines = Json::parse(text, nullptr, false);
                if (lines.is_array()) {
                    for (const auto& entry : lines) {
                        take_line(entry);
                        break;
                    }
                } else {
                    data.gs_ip = text;
                }
            } else {
                data.gs_ip = text;
            }
        }
    }
    if (data.gs_ip.empty()) {
        if (const auto ip = root.find("ip"); ip != root.end() && ip->is_string())
            data.gs_ip = ip->get<std::string>();
    }
    // Official precedence: webRtcPort, then gsPort (legacy GS), then the port
    // carried by the selected multi-line entry.
    bool port_found = false;
    for (const char* key : {"webRtcPort", "gsPort"}) {
        const auto value = root.find(key);
        if (value == root.end()) continue;
        if (const auto port = JsonInt(*value); port && *port > 0) {
            data.web_rtc_port = static_cast<int>(*port);
            port_found = true;
            break;
        }
    }
    if (!port_found && line_port > 0) data.web_rtc_port = line_port;
    data.gsm_token = JsonText(root.value("gsmToken", Json()));
    data.user_id = JsonText(root.value("userId", Json()));
    data.player_id = JsonText(root.value("playerId", Json()));
    data.player_role = JsonText(root.value("playerRole", Json()));
    data.gsm_token_json = JsonDumpOrEmpty(root.value("gsmToken", Json()));
    data.user_id_json = JsonDumpOrEmpty(root.value("userId", Json()));
    data.player_id_json = JsonDumpOrEmpty(root.value("playerId", Json()));
    ReadPlayerRole(root, data);
    if (const auto codec = JsonInt(root.value("codecType", Json())))
        data.codec_type = static_cast<int>(*codec);
    data.packed_data_kind = "missing";
    if (const auto packed = root.find("packedData"); packed != root.end()) {
        if (packed->is_string()) {
            data.packed_data = packed->get<std::string>();
            data.packed_data_kind = "string";
        } else if (packed->is_object()) {
            // Official Mu/PackedData applies encodeURI to this value. If a
            // dispatch returns an object, the browser's implicit toString is
            // "[object Object]", which no pod can parse; serialize it instead.
            data.packed_data = packed->dump();
            data.packed_data_kind = "object";
            data.packed_data_key_mask = PackedKeyMask(*packed);
        } else if (packed->is_array()) {
            data.packed_data = packed->dump();
            data.packed_data_kind = "array";
        } else if (packed->is_null()) {
            data.packed_data_kind = "null";
        } else {
            data.packed_data = JsonText(*packed);
            data.packed_data_kind = "scalar";
        }
        data.packed_data_length = data.packed_data.size();
    }
    data.auth = JsonText(root.value("auth", Json()));
    data.verify = JsonText(root.value("verify", Json()));
    data.assign_result_param = JsonText(root.value("assignResultParam", Json()));
    data.game_id = JsonText(root.value("gameId", Json()));
    data.instance_id = JsonText(root.value("instanceId", Json()));
    data.node_id = JsonText(root.value("nodeId", Json()));
    data.scheme_type = JsonText(root.value("schemeType", Json()));
    data.gs_ver = JsonText(root.value("gsVer", Json()));
    return data;
}

std::optional<int> ParseServerInputVersion(const std::string& text) {
    if(text.empty() || text.size()>65536) return std::nullopt;
    bool quoted=false,escaped=false;
    int depth=0;
    for(const char c:text) {
        if(quoted) {
            if(escaped) escaped=false;
            else if(c=='\\') escaped=true;
            else if(c=='"') quoted=false;
        } else if(c=='"') quoted=true;
        else if(c=='{' || c=='[') {if(++depth>16) return std::nullopt;}
        else if(c=='}' || c==']') {if(--depth<0) return std::nullopt;}
    }
    const auto root=Json::parse(text,nullptr,false);
    if(!root.is_object()) return std::nullopt;
    const auto functions=root.find("function");
    if(functions==root.end() || !functions->is_object()) return std::nullopt;
    const auto version=functions->find("input_ver");
    if(version==functions->end()) return std::nullopt;
    for(int i=1;i<=3;++i) {
        if((version->is_number() && *version==i) ||
           (version->is_string() && version->get_ref<const std::string&>()==std::to_string(i))) return i;
    }
    return std::nullopt;
}

bool ParseAnswerPayloadTypes(const std::string& sdp, int* video_payload, int* audio_payload) {
    if (video_payload) *video_payload = 0;
    if (audio_payload) *audio_payload = 0;

    bool in_video = false;
    bool in_audio = false;
    std::size_t start = 0;
    while (start <= sdp.size()) {
        std::size_t end = sdp.find('\n', start);
        const std::size_t line_end = (end == std::string::npos) ? sdp.size() : end;
        std::string line = sdp.substr(start, line_end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.compare(0, 8, "m=video ") == 0) {
            in_video = true;
            in_audio = false;
        } else if (line.compare(0, 8, "m=audio ") == 0) {
            in_audio = true;
            in_video = false;
        } else if (line.compare(0, 9, "a=rtpmap:") == 0) {
            const auto space = line.find(' ');
            if (space != std::string::npos) {
                int payload = 0;
                try {
                    payload = std::stoi(line.substr(9, space - 9));
                } catch (...) {
                    payload = 0;
                }
                std::string codec = line.substr(space + 1);
                for (auto& c : codec)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (in_video && video_payload && *video_payload == 0 &&
                    codec.compare(0, 4, "h264") == 0)
                    *video_payload = payload;
                else if (in_audio && audio_payload && *audio_payload == 0 &&
                         codec.compare(0, 4, "opus") == 0)
                    *audio_payload = payload;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return video_payload && *video_payload > 0 && audio_payload && *audio_payload > 0;
}

std::string RewriteCandidateLines(const std::string& sdp, const std::string& ip, int port) {
    // Faithful port of BH(sdp, ip, "a=candidate", port) of the official bundle
    // (@1921xxx). The web client always splits on "\r\n"; answers that use LF
    // only would be treated as one line there, so we keep the line structure
    // by splitting on "\n" in that case.
    const bool crlf = sdp.find("\r\n") != std::string::npos;
    const std::string delimiter = crlf ? "\r\n" : "\n";
    const std::string port_text = std::to_string(port);

    std::vector<std::string> lines;
    {
        std::size_t start = 0;
        while (true) {
            const auto pos = sdp.find(delimiter, start);
            if (pos == std::string::npos) {
                lines.push_back(sdp.substr(start));
                break;
            }
            lines.push_back(sdp.substr(start, pos - start));
            start = pos + delimiter.size();
        }
    }

    for (auto& line : lines) {
        if (line.find("a=candidate") == std::string::npos) continue;
        line = ReplaceAllIpv4(line, ip);

        // Inner helper of the official BH (@1921xxx), reproduced exactly:
        //   i = indexOf(ip)            (may be -1)
        //   e = ip.length
        //   r = indexOf(" typ host")   (may be -1; slice resolves it as len-1)
        //   a = slice(i + e + 1, r)
        //   line.replace(a, port)      (first occurrence)
        // Deviation guard: when the slice is empty the JS replace would insert
        // the port at the start of the line; we leave the line unchanged.
        const long long len = static_cast<long long>(line.size());
        const auto ip_pos = line.find(ip);
        const long long i =
            ip_pos == std::string::npos ? -1 : static_cast<long long>(ip_pos);
        const long long begin = i + static_cast<long long>(ip.size()) + 1;
        const auto host_pos = line.find(" typ host");
        const long long end =
            host_pos == std::string::npos ? len - 1 : static_cast<long long>(host_pos);
        if (begin >= end) continue;
        const std::string segment =
            line.substr(static_cast<std::size_t>(begin),
                        static_cast<std::size_t>(end - begin));
        const auto hit = line.find(segment);
        if (hit == std::string::npos) continue;
        line.replace(hit, segment.size(), port_text);
    }

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += delimiter;
        out += lines[i];
    }
    return out;
}

std::string RewriteProxyArmCandidates(const std::string& sdp, const std::string& ip,
                                      int port) {
    // KH of the official bundle (@1922454): replace everything from the first
    // "a=candidate" through "a=end-of-candidates" with one relay srflx line.
    const auto candidate = sdp.find("a=candidate");
    if (candidate == std::string::npos) return sdp;
    const std::string port_text = std::to_string(port);
    const std::string line = "a=candidate:1221718136 1 udp 1694498815 " + ip + " " +
                             port_text + " typ srflx raddr 0.0.0.0 rport " + port_text +
                             " generation 0\n";
    const auto end = sdp.find("a=end-of-candidates");
    if (end == std::string::npos) return sdp.substr(0, candidate) + line;
    return sdp.substr(0, candidate) + line + sdp.substr(end);
}

}  // namespace cloudgenshin::endfield
