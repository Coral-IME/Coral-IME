#include <windows.h>
#include "syllable_helpers.h"
#include "core_operations.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>

namespace
{
namespace fs = std::filesystem;
bool nested_in(const fs::path& candidate, const fs::path& parent)
{
    auto first = candidate.begin(), second = parent.begin();
    while (second != parent.end())
    {
        if (first == candidate.end() || ::CompareStringOrdinal(first->c_str(), -1, second->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
        ++first; ++second;
    }
    return true;
}
void usage() { std::cout << "coral_resource_compiler --source-dir DIR --index FILE --bigram FILE --output-dir DIR\n"; }
}
int wmain(int argc, wchar_t** argv)
{
    if (argc == 2 && std::wstring(argv[1]) == L"--help") { usage(); return 0; }
    try
    {
        if (argc != 9) throw std::invalid_argument("four named path arguments are required");
        std::map<std::wstring, fs::path> options;
        for (int index = 1; index < argc; index += 2)
        {
            const std::wstring name(argv[index]);
            if (name != L"--source-dir" && name != L"--index" && name != L"--bigram" && name != L"--output-dir")
                throw std::invalid_argument("unknown option");
            if (!*argv[index + 1] || !options.emplace(name, fs::absolute(argv[index + 1])).second)
                throw std::invalid_argument("empty or duplicate option");
        }
        const auto source = fs::weakly_canonical(options.at(L"--source-dir"));
        const auto root = fs::weakly_canonical(options.at(L"--output-dir"));
        const auto index = options.at(L"--index"), bigram = options.at(L"--bigram");
        if (!fs::is_directory(source) || nested_in(root, source)) throw std::invalid_argument("output must be outside source directory");
        for (const auto& input : {index, bigram, source / "umode_radical.csv", source / "umode_stroke.csv", source / "eng_lexicon.csv"})
            if (!fs::is_regular_file(input)) throw std::invalid_argument("required source file is missing");
        const auto data = root / "data", general = data / "generallexicon";
        const std::array<fs::path, 10> outputs{general / "primary.marisa", general / "primary.dat",
            general / "mainlexicon.coral", general / "languagemodel.coral", general / "english.marisa", general / "english.dat",
            data / "bushou.marisa", data / "bushou.dat", data / "stroke.marisa", data / "stroke.dat"};
        for (const auto& output : outputs)
            if (fs::exists(output) || !nested_in(fs::weakly_canonical(output.parent_path()), root))
                throw std::invalid_argument("output exists or escapes output directory");
        fs::create_directories(general);
        // 既有算法的路径转换与参数顺序保留在本可执行文件内，
        // 不通过 DLL ABI 暴露。
        const std::array<std::string, 19> paths{index.string(), outputs[0].string(), outputs[1].string(),
            (source / "umode_radical.csv").string(), outputs[6].string(), outputs[7].string(),
            (source / "umode_stroke.csv").string(), outputs[8].string(), outputs[9].string(),
            (source / "eng_lexicon.csv").string(), outputs[4].string(), outputs[5].string(), "",
            bigram.string(), general.string(), index.string(), outputs[0].string(), outputs[1].string(), ""};
        const bool compiled = coral::runtime::compile_resource_files(paths[0].c_str(), paths[1].c_str(), paths[2].c_str(),
            paths[3].c_str(), paths[4].c_str(), paths[5].c_str(), paths[6].c_str(), paths[7].c_str(), paths[8].c_str(),
            paths[9].c_str(), paths[10].c_str(), paths[11].c_str(), paths[12].c_str(), paths[13].c_str(), paths[14].c_str(),
            paths[15].c_str(), paths[16].c_str(), paths[17].c_str(), paths[18].c_str());
        return compiled ? 0 : 1;
    }
    catch (const std::exception& error) { std::cerr << "Resource compilation failed: " << error.what() << '\n'; usage(); return 2; }
}
