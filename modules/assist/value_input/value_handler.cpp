#include "value_handler.h"
#include "arithmetic/arithmetic_processor.h"
#include "date_processor.h"
#include "time_processor.h"
#include "number_text.h"

bool coral::assist::ValueHandler::parse(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents) {
    if (input.size() < 2 || input.size() > 129 || (input[0] != 'v' && input[0] != 'V')) return false;
    const std::string normalized = input[0] == 'V' ? "v" + input.substr(1) : input;
    const auto body = std::string_view(normalized).substr(1);
    bool ok = false;
    if (body.find(':') != body.npos) {
        coral::assist::TimeProcessor time;
        return time.process(normalized, sents);
    }
    if (body.find_first_of("nyr") == body.npos && body.find_first_not_of("0123456789.+-*/^()") == body.npos) {
        coral::assist::ArithmeticProcessor calculator;
        ok = calculator.process(normalized, sents);
        coral::assist::NumberProcessor number;
        ok = number.process(normalized, sents) || ok;
    }
    if (body.find_first_of(".-/nyr") != body.npos) {
        coral::assist::DateProcessor date;
        ok = date.process(normalized, sents) || ok;
    }
    return ok;
}
