#pragma once

#include <array>
#include <string_view>

namespace coral::composition {

// 固定候选窗的逻辑像素尺寸；以 16px 字号、96 DPI 为基准，与渲染端同步缩放。
struct CandidateRowMetrics {
    static constexpr int kPanelWidth = 530;
    static constexpr int kPaddingX = 4;
    static constexpr int kToggleGap = 4;
    static constexpr int kToggleSize = 26;
    static constexpr int kItemPaddingX = 8;
    static constexpr int kIndexGap = 5;
    static constexpr int kCollapsedMaxColumns = 9;

    // 引擎没有字体测量上下文，按 Unicode 字符数估算；这三项可按实机结果校准。
    // 拉丁字符也按全宽预留，兼顾较宽字形及字体回退；序号预留字母 W 的宽度。
    int character_width = 16;
    int index_width = 12;
    int available_width = kPanelWidth - kPaddingX * 2 - kToggleGap - kToggleSize;
};

// 只约束模型拼句，词库候选不占预算。首选本身超长时保留一条，沿用原显示方式。
class ComposedCandidateCapacity {
public:
    explicit ComposedCandidateCapacity(CandidateRowMetrics metrics = {}) : metrics_(metrics) {
        for (int grid = 1; grid <= CandidateRowMetrics::kCollapsedMaxColumns; ++grid)
            remaining_[grid] = grid;
    }

    bool try_append(std::string_view text) {
        if (full_) return false;
        int width = CandidateRowMetrics::kItemPaddingX * 2 +
            CandidateRowMetrics::kIndexGap + metrics_.index_width;
        for (unsigned char byte : text) {
            if ((byte & 0xc0) != 0x80) width += metrics_.character_width;
            if (width > metrics_.available_width) break;
        }
        bool fits = false;
        if (metrics_.available_width > 0 && width <= metrics_.available_width) {
            // 与折叠布局一样遍历全部列数，四字词可以使用五列，避免六列网格浪费空位。
            for (int grid = 1; grid <= CandidateRowMetrics::kCollapsedMaxColumns; ++grid) {
                remaining_[grid] -= (width * grid + metrics_.available_width - 1) / metrics_.available_width;
                if (remaining_[grid] >= 0) fits = true;
            }
        }
        full_ = !fits;
        const bool accepted = first_ || fits;
        first_ = false;
        return accepted;
    }

private:
    CandidateRowMetrics metrics_;
    std::array<int, CandidateRowMetrics::kCollapsedMaxColumns + 1> remaining_{};
    bool first_ = true;
    bool full_ = false;
};

} // 命名空间 coral::composition
