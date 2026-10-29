#ifndef CORAL_INCLUDE_CORAL_SPEECH_SHORTCUT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_SPEECH_SHORTCUT_H_INCLUDED
#include <array>
#include <string_view>

namespace coral::speech {
struct ShortcutOption { const char* value; const wchar_t* label; unsigned modifiers; unsigned key; };
inline constexpr std::array<ShortcutOption, 5> kSpeechShortcuts{{
    {"ctrl_win_shift", L"Ctrl + Win + Shift", 7, 0},
    {"ctrl_win_space", L"Ctrl + Win + Space", 3, 0x20},
    {"ctrl_alt_space", L"Ctrl + Alt + Space", 9, 0x20},
    {"ctrl_shift_f8", L"Ctrl + Shift + F8", 5, 0x77},
    {"ctrl_win", L"Ctrl + Win", 3, 0}
}};
inline const ShortcutOption& speech_shortcut(std::string_view value) {
    for (const auto& option : kSpeechShortcuts) if (value == option.value) return option;
    return kSpeechShortcuts.front();
}
enum class PushToTalkEvent { kNone, kBegin, kEnd, kCancel };
// 只保存按键状态；组合完整时开始，任一必要键松开时结束，额外按键取消。
class PushToTalkTracker {
public:
    void reset() { down_.fill(false); active_ = false; blocked_ = false; }
    bool active() const { return active_; }
    bool down(unsigned key) const { return key < down_.size() && down_[key]; }
    PushToTalkEvent update(unsigned key, bool down, const ShortcutOption& option = kSpeechShortcuts.back()) {
        if (key >= down_.size() || down_[key] == down) return PushToTalkEvent::kNone;
        down_[key] = down;
        if (blocked_) {
            if (!any_down()) blocked_ = false;
            return PushToTalkEvent::kNone;
        }
        if (active_) {
            if (down && !chord_key(key, option)) {
                active_ = false;
                blocked_ = any_down();
                return PushToTalkEvent::kCancel;
            }
            if (modifiers() != option.modifiers || (option.key && !down_[option.key])) {
                active_ = false;
                blocked_ = any_down();
                return PushToTalkEvent::kEnd;
            }
        } else if (down && chord_key(key, option) && modifiers() == option.modifiers &&
            (!option.key || down_[option.key]) && !other_down(option)) {
            active_ = true;
            return PushToTalkEvent::kBegin;
        }
        return PushToTalkEvent::kNone;
    }
private:
    static unsigned modifier(unsigned key) {
        if (key == 0x11 || key == 0xa2 || key == 0xa3) return 1;
        if (key == 0x5b || key == 0x5c) return 2;
        if (key == 0x10 || key == 0xa0 || key == 0xa1) return 4;
        if (key == 0x12 || key == 0xa4 || key == 0xa5) return 8;
        return 0;
    }
    static bool chord_key(unsigned key, const ShortcutOption& option) {
        return (modifier(key) & option.modifiers) != 0 || (option.key && key == option.key);
    }
    unsigned modifiers() const {
        unsigned result = 0;
        for (unsigned i = 0; i < down_.size(); ++i) if (down_[i]) result |= modifier(i);
        return result;
    }
    bool any_down() const {
        for (bool value : down_) if (value) return true;
        return false;
    }
    bool other_down(const ShortcutOption& option) const {
        for (unsigned i = 0; i < down_.size(); ++i)
            if (down_[i] && !chord_key(i, option)) return true;
        return false;
    }
    std::array<bool, 256> down_{};
    bool active_ = false;
    bool blocked_ = false;
};
}
#endif
