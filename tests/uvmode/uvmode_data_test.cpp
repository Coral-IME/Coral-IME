#include "shape_lexicon_format.h"
#include <iostream>
#include <sstream>
#include "source_csv.h"
#include <iterator>

static std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static bool Check(bool ok, const char* name) {
    if (!ok) std::cerr << "FAIL: " << name << '\n';
    return ok;
}
static bool Same(const coral::assist::ShapeDataView& a, const coral::assist::ShapeDataView& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = a[i], y = b[i];
        if (x.size() != y.size()) return false;
        for (std::size_t j = 0; j < x.size(); ++j) {
            const auto left = a.get_phrase_view(x[j]), right = b.get_phrase_view(y[j]);
            if (left.word != right.word || left.pinyin != right.pinyin) return false;
        }
    }
    return true;
}
static bool CsvSources() {
    std::istringstream input("\xEF\xBB\xBF\"word\",code,pinyin\r\n\"wood,radical\",mu,\"m\"\"u\"\r\nlast,h,last");
    std::vector<std::string> fields;
    coral::data::read_source_csv_header(input, {"word", "code", "pinyin"});
    if (!Check(coral::data::read_source_csv_row(input, fields) &&
        fields == std::vector<std::string>{"wood,radical", "mu", "m\"u"}, "quoted CSV fields and BOM")) return false;
    if (!Check(coral::data::read_source_csv_row(input, fields) &&
        fields == std::vector<std::string>{"last", "h", "last"}, "CSV final row without newline")) return false;
    if (!Check(!coral::data::read_source_csv_row(input, fields), "CSV end of file")) return false;
    std::istringstream multiline("\"first\nsecond\",x,y\n");
    if (!Check(coral::data::read_source_csv_row(multiline, fields) && fields[0] == "first\nsecond", "quoted CSV newline")) return false;
    for (const auto& text : {"\"unfinished,x,y", "x,\"closed\"junk,y", "un\"quoted,x,y"}) {
        std::istringstream broken(text);
        bool rejected = false;
        try { coral::data::read_source_csv_row(broken, fields); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!Check(rejected, "malformed CSV rejected")) return false;
    }
    std::istringstream oldText("word code pinyin\n");
    bool rejected = false;
    try { coral::data::read_source_csv_header(oldText, {"word", "code", "pinyin"}); }
    catch (const std::runtime_error&) { rejected = true; }
    return Check(rejected, "source CSV header required");
}
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--repack") {
        const auto old = Read(argv[2]);
        coral::assist::ShapeDataView before;
        if (!before.bind(old.data(), old.size())) return 1;
        std::vector<std::vector<WordAndPinyin>> rows(before.size());
        for (std::size_t i = 0; i < rows.size(); ++i)
            for (const auto& info : before[i]) {
                const auto phrase = before.get_phrase_view(info);
                rows[i].push_back({std::string(phrase.word), std::string(phrase.pinyin)});
            }
        if (!dump_stroke_lexicon(rows, argv[3])) return 1;
        const auto packed = Read(argv[3]);
        coral::assist::ShapeDataView after;
        if (!after.bind(packed.data(), packed.size()) || !Same(before, after)) return 1;
        std::cout << "entries identical: " << old.size() << " -> " << packed.size() << " bytes\n";
        return 0;
    }
    if (argc < 2) return 2;
    if (!CsvSources()) return 1;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    const auto file = directory / "roundtrip.dat";
    const std::vector<std::vector<WordAndPinyin>> rows{
        {{u8"木", "mu"}, {u8"木", "mu"}}, {}, {{u8"瞐", "mo"}, {u8"木", "mu"}}};
    if (!Check(dump_stroke_lexicon(rows, file), "writer")) return 1;
    auto bytes = Read(file);
    coral::assist::ShapeDataView view;
    if (!Check(view.bind(bytes.data(), bytes.size()), "reader") ||
        !Check(view.size() == 3 && view[0].size() == 2 && view[1].empty(), "row counts") ||
        !Check(view.get_phrase_view(view[2][0]).word == u8"瞐", "roundtrip word") ||
        !Check(view[0][0].word_off == view[2][1].word_off && view[0][0].pinyin_off == view[0][1].pinyin_off,
            "shared word and pinyin offsets") ||
        !Check(view[3].empty(), "out of range key")) return 1;
    for (std::size_t n = 0; n < sizeof(coral::storage::ShapeArchiveHeader) + 3 * sizeof(coral::storage::ShapeArchiveIndex); ++n)
        if (!Check(!view.bind(bytes.data(), n), "truncated indices")) return 1;
    auto broken = bytes;
    broken[0] = static_cast<char>(255);
    if (!Check(!view.bind(broken.data(), broken.size()), "invalid key count")) return 1;
    broken = bytes;
    std::memset(broken.data() + sizeof(coral::storage::ShapeArchiveHeader) + sizeof(uint32_t), 255, sizeof(uint32_t));
    if (!Check(!view.bind(broken.data(), broken.size()), "oversized row count")) return 1;
    broken = bytes;
    const std::size_t firstInfo = sizeof(coral::storage::ShapeArchiveHeader) + 3 * sizeof(coral::storage::ShapeArchiveIndex);
    std::memset(broken.data() + firstInfo, 255, sizeof(uint32_t));
    if (!Check(!view.bind(broken.data(), broken.size()), "invalid string offset")) return 1;
    broken = bytes;
    broken.back() = 'x';
    if (!Check(!view.bind(broken.data(), broken.size()), "unterminated pool")) return 1;
    if (!Check(dump_stroke_lexicon({}, file), "empty writer")) return 1;
    bytes = Read(file);
    if (!Check(view.bind(bytes.data(), bytes.size()) && view.size() == 0, "empty dictionary")) return 1;
    std::cout << "U dictionary roundtrip and malformed-data checks passed\n";
}
