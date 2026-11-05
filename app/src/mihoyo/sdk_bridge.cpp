#include "mihoyo/sdk_bridge.hpp"
#include "mihoyo/combo_login_data.hpp"
#include "mihoyo/sdk_report_diagnostics.hpp"
#include "mihoyo/sdk_json.hpp"
#include "mihoyo/sdk_device_fingerprint.hpp"

#include <mbedtls/aes.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace cloudgenshin::sdk {
namespace {
// Published application constant of the official web clients' SDK bridge
// (cg-sdk / web.b). It is an application key, never an account credential.
constexpr char kSdkDataKey[] = "OK20kydiRu47rOH7HNXzA12xxtlYVOUx";
constexpr std::size_t kMaximumMessageBytes = 256 * 1024;
constexpr std::size_t kMaximumWebviewUserAgentBytes = 1024;
constexpr std::int64_t kKeepPlayingIntervalMs = 30000;
std::optional<std::uint32_t> NumericSdkId(const std::string& text) {
    std::uint32_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !value || value>2147483647U)
        return std::nullopt;
    return value;
}

// Legacy local fallback, NOT a verified service-issued device fingerprint.
// A non-empty 13-character value did not resolve Star Rail's 1001_2 in prior
// runs. Kept only for other established profiles; Star Rail now follows the
// official unconfigured-host empty-string contract instead.
std::string DeviceFingerprint(const std::string& device_id) {
    // FNV-1a 64-bit over the device id, rendered in base36 and padded/truncated
    // to 13 lowercase alphanumerics.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : device_id) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string out;
    do {
        out.push_back(digits[hash % 36]);
        hash /= 36;
    } while (hash && out.size() < 13);
    while (out.size() < 13) out.push_back('0');
    return out.substr(0,13);
}

// One-shot diagnostic categories per bridge session.
enum EmitBit : std::uint32_t {
    kEmitRmqInvalid = 1u << 0,
    kEmitMessageInvalid = 1u << 1,
    kEmitOtherChannel = 1u << 2,
    kEmitDecryptFailed = 1u << 3,
    kEmitParseFailed = 1u << 4,
    kEmitCustom = 1u << 5,
    kEmitCloudData = 1u << 6,
    kEmitKeepAliveOn = 1u << 7,
    kEmitKeepAliveOff = 1u << 8,
    kEmitInit = 1u << 9,
    kEmitLogin = 1u << 10,
    kEmitWillEnter = 1u << 11,
    kEmitEnter = 1u << 12,
    kEmitLogout = 1u << 13,
    kEmitSwitchRole = 1u << 14,
    kEmitUserCenter = 1u << 15,
    kEmitPayment = 1u << 16,
    kEmitProducts = 1u << 17,
    kEmitInfo = 1u << 18,
    kEmitAgreement = 1u << 19,
    kEmitWebview = 1u << 20,
    kEmitLanguage = 1u << 21,
    kEmitUnknown = 1u << 22,
    kEmitKeepPlaying = 1u << 23,
    kEmitKeepPlayingFailed = 1u << 24,
    kEmitReplySent = 1u << 25,
    kEmitReplyFailed = 1u << 26,
    kEmitEncryptFailed = 1u << 27,
    kEmitAckSent = 1u << 28,
    kEmitAckFailed = 1u << 29,
    kEmitAckReceived = 1u << 30,
    kEmitResponseIgnored = 1u << 31,
};

void Varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        const auto byte = static_cast<std::uint8_t>(value & 127);
        value >>= 7;
        out.push_back(byte | (value ? 128 : 0));
    } while (value);
}
void VarintField(std::vector<std::uint8_t>& out, unsigned field, std::uint64_t value) {
    if (!value) return; // proto3 writers omit zero-valued fields
    Varint(out, (field << 3) | 0);
    Varint(out, value);
}
void Text(std::vector<std::uint8_t>& out, unsigned field, const std::string& value) {
    if (value.empty()) return;
    Varint(out, (field << 3) | 2);
    Varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}
void Bytes(std::vector<std::uint8_t>& out, unsigned field,
           const std::vector<std::uint8_t>& value) {
    if (value.empty()) return;
    Varint(out, (field << 3) | 2);
    Varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool done() const { return offset_ == bytes_.size(); }
    bool varint(std::uint64_t& value) {
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            if (offset_ == bytes_.size()) return false;
            const auto byte = bytes_[offset_++];
            value |= static_cast<std::uint64_t>(byte & 127) << shift;
            if (!(byte & 128)) return true;
        }
        return false;
    }
    bool bytes(std::span<const std::uint8_t>& value) {
        std::uint64_t length = 0;
        if (!varint(length) || length > bytes_.size() - offset_) return false;
        value = bytes_.subspan(offset_, static_cast<std::size_t>(length));
        offset_ += static_cast<std::size_t>(length);
        return true;
    }
    bool advance(std::size_t count) {
        if (count > bytes_.size() - offset_) return false;
        offset_ += count;
        return true;
    }
    bool skip() {
        std::uint64_t ignored = 0;
        return varint(ignored);
    }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};
char LowerOf(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}
char UpperOf(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}
// The official module helper: lower the first character, upper every character
// following an underscore and drop the underscores (login_enter_game ->
// loginEnterGame).
std::string CamelCase(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_') continue;
        if (i == 0) out.push_back(LowerOf(c));
        else if (name[i - 1] == '_') out.push_back(UpperOf(c));
        else out.push_back(c);
    }
    return out;
}
// Field identifiers of a decoded payload for the diagnostics trace. Objects
// contribute their key names only (at most eight); all other shapes contribute
// a type marker. Values are never copied, so account data cannot leak here.
std::string ParamKeys(const nlohmann::json& params) {
    if (params.is_object()) {
        std::string out;
        unsigned count = 0;
        for (auto it = params.begin(); it != params.end() && count < 8; ++it, ++count) {
            if (!out.empty()) out.push_back(',');
            // The game appends its numeric account id to cloud_local_data.
            // It is a key name, not a schema identifier; do not expose it.
            if(it.key().starts_with("cloud_local_data_")) out += "cloud_local_data_[redacted]";
            else if(it.key().size()<=64 && std::all_of(it.key().begin(),it.key().end(),[](unsigned char c) {
                return c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z');
            })) out += it.key();
            else out += "[dynamic-key]";
        }
        return out;
    }
    if (params.is_array()) return "[array]";
    if (params.is_string()) return "[string]";
    if (params.is_number()) return "[number]";
    if (params.is_boolean()) return "[bool]";
    return "[null]";
}
} // namespace

std::optional<ReliableMessageQueueData> DecodeReliableMessageQueueData(
    std::span<const std::uint8_t> bytes) {
    ReliableMessageQueueData message;
    Reader reader(bytes);
    while (!reader.done()) {
        std::uint64_t key = 0;
        if (!reader.varint(key)) return std::nullopt;
        const auto field = static_cast<unsigned>(key >> 3);
        const auto wire = static_cast<unsigned>(key & 7);
        if (wire == 0) {
            std::uint64_t value = 0;
            if (!reader.varint(value)) return std::nullopt;
            switch (field) {
            case 1: message.msg_id = static_cast<std::uint32_t>(value); break;
            case 2: message.msg_type = static_cast<std::uint32_t>(value); break;
            case 3: message.seq_id = static_cast<std::uint32_t>(value); break;
            case 4: message.seq_cnt = static_cast<std::uint32_t>(value); break;
            case 5: message.total_len = static_cast<std::uint32_t>(value); break;
            case 6: message.data_len = static_cast<std::uint32_t>(value); break;
            default: break;
            }
        } else if (wire == 2) {
            std::span<const std::uint8_t> value;
            if (!reader.bytes(value)) return std::nullopt;
            if (field == 7) message.data.assign(value.begin(), value.end());
            else if (field == 8) message.tag.assign(value.begin(), value.end());
        } else if (wire == 1) {
            if (!reader.advance(8)) return std::nullopt;
        } else if (wire == 5) {
            if (!reader.advance(4)) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return message;
}

std::vector<std::uint8_t> EncodeReliableMessageQueueData(
    const ReliableMessageQueueData& message) {
    std::vector<std::uint8_t> out;
    out.reserve(32 + message.data.size());
    VarintField(out, 1, message.msg_id);
    VarintField(out, 2, message.msg_type);
    VarintField(out, 3, message.seq_id);
    VarintField(out, 4, message.seq_cnt);
    VarintField(out, 5, message.total_len);
    VarintField(out, 6, message.data_len);
    Bytes(out, 7, message.data);
    Text(out, 8, message.tag);
    return out;
}

std::optional<SdkGameDataMessage> DecodeSdkGameDataMessage(
    std::span<const std::uint8_t> bytes) {
    SdkGameDataMessage message;
    Reader reader(bytes);
    while (!reader.done()) {
        std::uint64_t key = 0;
        if (!reader.varint(key)) return std::nullopt;
        const auto field = static_cast<unsigned>(key >> 3);
        const auto wire = static_cast<unsigned>(key & 7);
        if (wire == 2) {
            std::span<const std::uint8_t> value;
            if (!reader.bytes(value)) return std::nullopt;
            if (field == 1) message.name.assign(value.begin(), value.end());
            else if (field == 2) message.data.assign(value.begin(), value.end());
        } else if (wire == 0) {
            if (!reader.skip()) return std::nullopt;
        } else if (wire == 1) {
            if (!reader.advance(8)) return std::nullopt;
        } else if (wire == 5) {
            if (!reader.advance(4)) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return message;
}

std::vector<std::uint8_t> EncodeSdkGameDataMessage(const SdkGameDataMessage& message) {
    std::vector<std::uint8_t> out;
    out.reserve(16 + message.data.size());
    Text(out, 1, message.name);
    Bytes(out, 2, message.data);
    return out;
}

std::optional<std::string> EncryptGameDataPayload(std::string_view json) {
    if (json.empty() || json.size() > kMaximumMessageBytes) return std::nullopt;
    std::vector<std::uint8_t> buffer(json.begin(), json.end());
    const auto pad = static_cast<std::uint8_t>(16 - buffer.size() % 16);
    buffer.insert(buffer.end(), pad, pad);
    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    int code = mbedtls_aes_setkey_enc(&context,
        reinterpret_cast<const unsigned char*>(kSdkDataKey), 256);
    for (std::size_t offset = 0; !code && offset < buffer.size(); offset += 16)
        code = mbedtls_aes_crypt_ecb(&context, MBEDTLS_AES_ENCRYPT, buffer.data() + offset,
                                     buffer.data() + offset);
    mbedtls_aes_free(&context);
    if (code) return std::nullopt;
    std::string encoded(4 * ((buffer.size() + 2) / 3) + 1, '\0');
    std::size_t count = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded.data()), encoded.size(),
            &count, buffer.data(), buffer.size()) != 0) return std::nullopt;
    encoded.resize(count);
    return encoded;
}

std::optional<std::string> DecryptGameDataPayload(std::string_view base64_text) {
    if (base64_text.empty() || base64_text.size() > kMaximumMessageBytes * 2) return std::nullopt;
    std::vector<std::uint8_t> buffer(base64_text.size() / 4 * 3 + 3);
    std::size_t count = 0;
    if (mbedtls_base64_decode(buffer.data(), buffer.size(), &count,
            reinterpret_cast<const unsigned char*>(base64_text.data()),
            base64_text.size()) != 0) return std::nullopt;
    buffer.resize(count);
    if (buffer.empty() || buffer.size() % 16 != 0) return std::nullopt;
    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    int code = mbedtls_aes_setkey_dec(&context,
        reinterpret_cast<const unsigned char*>(kSdkDataKey), 256);
    for (std::size_t offset = 0; !code && offset < buffer.size(); offset += 16)
        code = mbedtls_aes_crypt_ecb(&context, MBEDTLS_AES_DECRYPT, buffer.data() + offset,
                                     buffer.data() + offset);
    mbedtls_aes_free(&context);
    if (code) return std::nullopt;
    const auto pad = buffer.back();
    if (pad == 0 || pad > 16 || pad > buffer.size()) return std::nullopt;
    for (std::size_t i = buffer.size() - pad; i < buffer.size(); ++i)
        if (buffer[i] != pad) return std::nullopt;
    buffer.resize(buffer.size() - pad);
    return std::string(buffer.begin(), buffer.end());
}

Bridge::Bridge(BridgeContext context, SendProxy send_proxy)
    : context_(std::move(context)), send_(std::move(send_proxy)) {}

void Bridge::Emit(const char* tag, int code) {
    if (context_.event) context_.event(tag, code);
}

void Bridge::EmitOnce(std::uint32_t bit, const char* tag) {
    if (emitted_ & bit) return;
    emitted_ |= bit;
    Emit(tag, 0);
}

void Bridge::Trace(const std::string& line) {
    if (context_.trace) context_.trace(line);
}

bool Bridge::HandleProxyPacket(std::uint16_t command, std::span<const std::uint8_t> body,
                               std::int64_t monotonic_ms) {
    if (command == kReliableMessageQueueCmd) {
        const auto message = DecodeReliableMessageQueueData(body);
        if (!message) {
            EmitOnce(kEmitRmqInvalid, "GAME_SDK_RMQ_INVALID");
            return true;
        }
        ReceiveQueueMessage(*message, monotonic_ms);
        return true;
    }
    if (command == kRtcNotPlayingTipsCmd) {
        // The official page reports the next user action as "still playing"
        // instead of answering the tip by itself.
        no_action_tip_ = true;
        last_keep_playing_ms_ = -1;
        Emit("GAME_SDK_NO_ACTION_TIP", 0);
        return true;
    }
    return false;
}

void Bridge::ReceiveQueueMessage(const ReliableMessageQueueData& message,
                                 std::int64_t monotonic_ms) {
    Trace("rmq type=" + std::to_string(message.msg_type) + " id=" +
          std::to_string(message.msg_id) + " total=" + std::to_string(message.total_len) +
          " data=" + std::to_string(message.data.size()));
    if (message.msg_type == kRmqMessageAck) {
        // Acks confirm our outbound queue entries; nothing to retransmit.
        EmitOnce(kEmitAckReceived, "GAME_SDK_ACK_RECEIVED");
        return;
    }
    if (message.msg_type == kRmqMessageNormal) {
        if (retransmitting_) {
            retransmitting_ = false;
            receive_buffer_.clear();
        }
    } else if (message.msg_type == kRmqMessageReTransmit) {
        if (!retransmitting_) {
            retransmitting_ = true;
            receive_buffer_.clear();
        }
    } else {
        return; // Rebuilt and unknown types are ignored like the official receiver
    }
    if (message.total_len == 0 || message.total_len > kMaximumMessageBytes ||
        receive_buffer_.size() + message.data.size() > kMaximumMessageBytes) {
        EmitOnce(kEmitRmqInvalid, "GAME_SDK_RMQ_INVALID");
        receive_buffer_.clear();
        receive_total_length_ = 0;
        return;
    }
    receive_total_length_ = message.total_len;
    receive_buffer_.insert(receive_buffer_.end(), message.data.begin(), message.data.end());
    if (receive_buffer_.size() < receive_total_length_) return;
    // Official order: deliver to the business layer first, then clear the
    // buffer, then acknowledge.
    DeliverMessage(receive_buffer_, monotonic_ms);
    receive_buffer_.clear();
    receive_total_length_ = 0;
    SendAck(message.msg_id);
}

void Bridge::DeliverMessage(std::span<const std::uint8_t> bytes, std::int64_t monotonic_ms) {
    const auto message = DecodeSdkGameDataMessage(bytes);
    if (!message) {
        EmitOnce(kEmitMessageInvalid, "GAME_SDK_MESSAGE_INVALID");
        return;
    }
    if (message->name != "SDK") {
        // The official client only logs other channel names.
        Trace("channel name=" + message->name + " bytes=" + std::to_string(message->data.size()));
        EmitOnce(kEmitOtherChannel, "GAME_SDK_CHANNEL_OTHER");
        return;
    }
    HandleGamePayload(message->data, monotonic_ms);
}

void Bridge::HandleGamePayload(std::span<const std::uint8_t> bytes, std::int64_t monotonic_ms) {
    // Received payload = UTF-8 bytes of a Base64 AES ciphertext.
    const std::string encoded(bytes.begin(), bytes.end());
    const auto plain = DecryptGameDataPayload(encoded);
    if (!plain) {
        Trace("decrypt_fail bytes=" + std::to_string(bytes.size()));
        EmitOnce(kEmitDecryptFailed, "GAME_SDK_DECRYPT_FAILED");
        return;
    }
    const auto message = ParseSdkJson(*plain);
    if (message.is_discarded() || !message.is_object() || !message.contains("f") ||
        !message["f"].is_string()) {
        EmitOnce(kEmitParseFailed, "GAME_SDK_PARSE_FAILED");
        return;
    }
    const std::string outer = message["f"].get<std::string>();
    const bool index_present = message.contains("i") && message["i"].is_number_integer();
    const std::int64_t index = index_present ? message["i"].get<std::int64_t>() : 0;
    const auto parse_params = [](const Json& value) {
        if (!value.is_string()) return value;
        const std::string text = value.get<std::string>();
        if (text.empty()) return Json(std::string());
        const auto parsed = ParseSdkJson(text);
        return parsed.is_discarded() ? Json(text) : parsed;
    };

    std::string invoker = "_custom_";
    std::string function = outer;
    Json params = Json(nullptr);
    if ((outer == "invoke" || outer == "invoke_return" || outer == "webview") &&
        message.contains("p") && message["p"].is_string()) {
        const auto inner = ParseSdkJson(message["p"].get_ref<const std::string&>());
        if (!inner.is_discarded() && inner.is_object() && inner.contains("f") &&
            inner["f"].is_string()) {
            invoker = outer;
            function = inner["f"].get<std::string>();
            if (inner.contains("p")) params = parse_params(inner["p"]);
        } else {
            params = parse_params(message["p"]);
        }
    } else if (message.contains("p")) {
        params = parse_params(message["p"]);
    }
    // The official app rewrites a wrapped cloud_keep_alive invoke to the
    // custom form before dispatch.
    if (invoker == "invoke" && function == "cloud_keep_alive") invoker = "_custom_";

    // Protocol metadata only: identifiers, index and parameter key names.
    Trace("in inv=" + outer + " f=" + function +
          " idx=" + (index_present ? std::to_string(index) : std::string("-")) +
          " keys=" + ParamKeys(params));
    // Star Rail's first telemetry burst consumes 24 reports before the title
    // request can fail. Keep a larger, still fixed opt-in schema/scalar budget;
    // no raw body is recorded, and normal gameplay does not enable this trace.
    const unsigned report_budget=context_.app_id=="8" ? 64 : 24;
    const bool trace_report=context_.trace && report_diagnostics_count_<report_budget;
    const bool watch_star_rail=context_.app_id=="8" &&
        (!star_rail_network_error_reported_ || !star_rail_nonzero_retcode_reported_);
    if(function=="report_report_event" && (trace_report || watch_star_rail)) {
        if(trace_report) {
            ++report_diagnostics_count_;
            Trace("report schema="+GameReportSchema(params));
        }
        // One fixed failure marker in normal Star Rail runs, even after the
        // opt-in trace budget is exhausted. No report body/identifiers escape
        // the bounded scanner; other games' normal reporting is unchanged.
        for(const auto& code:InspectGameReport(params)) {
            const bool known_error=watch_star_rail && !star_rail_network_error_reported_ &&
                std::string_view(code.event)=="GAME_SDK_REPORT_NET_1001_2";
            // The official telemetry can report numeric retcode=1001 without
            // the UI's literal suffix. Preserve the first nonzero retcode,
            // once per bridge, without pretending it proves a specific text.
            if(watch_star_rail && !star_rail_nonzero_retcode_reported_ &&
               std::string_view(code.event)=="GAME_SDK_REPORT_RETCODE" && code.value!=0) {
                Emit("GAME_SDK_REPORT_FIRST_NONZERO_RETCODE",code.value);
                star_rail_nonzero_retcode_reported_=true;
            }
            if(trace_report || known_error) Emit(code.event,code.value);
            if(known_error) star_rail_network_error_reported_=true;
        }
    }
    std::optional<std::string> reply;
    if (invoker == "_custom_") {
        reply = DispatchCustom(function, params, index_present, index, monotonic_ms);
    } else if (invoker == "invoke") {
        reply = DispatchInvoke(function, params, index_present, index);
    } else if (invoker == "invoke_return") {
        // The game asks the hosted SDK modules for values through the return
        // channel; the official web InfoModule answers these four identifiers.
        reply = DispatchInvokeReturn(function, index_present, index);
    } else if (invoker == "webview") {
        // sdkWebview module: get/set_global_user_agent are real handlers. Other
        // targets are absent, and the official dispatcher serialises that
        // missing-target error object as "{}" instead of staying silent.
        reply = DispatchWebview(function, params, index_present, index);
    } else {
        EmitOnce(kEmitResponseIgnored, "GAME_SDK_RESPONSE_IGNORED");
    }
    if (reply) SendEncryptedReply(*reply);
    else Trace("out f=" + function + " reply=none");
}

std::string Bridge::BuildReply(const std::string& type, bool index_present,
                               std::int64_t index, bool has_data, const Json& data) {
    Json p = Json::object();
    if (index_present) p["index"] = index;
    if (has_data)
        p["data"] = (data.is_object() || data.is_array()) ? Json(data.dump()) : data;
    Json message = Json::object();
    message["f"] = type;
    message["p"] = p.dump();
    return message.dump();
}

std::optional<std::string> Bridge::DispatchInvokeReturn(const std::string& function,
                                                        bool index_present, std::int64_t index) {
    if (!index_present || index <= 0) return std::nullopt;
    // Official combo-web InfoModule (ao class) invoke-return surface:
    //   getDeviceFingerprint -> "" when the host configured no function
    //   getDeviceId          -> host device_id
    //   getChannelId         -> static channel id (1 for hk4e_cn)
    //   getSubChannelId      -> "1" for the CN area
    //   getDeferredDeeplink  -> undefined until the host sets it
    // The official web login module (io class) answers two more identifiers:
    //   login_get_asterisk_name -> stored user_info display name; the official
    //                              fallback when user_info is absent is ""
    //   login_has_scan_func     -> String(qr_enabled || true) -> "true"
    // Any other identifier is not registered in the web module; the page
    // serialises its error object, which stringifies to "{}".
    bool has_data = true;
    Json data = std::string("{}");
    if (function == "info_get_device_fingerprint") {
        // ae.device_fp has a DEFAULT callback to the official FP SDK; the
        // unconfigured-function fallback alone does not describe initialization.
        // Never substitute a local FNV value for Star Rail's service response.
        const auto fp=context_.app_id=="8"
            ? (ValidSdkDeviceFingerprint(context_.device_fingerprint) ? context_.device_fingerprint : std::string{})
            : DeviceFingerprint(context_.device_id);
        data=fp;
        if (context_.event) context_.event("GAME_SDK_DEVICE_FINGERPRINT_PRESENT",!fp.empty());
    } else if (function == "info_get_device_id") {
        data = context_.device_id;
    } else if (function == "info_get_channel_id") {
        data = context_.channel_id.empty() ? std::string("1") : context_.channel_id;
    } else if (function == "info_get_sub_channel_id") {
        data = std::string("1");
    } else if (function == "info_get_cps") {
        // Official page: `${platform}_${utm_source || "mihoyo"}`; on the
        // WebPC/WebKeyboard terminals this is pc_mihoyo. The game sends it in
        // the login request, so replying "{}" breaks the channel parameters.
        data = context_.cps.empty() ? std::string("pc_mihoyo") : context_.cps;
    } else if (function == "info_get_uapc") {
        // Official page and native holder both answer the empty string.
        data = std::string();
    } else if (function == "info_get_deferred_deeplink") {
        has_data = false;
    } else if (function == "login_get_asterisk_name") {
        data = std::string();
        if (const auto sdk = auth::DecodeComboSdkData(context_.sdk_login_data);
            sdk && sdk->contains("asterisk_name")) data = (*sdk)["asterisk_name"];
    } else if (function == "login_has_scan_func") {
        data = std::string("true");
    }
    return BuildReply("on_get_invoke_response", index_present, index, has_data, data);
}

std::optional<std::string> Bridge::DispatchWebview(const std::string& function, const Json& params,
                                                   bool index_present, std::int64_t index) {
    // Official sdkWebview (Rr) handlers:
    //   getGlobalUserAgent() -> ce.get("ua") || ""
    //   setGlobalUserAgent(e) -> ce.set("ua", e.content), then propagate to
    //                            open frames; the handler returns undefined.
    // The SDK dispatcher replies only when the webview result is a string (or
    // an error object) and the index is positive, so a successful set has no
    // reply. Do not substitute a fabricated Android/browser UA for the empty
    // initial value: the game uses this contract to install its own UA.
    if (function == "get_global_user_agent") {
        EmitOnce(kEmitWebview, "GAME_SDK_WEBVIEW_REQUEST");
        if (!index_present || index <= 0) return std::nullopt;
        return BuildReply("on_get_invoke_response", index_present, index, true,
                          webview_user_agent_);
    }
    if (function == "set_global_user_agent") {
        EmitOnce(kEmitWebview, "GAME_SDK_WEBVIEW_REQUEST");
        if (params.is_object() && params.contains("content") && params["content"].is_string()) {
            const auto& content = params["content"].get_ref<const std::string&>();
            // The official page stores whatever the game supplies. Bound it and
            // reject control characters so a malformed message cannot grow the
            // session state or inject line breaks into later diagnostics.
            const bool safe = content.size() <= kMaximumWebviewUserAgentBytes &&
                std::all_of(content.begin(), content.end(), [](unsigned char c) {
                    return c >= 0x20 && c != 0x7f;
                });
            if (safe) webview_user_agent_ = content;
        }
        return std::nullopt;
    }
    // Missing webview target: the official wrapper returns its NO_SUCH_METHOD
    // error object, whose JSON serialisation is "{}".
    EmitOnce(kEmitResponseIgnored, "GAME_SDK_RESPONSE_IGNORED");
    if (index_present && index > 0)
        return BuildReply("on_get_invoke_response", true, index, true, std::string("{}"));
    return std::nullopt;
}

std::optional<std::string> Bridge::DispatchCustom(const std::string& function, const Json& params,
                                                  bool index_present, std::int64_t index,
                                                  std::int64_t monotonic_ms) {
    EmitOnce(kEmitCustom, "GAME_SDK_CUSTOM_REQUEST");
    bool has_data = false;
    Json data;
    if (function == "cloud_get_data") {
        const std::string key = params.is_string()
            ? params.get<std::string>()
            : (params.is_null() ? std::string() : params.dump());
        if (key == "account_id") {
            data = Json{{"account_id", context_.account_id}};
        } else {
            data = Json(std::string());
            if (cloud_data_.is_object() && cloud_data_.contains(key)) {
                const auto& saved=cloud_data_[key];
                // Official ys/sr host returns cachedValue || "". JS considers
                // false/zero/null falsy, but empty objects and arrays truthy.
                const bool falsy=saved.is_null() || (saved.is_boolean() && !saved.get<bool>()) ||
                    (saved.is_number() && saved.get<double>()==0);
                if(!falsy) data=saved;
            }
        }
        has_data = true;
    } else if (function == "cloud_set_data") {
        if(context_.trace && params.is_object() && cloud_schema_count_<4) {
            ++cloud_schema_count_;
            for(const auto& value:params) Trace("cloud local schema="+GameCloudDataSchema(value));
        }
        if (params.is_object()) {
            cloud_data_ = params;
        } else if (params.is_string()) {
            const auto parsed = ParseSdkJson(params.get_ref<const std::string&>());
            cloud_data_ = parsed.is_discarded() ? Json(params.get<std::string>()) : parsed;
        } else {
            cloud_data_ = params;
        }
        EmitOnce(kEmitCloudData, "GAME_SDK_CLOUD_DATA_STORED");
    } else if (function == "cloud_get_clipboard_data") {
        data = Json{{"ret", 0},
                    {"key", params.is_string() ? params.get<std::string>() : params.dump()},
                    {"content", ""}};
        has_data = true;
    } else if (function == "cloud_transmit_data") {
        // The official handler forwards the screenshot part number and saves
        // the image; this client only completes the protocol handshake.
        if (params.is_object() && params.contains("part")) {
            data = params["part"];
            has_data = true;
        }
    } else if (function == "cloud_keep_alive") {
        // Official startGameDataKeepAlive(duration): duration is a MINUTE
        // budget, not the send interval. Refresh immediately, then once per
        // minute for ceil(duration - idleTimeout + 1) total ticks. A restart
        // replaces the old timer; stopping also reports one final user action.
        if (params.is_object() && params.contains("enable") && params["enable"].is_boolean()) {
            keep_alive_enabled_ = false;
            keep_alive_ticks_left_ = 0;
            next_keep_alive_ms_ = 0;
            if (params["enable"].get<bool>()) {
                auto duration = context_.keep_alive_duration_minutes;
                if (params.contains("duration") && !params["duration"].is_null()) {
                    if (!params["duration"].is_number()) return std::nullopt;
                    duration = params["duration"].get<double>();
                }
                if (!duration) {
                    // The native path does not yet fetch boxConfig's default.
                    // Do not invent an unbounded keep-alive subscription.
                    SendKeepPlaying(monotonic_ms);
                    Emit("GAME_SDK_KEEP_ALIVE_DEFAULT_UNAVAILABLE", 0);
                } else if (std::isfinite(*duration) && *duration >= 0 && *duration <= 1440 &&
                           std::isfinite(context_.idle_timeout_minutes) &&
                           context_.idle_timeout_minutes >= 0 && context_.idle_timeout_minutes <= 1440) {
                    const double ticks = std::ceil(*duration - context_.idle_timeout_minutes + 1);
                    if (ticks > 0) {
                        SendKeepPlaying(monotonic_ms);
                        keep_alive_ticks_left_ = static_cast<std::uint32_t>(ticks) - 1;
                        keep_alive_enabled_ = keep_alive_ticks_left_ > 0;
                        if (keep_alive_enabled_) next_keep_alive_ms_ = monotonic_ms + 60000;
                    }
                    EmitOnce(kEmitKeepAliveOn, "GAME_SDK_KEEP_ALIVE_ENABLED");
                } else {
                    Emit("GAME_SDK_KEEP_ALIVE_DURATION_INVALID", 0);
                }
            } else {
                SendKeepPlaying(monotonic_ms);
                EmitOnce(kEmitKeepAliveOff, "GAME_SDK_KEEP_ALIVE_DISABLED");
            }
        }
    }
    if (!index_present || index <= 0) return std::nullopt;
    return BuildReply("on_get_invoke_response", index_present, index, has_data, data);
}

std::optional<std::string> Bridge::DispatchInvoke(const std::string& function, const Json& params,
                                                  bool index_present, std::int64_t index) {
    (void)params;
    if (function == "Init") {
        // The official Android holder answers Init and immediately pushes its
        // box config (save-image / clipboard / share timeouts). The web bundle
        // has the same fields in its config centre; values below are the
        // official defaults, never account data.
        EmitOnce(kEmitInit, "GAME_SDK_INIT");
        SendBoxConfig();
        const Json data = Json{{"ret", 0}, {"msg", "mihoyo web sdk init success"}};
        return BuildReply("on_init_response", index_present, index, true, data);
    }
    if (function == "login_login") {
        const bool first_login = !(emitted_ & kEmitLogin);
        EmitOnce(kEmitLogin, "GAME_SDK_LOGIN");
        const auto sdk_data = auth::DecodeComboSdkData(context_.sdk_login_data);
        const auto app_id=NumericSdkId(context_.app_id), channel_id=NumericSdkId(context_.channel_id);
        Json data=sdk_data ? *sdk_data : Json::object();
        // hkrpg's official combo-web _createComboLoginSuccResult explicitly
        // omits heartbeat/fatigue_remind/data before expanding result.data.
        // They belong to host SDK services, not the H5 game's login reply.
        // Preserve the established Genshin/Android profiles independently.
        if(context_.app_id=="8") {data.erase("heartbeat");data.erase("fatigue_remind");}
        const bool legacy_type_resolved=sdk_data && auth::ResolveLegacyGenshinAccountType(data,
            context_.app_id,context_.channel_id,context_.open_id,context_.account_id);
        if (first_login) {
            // Presence only. Neither this sink nor the trace gets any values.
            Emit("GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT", !context_.channel_token.empty());
            Emit("GAME_SDK_LOGIN_METADATA_PRESENT", sdk_data && !sdk_data->empty());
            Emit("GAME_SDK_LOGIN_ACCOUNT_TYPE_PRESENT", sdk_data && data.contains("account_type"));
            Emit("GAME_SDK_LOGIN_COMBO_ID_PRESENT", sdk_data && sdk_data->contains("combo_id"));
            if(legacy_type_resolved) Emit("GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED",1);
        }
        if (!sdk_data || context_.device_id.empty() || !app_id || !channel_id ||
            context_.open_id.empty() || context_.combo_token.empty()) {
            Emit("GAME_SDK_LOGIN_LOCAL_DATA_INVALID", 0);
            if (!index_present || index <= 0) return std::nullopt;
            return BuildReply("on_get_invoke_response", index_present, index, true,
                              Json{{"ret", -1}, {"msg", "local login data is incomplete"}});
        }
        const auto put = [&data](const char* key, const std::string& value) {
            if (!value.empty()) data[key] = value;
        };
        put("device_id", context_.device_id);
        put("app_id", context_.app_id);
        put("channel_id", context_.channel_id);
        put("channel_token", context_.channel_token);
        put("open_id", context_.open_id);
        put("combo_token", context_.combo_token);
        // Both UserData.toDataJSON and the web Combo result use JSON numbers
        // for these ids. Their HTTP/header representation remains a string.
        data["app_id"]=*app_id;
        data["channel_id"]=*channel_id;
        if (!index_present || index <= 0) return std::nullopt;
        const Json result = Json{{"ret", 0}, {"msg", "成功"}, {"data", data}};
        return BuildReply("on_get_invoke_response", index_present, index, true, result);
    }
    if (function == "login_will_enter_game") {
        EmitOnce(kEmitWillEnter, "GAME_SDK_WILL_ENTER_GAME");
        if (!index_present || index <= 0) return std::nullopt;
        const Json result = Json{{"ret", 0}, {"msg", "成功"}};
        return BuildReply("on_get_invoke_response", index_present, index, true, result);
    }
    if (function == "login_enter_game") {
        // The official client has no login.enterGame implementation and
        // answers with its NO_SUCH_METHOD error; the game accepts that reply.
        EmitOnce(kEmitEnter, "GAME_SDK_ENTER_GAME");
        if (!index_present || index <= 0) return std::nullopt;
        const Json result = Json{{"ret", -10},
                                 {"msg", "function not found in login:enterGame"},
                                 {"isReqError", true}};
        return BuildReply("on_get_invoke_response", index_present, index, true, result);
    }
    if (function == "login_logout" || function == "login_logout_without_confirm") {
        // The official session stops the cloud game; no reply is sent.
        EmitOnce(kEmitLogout, "GAME_SDK_LOGOUT");
        if (context_.on_session_end) context_.on_session_end();
        return std::nullopt;
    }
    if (function == "login_switch_role") {
        EmitOnce(kEmitSwitchRole, "GAME_SDK_SWITCH_ROLE");
        if (context_.on_session_end) context_.on_session_end();
        return std::nullopt;
    }
    if (function == "login_open_user_center") {
        EmitOnce(kEmitUserCenter, "GAME_SDK_USER_CENTER");
        return std::nullopt;
    }
    if (function == "login_pay") {
        // Official payment-blocked reply; this client never buys anything.
        EmitOnce(kEmitPayment, "GAME_SDK_PAYMENT_BLOCKED");
        if (!index_present || index <= 0) return std::nullopt;
        const Json result = Json{{"ret", -108}, {"message", "payment is blocked"}};
        return BuildReply("on_get_invoke_response", index_present, index, true, result);
    }
    if (function == "login_get_product_list") {
        // No product backend on this client; the official blocked deployment
        // also answers nothing.
        EmitOnce(kEmitProducts, "GAME_SDK_PRODUCT_LIST_UNSUPPORTED");
        return std::nullopt;
    }
    if (function == "info_get_cps" || function == "info_get_uapc") {
        // Official web page handlers: getCps returns "<platform>_<utm_source>"
        // (pc_mihoyo on WebPC/WebKeyboard) and getUapc returns "". The native
        // holder answers the return channel with the same pair, and the game
        // includes them in its login request; the old "no reply"/"{}" values
        // made the server-facing channel parameters invalid.
        EmitOnce(kEmitInfo, "GAME_SDK_INFO_REQUEST");
        if (!index_present || index <= 0) return std::nullopt;
        const std::string value = function == "info_get_cps" ? context_.cps : std::string();
        return BuildReply("on_get_invoke_response", index_present, index, true, value);
    }
    if (function.starts_with("launch_show_user_agreement")) {
        // The account-level agreement was resolved during cloud sign-in; the
        // in-game webview is reported as already resolved (not shown).
        EmitOnce(kEmitAgreement, "GAME_SDK_AGREEMENT_NOT_SHOWN");
        if (!index_present || index <= 0) return std::nullopt;
        const Json result = Json{{"ret", 0},
                                 {"msg", "成功"},
                                 {"data", Json{{"is_show", false}}}};
        return BuildReply("on_get_invoke_response", index_present, index, true, result);
    }
    if (function.starts_with("webview_") || function.starts_with("notice_") ||
        function == "launch_open_url") {
        // Webview surfaces are not rendered by this client. The official page
        // still answers every webview call that produced a result (its missing
        // target surfaces an error object), so answer with an empty result
        // instead of leaving the game's invoke chain pending.
        EmitOnce(kEmitWebview, "GAME_SDK_WEBVIEW_REQUEST");
        if (!index_present || index <= 0) return std::nullopt;
        return BuildReply("on_get_invoke_response", index_present, index, true, Json(std::string()));
    }
    if (function == "all_set_language") {
        EmitOnce(kEmitLanguage, "GAME_SDK_SET_LANGUAGE");
        return std::nullopt;
    }
    // Unknown message: the official registry answers NO_SUCH_METHOD.
    EmitOnce(kEmitUnknown, "GAME_SDK_UNKNOWN_MESSAGE");
    if (!index_present || index <= 0) return std::nullopt;
    const auto underscore = function.find('_');
    const std::string module_name =
        underscore == std::string::npos ? std::string("null") : function.substr(0, underscore);
    const std::string method_name = underscore == std::string::npos
        ? CamelCase(function) : CamelCase(function.substr(underscore + 1));
    const Json result = Json{{"ret", -10},
                             {"msg", "function not found in " + module_name + ":" + method_name},
                             {"isReqError", true}};
    return BuildReply("on_get_invoke_response", index_present, index, true, result);
}

void Bridge::SendBoxConfig() {
    if (box_config_sent_) return;
    box_config_sent_ = true;
    // Official BaseSdkHolder Init branch: the deployment Box config is pushed
    // verbatim after Init; these are the shipped defaults (never account data).
    Json box = Json::object();
    box["save_image_loading"] = true;
    box["save_image_time_out"] = 10;
    box["get_clipboard_data_timeout"] = 3;
    box["share_time_out"] = 10;
    box["share_text_max_length"] = 1048576;
    Json message = Json::object();
    message["f"] = "on_set_box_config";
    message["p"] = box.dump();
    Trace("out f=on_set_box_config");
    SendEncryptedReply(message.dump());
}

void Bridge::SendAck(std::uint32_t acked_msg_id) {
    // ReliableMessageQueueAck { msgId = 1 } wrapped in an Ack queue entry.
    std::vector<std::uint8_t> ack;
    VarintField(ack, 1, acked_msg_id);
    ReliableMessageQueueData message;
    message.msg_id = receiver_message_id_++;
    message.msg_type = kRmqMessageAck;
    message.seq_id = 1;
    message.seq_cnt = 1;
    message.total_len = static_cast<std::uint32_t>(ack.size());
    message.data_len = static_cast<std::uint32_t>(ack.size());
    message.data = std::move(ack);
    Trace("out ack=" + std::to_string(acked_msg_id));
    if (!send_ || !send_(kReliableMessageQueueCmd, EncodeReliableMessageQueueData(message)))
        EmitOnce(kEmitAckFailed, "GAME_SDK_ACK_SEND_FAILED");
}

void Bridge::SendEncryptedReply(const std::string& wire_json) {
    const auto cipher = EncryptGameDataPayload(wire_json);
    if (!cipher) {
        EmitOnce(kEmitEncryptFailed, "GAME_SDK_ENCRYPT_FAILED");
        return;
    }
    const auto reply_json = ParseSdkJson(wire_json);
    if (reply_json.is_object() && reply_json.contains("f") && reply_json["f"].is_string())
        Trace("send f=" + reply_json["f"].get<std::string>() +
              " bytes=" + std::to_string(wire_json.size()));
    SdkGameDataMessage sdk;
    sdk.name = "SDK";
    sdk.data.assign(cipher->begin(), cipher->end());
    const auto payload = EncodeSdkGameDataMessage(sdk);
    ReliableMessageQueueData message;
    message.msg_id = sender_message_id_++;
    message.msg_type = kRmqMessageNormal;
    message.seq_id = 1;
    message.seq_cnt = 1;
    message.total_len = static_cast<std::uint32_t>(payload.size());
    message.data_len = static_cast<std::uint32_t>(payload.size());
    message.data = payload;
    if (!send_ || !send_(kReliableMessageQueueCmd, EncodeReliableMessageQueueData(message))) {
        EmitOnce(kEmitReplyFailed, "GAME_SDK_REPLY_SEND_FAILED");
        return;
    }
    EmitOnce(kEmitReplySent, "GAME_SDK_REPLY_SENT");
}

bool Bridge::SendKeepPlaying(std::int64_t monotonic_ms) {
    if (context_.game_token.empty()) return false;
    // proto.msg.RtcKeepPlaying { gameToken = 1 }
    std::vector<std::uint8_t> body;
    Text(body, 1, context_.game_token);
    if (!send_ || !send_(kRtcKeepPlayingCmd, body)) {
        EmitOnce(kEmitKeepPlayingFailed, "GAME_SDK_KEEP_PLAYING_FAILED");
        return false;
    }
    last_keep_playing_ms_ = monotonic_ms;
    Trace("send keepalive");
    EmitOnce(kEmitKeepPlaying, "GAME_SDK_KEEP_PLAYING_SENT");
    return true;
}

void Bridge::OnLocalInput(std::int64_t monotonic_ms) {
    if (no_action_tip_) {
        // The next user action answers the idle tip, like the official page.
        if (SendKeepPlaying(monotonic_ms)) no_action_tip_ = false;
        return;
    }
    if (last_keep_playing_ms_ < 0 ||
        monotonic_ms - last_keep_playing_ms_ >= kKeepPlayingIntervalMs)
        SendKeepPlaying(monotonic_ms);
}

void Bridge::Poll(std::int64_t monotonic_ms) {
    if (keep_alive_enabled_ && next_keep_alive_ms_ && monotonic_ms >= next_keep_alive_ms_) {
        SendKeepPlaying(monotonic_ms);
        --keep_alive_ticks_left_;
        keep_alive_enabled_ = keep_alive_ticks_left_ > 0;
        next_keep_alive_ms_ = keep_alive_enabled_ ? monotonic_ms + 60000 : 0;
    }
}

} // namespace cloudgenshin::sdk
