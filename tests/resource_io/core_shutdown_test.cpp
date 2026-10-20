#include "pch.h"
#include "core_operations.h"
#include "syllable_parser.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv)
{
    if (argc != 4 || !std::getenv("CORAL_TEST_NAMESPACE") || !std::getenv("CORAL_USER_DATA_DIR")) return 2;
    try
    {
        const auto runtime = std::filesystem::absolute(argv[1]), user = std::filesystem::absolute(argv[2]);
        const std::string scenario = argv[3];
        std::filesystem::create_directories(user / "config");
        const auto config = user / "config/coralpinyinConfig.ini";
        std::filesystem::copy_file(runtime / "config/coralpinyinConfig.ini", config, std::filesystem::copy_options::overwrite_existing);
        if (scenario == "failure")
        {
            std::ifstream input(config, std::ios::binary); std::ostringstream source; source << input.rdbuf(); input.close();
            auto text = source.str(); const auto key = text.find("CorrectionData");
            const auto begin = text.find('=', key), end = text.find('\n', begin);
            require(key != std::string::npos && begin != std::string::npos && end != std::string::npos, "checksum fixture exists");
            text.replace(begin + 1, end - begin - 1, " incorrect-checksum");
            { std::ofstream output(config, std::ios::binary | std::ios::trunc); output << text; }
            require(!coral::runtime::start_runtime(user.string().c_str(), true), "invalid model rejected");
            require(!coral::runtime::runtime_ready(), "failed startup cleaned up");
        }
        else
        {
            require(scenario == "sync" || scenario == "async", "known shutdown scenario");
            require(coral::runtime::start_runtime(user.string().c_str(), scenario == "sync"), "initialize static core");
            if (scenario == "sync")
            {
                std::vector<std::string> words, pinyins; int count = 0;
                require(coral::runtime::parse_candidates("", "nihao", std::back_inserter(words), std::back_inserter(pinyins), 0, 7, 256, &count, 0), "real query before shutdown");
                require(count > 0, "real candidates returned");
            }
            coral::runtime::stop_runtime();
            require(!coral::phonetics::SyllableParser::instance().get_lexicon_manager().get_primary_lexicon().is_loaded(), "stopped model is released");
            require(!coral::runtime::runtime_ready(), "core stopped");
            coral::runtime::stop_runtime();
        }
        std::cout << "Normal static-core process exit: " << scenario << '\n';
        return 0;
    }
    catch (const std::exception& error) { coral::runtime::stop_runtime(); std::cerr << error.what() << '\n'; return 1; }
}
