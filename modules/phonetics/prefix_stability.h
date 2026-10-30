#ifndef CORAL_MODULES_PHONETICS_PREFIX_STABILITY_H_INCLUDED
#define CORAL_MODULES_PHONETICS_PREFIX_STABILITY_H_INCLUDED
#include <string>
#include <string_view>
#include <vector>

namespace coral::phonetics {
struct PrefixAnchor {
    int begin = 0, end = 0;
    std::string text;
    unsigned age = 0;
    bool complete = true;
};

// 状态属于候选源实例；只记录已经返回给该会话的首选，不写入用户词频。
class PrefixStability {
public:
    void reset();
    const std::vector<PrefixAnchor>& prepare(std::string_view condition, std::string_view input, int mode);
    void observe(std::string_view word, std::string_view pinyin);
private:
    std::string condition_, input_;
    int mode_ = -1;
    bool advance_age_ = true;
    std::vector<PrefixAnchor> shown_, anchors_;
};

// 对串行解析作显式的请求级借用；析构恢复外层请求，不在共享解析器保存会话历史。
class PrefixStabilityScope {
public:
    explicit PrefixStabilityScope(const std::vector<PrefixAnchor>& anchors);
    ~PrefixStabilityScope();
    PrefixStabilityScope(const PrefixStabilityScope&) = delete;
    PrefixStabilityScope& operator=(const PrefixStabilityScope&) = delete;
    static const std::vector<PrefixAnchor>* current();
private:
    const std::vector<PrefixAnchor>* previous_;
};
}
#endif
