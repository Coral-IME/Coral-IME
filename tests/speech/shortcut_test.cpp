#include "coral/speech/shortcut.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace coral::speech;
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
int main() {
    try {
        require(std::string(speech_shortcut("bad").value) == "ctrl_win_shift", "Invalid shortcut fallback");
        const auto& option = kSpeechShortcuts[0];
        std::array<unsigned,3> keys{0xa0,0xa2,0x5b};
        std::sort(keys.begin(),keys.end());
        do {
            ShortcutTracker tracker;
            for (auto key : keys) require(!tracker.update(key,true,option), "Modifier chord fired before release");
            require(!tracker.update(keys.back(),true,option), "Autorepeat triggered");
            require(tracker.update(keys[0],false,option), "Chord order not supported");
            require(!tracker.update(keys[1],false,option) && !tracker.update(keys[2],false,option), "Chord fired twice");
        } while (std::next_permutation(keys.begin(),keys.end()));
        ShortcutTracker tracker;
        for (auto key : keys) tracker.update(key,true,option);
        tracker.update('S',true,option);
        require(!tracker.update(keys[0],false,option), "Longer chord prefix triggered voice");
        tracker.reset();
        tracker.update('S',true,option);
        for (auto key : keys) tracker.update(key,true,option);
        require(!tracker.update(keys[0],false,option), "Held non-modifier triggered voice");
        tracker.reset();
        for (auto key : keys) tracker.update(key,true,option);
        tracker.update(0xa4,true,option);
        tracker.update(0xa4,false,option);
        require(!tracker.update(keys[0],false,option), "Extra Alt modifier triggered voice");
        tracker.reset();
        tracker.update(0xa3,true,option); tracker.update(0x5c,true,option); tracker.update(0xa1,true,option);
        require(tracker.update(0xa1,false,option), "Right modifier keys not supported");
        for (const auto& shortcut : kSpeechShortcuts) {
            if (!shortcut.key) continue;
            tracker.reset();
            if (shortcut.modifiers & 1) tracker.update(0xa2,true,shortcut);
            if (shortcut.modifiers & 2) tracker.update(0x5b,true,shortcut);
            if (shortcut.modifiers & 4) tracker.update(0xa0,true,shortcut);
            if (shortcut.modifiers & 8) tracker.update(0xa4,true,shortcut);
            require(tracker.update(shortcut.key,true,shortcut), "Configurable chord did not fire");
            require(!tracker.update(shortcut.key,true,shortcut), "Configurable chord repeated");
        }
        // 按下发言：Ctrl+Win 按下开始，松开结束；额外按键和更长组合不误触发。
        {
            PushToTalkTracker ptt;
            require(ptt.update(0xa2,true)==PushToTalkEvent::kNone, "Push-to-talk began on Ctrl");
            require(ptt.update(0x5b,true)==PushToTalkEvent::kBegin, "Push-to-talk did not begin on Ctrl+Win");
            require(ptt.active() && ptt.update(0x5b,true)==PushToTalkEvent::kNone, "Push-to-talk repeated begin");
            require(ptt.update(0x5b,false)==PushToTalkEvent::kEnd && !ptt.active(), "Push-to-talk did not end on release");
            require(ptt.update(0xa2,false)==PushToTalkEvent::kNone, "Push-to-talk ended twice");
        }
        {
            PushToTalkTracker ptt;
            ptt.update(0x5c,true);
            require(ptt.update(0xa3,true)==PushToTalkEvent::kBegin, "Push-to-talk right modifiers/order failed");
            require(ptt.update(0xa3,false)==PushToTalkEvent::kEnd, "Push-to-talk did not end on Ctrl release");
        }
        {
            PushToTalkTracker ptt;
            require(ptt.update('S',true)==PushToTalkEvent::kNone, "Push-to-talk began while typing");
            ptt.update(0xa2,true); ptt.update(0x5b,true);
            require(!ptt.active(), "Push-to-talk began with another key held");
            ptt.update('S',false); ptt.update(0xa2,false); ptt.update(0x5b,false);
            require(ptt.update(0xa2,true)==PushToTalkEvent::kNone &&
                ptt.update(0x5b,true)==PushToTalkEvent::kBegin, "Push-to-talk did not recover after keys released");
        }
        {
            PushToTalkTracker ptt;
            ptt.update(0xa2,true); ptt.update(0x5b,true);
            require(ptt.update(0xa0,true)==PushToTalkEvent::kEnd, "Push-to-talk ignored extra modifier");
            ptt.update(0xa0,false);
            require(!ptt.active() && ptt.update(0xa2,false)==PushToTalkEvent::kNone,
                "Push-to-talk rearmed while the chord was still held");
        }
        {
            PushToTalkTracker ptt;
            ptt.update(0xa2,true); ptt.update(0x5b,true);
            require(ptt.update('A',true)==PushToTalkEvent::kEnd, "Push-to-talk ignored extra key");
            ptt.update('A',false); ptt.update(0xa2,false);
            require(ptt.update(0xa2,true)==PushToTalkEvent::kNone, "Push-to-talk restarted with Win still held");
            ptt.update(0xa2,false); ptt.update(0x5b,false);
            require(ptt.update(0xa2,true)==PushToTalkEvent::kNone &&
                ptt.update(0x5b,true)==PushToTalkEvent::kBegin, "Push-to-talk did not restart after full release");
        }
        std::cout << "Speech shortcut tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
