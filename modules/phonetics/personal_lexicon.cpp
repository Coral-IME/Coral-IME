#include "pch.h"
#include "personal_lexicon.h"
#include "latin_lexicon.h"
#include "syllable_parser.h"
#include "runtime_context.h"
#include <cmath>
#include <map>

namespace {
coral::history::Partition ToPartition(coral::LanguageVariant variant) {
    return variant == coral::LanguageVariant::Traditional
        ? coral::history::Partition::Traditional : coral::history::Partition::Simplified;
}
coral::phonetics::Score FormulaD(coral::phonetics::Score d, coral::phonetics::Score t, coral::phonetics::Score da, coral::phonetics::Score ta) {
    if (da > 50) da -= 1;
    coral::phonetics::Score value = d + da * std::exp((ta - t) / 200);
    if ((t - ta) < 4000) value += 4;
    return std::min<coral::phonetics::Score>(value, 55);
}
coral::phonetics::Score Sigmoid(coral::phonetics::Score d) { return 1 / (1 + std::exp(-d)); }
coral::phonetics::Score FormulaP(coral::phonetics::Score d) {
    const coral::phonetics::Score w = Sigmoid((d - 4.f) / 0.4f) / 800.f;
    const coral::phonetics::Score phi = Sigmoid(1.702f * (d / 10 - 0.5f) / 0.5f);
    return w * phi;
}
bool ContainsLetter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    });
}
}  // namespace

bool coral::phonetics::PersonalLexicon::load(const filesystem::path& userLexiconBinPath, const syllable_strview_map& syllables) {
    static_cast<void>(syllables);
    legacy_base_path_ = userLexiconBinPath;
    legacy_base_path_.replace_extension(".coral");
    std::string error;
    // 先启动策略迁移。旧的过渡期历史文件可能
    // 仍包含候选策略分区，历史管理器现在会将其过滤。
    if (!candidate_policy_->Start(legacy_base_path_, error)) {
        LOG_ERROR("Langengine: Failed to start candidate policy manager - " + error);
        return false;
    }
    if (!history_->Start(legacy_base_path_, error)) {
        candidate_policy_->Shutdown();
        LOG_ERROR("Langengine: Failed to start user history manager - " + error);
        return false;
    }
    is_alive_ = true;
    const auto entries = history_->AllEntries();
    size_ = static_cast<int>(entries.size());
    for (const auto& entry : entries) max_len_ = std::max(max_len_, static_cast<int>(entry.lookup_key.size()));
    return true;
}

void coral::phonetics::PersonalLexicon::get_phrases(const coral::phonetics::PhoneticSequence& pinyin_sequence, coral::phonetics::ParsePhrases& phrases, coral::LanguageVariant variant) {
    if (!is_alive_ || !coral::IsValidLanguageVariant(variant)) return;
    const auto& query = pinyin_sequence.sub_sequences_.empty() ? pinyin_sequence.sequences_ : pinyin_sequence.sub_sequences_;
    const std::string key = coral::phonetics::syntax::join_string_views('\'', query);
    const auto entries = history_->Lookup(ToPartition(variant), key);
    const std::uint64_t currentSequence = history_->currentSequence() + 1;
    const auto& original = pinyin_sequence.original_sequences_.empty() ? pinyin_sequence.sequences_ : pinyin_sequence.original_sequences_;
    const std::string originalPinyin = coral::phonetics::syntax::join_string_views('\'', original);
    for (const auto& entry : entries) {
        // 迁移期间忽略旧自动历史文件中的墓碑记录。
        if ((entry.flags & coral::history::kDeleted) || entry.candidate_position == 0) continue;
        if (!pinyin_sequence.sub_sequences_.empty() && !coral::phonetics::syntax::compare_phrase_pinyin(pinyin_sequence.sequences_, entry.full_pinyin, false)) continue;
        coral::phonetics::Score dee = entry.score_state;
        if (entry.last_used_sequence < currentSequence) dee = FormulaD(0, static_cast<coral::phonetics::Score>(currentSequence), dee, static_cast<coral::phonetics::Score>(entry.last_used_sequence));
        const coral::phonetics::Score probability = FormulaP(dee);
        const coral::phonetics::Score weight = probability > 0 ? std::log(probability) : kLogInfiniteSimal;
        auto existing = std::find_if(phrases.begin(), phrases.end(), [&](const coral::phonetics::LexiconPhrase& phrase) { return phrase.phrase == entry.phrase; });
        if (existing != phrases.end()) {
            existing->phrase_pinyin = originalPinyin;
            existing->phrase_weight = std::max(existing->phrase_weight, weight);
        } else {
            phrase_storage_.push_back(entry.phrase);
            phrases.emplace_back(std::string_view(phrase_storage_.back()), originalPinyin,
                weight, kInvalidPhraseId, kMinBackprob, false, false, false,
                coral::Engine::compatInstance().config().isShowInfos() ? is_corrected_ : false,
                -1, 0);
        }
    }
}

bool coral::phonetics::PersonalLexicon::get_jian_pin_code(const string& py, string& jianPin, bool filterYuan) {
    vector<string> values;
    hlp::splitString(py, "'", values);
    if (values.size() == 1) filterYuan = true;
    if (values.empty()) return false;
    for (const auto& value : values) {
        string_view code;
        if (!coral::phonetics::syntax::get_jian_pin_code(value, code, filterYuan)) return false;
        jianPin += string(code) + "'";
    }
    hlp::trimright(jianPin, "'");
    return true;
}

void coral::phonetics::PersonalLexicon::prepare_commit_phrases(vector<string>& pys, vector<string>& phrases, const string& pinyin, const string& sentence) {
    pys.push_back(pinyin);
    std::string normalized = sentence;
    phrases.push_back(hlp::replaceString(normalized, "'", ""));
}

bool coral::phonetics::PersonalLexicon::update_user_lexicon(const string& py, const string& sent, coral::LanguageVariant variant) {
    if (!is_alive_ || !coral::IsValidLanguageVariant(variant) || py.empty() || sent.empty() || sent.find_first_of("~_-+\\/:|=*/") != std::string::npos) return false;
    vector<string> pinyins, phrases;
    prepare_commit_phrases(pinyins, phrases, py, sent);
    if (pinyins.size() != phrases.size()) return false;
    bool result = true;
    for (std::size_t i = 0; i < pinyins.size(); ++i) {
        std::vector<coral::history::LearnInput> batch;
        batch.push_back({ToPartition(variant), pinyins[i], pinyins[i], phrases[i], coral::history::kNone});
        string shortPinyin;
        if (get_jian_pin_code(pinyins[i], shortPinyin) && shortPinyin != pinyins[i] && hlp::str2wstr(phrases[i]).size() <= 10 && !ContainsLetter(sent)) {
            batch.push_back({ToPartition(variant), shortPinyin, pinyins[i], phrases[i], coral::history::kShortPinyin});
        }
        result = history_->LearnBatch(batch) && result;
    }
    size_ = static_cast<int>(history_->AllEntries().size());
    max_len_ = std::max(max_len_, static_cast<int>(py.size()));
    return result;
}

bool coral::phonetics::PersonalLexicon::load_by_line(const string& pinyinCode, const string& quanPyCode, const string& sent,
                              coral::phonetics::Score dee, int tickTime, const syllable_strview_map& syllables, coral::LanguageVariant variant) {
    if (!is_alive_ || !coral::IsValidLanguageVariant(variant)) return false;
    const auto parts = hlp::splitStringView(pinyinCode, '\'');
    if (std::any_of(parts.begin(), parts.end(), [&](const auto& part) {
        return syllables.find(part) == syllables.end() && (english_lexicon_ == nullptr || !english_lexicon_->is_word_in_lexicon(std::string(part)));
    })) return false;
    coral::history::Entry entry;
    entry.partition = ToPartition(variant); entry.lookup_key = pinyinCode; entry.full_pinyin = quanPyCode; entry.phrase = sent;
    entry.commit_count = 1; entry.score_state = dee; entry.last_used_sequence = static_cast<std::uint64_t>(std::max(tickTime, 1));
    return history_->Upsert(std::move(entry));
}

bool coral::phonetics::PersonalLexicon::pin_candidate_first(const string& pinyin, const string& word,
                                      const string& fullPinyin, coral::LanguageVariant variant) {
    if (!is_alive_ || word.empty() || pinyin.empty() || !coral::IsValidLanguageVariant(variant)) return false;
    coral::candidate_policy::Rule rule;
    rule.variant = variant;
    rule.kind = coral::candidate_policy::RuleKind::Promote;
    rule.pinned = true;
    rule.lookup_key = pinyin;
    rule.full_pinyin = fullPinyin;
    rule.phrase = word;
    const bool ok = candidate_policy_->SetRule(std::move(rule));
    if (ok) coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
    return ok;
}

bool coral::phonetics::PersonalLexicon::unpin_candidate_first(const string& pinyin, const string& word,
                                      coral::LanguageVariant variant) {
    if (!is_alive_ || !coral::IsValidLanguageVariant(variant)) return false;
    for (const auto& rule : candidate_policy_->Lookup(variant, pinyin)) {
        if (rule.phrase == word && rule.kind == coral::candidate_policy::RuleKind::Promote && rule.pinned) {
            const bool ok = candidate_policy_->Remove({variant, pinyin, word});
            if (ok) coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
            return ok;
        }
    }
    return false;
}

bool coral::phonetics::PersonalLexicon::disable_candidate(const string& pinyin, const string& word,
                                    const string& fullPinyin, coral::LanguageVariant variant) {
    if (!is_alive_ || !coral::CanDisableCandidate(word) || pinyin.empty() || !coral::IsValidLanguageVariant(variant)) return false;
    coral::candidate_policy::Rule rule;
    rule.variant = variant;
    rule.kind = coral::candidate_policy::RuleKind::Disable;
    rule.lookup_key = pinyin;
    rule.full_pinyin = fullPinyin;
    rule.phrase = word;
    const bool ok = candidate_policy_->SetRule(std::move(rule));
    if (ok) coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
    return ok;
}

std::vector<coral::candidate_policy::Rule> coral::phonetics::PersonalLexicon::get_candidate_policies(
    const std::string& lookupKey, coral::LanguageVariant variant) const {
    if (!is_alive_ || !coral::IsValidLanguageVariant(variant)) return {};
    return candidate_policy_->Lookup(variant, lookupKey);
}

bool coral::phonetics::PersonalLexicon::add_phrase_shortcut(const std::string& code, const std::string& phrase) {
    if (!is_alive_ || code.empty() || phrase.empty()) return false;
    bool ok = true;
    for (const auto variant : { coral::candidate_policy::LanguageVariant::Simplified,
                                coral::candidate_policy::LanguageVariant::Traditional }) {
        coral::candidate_policy::Rule rule;
        rule.variant = variant;
        rule.kind = coral::candidate_policy::RuleKind::Promote;
        rule.pinned = false;
        rule.lookup_key = code;
        rule.full_pinyin = code;
        rule.phrase = phrase;
        ok = candidate_policy_->SetRule(std::move(rule)) && ok;
    }
    coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
    return ok;
}

bool coral::phonetics::PersonalLexicon::remove_phrase_shortcut(const std::string& code, const std::string& phrase) {
    if (!is_alive_ || code.empty() || phrase.empty()) return false;
    bool removed = false;
    for (const auto variant : { coral::candidate_policy::LanguageVariant::Simplified,
                                coral::candidate_policy::LanguageVariant::Traditional }) {
        for (const auto& rule : candidate_policy_->Lookup(variant, code)) {
            if (rule.phrase == phrase && rule.kind == coral::candidate_policy::RuleKind::Promote && !rule.pinned)
                removed = candidate_policy_->Remove({variant, code, phrase}) || removed;
        }
    }
    coral::phonetics::SyllableParser::instance().clear_pinyin_model_cache();
    return removed;
}

std::vector<CustomPhraseEntry> coral::phonetics::PersonalLexicon::list_phrase_shortcuts() const {
    std::vector<CustomPhraseEntry> result;
    if (!is_alive_) return result;
    // 简繁两套规则按 (输入码, 短语) 合并；固定到首位的候选不属于常用语。
    std::map<std::pair<std::string, std::string>, std::size_t> seen;
    for (const auto& rule : candidate_policy_->AllRules()) {
        if (rule.kind != coral::candidate_policy::RuleKind::Promote || rule.pinned) continue;
        const auto [it, inserted] = seen.emplace(std::make_pair(rule.lookup_key, rule.phrase), result.size());
        if (!inserted) {
            result[it->second].sequence = std::max(result[it->second].sequence, rule.updated_sequence);
            continue;
        }
        result.push_back({rule.lookup_key, rule.phrase, rule.updated_sequence});
    }
    std::sort(result.begin(), result.end(), [](const CustomPhraseEntry& a, const CustomPhraseEntry& b) {
        return a.sequence > b.sequence;
    });
    return result;
}

bool coral::phonetics::PersonalLexicon::store_lexicon() const {
    if (!is_alive_) return true;
    std::string error;
    if (!history_->Flush(error)) { LOG_ERROR("Langengine: User history checkpoint failed - " + error); return false; }
    if (!candidate_policy_->Flush(error)) { LOG_ERROR("Langengine: Candidate policy checkpoint failed - " + error); return false; }
    return true;
}

bool coral::phonetics::PersonalLexicon::maintain_user_data() {
    if (is_alive_) {
        history_->RequestCompact();
        candidate_policy_->RequestCheckpoint();
    }
    return true;
}

bool coral::phonetics::PersonalLexicon::clear_user_lexicon() {
    if (!is_alive_) return true;
    if (!history_->ClearAll() || !candidate_policy_->ClearAll()) return false;
    std::string error;
    if (!history_->Flush(error)) { LOG_ERROR("Langengine: Failed to persist cleared user history - " + error); return false; }
    if (!candidate_policy_->Flush(error)) { LOG_ERROR("Langengine: Failed to persist cleared candidate policy - " + error); return false; }
    size_ = 0; return true;
}

bool coral::phonetics::PersonalLexicon::re_connect_user_lexicon() { return is_alive_; }
bool coral::phonetics::PersonalLexicon::revert_last_action_for_phrase(const std::string& phrase) { return history_->RevertLast(phrase); }

std::pair<string, string> coral::phonetics::PersonalLexicon::get_user_lexicon_path() const {
    if (history_ && !history_->path().empty()) return {history_->path().string(), history_->path().string()};
    return {legacy_base_path_.string(), legacy_base_path_.string()};
}

bool coral::phonetics::PersonalLexicon::upload_user_dictionary(coral::LanguageVariant variant) { static_cast<void>(variant); return true; }

bool coral::phonetics::PersonalLexicon::export_unified_user_lexicon_as_csv(const std::filesystem::path& exportPath) const {
    if (!is_alive_) return false;
    std::ofstream output(exportPath, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write("\xEF\xBB\xBF", 3); output << "Phrase,Pinyin,FullPinyin,Charset\n";
    for (const auto& entry : history_->AllEntries()) {
        if (entry.partition != coral::history::Partition::Simplified && entry.partition != coral::history::Partition::Traditional) continue;
        output << entry.phrase << ',' << entry.lookup_key << ',' << entry.full_pinyin << ','
               << (entry.partition == coral::history::Partition::Simplified ? 'S' : 'T') << '\n';
    }
    return static_cast<bool>(output);
}

hlp::ImportResult coral::phonetics::PersonalLexicon::import_unified_user_lexicon_from_csv(const std::filesystem::path& importPath,
                                                                const syllable_strview_map& syllables) {
    hlp::ImportResult result;
    std::ifstream input(importPath, std::ios::binary);
    if (!input) { result.errors.emplace_back(-1, "can not open file: " + importPath.string()); return result; }
    std::string header;
    if (!std::getline(input, header)) { result.errors.emplace_back(0, "file is empty or can not read"); return result; }
    hlp::trimWhitespace(header); if (header.rfind("\xEF\xBB\xBF", 0) == 0) header.erase(0, 3);
    if (header != "Phrase,Pinyin,FullPinyin,Charset") { result.errors.emplace_back(1, "file type error: the header of CSV is wrong"); return result; }
    std::vector<coral::history::Entry> entries;
    std::string line; int lineNumber = 1;
    while (std::getline(input, line)) {
        ++lineNumber; hlp::trimWhitespace(line); if (line.empty()) continue; ++result.totalLinesRead;
        const auto fields = hlp::parse_csv_line(line);
        if (fields.size() != 4 || fields[0].empty() || fields[1].empty()) { result.errors.emplace_back(lineNumber, "invalid CSV fields"); continue; }
        const auto parts = hlp::splitStringView(fields[1], '\'');
        if (std::any_of(parts.begin(), parts.end(), [&](const auto& part) { return syllables.find(part) == syllables.end(); })) {
            result.errors.emplace_back(lineNumber, "pinyin is not valid: " + fields[1]); continue;
        }
        const char charset = static_cast<char>(std::toupper(static_cast<unsigned char>(fields[3].empty() ? ' ' : fields[3][0])));
        if (charset != 'S' && charset != 'T') { result.errors.emplace_back(lineNumber, "charset of item is wrong"); continue; }
        coral::history::Entry entry;
        entry.partition = charset == 'S' ? coral::history::Partition::Simplified : coral::history::Partition::Traditional;
        entry.phrase = fields[0]; entry.lookup_key = fields[1]; entry.full_pinyin = fields[2].empty() ? fields[1] : fields[2];
        entry.commit_count = 1; entry.score_state = 1; entries.push_back(std::move(entry));
    }
    if (!result.errors.empty()) return result;
    if (history_->AllEntries().size() + entries.size() > MAX_CAPACITY) {
        result.errors.emplace_back(-1, "Import would exceed total user history capacity (100000)"); return result;
    }
    for (auto& entry : entries) if (history_->Upsert(std::move(entry))) ++result.successfulImports;
    return result;
}

std::vector<coral::history::Entry> coral::phonetics::PersonalLexicon::history_entries() const {
    if (!is_alive_) return {};
    return history_->AllEntries();
}

std::uint64_t coral::phonetics::PersonalLexicon::history_sequence() const {
    return history_->currentSequence();
}

bool coral::phonetics::PersonalLexicon::import_history_entries(std::vector<coral::history::Entry> entries) {
    if (!is_alive_ || entries.empty()) return false;
    for (const auto& entry : entries) max_len_ = std::max(max_len_, static_cast<int>(entry.lookup_key.size()));
    if (!history_->UpsertBatch(std::move(entries))) return false;
    std::string error;
    if (!history_->Flush(error)) LOG_ERROR("Langengine: Failed to persist imported user lexicon - " + error);
    size_ = static_cast<int>(history_->AllEntries().size());
    return true;
}

bool coral::phonetics::PersonalLexicon::remove_history_entries(const std::vector<coral::history::EntryIdentity>& identities) {
    if (!is_alive_ || identities.empty()) return false;
    if (!history_->RemoveBatch(identities)) return false;
    std::string error;
    if (!history_->Flush(error)) LOG_ERROR("Langengine: Failed to persist reverted user lexicon import - " + error);
    size_ = static_cast<int>(history_->AllEntries().size());
    return true;
}

void coral::phonetics::PersonalLexicon::set_is_corrected(bool value) { is_corrected_ = value; }
void coral::phonetics::PersonalLexicon::shutdown() {
    if (history_) history_->Shutdown();
    if (candidate_policy_) candidate_policy_->Shutdown();
    is_alive_ = false;
}
