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
        // 回车提交 commit 后，逗号必须当次上屏，不能等下一个 t 才转换。
        for (const auto punctuation : std::wstring(L"，：。")) {
            const auto converted = following_punctuation(false, true, false, L't', punctuation);
            const auto expected = punctuation == L'，' ? L',' : punctuation == L'：' ? L':' : L'.';
            if (converted != expected)
                throw std::runtime_error("committed commit: punctuation must convert before the next letter");
        }
        // 为独立的字母选项手工挑选的已上屏文本用例。
        for (wchar_t letter : std::wstring(L"azAZａｚＡＺ")) {
            for (const auto punctuation : std::wstring(L"，：。")) {
                const auto expected = punctuation == L'，' ? L',' : punctuation == L'：' ? L':' : L'.';
                expect(following_punctuation(false, true, false, letter, punctuation) == expected);
                expect(following_punctuation(true, true, false, letter, punctuation) == expected);
                expect(following_punctuation(true, false, false, letter, punctuation) == punctuation);
                expect(following_punctuation(false, true, true, letter, punctuation) == punctuation);
                expect(digit_separator_replacement(true, letter, punctuation, L'1') == L'\0');
            }
            expect(following_punctuation(false, true, false, letter, L'；') == L'；');
        }
        // 即使历史中残留中文逗号，所有字母也不能走退格注入路径。
        for (const auto next : std::wstring(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZａｚＡＺ")) {
            expect(digit_separator_replacement(true, L't', L'，', next) == L'\0');
            expect(digit_separator_replacement(true, L'1', L'，', next) == L'\0');
        }
        // 数字行为保留：点立即转换，逗号和冒号只在两个数字之间替换。
        expect(following_punctuation(true, false, false, L'0', L'。') == L'.');
        expect(following_punctuation(true, true, false, L'1', L'，') == L'，');
        expect(following_punctuation(true, true, false, L'1', L'：') == L'：');
        expect(following_punctuation(false, true, false, L'0', L'。') == L'。');
        expect(following_punctuation(true, true, false, L'中', L'。') == L'。');
        expect(following_punctuation(true, true, false, L'a', L'》') == L'》');
        expect(following_punctuation(true, true, false, L'\0', L'，') == L'，');
        expect(digit_separator_replacement(true, L'１', L'，', L'2') == L',');
        expect(digit_separator_replacement(true, L'1', L'：', L'２') == L':');
        expect(digit_separator_replacement(false, L'1', L'，', L'2') == L'\0');
        expect(digit_separator_replacement(true, L'中', L'，', L'2') == L'\0');
        std::cout << "PASS: immediate committed-letter punctuation, no letter injection, digit fallback, option off, composition\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
