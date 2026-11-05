#pragma once

// Wire codec for the WeLink "welink" DataChannel used by Endfield Cloud.
//
// Message shapes re-implement the official jspb-generated protobuf
// definitions of the web client bundle (offsets documented in
// docs/cloudgenshin-endfield-findings.md):
//   WLCGV2.SimpleCmd       @3342111  cmd(1,string) arg(2,string)
//                                    data(3,bytes) arg1(4,string) field5(string,unused)
//   CG.PROTO.InputOPData   @3216971  op_list: repeated OneInputOPData (field 1)
//   CG.PROTO.OneInputOPData @3211172 input_op(1,int32) input_state(2,enum)
//                                    value(3,int32) cursor_pos(4,CoordinatePos)
//                                    mouse_pos(5,CoordinatePos) op_device(6,int32)
//                                    xinput_user_index(7,int32)
//   CG.PROTO.CoordinatePos @3208419  x(1,int32) y(2,int32)
//
// jspb semantics that matter on the wire:
//  - a field is present only when it was explicitly set, even when the
//    value is 0 (callers pass std::nullopt to omit a field);
//  - negative int32 values sign-extend to 64 bits (binary varint).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudgenshin::endfield {

struct SimpleCmd {
    std::string cmd;                                // field 1, always written
    std::optional<std::string> arg;                 // field 2
    std::optional<std::vector<std::uint8_t>> data;  // field 3
    std::optional<std::string> arg1;                // field 4
    std::optional<std::string> arg2;                // field 5

    std::vector<std::uint8_t> Serialize() const;

    // Lenient parse: known fields are decoded, unknown ones skipped.
    // Returns std::nullopt only when the buffer is malformed.
    static std::optional<SimpleCmd> Parse(const std::uint8_t* data, std::size_t size);
    static std::optional<SimpleCmd> Parse(const std::vector<std::uint8_t>& bytes) {
        return Parse(bytes.data(), bytes.size());
    }
    static std::optional<SimpleCmd> Parse(std::string_view bytes) {
        return Parse(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    }
};

struct CoordinatePos {
    std::int32_t x = 0;  // field 1
    std::int32_t y = 0;  // field 2
};

// One operation of the CG.PROTO input protocol. Every field mirrors the
// jspb "set" state: std::nullopt means the field is absent from the wire.
struct OneInputOpData {
    std::optional<std::int32_t> input_op;            // field 1
    std::optional<std::int32_t> input_state;         // field 2 (enum, varint)
    std::optional<std::int32_t> value;               // field 3
    std::optional<CoordinatePos> cursor_pos;         // field 4
    std::optional<CoordinatePos> mouse_pos;          // field 5
    std::optional<std::int32_t> op_device;           // field 6
    std::optional<std::int32_t> xinput_user_index;   // field 7

    void AppendTo(std::vector<std::uint8_t>& out) const;
};

// Wire form of one input op (findings 9.4):
//   SimpleCmd{cmd:"CtrlMsg", data: InputOPData{op_list:[op]}}
std::vector<std::uint8_t> CtrlMsg(const OneInputOpData& op);

// Multiple ops in one InputOPData (the web client's full-state / "V2" form).
// Op order is preserved on the wire.
std::vector<std::uint8_t> CtrlMsg(const std::vector<OneInputOpData>& ops);
// Official setGameResolution -> SimpleCmd{cmd, arg:width, arg1:height}.
std::vector<std::uint8_t> GameResolutionFrame(int width,int height);

// encodeURI(text): UTF-8 bytes outside the JS encodeURI unescaped set become
// %XX with uppercase hex. ASCII in/out (percent escapes are ASCII).
std::string UriEncode(std::string_view text);

// btoa(encodeURI(text)): the intermediate string the web client passes to
// jspb. NOTE: jspb represents bytes fields as base64 strings and its writer
// decodes them, so this is *not* the PackedData wire payload; use UriEncode()
// for the "data" field (findings 14.3 correction).
std::string BtoaEncodeUri(std::string_view text);

}  // namespace cloudgenshin::endfield
