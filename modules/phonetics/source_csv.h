#pragma once

#include <istream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace coral::data {

// 读取一条 CSV 记录，包括转义引号和带引号的逗号/换行。
inline bool read_source_csv_row(std::istream& input, std::vector<std::string>& fields)
{
    enum class State { Start, Plain, Quoted, Closed };
    State state = State::Start;
    fields.clear();
    std::string field;
    bool consumed = false;
    char ch;
    while (input.get(ch)) {
        consumed = true;
        if (state == State::Quoted) {
            if (ch == '"') state = State::Closed;
            else field += ch;
            continue;
        }
        if (state == State::Closed && ch == '"') {
            field += '"';
            state = State::Quoted;
            continue;
        }
        if (ch == ',' || ch == '\n' || ch == '\r') {
            fields.push_back(std::move(field));
            field.clear();
            state = State::Start;
            if (ch != ',') {
                if (ch == '\r' && input.peek() == '\n') input.get();
                return true;
            }
            continue;
        }
        if (state == State::Closed) throw std::runtime_error("Unexpected character after CSV quote");
        if (ch == '"') {
            if (state != State::Start) throw std::runtime_error("Unexpected quote in CSV field");
            state = State::Quoted;
        } else {
            field += ch;
            state = State::Plain;
        }
    }
    if (input.bad()) throw std::runtime_error("Cannot read CSV input");
    if (state == State::Quoted) throw std::runtime_error("Unterminated CSV quote");
    if (!consumed) return false;
    fields.push_back(std::move(field));
    return true;
}

inline void read_source_csv_header(std::istream& input, const std::vector<std::string>& expected)
{
    // 首字段之前允许 BOM，包括带引号的首字段。
    if (input.peek() == 0xEF) {
        const int first = input.get(), second = input.get(), third = input.get();
        if (first != 0xEF || second != 0xBB || third != 0xBF)
            throw std::runtime_error("Invalid UTF-8 BOM");
    }
    std::vector<std::string> fields;
    if (!read_source_csv_row(input, fields) || fields != expected)
        throw std::runtime_error("Unexpected source CSV header");
}

} // 命名空间 coral::data 结束
