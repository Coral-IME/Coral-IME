#include "expression_evaluator.h"
#include "../number_syntax.h"
#include <cmath>
#include <string_view>

namespace {
class Parser {
    std::string_view text;
    std::size_t pos = 0;
    bool ok = true;
    BigFloat atom(int depth) {
        if (depth > 32 || pos == text.size()) { ok = false; return BigFloat(); }
        bool negative = text[pos] == '-';
        if (negative) ++pos;
        if (pos < text.size() && text[pos] == '(') {
            ++pos;
            BigFloat result = expression(0, depth + 1);
            if (pos == text.size() || text[pos++] != ')') ok = false;
            return negative ? BigFloat(0) - result : result;
        }
        const auto begin = pos;
        while (pos < text.size() && ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == '.')) ++pos;
        coral::uv::DecimalParts parts;
        const auto number = text.substr(begin, pos - begin);
        if (!coral::uv::decimal_parts(number, parts)) { ok = false; return BigFloat(); }
        return BigFloat((negative ? "-" : "") + std::string(number));
    }
    static int precedence(char c) {
        return c == '+' || c == '-' ? 1 : c == '*' || c == '/' || c == '^' ? 2 : 0;
    }
    BigFloat expression(int minimum, int depth) {
        BigFloat left = atom(depth);
        while (ok && pos < text.size()) {
            const char op = text[pos];
            const int priority = precedence(op);
            if (!priority || priority <= minimum) break;
            ++pos;
            BigFloat right = expression(priority, depth + 1);
            if (!ok) break;
            switch (op) {
            case '+': left = left + right; break;
            case '-': left = left - right; break;
            case '*': left = left * right; break;
            default: {
                const double l = left.to_double(), r = right.to_double();
                if (op == '/' && r == 0) { ok = false; break; }
                const double result = op == '/' ? l / r : std::pow(l, r);
                if (!std::isfinite(result)) { ok = false; break; }
                left = result;
                break;
            }
            }
        }
        return left;
    }
public:
    explicit Parser(std::string_view input) : text(input) {}
    bool compute(BigFloat& ans) {
        if (text.empty() || text.size() > 128) return false;
        ans = expression(0, 0);
        return ok && pos == text.size() && !ans.has_error();
    }
};
}
bool coral::assist::ExpressionEvaluator::compute(const std::string& expr, BigFloat& ans) {
    return Parser(expr).compute(ans);
}
