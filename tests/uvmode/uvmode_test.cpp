#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "value_handler.h"
#include "date_processor.h"
#include "coral/composition/assist_hints.h"

static bool Has(const std::string& input, const char* value) {
    std::vector<coral::phonetics::CandidateResult> solutions;
    coral::assist::ValueHandler::parse(input, solutions);
    return std::any_of(solutions.begin(), solutions.end(),
        [value](const coral::phonetics::CandidateResult& s) { return s.sentence == value; });
}
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (false)

int main(int argc, char** argv)
{
    const std::vector<std::string> inputs{
        "v123", "v123.45", "v1+2", "v1/2", "v12:30", "v12:30:45",
        "v2026.10.9", "v2010/1/23", "v2020n2y29r", "v10y9r", "v2026.10"};
    if (argc > 1 && std::string(argv[1]) == "--benchmark") {
        const auto start = std::chrono::steady_clock::now();
        std::size_t count = 0;
        for (int i = 0; i < 100; ++i)
            for (const auto& input : inputs) {
                std::vector<coral::phonetics::CandidateResult> solutions;
                coral::assist::ValueHandler::parse(input, solutions);
                count += solutions.size();
            }
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "queries=1100 elapsed_us=" << us << " candidates=" << count << '\n';
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--snapshot") for (const auto& input : inputs) {
        std::vector<coral::phonetics::CandidateResult> solutions;
        coral::assist::ValueHandler::parse(input, solutions);
        std::cout << input << '\n';
        for (const auto& s : solutions) std::cout << "  " << s.sentence << '\n';
    }
    CHECK(Has("v1+2", "3"));
    CHECK(Has("v1-2", "-1"));
    CHECK(Has("v0/2", "0"));
    CHECK(!Has("v2/0", "inf"));
    CHECK(Has("v(1+2)*3", "9"));
    CHECK(Has("v2*(3+4)", "14"));
    CHECK(Has("v-2*-3", "6"));
    CHECK(Has("v0.1+0.2", "0.3"));
    CHECK(Has("v99999999999999999999+1", "100000000000000000000"));
    CHECK(Has("V1+2", "3"));
    CHECK(Has("v1/2", "50.00%") && Has("v1/2", u8"1月2日"));
    CHECK(Has("v2026.10", u8"2026年10月"));
    CHECK(Has("v00:00", u8"零时零分"));
    CHECK(Has("v59:59", u8"59分59秒"));
    CHECK(Has("v2020n2y29r", u8"2020年2月29日"));
    for (const auto& input : {"v", "v1+", "v(1+2", "v1)", "v2/0", "v99:99", "v1:2:3:4", "v1..2", "vhello"}) {
        std::vector<coral::phonetics::CandidateResult> solutions;
        CHECK(!coral::assist::ValueHandler::parse(input, solutions));
    }
    std::vector<coral::phonetics::CandidateResult> solutions;
    coral::assist::DateProcessor date;
    CHECK(!date.process("v2026.2.29", solutions));
    CHECK(!date.process("v2026.13.1", solutions));
    CHECK(!date.process("v2026.1/2", solutions));
    CHECK(!date.process("v99999.1.1", solutions));
    CHECK(!coral::assist::ValueHandler::parse("v" + std::string(200, '9'), solutions));
    CHECK(!coral::assist::ValueHandler::parse("v" + std::string(100, '9') + ":00", solutions));
    using namespace coral::composition;
    CHECK(specialMode(std::string_view("u"), false, true, true) == SpecialMode::Stroke);
    CHECK(specialMode(std::string_view("uu"), false, true, true) == SpecialMode::Symbols);
    CHECK(specialMode(std::string_view("u"), true, true, true) == SpecialMode::Normal);
    CHECK(specialMode(std::wstring_view(L"Uu"), true, true, true) == SpecialMode::Symbols);
    CHECK(specialMode(std::string_view("V2026n10y9r"), true, true, true) == SpecialMode::Convert);
    CHECK(specialMode(std::string_view("vhello"), false, true, true) == SpecialMode::Normal);
    CHECK(specialMode(std::string_view("v1"), false, true, false) == SpecialMode::Normal);
    CHECK(specialMode(std::string_view("u"), false, false, true) == SpecialMode::Normal);
    CHECK(specialModeHint(SpecialMode::Normal, L"nihao", true).empty());
    CHECK(specialModeHint(SpecialMode::Stroke, L"u", false).find(L"h横") != std::wstring_view::npos);
    CHECK(specialModeHint(SpecialMode::Convert, L"v1+", false).find(L"补全") != std::wstring_view::npos);
    CHECK(specialModeHint(SpecialMode::Stroke, L"U", false).find(L"Umumumu") != std::wstring_view::npos);
    CHECK(specialModeHint(SpecialMode::Convert, L"V", false).find(L"V123.45") != std::wstring_view::npos);
    std::cout << "U/V grammar, arithmetic and guidance checks passed\n";
    return 0;
}
