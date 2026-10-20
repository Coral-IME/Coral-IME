#ifndef CORAL_INCLUDE_CORAL_TRANSLATION_TRANSLATION_CLIENT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_TRANSLATION_TRANSLATION_CLIENT_H_INCLUDED
#include "coral/translation/translation_session.h"
#include <atomic>
#include <functional>
#include <memory>

namespace coral::translation {
struct Credentials { std::string access_key_id; std::string access_key_secret; };
using Cancellation = std::shared_ptr<std::atomic<bool>>;
using Backend = std::function<Result(const Request&, const Cancellation&)>;
// 阻塞式传输，仅由专属工作线程调用。无日志、无重试。
Result translate_general(const Request& request, const Credentials& credentials,
    const Cancellation& cancelled);
}
#endif
