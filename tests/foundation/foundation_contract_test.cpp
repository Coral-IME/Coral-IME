#include "slice_view.h"
#include "scalar_text_codec.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using coral::foundation::SliceView;
using coral::preferences::ScalarTextCodec;

static_assert(std::is_constructible_v<SliceView<int>, std::vector<int>&>);
static_assert(std::is_constructible_v<SliceView<const int>, const std::vector<int>&>);
static_assert(!std::is_constructible_v<SliceView<int>, const std::vector<int>&>);
static_assert(!std::is_constructible_v<SliceView<const int>, std::vector<int>&&>);
static_assert(std::is_constructible_v<SliceView<const int>, SliceView<int>>);
static_assert(!std::is_constructible_v<SliceView<int>, SliceView<const int>>);
struct Parent { int value; };
struct Child : Parent { int extra; };
static_assert(!std::is_constructible_v<SliceView<Parent>, Child (&)[2]>);

namespace {
int failures = 0;
void check(bool result, const char* label)
{
    if (!result) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}

template <typename Exception, typename Action>
bool rejects(Action action)
{
    try { action(); }
    catch (const Exception&) { return true; }
    return false;
}

void check_views()
{
    SliceView<int> empty;
    check(empty.empty() && empty.begin() == nullptr && empty.end() == nullptr, "null empty view");
    check(empty.subview(0).empty(), "empty subview does not advance null pointer");
    check(rejects<std::invalid_argument>([] { SliceView<int> bad(nullptr, 1); }), "null nonempty view rejected");
    check(rejects<std::out_of_range>([&] { empty.at(0); }), "empty at rejects");

    std::array<int, 4> values{3, 5, 7, 9};
    SliceView<int> view(values);
    view.at(1) = 11;
    const SliceView<const int> readonly(view);
    check(values[1] == 11 && readonly.at(1) == 11, "mutable and readonly storage alias");
    const auto middle = view.subview(1, 2);
    check(middle.size() == 2 && middle[0] == 11 && middle[1] == 7, "bounded subview");
    check(view.subview(4).empty() && view.subview(4).data() == view.end(), "one-past empty subview");
    check(rejects<std::out_of_range>([&] { view.at(4); }), "index outside storage rejected");
    check(rejects<std::out_of_range>([&] { view.subview(5); }), "offset outside storage rejected");
    check(rejects<std::out_of_range>([&] { view.subview(3, 2); }), "length beyond storage rejected");
    check(rejects<std::out_of_range>([&] { view.subview(1, std::numeric_limits<std::size_t>::max() - 1); }), "length arithmetic does not wrap");
    int total = 0;
    for (int value : readonly) { total += value; }
    check(total == 30, "contiguous iteration");
    int raw[]{1, 2};
    const SliceView<const int> raw_view(raw);
    check(raw_view.size() == 2 && raw_view[1] == 2, "built-in array view");
    const std::vector<int> vector{13, 17};
    const SliceView<const int> vector_view(vector);
    check(vector_view.at(1) == 17, "const container view");
}

void check_scalar_values()
{
    using Integer = ScalarTextCodec<int>;
    using Boolean = ScalarTextCodec<bool>;
    for (int value : {0, -1, 42, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        check(Integer::try_parse(Integer::format(value)) == value, "integer round trip and limits");
    }
    check(Integer::try_parse(" \t-42\r\n") == -42, "ASCII surrounding whitespace");
    for (const auto text : {"", " ", "42x", "1 2", "2147483648", "-2147483649", "+", "0x10"}) {
        check(!Integer::try_parse(text).has_value(), "invalid integer rejected completely");
    }
    check(!Integer::try_parse(std::string_view("1\0x", 3)), "embedded NUL rejected");
    for (const auto text : {"true", "TRUE", " Yes ", "oN", "1"}) {
        check(Boolean::try_parse(text) == true, "exact affirmative token");
    }
    for (const auto text : {"false", "FALSE", " No ", "off", "0"}) {
        const auto result = Boolean::try_parse(text);
        check(result.has_value() && !*result, "valid false distinguished from invalid");
    }
    for (const auto text : {"truthy", "yesterday", "only", "10", "2", "", " "}) {
        check(!Boolean::try_parse(text).has_value(), "boolean prefix is not accepted");
    }
    check(Integer::parse("bad") == 0 && !Boolean::parse("bad"), "fallback semantics explicit");
    check(Boolean::format(true) == "true" && Boolean::format(false) == "false", "canonical boolean output");
    const std::string text(" A\0B ", 5);
    check(ScalarTextCodec<std::string>::parse(text) == text, "string bytes preserved");
    check(ScalarTextCodec<const char*>::format(nullptr).empty(), "null string input");
}
} // namespace

int main()
{
    check_views();
    check_scalar_values();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
