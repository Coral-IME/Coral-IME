#include "coral/transport/session_wire.h"
#include <limits>
#include <type_traits>
#include <utility>

namespace coral::transport
{
namespace
{
class ByteSink
{
public:
    explicit ByteSink(std::size_t limit) : limit_(limit) {}
    template<class T> bool integer(T value)
    {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        if (bytes_.size() > limit_ || sizeof(T) > limit_ - bytes_.size()) return false;
        for (std::size_t index = 0; index < sizeof(T); ++index)
            bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
        return true;
    }
    bool signed_integer(std::int32_t value) { return integer(static_cast<std::uint32_t>(value)); }
    bool boolean(bool value) { return integer<std::uint8_t>(value ? 1 : 0); }
    bool text(const std::string& value)
    {
        if (value.size() > kMaxStringBytes || !integer<std::uint32_t>(static_cast<std::uint32_t>(value.size())) ||
            value.size() > limit_ - bytes_.size()) return false;
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
private:
    std::size_t limit_;
    std::vector<std::uint8_t> bytes_;
};
class ByteCursor
{
public:
    ByteCursor(const std::uint8_t* data, std::size_t size) : data_(data), remaining_(size) {}
    template<class T> bool integer(T& value)
    {
        static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
        if (remaining_ < sizeof(T)) return false;
        std::uint64_t decoded = 0;
        for (std::size_t index = 0; index < sizeof(T); ++index)
            decoded |= static_cast<std::uint64_t>(data_[index]) << (8 * index);
        value = static_cast<T>(decoded);
        data_ += sizeof(T); remaining_ -= sizeof(T);
        return true;
    }
    bool signed_integer(int& value)
    {
        static_assert(std::numeric_limits<int>::max() >= std::numeric_limits<std::int32_t>::max());
        std::uint32_t decoded = 0;
        if (!integer(decoded)) return false;
        value = decoded <= 0x7fffffffU ? static_cast<std::int32_t>(decoded)
            : -1 - static_cast<std::int32_t>(0xffffffffU - decoded);
        return true;
    }
    bool boolean(bool& value)
    {
        std::uint8_t decoded = 0;
        if (!integer(decoded) || decoded > 1) return false;
        value = decoded != 0; return true;
    }
    bool text(std::string& value)
    {
        std::uint32_t length = 0;
        if (!integer(length) || length > kMaxStringBytes || length > remaining_) return false;
        value.assign(reinterpret_cast<const char*>(data_), length);
        data_ += length; remaining_ -= length;
        return true;
    }
    std::size_t remaining() const { return remaining_; }
private:
    const std::uint8_t* data_;
    std::size_t remaining_;
};
struct Envelope
{
    SessionOperation operation = SessionOperation::kPing;
    std::uint64_t request_id = 0, session_id = 0, session_generation = 0;
    std::uint64_t server_generation = 0, client_id = 0, deadline_tick_ms = 0;
    std::uint32_t payload_size = 0;
};
bool known_operation(SessionOperation operation)
{
    const auto value = static_cast<std::uint16_t>(operation);
    return value >= 1 && value <= 18;
}
bool valid_request(const SessionRequest& request)
{
    if (!known_operation(request.operation)) return false;
    switch (request.operation)
    {
    case SessionOperation::kSendKey:
        return static_cast<std::uint32_t>(request.key_event.key) <= static_cast<std::uint32_t>(composition::Key::kDelete) &&
            request.key_event.codepoint <= 0x10ffff &&
            (request.key_event.codepoint < 0xd800 || request.key_event.codepoint > 0xdfff);
    case SessionOperation::kParseFull:
        return request.offset >= 0 && request.page_size > 0 && request.page_size <= static_cast<int>(kMaxCandidates) &&
            (request.mode == composition::kModeQuanpin || request.mode == composition::kModeDoublepinyin);
    case SessionOperation::kSelectCandidate:
        return request.candidate_index >= 0 && request.candidate_index < static_cast<int>(kMaxCandidates);
    case SessionOperation::kApplyCandidateAction:
        return IsValidCandidateAction(request.candidate_action) && IsValidLanguageVariant(request.variant);
    case SessionOperation::kUpdateConfig:
        return request.config.limit_count > 0 && request.config.limit_count <= static_cast<int>(kMaxCandidates) &&
            request.config.fuzzy_choices.size() <= kMaxFuzzyChoices;
    case SessionOperation::kCancel:
        return request.cancel_request_id != 0;
    case SessionOperation::kRefineCandidates:
    case SessionOperation::kCancelRefinement:
        return request.query_request_id != 0;
    default: return true;
    }
}
bool write_request_body(const SessionRequest& request, ByteSink& sink)
{
    switch (request.operation)
    {
    case SessionOperation::kSendKey:
        return sink.integer(request.key_event.codepoint) && sink.integer(static_cast<std::uint32_t>(request.key_event.key)) &&
            sink.boolean(request.key_event.shift) && sink.boolean(request.key_event.ctrl) && sink.boolean(request.key_event.alt) &&
            sink.integer(request.key_event.vk) && sink.text(request.input_context.process_name);
    case SessionOperation::kParseFull:
        return sink.text(request.condition) && sink.text(request.pinyin) && sink.signed_integer(request.offset) &&
            sink.signed_integer(request.page_size) && sink.signed_integer(request.mode);
    case SessionOperation::kSelectCandidate: return sink.signed_integer(request.candidate_index);
    case SessionOperation::kRecordSelected:
        return sink.text(request.pinyin) && sink.text(request.word) && sink.integer(request.condition_id);
    case SessionOperation::kRevertLastAction: return sink.text(request.word);
    case SessionOperation::kApplyCandidateAction:
        return sink.text(request.pinyin) && sink.text(request.word) &&
            sink.integer(static_cast<std::uint8_t>(request.candidate_action)) && sink.integer(static_cast<std::uint8_t>(request.variant));
    case SessionOperation::kUpdateConfig:
    {
        const auto& config = request.config;
        if (!sink.integer(config.revision) || !sink.boolean(config.u_mode) || !sink.boolean(config.v_mode) ||
            !sink.boolean(config.smart_prediction) || !sink.boolean(config.pinyin_correction) || !sink.boolean(config.full_width) || !sink.boolean(config.traditional_chinese) ||
            !sink.boolean(config.show_infos) || !sink.signed_integer(config.limit_count) || !sink.signed_integer(config.combined_lexicon_version) ||
            !sink.integer<std::uint32_t>(static_cast<std::uint32_t>(config.fuzzy_choices.size()))) return false;
        for (int choice : config.fuzzy_choices) if (!sink.signed_integer(choice)) return false;
        return true;
    }
    case SessionOperation::kCancel: return sink.integer(request.cancel_request_id);
    case SessionOperation::kRefineCandidates:
    case SessionOperation::kCancelRefinement: return sink.integer(request.query_request_id);
    default: return true;
    }
}
bool read_request_body(SessionRequest& request, ByteCursor& cursor)
{
    switch (request.operation)
    {
    case SessionOperation::kSendKey:
    {
        std::uint32_t key = 0;
        if (!cursor.integer(request.key_event.codepoint) || !cursor.integer(key) || !cursor.boolean(request.key_event.shift) ||
            !cursor.boolean(request.key_event.ctrl) || !cursor.boolean(request.key_event.alt) || !cursor.integer(request.key_event.vk) ||
            !cursor.text(request.input_context.process_name)) return false;
        request.key_event.key = static_cast<composition::Key>(key); return true;
    }
    case SessionOperation::kParseFull:
        return cursor.text(request.condition) && cursor.text(request.pinyin) && cursor.signed_integer(request.offset) &&
            cursor.signed_integer(request.page_size) && cursor.signed_integer(request.mode);
    case SessionOperation::kSelectCandidate: return cursor.signed_integer(request.candidate_index);
    case SessionOperation::kRecordSelected:
        return cursor.text(request.pinyin) && cursor.text(request.word) && cursor.integer(request.condition_id);
    case SessionOperation::kRevertLastAction: return cursor.text(request.word);
    case SessionOperation::kApplyCandidateAction:
    {
        std::uint8_t action = 0, variant = 0;
        if (!cursor.text(request.pinyin) || !cursor.text(request.word) || !cursor.integer(action) || !cursor.integer(variant)) return false;
        request.candidate_action = static_cast<CandidateAction>(action);
        request.variant = static_cast<LanguageVariant>(variant); return true;
    }
    case SessionOperation::kUpdateConfig:
    {
        auto& config = request.config;
        std::uint32_t count = 0;
        if (!cursor.integer(config.revision) || !cursor.boolean(config.u_mode) || !cursor.boolean(config.v_mode) ||
            !cursor.boolean(config.smart_prediction) || !cursor.boolean(config.pinyin_correction) || !cursor.boolean(config.full_width) || !cursor.boolean(config.traditional_chinese) ||
            !cursor.boolean(config.show_infos) || !cursor.signed_integer(config.limit_count) || !cursor.signed_integer(config.combined_lexicon_version) ||
            !cursor.integer(count) || count > kMaxFuzzyChoices || count > cursor.remaining() / 4) return false;
        config.fuzzy_choices.resize(count);
        for (int& choice : config.fuzzy_choices) if (!cursor.signed_integer(choice)) return false;
        return true;
    }
    case SessionOperation::kCancel: return cursor.integer(request.cancel_request_id);
    case SessionOperation::kRefineCandidates:
    case SessionOperation::kCancelRefinement: return cursor.integer(request.query_request_id);
    default: return true;
    }
}
bool valid_reply(const SessionReply& reply)
{
    return known_operation(reply.operation) &&
        static_cast<std::uint32_t>(reply.engine.status) <= static_cast<std::uint32_t>(composition::SessionStatus::kInternalError) &&
        reply.engine.output.candidates.offset >= 0 && reply.engine.output.candidates.candidates.size() <= kMaxCandidates;
}
bool write_reply_body(const SessionReply& reply, ByteSink& sink)
{
    const auto& engine = reply.engine;
    const auto& output = engine.output;
    const auto& page = output.candidates;
    if (!sink.integer(static_cast<std::uint32_t>(engine.status)) || !sink.boolean(engine.result) || !sink.integer(engine.value) ||
        !sink.boolean(output.consumed) || !sink.boolean(output.candidates_visible) || !sink.signed_integer(page.offset) ||
        !sink.boolean(page.has_next_page) || !sink.text(output.composition) || !sink.text(output.commit_text) || !sink.text(engine.message) ||
        !sink.integer<std::uint32_t>(static_cast<std::uint32_t>(page.candidates.size()))) return false;
    for (const auto& candidate : page.candidates)
        if (!sink.integer(candidate.id) || !sink.text(candidate.word) || !sink.text(candidate.pinyin) || !sink.text(candidate.display_text)) return false;
    return true;
}
bool read_reply_body(SessionReply& reply, ByteCursor& cursor)
{
    auto& engine = reply.engine;
    auto& output = engine.output;
    auto& page = output.candidates;
    std::uint32_t status = 0, count = 0;
    if (!cursor.integer(status) || !cursor.boolean(engine.result) || !cursor.integer(engine.value) ||
        !cursor.boolean(output.consumed) || !cursor.boolean(output.candidates_visible) || !cursor.signed_integer(page.offset) ||
        !cursor.boolean(page.has_next_page) || !cursor.text(output.composition) || !cursor.text(output.commit_text) || !cursor.text(engine.message) ||
        !cursor.integer(count) || count > kMaxCandidates || count > cursor.remaining() / 20) return false;
    engine.status = static_cast<composition::SessionStatus>(status);
    page.candidates.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        composition::Candidate candidate;
        if (!cursor.integer(candidate.id) || !cursor.text(candidate.word) || !cursor.text(candidate.pinyin) || !cursor.text(candidate.display_text)) return false;
        page.candidates.push_back(std::move(candidate));
    }
    return true;
}
bool read_envelope(const void* data, std::size_t size, bool is_reply, Envelope& envelope)
{
    if (!data || size < kEnvelopeBytes || size - kEnvelopeBytes > kMaxPayloadBytes) return false;
    ByteCursor cursor(static_cast<const std::uint8_t*>(data), kEnvelopeBytes);
    std::uint32_t magic = 0;
    std::uint16_t version = 0, envelope_size = 0, operation = 0, direction = 0;
    std::uint64_t reserved = 0;
    if (!cursor.integer(magic) || !cursor.integer(version) || !cursor.integer(envelope_size) || !cursor.integer(operation) ||
        !cursor.integer(direction) || !cursor.integer(envelope.payload_size) || !cursor.integer(envelope.request_id) ||
        !cursor.integer(envelope.session_id) || !cursor.integer(envelope.session_generation) || !cursor.integer(envelope.server_generation) ||
        !cursor.integer(envelope.client_id) || !cursor.integer(envelope.deadline_tick_ms) || !cursor.integer(reserved)) return false;
    envelope.operation = static_cast<SessionOperation>(operation);
    return magic == kWireMagic && version == kWireVersion && envelope_size == kEnvelopeBytes &&
        direction == (is_reply ? 1 : 0) && envelope.payload_size == size - kEnvelopeBytes && reserved == 0 &&
        (!is_reply || envelope.deadline_tick_ms == 0) && known_operation(envelope.operation);
}
void publish_frame(const Envelope& envelope, bool is_reply, const ByteSink& body, std::vector<std::uint8_t>& frame)
{
    ByteSink header(kEnvelopeBytes);
    header.integer(kWireMagic); header.integer(kWireVersion);
    header.integer<std::uint16_t>(static_cast<std::uint16_t>(kEnvelopeBytes));
    header.integer(static_cast<std::uint16_t>(envelope.operation));
    header.integer<std::uint16_t>(is_reply ? 1 : 0);
    header.integer<std::uint32_t>(static_cast<std::uint32_t>(body.bytes().size()));
    header.integer(envelope.request_id); header.integer(envelope.session_id);
    header.integer(envelope.session_generation); header.integer(envelope.server_generation);
    header.integer(envelope.client_id); header.integer(envelope.deadline_tick_ms);
    header.integer<std::uint64_t>(0);
    std::vector<std::uint8_t> result = header.bytes();
    result.insert(result.end(), body.bytes().begin(), body.bytes().end());
    frame.swap(result);
}
bool reject(std::string& error, const char* message) { error = message; return false; }
}
bool deadline_expired(std::uint64_t deadline_tick_ms, std::uint64_t now_tick_ms)
{
    return deadline_tick_ms != 0 && now_tick_ms >= deadline_tick_ms;
}
bool encode_request(const SessionRequest& request, std::vector<std::uint8_t>& frame, std::string& error)
{
    if (!valid_request(request)) return reject(error, "invalid session request fields");
    ByteSink body(kMaxPayloadBytes);
    if (!write_request_body(request, body)) return reject(error, "session request exceeds wire limits");
    Envelope envelope;
    envelope.operation = request.operation; envelope.request_id = request.request_id;
    envelope.session_id = request.session_id; envelope.session_generation = request.session_generation;
    envelope.server_generation = request.server_generation; envelope.client_id = request.client_id;
    envelope.deadline_tick_ms = request.deadline_tick_ms;
    publish_frame(envelope, false, body, frame); error.clear(); return true;
}
bool decode_request(const void* data, std::size_t size, SessionRequest& request, std::string& error)
{
    Envelope envelope;
    if (!read_envelope(data, size, false, envelope)) return reject(error, "invalid session request envelope");
    SessionRequest parsed;
    parsed.operation = envelope.operation; parsed.request_id = envelope.request_id;
    parsed.session_id = envelope.session_id; parsed.session_generation = envelope.session_generation;
    parsed.server_generation = envelope.server_generation; parsed.client_id = envelope.client_id;
    parsed.deadline_tick_ms = envelope.deadline_tick_ms;
    ByteCursor cursor(static_cast<const std::uint8_t*>(data) + kEnvelopeBytes, envelope.payload_size);
    if (!read_request_body(parsed, cursor) || cursor.remaining() != 0 || !valid_request(parsed))
        return reject(error, "invalid session request body");
    request = std::move(parsed); error.clear(); return true;
}
bool encode_reply(const SessionReply& reply, std::vector<std::uint8_t>& frame, std::string& error)
{
    if (!valid_reply(reply)) return reject(error, "invalid session reply fields");
    ByteSink body(kMaxPayloadBytes);
    if (!write_reply_body(reply, body)) return reject(error, "session reply exceeds wire limits");
    Envelope envelope;
    envelope.operation = reply.operation; envelope.request_id = reply.engine.request_id;
    envelope.session_id = reply.engine.session_id; envelope.session_generation = reply.engine.session_generation;
    envelope.server_generation = reply.engine.server_generation; envelope.client_id = reply.client_id;
    publish_frame(envelope, true, body, frame); error.clear(); return true;
}
bool decode_reply(const void* data, std::size_t size, SessionReply& reply, std::string& error)
{
    Envelope envelope;
    if (!read_envelope(data, size, true, envelope)) return reject(error, "invalid session reply envelope");
    SessionReply parsed;
    parsed.operation = envelope.operation; parsed.client_id = envelope.client_id;
    parsed.engine.request_id = envelope.request_id; parsed.engine.session_id = envelope.session_id;
    parsed.engine.session_generation = envelope.session_generation; parsed.engine.server_generation = envelope.server_generation;
    ByteCursor cursor(static_cast<const std::uint8_t*>(data) + kEnvelopeBytes, envelope.payload_size);
    if (!read_reply_body(parsed, cursor) || cursor.remaining() != 0 || !valid_reply(parsed))
        return reject(error, "invalid session reply body");
    reply = std::move(parsed); error.clear(); return true;
}
}
