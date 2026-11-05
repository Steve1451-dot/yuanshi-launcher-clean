#include "endfield/wl_proto.hpp"

namespace cloudgenshin::endfield {
std::vector<std::uint8_t> GameResolutionFrame(int width,int height) {
    if(width<=0 || height<=0 || width>8192 || height>8192) return {};
    SimpleCmd message;message.cmd="SetGameResolution";
    message.arg=std::to_string(width);message.arg1=std::to_string(height);
    return message.Serialize();
}
namespace {

// --- protobuf writing (jspb BinaryWriter semantics) -----------------------

void AppendVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
}

void AppendKey(std::vector<std::uint8_t>& out, std::uint32_t field,
               std::uint32_t wire_type) {
    AppendVarint(out, (static_cast<std::uint64_t>(field) << 3) | wire_type);
}

// Match jspb BinaryWriter.writeInt32: negative values sign-extend to 64 bits.
void AppendInt32(std::vector<std::uint8_t>& out, std::uint32_t field,
                 std::int32_t value) {
    AppendKey(out, field, 0);
    AppendVarint(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

void AppendString(std::vector<std::uint8_t>& out, std::uint32_t field,
                  std::string_view value) {
    AppendKey(out, field, 2);
    AppendVarint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

void AppendBytes(std::vector<std::uint8_t>& out, std::uint32_t field,
                 const std::vector<std::uint8_t>& bytes) {
    AppendKey(out, field, 2);
    AppendVarint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void AppendMessage(std::vector<std::uint8_t>& out, std::uint32_t field,
                   const std::vector<std::uint8_t>& message) {
    AppendBytes(out, field, message);
}

// --- protobuf reading ------------------------------------------------------

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size)
        : p_(data), end_(data + size) {}

    bool Done() const { return p_ >= end_; }

    bool ReadVarint(std::uint64_t& out) {
        out = 0;
        unsigned shift = 0;
        while (p_ < end_) {
            const auto byte = *p_++;
            if (shift < 64) out |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) return true;
            shift += 7;
            if (shift >= 70) return false;  // more than 10 bytes
        }
        return false;
    }

    bool ReadKey(std::uint32_t& field, std::uint32_t& wire_type) {
        std::uint64_t key = 0;
        if (!ReadVarint(key)) return false;
        field = static_cast<std::uint32_t>(key >> 3);
        wire_type = static_cast<std::uint32_t>(key & 0x7);
        return field != 0;
    }

    bool ReadBytes(std::string_view& out) {
        std::uint64_t size = 0;
        if (!ReadVarint(size)) return false;
        if (size > static_cast<std::uint64_t>(end_ - p_)) return false;
        out = std::string_view(reinterpret_cast<const char*>(p_),
                               static_cast<std::size_t>(size));
        p_ += size;
        return true;
    }

    bool Skip(std::uint32_t wire_type) {
        switch (wire_type) {
            case 0: {
                std::uint64_t ignored = 0;
                return ReadVarint(ignored);
            }
            case 1: {
                if (end_ - p_ < 8) return false;
                p_ += 8;
                return true;
            }
            case 2: {
                std::string_view ignored;
                return ReadBytes(ignored);
            }
            case 5: {
                if (end_ - p_ < 4) return false;
                p_ += 4;
                return true;
            }
            default:
                return false;
        }
    }

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

}  // namespace

std::vector<std::uint8_t> SimpleCmd::Serialize() const {
    std::vector<std::uint8_t> out;
    AppendString(out, 1, cmd);
    if (arg) AppendString(out, 2, *arg);
    if (data) AppendBytes(out, 3, *data);
    if (arg1) AppendString(out, 4, *arg1);
    if (arg2) AppendString(out, 5, *arg2);
    return out;
}

std::optional<SimpleCmd> SimpleCmd::Parse(const std::uint8_t* data, std::size_t size) {
    SimpleCmd msg;
    bool has_cmd = false;
    Reader reader(data, size);
    while (!reader.Done()) {
        std::uint32_t field = 0;
        std::uint32_t wire_type = 0;
        if (!reader.ReadKey(field, wire_type)) return std::nullopt;
        std::string_view bytes;
        switch (field) {
            case 1:
            case 2:
            case 4:
            case 5: {
                if (wire_type != 2 || !reader.ReadBytes(bytes)) return std::nullopt;
                if (field == 1) {
                    msg.cmd.assign(bytes);
                    has_cmd = true;
                } else if (field == 2) {
                    msg.arg = std::string(bytes);
                } else if (field == 4) {
                    msg.arg1 = std::string(bytes);
                } else {
                    msg.arg2 = std::string(bytes);
                }
                break;
            }
            case 3: {
                if (wire_type != 2 || !reader.ReadBytes(bytes)) return std::nullopt;
                msg.data = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
                break;
            }
            default: {
                if (!reader.Skip(wire_type)) return std::nullopt;
                break;
            }
        }
    }
    if (!has_cmd) return std::nullopt;
    return msg;
}

void OneInputOpData::AppendTo(std::vector<std::uint8_t>& out) const {
    if (input_op) AppendInt32(out, 1, *input_op);
    if (input_state) AppendInt32(out, 2, *input_state);
    if (value) AppendInt32(out, 3, *value);
    if (cursor_pos) {
        std::vector<std::uint8_t> pos;
        AppendInt32(pos, 1, cursor_pos->x);
        AppendInt32(pos, 2, cursor_pos->y);
        AppendMessage(out, 4, pos);
    }
    if (mouse_pos) {
        std::vector<std::uint8_t> pos;
        AppendInt32(pos, 1, mouse_pos->x);
        AppendInt32(pos, 2, mouse_pos->y);
        AppendMessage(out, 5, pos);
    }
    if (op_device) AppendInt32(out, 6, *op_device);
    if (xinput_user_index) AppendInt32(out, 7, *xinput_user_index);
}

std::vector<std::uint8_t> CtrlMsg(const OneInputOpData& op) {
    std::vector<std::uint8_t> input_op_data;
    std::vector<std::uint8_t> one_op;
    op.AppendTo(one_op);
    AppendMessage(input_op_data, 1, one_op);  // op_list[0]

    SimpleCmd cmd;
    cmd.cmd = "CtrlMsg";
    cmd.data = std::move(input_op_data);
    return cmd.Serialize();
}

std::vector<std::uint8_t> CtrlMsg(const std::vector<OneInputOpData>& ops) {
    std::vector<std::uint8_t> input_op_data;
    for (const auto& op : ops) {
        std::vector<std::uint8_t> one_op;
        op.AppendTo(one_op);
        AppendMessage(input_op_data, 1, one_op);
    }

    SimpleCmd cmd;
    cmd.cmd = "CtrlMsg";
    cmd.data = std::move(input_op_data);
    return cmd.Serialize();
}

namespace {

// encodeURI leaves these ASCII characters unescaped (ECMA-262 uriReserved +
// uriUnescaped + "#").
bool UriUnescaped(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    switch (c) {
        case '-': case '_': case '.': case '!': case '~': case '*': case '\'':
        case '(': case ')': case ';': case '/': case '?': case ':': case '@':
        case '&': case '=': case '+': case '$': case ',': case '#':
            return true;
        default:
            return false;
    }
}

}  // namespace

std::string UriEncode(std::string_view text) {
    // encodeURI: UTF-8 bytes outside the unescaped set become %XX (uppercase).
    std::string escaped;
    escaped.reserve(text.size() * 2);
    static const char kHex[] = "0123456789ABCDEF";
    for (const unsigned char c : text) {
        if (UriUnescaped(c)) {
            escaped.push_back(static_cast<char>(c));
        } else {
            escaped.push_back('%');
            escaped.push_back(kHex[c >> 4]);
            escaped.push_back(kHex[c & 0x0f]);
        }
    }
    return escaped;
}

std::string BtoaEncodeUri(std::string_view text) {
    const std::string escaped = UriEncode(text);

    // btoa: standard base64 (with padding) of the ASCII escape result.
    static const char kBase64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((escaped.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < escaped.size()) {
        const std::uint32_t v =
            (static_cast<std::uint32_t>(static_cast<unsigned char>(escaped[i])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(escaped[i + 1])) << 8) |
            static_cast<std::uint32_t>(static_cast<unsigned char>(escaped[i + 2]));
        out.push_back(kBase64[(v >> 18) & 0x3f]);
        out.push_back(kBase64[(v >> 12) & 0x3f]);
        out.push_back(kBase64[(v >> 6) & 0x3f]);
        out.push_back(kBase64[v & 0x3f]);
        i += 3;
    }
    if (i + 1 == escaped.size()) {
        const std::uint32_t v =
            static_cast<std::uint32_t>(static_cast<unsigned char>(escaped[i])) << 16;
        out.push_back(kBase64[(v >> 18) & 0x3f]);
        out.push_back(kBase64[(v >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == escaped.size()) {
        const std::uint32_t v =
            (static_cast<std::uint32_t>(static_cast<unsigned char>(escaped[i])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(escaped[i + 1])) << 8);
        out.push_back(kBase64[(v >> 18) & 0x3f]);
        out.push_back(kBase64[(v >> 12) & 0x3f]);
        out.push_back(kBase64[(v >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

}  // namespace cloudgenshin::endfield
