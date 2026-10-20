#include "coral/lexicon_catalog/service_endpoint.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value) { if (!value) throw std::runtime_error("Lexicon service fixture failed"); }
}
int main() {
    try {
        using coral::lexicon_catalog::LexiconService;
        const LexiconService disabled;
        require(!disabled.available() && disabled.catalog_url().empty() && disabled.location_url(7).empty());
        const LexiconService service("https://new.example.test:00080/catalog///");
        require(service.base() == "https://new.example.test:00080/catalog");
        require(service.catalog_url() == "https://new.example.test:00080/catalog/api/v1/dict?autoDownload=1");
        require(service.location_url(7) == "https://new.example.test:00080/catalog/api/v1/dict/getDownloadUrl?Id=7");
        require(service.location_url(INT32_MAX).find("Id=2147483647") != std::string::npos);
        require(service.location_url(0).empty() && service.location_url(UINT32_MAX).empty());
        const std::string invalid[] = {"http://new.example.test", "https://", "https://u:p@new.example.test", "https://new.example.test?x=1",
            "https://new.example.test/#fragment", "https://new.example.test/a b", "https://new.example.test/a;b", "https://new.example.test/\"",
            "https://new.example.test:0", "https://new.example.test:65536", "https://new.example.test:-1", "https://[::1]",
            "https://新.example.test", "https://new.example.test/\\x", std::string("https://new.example.test/") + '\0', std::string(8193, 'x')};
        for (const auto& base : invalid) {
            bool rejected = false;
            try { const LexiconService wrong(base); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected);
        }
        require(LexiconService("https://new.example.test:65535/a_~/").available());
        require(coral::lexicon_catalog::configured_lexicon_service().base() == CORAL_EXPECT_LEXICON_BASE);
        std::cout << "Explicit disabled service, independent routes and invalid bases passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
