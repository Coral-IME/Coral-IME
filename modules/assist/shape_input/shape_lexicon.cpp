#include "shape_lexicon.h"
#include <cassert>
#include "source_csv.h"
#include <sstream>
#include <map>
#include <iostream>

bool coral::assist::ShapeLexicon::load(const std::pair<std::filesystem::path, std::string>& triePath,
	const std::pair<std::filesystem::path, std::string>& dataPath,
	const std::filesystem::path& baseDir,
	const std::string& versionId)
{
	if(!std::filesystem::exists(triePath.first))
		return false;
	if (!triePath.second.empty() && hlp::calculateFileMD5(triePath.first.string()) != triePath.second)
        return false;
    trie_.load(triePath.first.string().c_str());
	if (!data_view_.map(dataPath.first.wstring(), hlp::buildSHNameW(versionId, triePath.first.stem().string()+"SharedMemory"), dataPath.second))
		return false;
	return true;
}

bool coral::assist::ShapeLexicon::build_lexicon(const std::filesystem::path& csvPath,
                                const std::filesystem::path& lexicon,
                                const std::filesystem::path& lexiconData)
{
    std::ifstream ifs(csvPath, std::ios::binary);
	if (!ifs.is_open())
		return false;

	std::map<std::string, std::vector<WordAndPinyin>> data;
	try {
		coral::data::read_source_csv_header(ifs, {"word", "code", "pinyin"});
		std::vector<std::string> fields;
		while (coral::data::read_source_csv_row(ifs, fields)) {
			if (fields.size() == 1 && fields[0].empty()) continue;
			if (fields.size() != 3 || fields[0].empty() || fields[1].empty() || fields[2].empty())
				throw std::runtime_error("Expected word,code,pinyin in U-mode CSV");
			data[fields[1]].push_back({fields[0], fields[2]});
		}
		if (data.empty()) throw std::runtime_error("U-mode CSV has no entries");
	} catch (const std::exception& error) {
		std::cerr << csvPath.string() << ": " << error.what() << '\n';
		return false;
	}
	ifs.close();

	// 构建 Trie
	marisa::Keyset keyset;
    // 嵌套vector，因为每个笔画、部首序列对应多个词条
	std::vector<std::vector<WordAndPinyin>> allData;
	allData.resize(data.size());
    for (const auto& pair : data)
    {
        const std::string& key_str = pair.first;
        marisa::Key key;
        key.set_str(static_cast<const char*>(key_str.data()), key_str.size());
        keyset.push_back(key);
    }
	trie_.build(keyset);

	for (const auto& [key, vec] : data)
    {
		marisa::Agent agent;
		agent.set_query(key.data(), key.size());
		trie_.lookup(agent);
		marisa::UInt32  resultIndex = agent.key().id();
        if (resultIndex < allData.size())
    		allData[resultIndex] = std::move(vec);
	}

	trie_.save(lexicon.string().c_str());
	return dump_stroke_lexicon(allData, lexiconData);
}
void coral::assist::ShapeLexicon::append_candidates(std::string_view prefix, const std::string& input,
    std::vector<coral::phonetics::CandidateResult>& sents, int limit) {
    if (prefix.empty() || limit <= 0) return;
    marisa::Agent agent;
    agent.set_query(prefix.data(), prefix.size());
    int added = 0;
    while (trie_.predictive_search(agent)) {
        for (const auto& info : data_view_[agent.key().id()]) {
            const auto phrase = data_view_.get_phrase_view(info);
            if (phrase.word.empty()) continue;
            coral::phonetics::CandidateResult solution;
            solution.sentence.assign(phrase.word.data(), phrase.word.size());
            if (phrase.word != phrase.pinyin) {
                solution.display_sentence = solution.sentence;
                solution.display_sentence += '(';
                solution.display_sentence.append(phrase.pinyin.data(), phrase.pinyin.size());
                solution.display_sentence += ')';
            }
            solution.pinyin.push_back(input);
            solution.transform_flags = MiddlewareType::NoStoreUserLexicon;
            sents.push_back(std::move(solution));
            if (++added >= limit) return;
        }
    }
}
