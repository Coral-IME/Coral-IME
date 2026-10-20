#ifndef CORAL_MODULES_TRANSLATION_ALIYUN_PROTOCOL_H_INCLUDED
#define CORAL_MODULES_TRANSLATION_ALIYUN_PROTOCOL_H_INCLUDED
#include "coral/translation/translation_client.h"
#include <map>
namespace coral::translation {
std::string sign_rpc(const std::string& method, const std::string& query, const std::string& body,
    std::map<std::string, std::string>& headers, const Credentials& credentials);
std::string encode_form(std::string_view value);
Result parse_translation_response(std::uint64_t id, long http_status, const std::string& body);
}
#endif
