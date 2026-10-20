#ifndef CORAL_INCLUDE_CORAL_TRANSLATION_TRANSLATION_SESSION_H_INCLUDED
#define CORAL_INCLUDE_CORAL_TRANSLATION_TRANSLATION_SESSION_H_INCLUDED

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coral::translation {

enum class Status { kIdle, kMissingKey, kNotConnected, kLoading, kReady, kFailed };
enum class Failure { kNone, kCredentials, kNetwork, kTimeout, kRejected, kLanguage,
    kInvalidResponse, kCancelled, kInternal };

struct Request {
    std::uint64_t id = 0;
    std::wstring text;
    std::string provider = "aliyun";
    std::string source_language = "zh";
    std::string target_language = "en";
};

struct Result {
    std::uint64_t request_id = 0;
    Status status = Status::kFailed;
    std::vector<std::wstring> translations;
    Failure failure = Failure::kNone;
    int transport_error = 0;
};

// 传输层与凭据不跨越此边界。未来的异步适配器
// 消费 request() 并在控制器线程上投递结果。
class TranslationSession final {
public:
    bool begin(std::wstring text, bool key_configured, std::string target = "en") {
        if (text.empty()) return false;
        request_ = { ++generation_, std::move(text) };
        request_.target_language = std::move(target);
        translations_.clear();
        failure_ = Failure::kNone;
        status_ = key_configured ? Status::kLoading : Status::kMissingKey;
        return true;
    }

    bool accept(Result result) {
        if (!active() || result.request_id != request_.id) return false;
        if (result.status != Status::kReady && result.status != Status::kFailed &&
            result.status != Status::kLoading && result.status != Status::kMissingKey) return false;
        failure_ = result.failure;
        translations_.clear();
        if (result.status == Status::kReady) {
            for (auto& text : result.translations) {
                if (!text.empty()) translations_.push_back(std::move(text));
            }
        }
        status_ = result.status == Status::kReady && translations_.empty()
            ? Status::kFailed : result.status;
        return true;
    }

    void reset() {
        ++generation_;
        request_ = {};
        translations_.clear();
        status_ = Status::kIdle;
        failure_ = Failure::kNone;
    }

    bool active() const { return status_ != Status::kIdle; }
    const Request& request() const { return request_; }
    const std::vector<std::wstring>& translations() const { return translations_; }
    std::wstring_view hint() const {
        switch (status_) {
        case Status::kMissingKey: return L"翻译：请在设置 → 高级中填写 AccessKey ID 和 Secret；Esc 返回输入";
        case Status::kNotConnected: return L"翻译：阿里云服务尚未接入；Esc 返回输入";
        case Status::kLoading: return L"翻译中…；Esc 返回输入";
        case Status::kReady: return L"翻译：方向键选择，回车输出；Esc 返回输入";
        case Status::kFailed:
            switch (failure_) {
            case Failure::kCredentials: return L"翻译凭据无效，请检查 AccessKey ID 和 Secret；Esc 返回输入";
            case Failure::kNetwork: return L"翻译连接失败，请检查网络；Esc 返回输入";
            case Failure::kTimeout: return L"翻译请求超时，请稍后重试；Esc 返回输入";
            case Failure::kRejected: return L"翻译请求被服务拒绝，请检查凭据或账户设置；Esc 返回输入";
            case Failure::kLanguage: return L"当前翻译语言暂不支持，请更换目标语言；Esc 返回输入";
            default: return L"翻译失败或暂无结果；Esc 返回输入";
            }
        default: return {};
        }
    }

private:
    std::uint64_t generation_ = 0;
    Request request_;
    Status status_ = Status::kIdle;
    Failure failure_ = Failure::kNone;
    std::vector<std::wstring> translations_;
};

} // 命名空间 coral::translation 结束

#endif
