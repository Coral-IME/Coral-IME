#include "digitpunctuation.h"

#include <iostream>
#include <stdexcept>
#include <string>

void expect(bool condition)
{
    if (!condition) throw std::runtime_error("Digit punctuation regression failed");
}

int main()
{
    using namespace coral::tsf;
    try {
        // 每个字符代表已上屏输出，包括零的独立
        // 标点/清除按键标志上屏路径与直通的小键盘数字。
        auto type = [](const std::wstring& keys, bool enabled) {
            std::wstring output;
            for (const auto key : keys) {
                const auto previous = output.empty() ? L'\0' : output.back();
                output += DigitFollowingPeriod(enabled, false, previous,
                    key == L'.' ? L'。' : key);
            }
            return output;
        };
        expect(type(L"1.0.0.0.0.", true) == L"1.0.0.0.0.");
        expect(type(L"0.0.12.345.6789.", true) == L"0.0.12.345.6789.");
        expect(type(L"1.0.0.", false) == L"1。0。0。");
        expect(type(L"1.a.中文.", true) == L"1.a。中文。");
        expect(type(L"1..", true) == L"1.。");
        expect(DigitFollowingPeriod(true, true, L'1', L'。') == L'。');
        expect(DigitFollowingPeriod(true, false, L'1', L'》') == L'》');
        expect(DigitFollowingPeriod(true, false, L'０', L'。') == L'.');
        expect(DigitFollowingPeriod(true, false, L'\0', L'。') == L'。');
        for (wchar_t digit = L'0'; digit <= L'9'; ++digit) {
            expect(DigitSeparatorReplacement(digit, L'。') == L'.');
            expect(DigitSeparatorReplacement(digit, L'，') == L',');
            expect(DigitSeparatorReplacement(digit, L'：') == L':');
        }
        expect(DigitSeparatorReplacement(L'中', L'。') == L'\0');
        expect(DigitSeparatorReplacement(L'1', L'；') == L'\0');
        // 为独立的字母选项手工挑选的已上屏文本用例。
        for (wchar_t letter : std::wstring(L"azAZａｚＡＺ")) {
            expect(following_period(false, true, false, letter, L'。') == L'.');
            expect(following_period(true, false, false, letter, L'。') == L'。');
            expect(following_period(false, true, true, letter, L'。') == L'。');
            expect(separator_replacement(false, true, letter, L'，', L'b') == L',');
            expect(separator_replacement(false, true, letter, L'：', L'B') == L':');
            expect(separator_replacement(false, true, letter, L'。', L'ｂ') == L'.');
            expect(separator_replacement(true, false, letter, L'，', L'b') == L'\0');
            expect(separator_replacement(true, true, letter, L'，', L'1') == L'\0');
            expect(separator_replacement(false, true, letter, L'；', L'b') == L'\0');
        }
        expect(following_period(true, false, false, L'0', L'。') == L'.');
        expect(following_period(false, true, false, L'0', L'。') == L'。');
        expect(following_period(true, true, false, L'中', L'。') == L'。');
        expect(following_period(true, true, false, L'a', L'》') == L'》');
        expect(separator_replacement(true, false, L'１', L'，', L'2') == L',');
        expect(separator_replacement(false, true, L'1', L'，', L'2') == L'\0');
        expect(separator_replacement(true, true, L'1', L'，', L'b') == L'\0');
        expect(separator_replacement(true, true, L'中', L'，', L'b') == L'\0');
        expect(separator_replacement(false, false, L'a', L'。', L'b') == L'\0');
        std::cout << "PASS: repeated zero/version/decimal sequences, option off, composition, Shift, separator fallback\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
