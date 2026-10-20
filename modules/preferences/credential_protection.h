#ifndef CORAL_MODULES_PREFERENCES_CREDENTIAL_PROTECTION_H_INCLUDED
#define CORAL_MODULES_PREFERENCES_CREDENTIAL_PROTECTION_H_INCLUDED
#include <string>
namespace coral::preferences {
bool protect_credentials(const std::string& id, const std::string& secret, std::string& encrypted);
bool unprotect_credentials(const std::string& encrypted, std::string& id, std::string& secret);
}
#endif
