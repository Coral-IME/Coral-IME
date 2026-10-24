#ifndef CORAL_INCLUDE_CORAL_TRANSPORT_SESSION_WIRE_H_INCLUDED
#define CORAL_INCLUDE_CORAL_TRANSPORT_SESSION_WIRE_H_INCLUDED

#include "coral/composition/session_port.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coral::transport
{
constexpr std::uint32_t kWireMagic = 0x34575343; // CSW4，小端序。
constexpr std::uint16_t kWireVersion = 6;
constexpr std::size_t kEnvelopeBytes = 72;
constexpr std::uint32_t kMaxPayloadBytes = 1024 * 1024;
constexpr std::uint32_t kMaxStringBytes = 256 * 1024;
constexpr std::uint32_t kMaxCandidates = 512;
constexpr std::uint32_t kMaxFuzzyChoices = 64;
enum class SessionOperation : std::uint16_t
{
    kHello = 1, kOpenSession = 2, kCloseSession = 3, kSendKey = 4,
    kParseFull = 5, kSelectCandidate = 6, kResetSession = 7,
    kFlushUserHistory = 8, kCancel = 9, kPing = 10,
    kRecordSelected = 11, kRevertLastAction = 12, kApplyCandidateAction = 13,
    kClearCache = 14, kUpdateLexicon = 15, kUpdateConfig = 16,
    kRefineCandidates = 17, kCancelRefinement = 18
};
// 内存中的消息；编译器布局绝不直接复制到线路上。
struct SessionRequest
{
    SessionOperation operation = SessionOperation::kPing;
    std::uint64_t request_id = 0;
    composition::SessionToken session_id = 0;
    std::uint64_t session_generation = 0;
    std::uint64_t server_generation = 0;
    std::uint64_t client_id = 0;
    std::uint64_t deadline_tick_ms = 0;
    std::uint64_t cancel_request_id = 0;
    std::uint64_t query_request_id = 0;
    composition::KeyEvent key_event;
    composition::InputContext input_context;
    std::string condition;
    std::string pinyin;
    int offset = 0;
    int page_size = 0;
    int mode = 0;
    int candidate_index = 0;
    std::string word;
    std::uint32_t condition_id = 0;
    CandidateAction candidate_action = CandidateAction::Disable;
    LanguageVariant variant = LanguageVariant::Simplified;
    composition::SessionSettings config;
};
struct SessionReply
{
    SessionOperation operation = SessionOperation::kPing;
    std::uint64_t client_id = 0;
    composition::SessionResult engine;
};
// 非法输入返回 false 且不改动目标。成功时清除错误。
// 字符串携带字节；会话文本策略与线路校验相互独立。
bool encode_request(const SessionRequest& request, std::vector<std::uint8_t>& frame, std::string& error);
bool decode_request(const void* data, std::size_t size, SessionRequest& request, std::string& error);
bool encode_reply(const SessionReply& reply, std::vector<std::uint8_t>& frame, std::string& error);
bool decode_reply(const void* data, std::size_t size, SessionReply& reply, std::string& error);
bool deadline_expired(std::uint64_t deadline_tick_ms, std::uint64_t now_tick_ms);
}
#endif // CORAL_INCLUDE_CORAL_TRANSPORT_SESSION_WIRE_H_INCLUDED
