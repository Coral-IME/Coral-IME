#include "coral/speech/shortcut.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace coral::speech;
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
int main() {
    try {
        require(std::string(speech_shortcut("bad").value) == "ctrl_win_shift", "Invalid shortcut fallback");
        for (const auto& option : kSpeechShortcuts) {
            std::vector<unsigned> keys;
            if (option.modifiers & 1) keys.push_back(0xa2);
            if (option.modifiers & 2) keys.push_back(0x5b);
            if (option.modifiers & 4) keys.push_back(0xa0);
            if (option.modifiers & 8) keys.push_back(0xa4);
            if (option.key) keys.push_back(option.key);
            std::sort(keys.begin(), keys.end());
            do {
                for (auto released : keys) {
                    PushToTalkTracker tracker;
                    for (std::size_t i = 0; i < keys.size(); ++i)
                        require(tracker.update(keys[i], true, option) ==
                            (i + 1 == keys.size() ? PushToTalkEvent::kBegin : PushToTalkEvent::kNone), "Wrong hold onset");
                    require(tracker.update(keys.back(), true, option) == PushToTalkEvent::kNone, "Autorepeat began another recording");
                    require(tracker.update(released, false, option) == PushToTalkEvent::kEnd, "Required key release did not end recording");
                    require(tracker.update(released, true, option) == PushToTalkEvent::kNone, "Rearmed before full release");
                    for (auto key : keys) tracker.update(key, false, option);
                    for (auto key : keys) tracker.update(key, true, option);
                    require(tracker.active(), "Did not rearm after full release");
                }
            } while (std::next_permutation(keys.begin(), keys.end()));
            PushToTalkTracker tracker;
            tracker.update('S', true, option);
            for (auto key : keys) tracker.update(key, true, option);
            require(!tracker.active(), "Held unrelated key triggered recording");
            tracker.reset();
            for (auto key : keys) tracker.update(key, true, option);
            require(tracker.update('S', true, option) == PushToTalkEvent::kCancel, "Longer shortcut uploaded accidental audio");
            tracker.reset();
            for (auto key : keys) tracker.update(key, true, option);
            require(tracker.update(0x1b, true, option) == PushToTalkEvent::kCancel, "Escape did not cancel");
        }
        PushToTalkTracker right;
        right.update(0x5c, true);
        require(right.update(0xa3, true) == PushToTalkEvent::kBegin, "Right modifiers/order failed");
        require(right.update(0xa2, true) == PushToTalkEvent::kNone, "Second Ctrl interrupted recording");
        require(right.update(0xa3, false) == PushToTalkEvent::kNone, "One of two held Ctrl keys ended recording");
        require(right.update(0xa2, false) == PushToTalkEvent::kEnd, "Last Ctrl release did not finish");
        std::cout << "Speech hold shortcut tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
