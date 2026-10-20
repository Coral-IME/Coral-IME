#include "coral/transport/session_wire.h"
#undef NDEBUG
#include <cassert>
#include <limits>
#include <string>
#include <vector>

using namespace coral::transport;
namespace
{
void set_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<std::uint8_t>(value >> (8 * index));
}
void rejects_request(const std::vector<std::uint8_t>& frame)
{
    SessionRequest destination;
    destination.request_id = 777; destination.word = "preserved";
    std::string error;
    assert(!decode_request(frame.data(), frame.size(), destination, error));
    assert(!error.empty() && destination.request_id == 777 && destination.word == "preserved");
}
void rejects_reply(const std::vector<std::uint8_t>& frame)
{
    SessionReply destination;
    destination.client_id = 777; destination.engine.message = "preserved";
    std::string error;
    assert(!decode_reply(frame.data(), frame.size(), destination, error));
    assert(!error.empty() && destination.client_id == 777 && destination.engine.message == "preserved");
}
void request_roundtrip(const SessionRequest& source, bool all_truncations = true)
{
    std::string error = "old failure";
    std::vector<std::uint8_t> frame;
    assert(encode_request(source, frame, error) && error.empty());
    SessionRequest parsed;
    assert(decode_request(frame.data(), frame.size(), parsed, error) && error.empty());
    std::vector<std::uint8_t> repeated;
    assert(encode_request(parsed, repeated, error) && repeated == frame);
    if (all_truncations)
        for (std::size_t size = 0; size < frame.size(); ++size)
            rejects_request({frame.begin(), frame.begin() + size});
    else
        rejects_request({frame.begin(), frame.end() - 1});
    auto tail = frame; tail.push_back(0); rejects_request(tail);
    set_u32(tail, 12, static_cast<std::uint32_t>(tail.size() - kEnvelopeBytes));
    rejects_request(tail);
    rejects_reply(frame);
}
void invalid_request(const SessionRequest& source)
{
    std::vector<std::uint8_t> destination{4, 5, 6};
    std::string error;
    assert(!encode_request(source, destination, error));
    assert((destination == std::vector<std::uint8_t>{4, 5, 6}) && !error.empty());
}
}
int main()
{
    // 独立的基准夹具，包含全部六个 uint64 字段及其字节顺序。
    SessionRequest hello;
    hello.operation = SessionOperation::kHello;
    hello.request_id = 0x0807060504030201ULL;
    hello.session_id = 0x1817161514131211ULL;
    hello.session_generation = 0x2827262524232221ULL;
    hello.server_generation = 0x3837363534333231ULL;
    hello.client_id = 0x4847464544434241ULL;
    hello.deadline_tick_ms = 0x5857565554535251ULL;
    const std::vector<std::uint8_t> golden{
        0x43,0x53,0x57,0x34, 5,0,72,0, 1,0,0,0, 0,0,0,0,
        1,2,3,4,5,6,7,8, 0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
        0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28, 0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,
        0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48, 0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,
        0,0,0,0,0,0,0,0};
    std::string error;
    std::vector<std::uint8_t> frame;
    assert(encode_request(hello, frame, error) && frame == golden);
    for (const auto index : {0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 64, 71})
    {
        auto broken = golden; broken[index] = 255; rejects_request(broken);
    }
    auto old = golden; old[0] = 'C'; old[1] = 'R'; old[2] = 'A'; old[3] = 'L'; rejects_request(old);
    old = golden; old[4] = 3; rejects_request(old);
    old = golden; old[4] = 4; rejects_request(old);
    SessionRequest parse_fixture;
    parse_fixture.operation = SessionOperation::kParseFull;
    parse_fixture.condition = "ab"; parse_fixture.pinyin = std::string("a\0", 2);
    parse_fixture.offset = 0x01020304; parse_fixture.page_size = 5; parse_fixture.mode = 1;
    assert(encode_request(parse_fixture, frame, error));
    const std::vector<std::uint8_t> parse_body{2,0,0,0,'a','b',2,0,0,0,'a',0,4,3,2,1,5,0,0,0,1,0,0,0};
    assert((std::vector<std::uint8_t>{frame.begin() + kEnvelopeBytes, frame.end()} == parse_body));
    SessionRequest null_output;
    assert(!decode_request(nullptr, 100, null_output, error));
    assert(!decode_request(golden.data(), std::numeric_limits<std::size_t>::max(), null_output, error));

    for (std::uint16_t value = 1; value <= 16; ++value)
    {
        SessionRequest source = hello;
        source.operation = static_cast<SessionOperation>(value);
        source.key_event.codepoint = 0x10ffff; source.key_event.key = coral::composition::Key::kDelete;
        source.key_event.shift = true; source.key_event.alt = true;
        source.input_context.process_name = std::string("a\0b", 3);
        source.condition = "zhong"; source.pinyin = "guo"; source.word = u8"中国";
        source.offset = 17; source.page_size = 512; source.mode = coral::composition::kModeDoublepinyin;
        source.candidate_index = 511; source.condition_id = 0xffffffffU;
        source.cancel_request_id = 88;
        source.config.revision = 0xffffffffffffffffULL; source.config.limit_count = 512;
        source.config.smart_prediction = false;
        source.config.combined_lexicon_version = -1;
        source.config.fuzzy_choices = {std::numeric_limits<std::int32_t>::min(), -1, 0, std::numeric_limits<std::int32_t>::max()};
        request_roundtrip(source);
    }
    for (auto variant : {coral::LanguageVariant::Simplified, coral::LanguageVariant::Traditional})
        for (auto action : {coral::CandidateAction::PinFirst, coral::CandidateAction::UnpinFirst, coral::CandidateAction::Disable})
        {
            SessionRequest source;
            source.operation = SessionOperation::kApplyCandidateAction;
            source.pinyin = "bei'jing"; source.word = u8"北京"; source.variant = variant; source.candidate_action = action;
            request_roundtrip(source);
            assert(encode_request(source, frame, error));
            auto broken = frame; broken.back() = 255; rejects_request(broken);
            broken = frame; broken[broken.size() - 2] = 255; rejects_request(broken);
        }
    SessionRequest invalid;
    invalid.operation = static_cast<SessionOperation>(0); invalid_request(invalid);
    invalid.operation = static_cast<SessionOperation>(17); invalid_request(invalid);
    invalid.operation = SessionOperation::kParseFull; invalid.page_size = 5;
    invalid.offset = -1; invalid_request(invalid); invalid.offset = 0;
    invalid.page_size = 0; invalid_request(invalid); invalid.page_size = 513; invalid_request(invalid);
    invalid.page_size = 5; invalid.mode = 2; invalid_request(invalid);
    invalid.operation = SessionOperation::kSelectCandidate;
    invalid.candidate_index = -1; invalid_request(invalid); invalid.candidate_index = 512; invalid_request(invalid);
    invalid.operation = SessionOperation::kCancel; invalid_request(invalid);
    invalid.operation = SessionOperation::kSendKey;
    invalid.key_event.codepoint = 0xd800; invalid_request(invalid);
    invalid.key_event.codepoint = 0x110000; invalid_request(invalid);
    invalid.key_event.codepoint = 0; invalid.key_event.key = static_cast<coral::composition::Key>(999); invalid_request(invalid);
    invalid.key_event.key = coral::composition::Key::kNone;
    invalid.input_context.process_name.assign(kMaxStringBytes, 'x'); request_roundtrip(invalid, false);
    invalid.input_context.process_name.push_back('x'); invalid_request(invalid);
    invalid = {}; invalid.operation = SessionOperation::kUpdateConfig;
    invalid.config.limit_count = 513; invalid_request(invalid);
    invalid.config.limit_count = 0; invalid_request(invalid);
    invalid.config.limit_count = 10; invalid.config.fuzzy_choices.resize(65); invalid_request(invalid);

    // 语义损坏在解码时同样被拒绝，而不仅在编码时。
    SessionRequest key;
    key.operation = SessionOperation::kSendKey;
    assert(encode_request(key, frame, error));
    auto broken = frame; broken[kEnvelopeBytes + 8] = 2; rejects_request(broken);
    broken = frame; set_u32(broken, kEnvelopeBytes, 0xdfff); rejects_request(broken);
    broken = frame; set_u32(broken, kEnvelopeBytes + 15, 0xffffffffU); rejects_request(broken);
    SessionRequest config;
    config.operation = SessionOperation::kUpdateConfig;
    assert(encode_request(config, frame, error));
    broken = frame; broken[kEnvelopeBytes + 8] = 2; rejects_request(broken);
    broken = frame; set_u32(broken, kEnvelopeBytes + 15, 0); rejects_request(broken);
    broken = frame; set_u32(broken, kEnvelopeBytes + 23, 65); rejects_request(broken);

    const std::vector<std::uint8_t> config_body{
        0,0,0,0,0,0,0,0, 1,1,1,1,0,0,0, 10,0,0,0, 255,255,255,255, 0,0,0,0};
    assert((std::vector<std::uint8_t>{frame.begin() + kEnvelopeBytes, frame.end()} == config_body));
    for (std::size_t offset = 8; offset < 15; ++offset) {
        broken = frame; broken[kEnvelopeBytes + offset] = 2; rejects_request(broken);
    }
    config.config.smart_prediction = false;
    assert(encode_request(config, frame, error));
    SessionRequest disabled;
    assert(decode_request(frame.data(), frame.size(), disabled, error) && !disabled.config.smart_prediction);

    SessionReply reply;
    reply.operation = SessionOperation::kParseFull; reply.client_id = hello.client_id;
    reply.engine.request_id = hello.request_id; reply.engine.session_id = hello.session_id;
    reply.engine.server_generation = hello.server_generation; reply.engine.result = true;
    reply.engine.output.candidates.offset = 3; reply.engine.output.candidates.has_next_page = true;
    reply.engine.output.candidates.candidates = {{1, u8"瞐", "umumumu", u8"瞐(mò)"}, {2, "9", "v(1+2)*3", ""}};
    assert(encode_reply(reply, frame, error));
    SessionReply parsed;
    assert(decode_reply(frame.data(), frame.size(), parsed, error));
    assert(parsed.client_id == reply.client_id && parsed.engine.request_id == reply.engine.request_id);
    assert(parsed.engine.output.candidates.candidates[0].display_text == u8"瞐(mò)");
    std::vector<std::uint8_t> repeated;
    assert(encode_reply(parsed, repeated, error) && repeated == frame);
    for (std::size_t size = 0; size < frame.size(); ++size) rejects_reply({frame.begin(), frame.begin() + size});
    broken = frame; broken[56] = 1; rejects_reply(broken);
    broken = frame; set_u32(broken, kEnvelopeBytes, 99); rejects_reply(broken);
    broken = frame; broken[kEnvelopeBytes + 4] = 2; rejects_reply(broken);
    broken = frame; set_u32(broken, kEnvelopeBytes + 11, 0xffffffffU); rejects_reply(broken);
    broken = frame; broken.push_back(0); set_u32(broken, 12, static_cast<std::uint32_t>(broken.size() - kEnvelopeBytes)); rejects_reply(broken);
    auto oversized = reply;
    oversized.engine.output.candidates.candidates.resize(513);
    auto unchanged = frame;
    assert(!encode_reply(oversized, frame, error) && frame == unchanged);
    SessionReply boundary;
    boundary.engine.output.composition.assign(kMaxStringBytes, 'c');
    boundary.engine.output.commit_text.assign(kMaxStringBytes, 't');
    boundary.engine.message.assign(kMaxStringBytes, 'm');
    boundary.engine.output.candidates.candidates = {{1, std::string(kMaxStringBytes - 52, 'w'), "", ""}};
    assert(encode_reply(boundary, frame, error) && frame.size() == kEnvelopeBytes + kMaxPayloadBytes);
    assert(decode_reply(frame.data(), frame.size(), parsed, error));
    assert(parsed.engine.output.candidates.candidates[0].word == boundary.engine.output.candidates.candidates[0].word);
    unchanged = frame;
    boundary.engine.output.candidates.candidates[0].word.push_back('w');
    assert(!encode_reply(boundary, frame, error) && frame == unchanged);
    frame.push_back(0); rejects_reply(frame);
    SessionReply reply_fixture;
    reply_fixture.engine.result = true; reply_fixture.engine.value = 0x04030201;
    reply_fixture.engine.output.consumed = true; reply_fixture.engine.output.candidates_visible = true;
    reply_fixture.engine.output.candidates.offset = 2; reply_fixture.engine.output.candidates.has_next_page = true;
    reply_fixture.engine.output.composition = "c";
    reply_fixture.engine.output.candidates.candidates = {{0x0807060504030201ULL, "w", "p", "d"}};
    assert(encode_reply(reply_fixture, frame, error));
    const std::vector<std::uint8_t> reply_body{0,0,0,0,1,1,2,3,4,1,1,2,0,0,0,1,
        1,0,0,0,'c',0,0,0,0,0,0,0,0,1,0,0,0,1,2,3,4,5,6,7,8,
        1,0,0,0,'w',1,0,0,0,'p',1,0,0,0,'d'};
    assert((std::vector<std::uint8_t>{frame.begin() + kEnvelopeBytes, frame.end()} == reply_body));
    broken = frame; set_u32(broken, kEnvelopeBytes + 29, 513); rejects_reply(broken);
    oversized = reply;
    oversized.engine.output.composition.assign(kMaxStringBytes, 'x');
    oversized.engine.output.commit_text.assign(kMaxStringBytes, 'x');
    oversized.engine.message.assign(kMaxStringBytes, 'x');
    oversized.engine.output.candidates.candidates[0].word.assign(kMaxStringBytes, 'x');
    unchanged = frame;
    assert(!encode_reply(oversized, frame, error) && frame == unchanged);
    assert(deadline_expired(5, 5) && deadline_expired(5, 6) && !deadline_expired(5, 4) && !deadline_expired(0, 999));
    return 0;
}
