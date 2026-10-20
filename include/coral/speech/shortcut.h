#ifndef CORAL_INCLUDE_CORAL_SPEECH_SHORTCUT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_SPEECH_SHORTCUT_H_INCLUDED
#include <array>
#include <cstdint>
#include <string_view>

namespace coral::speech {
struct ShortcutOption { const char* value; const wchar_t* label; unsigned modifiers; unsigned key; };
inline constexpr std::array<ShortcutOption, 4> kSpeechShortcuts{{
    {"ctrl_win_shift", L"Ctrl + Win + Shift", 7, 0},
    {"ctrl_win_space", L"Ctrl + Win + Space", 3, 0x20},
    {"ctrl_alt_space", L"Ctrl + Alt + Space", 9, 0x20},
    {"ctrl_shift_f8", L"Ctrl + Shift + F8", 5, 0x77}
}};
inline const ShortcutOption& speech_shortcut(std::string_view value) {
    for (const auto& option : kSpeechShortcuts) if (value == option.value) return option;
    return kSpeechShortcuts.front();
}
// 按下发言固定使用 Ctrl + Win：两键同时按下开始收集，任一键松开或按下其他键结束。
inline constexpr unsigned kPushToTalkModifiers = 3;
enum class PushToTalkEvent { kNone, kBegin, kEnd };
class PushToTalkTracker {
public:
    void reset() { down_.fill(false); active_ = false; blocked_ = false; }
    bool active() const { return active_; }
    bool down(unsigned key) const { return key < down_.size() && down_[key]; }
    PushToTalkEvent update(unsigned key, bool down) {
        if (key >= down_.size()) return PushToTalkEvent::kNone;
        if (down_[key] == down) return PushToTalkEvent::kNone;
        down_[key] = down;
        if (blocked_) {
            // 中途按下其他键或提前松开后，必须全部松开才能重新开始，避免组合键误触发。
            if (!any_down()) blocked_ = false;
            return PushToTalkEvent::kNone;
        }
        if (active_) {
            if ((!down && is_chord_key(key)) || (down && !is_chord_key(key))) {
                active_ = false;
                blocked_ = any_down();
                return PushToTalkEvent::kEnd;
            }
            return PushToTalkEvent::kNone;
        }
        // 只在没有其他按键时以完整的 Ctrl+Win 开始，不抢占更长的快捷键组合。
        if (!down || !is_chord_key(key)) return PushToTalkEvent::kNone;
        if (modifiers() == kPushToTalkModifiers && !other_down()) {
            active_ = true;
            return PushToTalkEvent::kBegin;
        }
        return PushToTalkEvent::kNone;
    }
private:
    static bool is_chord_key(unsigned key) {
        return key == 0x11 || key == 0xa2 || key == 0xa3 ||
            key == 0x5b || key == 0x5c;
    }
    unsigned modifiers() const {
        return ((down_[0x11] || down_[0xa2] || down_[0xa3]) ? 1u : 0u) |
            ((down_[0x5b] || down_[0x5c]) ? 2u : 0u) |
            ((down_[0x10] || down_[0xa0] || down_[0xa1]) ? 4u : 0u) |
            ((down_[0x12] || down_[0xa4] || down_[0xa5]) ? 8u : 0u);
    }
    bool any_down() const {
        for (unsigned i = 0; i < down_.size(); ++i) if (down_[i]) return true;
        return false;
    }
    bool other_down() const {
        for (unsigned i = 0; i < down_.size(); ++i)
            if (down_[i] && !is_chord_key(i)) return true;
        return false;
    }
    std::array<bool, 256> down_{};
    bool active_ = false;
    bool blocked_ = false;
};
// 只保存按键是否按下，不保存文字。纯修饰键在松开时触发，避免抢占更长组合。
class ShortcutTracker {
public:
    void reset() { down_.fill(false); armed_ = false; used_ = false; }
    bool update(unsigned key, bool down, const ShortcutOption& option) {
        if (key >= down_.size()) return false;
        const bool repeat = down_[key] == down;
        const auto before = modifiers();
        down_[key] = down;
        const auto after = modifiers();
        const bool modifier = is_modifier(key);
        bool fire = false;
        if (!repeat) {
            if (down && !modifier) {
                used_ = true;
                fire = option.key && key == option.key && after == option.modifiers;
            }
            if (!option.key) {
                if (down && modifier && after == option.modifiers && !used_) armed_ = true;
                if (down && modifier && (after & ~option.modifiers)) { used_ = true; armed_ = false; }
                if (!down && modifier && armed_ && !used_ && before == option.modifiers) {
                    fire = true; armed_ = false; used_ = true;
                }
            }
        }
        if (!after) {
            armed_ = false; used_ = false;
            for (unsigned i = 0; i < down_.size(); ++i)
                if (down_[i] && !is_modifier(i)) { used_ = true; break; }
        }
        return fire;
    }
    static bool is_modifier(unsigned key) {
        return key == 0x10 || key == 0x11 || key == 0x12 ||
            (key >= 0xa0 && key <= 0xa5) || key == 0x5b || key == 0x5c;
    }
private:
    unsigned modifiers() const {
        return ((down_[0x11] || down_[0xa2] || down_[0xa3]) ? 1u : 0u) |
            ((down_[0x5b] || down_[0x5c]) ? 2u : 0u) |
            ((down_[0x10] || down_[0xa0] || down_[0xa1]) ? 4u : 0u) |
            ((down_[0x12] || down_[0xa4] || down_[0xa5]) ? 8u : 0u);
    }
    std::array<bool, 256> down_{};
    bool armed_ = false;
    bool used_ = false;
};
}
#endif
