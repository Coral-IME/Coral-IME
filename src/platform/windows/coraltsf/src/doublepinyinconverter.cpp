#include "pch.h"
#include "doublepinyinconverter.h"
#include "ini_store.h"
#include "layout_metadata.h"

// 声母对应的合法韵母，不包括零声母
static const std::unordered_map<coral::phonetics::TextSlice, std::vector<coral::phonetics::TextSlice>> letters =
{
    {"b", {"a", "o", "i", "u", "ai", "ei", "ao", "iao", "ie", "an", "ian", "en", "in", "ang", "eng", "ing"}},
    {"p", {"a", "o", "i", "u", "ai", "ei", "ao", "iao", "ou", "ie", "an", "ian", "en", "in", "ang", "eng", "ing"}},
    {"m", {"a", "o", "e", "i", "u", "ai", "ei", "ao", "iao", "ou", "ie", "iu", "an", "ian", "en", "in", "ang", "eng", "ing"}},
    {"f", {"a", "o", "u", "ei", "ou", "an", "en", "ang", "eng"}},
    {"d", {"a", "e", "i", "u", "uo", "ei", "en", "ai", "ui", "ao", "iao", "ou", "iu", "ie", "ia", "an", "ian", "uan", "un", "ang", "eng", "ing", "ong"}},
    {"t", {"a", "e", "i", "u", "uo", "ai", "ei", "ui", "ao", "iao", "ou", "ie", "an", "ian", "uan", "un", "ang", "eng", "ing", "ong"}},
    {"n", {"a", "e", "i", "u", "uo", "v", "ve", "ai", "ei", "en","ao", "iao", "ou", "iu", "ie", "ue", "un", "an", "ian", "uan", "in", "ang", "iang", "eng", "ing", "ong"}},
    {"l", {"a", "e", "i", "u", "uo", "v", "ve","ai", "ei", "ao", "iao", "ou", "iu", "ie", "ia", "ue", "an", "ian", "uan", "in", "un", "ang", "iang", "eng", "ing", "ong"}},
    {"g", {"a", "e", "u", "ua", "uo", "ai", "uai", "ei", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "uang", "eng", "ong"}},
    {"k", {"a", "e", "u", "ua", "uo", "ai", "uai", "ei", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "uang", "eng", "ong"}},
    {"h", {"a", "e", "u", "ua", "uo", "ai", "uai", "ei", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "uang", "eng", "ong"}},
    {"j", {"i", "ia", "u", "iao", "iu", "ie", "ue", "ian", "uan", "in", "un", "iang", "ing", "iong"}},
    {"q", {"i", "ia", "u", "iao", "iu", "ie", "ue", "ian", "uan", "in", "un", "iang", "ing", "iong"}},
    {"x", {"i", "ia", "u", "iao", "iu", "ie", "ue", "ian", "uan", "in", "un", "iang", "ing", "iong"}},
    {"zh", {"a", "e", "i", "u", "ua", "uo", "ai", "uai", "ei", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "uang", "eng", "ong"}},
    {"ch", {"a", "e", "i", "u", "uo", "ai", "uai", "ui", "ao", "ou", "an", "uan", "en", "un", "ua", "ang", "uang", "eng", "ong"}},
    {"sh", {"a", "e", "i", "u", "ua", "uo", "ai", "uai", "ui", "ao", "ou", "an", "uan", "en", "ei", "un", "ang", "uang", "eng"}},
    {"r", {"e", "i", "u", "uo", "ui", "ua", "ao", "ou", "an", "uan", "en", "un", "ang", "eng", "ong"}},
    {"z", {"a", "e", "i", "u", "uo", "ai", "ei", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "eng", "ong"}},
    {"c", {"a", "e", "i", "u", "uo", "ai", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "eng", "ong"}},
    {"s", {"a", "e", "i", "u", "uo", "ai", "ui", "ao", "ou", "an", "uan", "en", "un", "ang", "eng", "ong"}},
    {"y", {"a", "e", "i", "u", "ao", "ou", "ue", "an", "uan", "in", "un", "ang", "ing", "ong"}},
    {"w", {"a", "o", "u", "ai", "ei", "an", "en", "ang", "eng"}}
};

BaseDoublepinyinScheme::BaseDoublepinyinScheme(const coral::phonetics::TextSlice name, const coral::phonetics::TextSlice display_name, const MappingTables& tables) noexcept
    : m_name(name)
    , m_displayName(display_name)
    , m_tables(tables)
{}

coral::phonetics::TextSlice BaseDoublepinyinScheme::get_name() const noexcept {
	return m_name;
}

const coral::phonetics::TextSlice BaseDoublepinyinScheme::getShengmuFromKey(char key) const {
    if (const auto it = m_tables.shengmuMap.find(key); it != m_tables.shengmuMap.end()) {
        return it->second;
    }
    return {};
}

const std::vector<std::string>& BaseDoublepinyinScheme::getYunmusFromKey(char key) const {
    if (const auto it = m_tables.yunmuMap.find(key); it != m_tables.yunmuMap.end()) {
        return it->second;
    }
    static const std::vector<std::string> empty_vec;
    return empty_vec;
}

const coral::phonetics::TextSlice BaseDoublepinyinScheme::getZeroShengmuFromKey(char key) const {
    if (const auto it = m_tables.zeroShengmuMap.find(key); it != m_tables.zeroShengmuMap.end()) {
        return it->second;
    }
    return {};
}

const coral::phonetics::TextSlice BaseDoublepinyinScheme::getZeroYunmuFromKey(char key) const {
    const char outerKey = m_pendingShengmu.front();
    if (const auto it = m_tables.zeroYunmuMap.find(outerKey); it != m_tables.zeroYunmuMap.end()) {
        const auto& innerMap = it->second;
        if (const auto innerIt = innerMap.find(key); innerIt != innerMap.end()) {
            return innerIt->second;
        }
    }
    return {};
}

void BaseDoublepinyinScheme::normalSmRecord(InputRecord& recordS, coral::phonetics::TextSlice shengmu)const {
    m_state = ParseState::AwaitingYunmu;
    m_pendingShengmu = shengmu;
    recordS.type = InputType::Shengmu;
    recordS.isZeroShengmu = false;
    if (m_inputHistory.empty()) {
        recordS.length = static_cast<uint8_t>(shengmu.length());
    }
    else {
        recordS.length = static_cast<uint8_t>(shengmu.length()) + 1;  // 1的长度给了分隔符
    }
    
    recordS.shengmu = shengmu;
    m_inputHistory.push_back(recordS);
#ifdef _DEBUG
    debugPrintState();
#endif
}

void BaseDoublepinyinScheme::zeroSmRecord(InputRecord& recordS, coral::phonetics::TextSlice shengmu) const {
    m_state = ParseState::AwaitingYunmu;
    m_pendingShengmu = shengmu;
    recordS.type = InputType::Shengmu;
    recordS.isZeroShengmu = true;
    if (m_inputHistory.empty()) {
        recordS.length = static_cast<uint8_t>(shengmu.length());
    }
    else {
        recordS.length = static_cast<uint8_t>(shengmu.length()) + 1;
    }
    recordS.shengmu = shengmu;
    m_inputHistory.push_back(recordS);
#ifdef _DEBUG
    debugPrintState();
#endif
}

void BaseDoublepinyinScheme::zeroYmRecord(InputRecord& recordY, coral::phonetics::TextSlice yunmu) const {
    m_state = ParseState::AwaitingShengmu;
    recordY.type = InputType::Yunmu;
    recordY.length = static_cast<uint8_t>(yunmu.length());
    m_pendingShengmu = {}; // 声母置空
    m_inputHistory.push_back(recordY);
#ifdef _DEBUG
    debugPrintState();
#endif
}

void BaseDoublepinyinScheme::normalYmRecord(InputRecord& recordY, coral::phonetics::TextSlice yunmu) const {
    m_state = ParseState::AwaitingShengmu;
    recordY.type = InputType::Yunmu;
    recordY.length = static_cast<uint8_t>(yunmu.length());
    m_pendingShengmu = {};
    m_inputHistory.push_back(recordY);
#ifdef _DEBUG
    debugPrintState();
#endif
}

void BaseDoublepinyinScheme::signRecord() const {
    InputRecord recordSign;
    m_state = ParseState::AwaitingShengmu;
    recordSign.type = InputType::Yunmu;  // 是符号输入，但是假定输入韵母不影响后续状态
    recordSign.length = 1;
    recordSign.isSign = true;
    m_pendingShengmu = {};
    m_inputHistory.push_back(recordSign);
#ifdef _DEBUG
	debugPrintState();
#endif
}

CString BaseDoublepinyinScheme::parseDoublepinyinInput(char letter) const
{
    switch (m_state) {
        case ParseState::AwaitingShengmu: {
            InputRecord recordS;
            coral::phonetics::TextSlice shengmu = getShengmuFromKey(letter);
            // 正常声母
            if (!shengmu.empty()) {
                normalSmRecord(recordS, shengmu);
                if(m_inputHistory.size() == 1)  // 仅当只有第一个输入声母的时候不加'
                    return CString(m_pendingShengmu.data(), static_cast<int>(m_pendingShengmu.length()));
                else
                    return '\'' + CString(m_pendingShengmu.data(), static_cast<int>(m_pendingShengmu.length()));
            }
            else {  // 零声母情况
                shengmu = getZeroShengmuFromKey(letter);
                if (!shengmu.empty()) {
                    zeroSmRecord(recordS, shengmu);
                    if(m_inputHistory.size() == 1)
                        return CString(m_pendingShengmu.data(), static_cast<int>(m_pendingShengmu.length()));
                    else
                        return '\'' + CString(m_pendingShengmu.data(), static_cast<int>(m_pendingShengmu.length()));
                }
            }
            signRecord();   //符号输入、无声母映射输入的情况
            return CString(letter);
        }
        case ParseState::AwaitingYunmu: {
            InputRecord recordY;
            if (m_inputHistory.back().isZeroShengmu) {
                // 上一个声母是零声母
                coral::phonetics::TextSlice yunmu = getZeroYunmuFromKey(letter);
                if (!yunmu.empty()) {
                    zeroYmRecord(recordY, yunmu);
                    return CString(yunmu.data(), static_cast<int>(yunmu.length()));
                }
            }
            else {
                // 上一个声母是正常声母
                const auto& yunmus = getYunmusFromKey(letter);
                coral::phonetics::TextSlice yunmu;
                const auto& compatibleYunmus = letters.find(m_pendingShengmu);
                if (compatibleYunmus != letters.end()) {
                    for (const auto& candidate : yunmus) {
                        if (std::find(compatibleYunmus->second.begin(), compatibleYunmus->second.end(), candidate)
                            != compatibleYunmus->second.end()) {
                            yunmu = candidate;
                            break;
                        }
                    }
                    if (!yunmu.empty()) {
                        normalYmRecord(recordY, yunmu);
                        return CString(yunmu.data(), static_cast<int>(yunmu.length()));
                    }
                }
            }
            // 无法和当前声母组成合法拼音的情况:
            // 例如输入tt处理为t't的简拼
            // 所以状态不变，还是等待韵母，输入映射回退为声母
            if (letter == ';' || letter == ':' || letter == '|' || letter == '\'' || letter == '\\') {    
                // 如果是输入;的情况且无合法拼音,以及其他符号的情况下
                m_state = ParseState::AwaitingShengmu;
                signRecord();
                return CString(letter);
            }
            m_state = ParseState::AwaitingYunmu;
            recordY.type = InputType::Shengmu;
            coral::phonetics::TextSlice fallbackShengmu = getShengmuFromKey(letter);
            if (!fallbackShengmu.empty()) {
                normalSmRecord(recordY, fallbackShengmu);
            }
            else {
                fallbackShengmu = getZeroShengmuFromKey(letter);
                if (!fallbackShengmu.empty())
                    zeroSmRecord(recordY, fallbackShengmu);
            }
            return '\'' + CString(fallbackShengmu.data(), static_cast<int>(fallbackShengmu.length()));
        }
    }
    return {};
}

void BaseDoublepinyinScheme::resetState() noexcept {
    m_state = ParseState::AwaitingShengmu;
    m_pendingShengmu = {};
    m_inputHistory.clear();
    m_lastEraseLength = 0;
    m_pendingShengmu = {};
}

void BaseDoublepinyinScheme::revertLastInput() noexcept {
    if (m_inputHistory.empty()) return;

    const InputRecord last = m_inputHistory.back();
    m_inputHistory.pop_back();
    m_lastEraseLength = last.length;
    if (last.type == InputType::Yunmu) {
        // 回退的是韵母
        m_state = ParseState::AwaitingYunmu;
        if (!m_inputHistory.empty())
            m_pendingShengmu = m_inputHistory.back().shengmu;
        else
            m_pendingShengmu = {};  // 正常来讲不会走else，但是为了安全
    }
    else {
        // 回退的是声母
        if (!m_inputHistory.empty()) {
            if (m_inputHistory.back().type == InputType::Shengmu) { // 应对诸如t't回退到t的情况
                m_state = ParseState::AwaitingYunmu;
                m_pendingShengmu = m_inputHistory.back().shengmu;
            }
            else
                m_state = ParseState::AwaitingShengmu;
        }
        else {
            m_pendingShengmu = {};  // 删除的是第一个声母，会走这个
        }
    }
#ifdef _DEBUG
    debugPrintState();
#endif
}

uint8_t BaseDoublepinyinScheme::getLastEraseLength() const noexcept {
    return static_cast<uint8_t>(m_lastEraseLength);
}

bool BaseDoublepinyinScheme::zeroProcess(char letter) const {
    // 如果前一个输入是零声母 && 后一次输入可以跟零声母组成合法映射音节（即oa->a而非op->o'p的情况)
    // 那么就需要对上层candidate的addleter内部做一下处理，即oa先映射o再映射a时，应当先去掉o再加上a。
    if (!m_inputHistory.empty() && 
        m_inputHistory.back().isZeroShengmu && 
        !getZeroYunmuFromKey(letter).empty()) {
        return true;
    }
    return false;
}

uint8_t BaseDoublepinyinScheme::getNextMoveLength(int historyIndex, uint8_t& historyIndexMove) const noexcept {  // 右移长度
    if (m_inputHistory.empty() || historyIndex < 0 || static_cast<size_t>(historyIndex) >= m_inputHistory.size()) {
        return 0;
    }

    if (m_inputHistory.size() >= 2 && static_cast<size_t>(historyIndex) <= m_inputHistory.size() - 2) {
        if (m_inputHistory[historyIndex].isZeroShengmu &&
            m_inputHistory[static_cast<size_t>(historyIndex) + 1].type == InputType::Yunmu &&
            !m_inputHistory[static_cast<size_t>(historyIndex) + 1].isSign) {
            historyIndexMove = 2;
            if (historyIndex == 0)  // 处理光标在最开始不存在多一个'的情况
                return m_inputHistory[static_cast<size_t>(historyIndex) + 1].length;
            return m_inputHistory[static_cast<size_t>(historyIndex) + 1].length + 1;
        }
    }
	historyIndexMove = 1;
	return m_inputHistory[historyIndex].length;
}

uint8_t BaseDoublepinyinScheme::getPrevMoveLength(int historyIndex, uint8_t& historyIndexMove) const noexcept {   // 左移长度
	if (historyIndex <= 0 || 
        m_inputHistory.empty() ||
        static_cast<size_t>(historyIndex) > m_inputHistory.size()) {
		return 0;
	}

    if (historyIndex >= 2 && m_inputHistory.size() >= 2) {  // 这里的判断是对零声母做处理
        if (m_inputHistory[static_cast<size_t>(historyIndex) - 2].isZeroShengmu &&
            m_inputHistory[static_cast<size_t>(historyIndex) - 1].type == InputType::Yunmu &&
            !m_inputHistory[static_cast<size_t>(historyIndex) - 1].isSign) {
            historyIndexMove = 2;
            return m_inputHistory[static_cast<size_t>(historyIndex) - 1].length + 1;  // 例如oa->a的情况，只普通光标左移1，双拼光标左移2
            // 最后又加了一个1是因为考虑到音节的切分’符
        }
    }
	historyIndexMove = 1;
	return m_inputHistory[static_cast<size_t>(historyIndex) - 1].length;
}

IniBasedDoublepinyinScheme::IniBasedDoublepinyinScheme(const std::string& name, const std::string& displayName, const std::string& iniPath)
    : BaseDoublepinyinScheme(name, displayName, loadTables(iniPath)) {}


MappingTables IniBasedDoublepinyinScheme::loadTables(const std::string& path) {
    MappingTables tables;
    IniFileManager ini(path);
    auto shengmu = ini.getSection("shengmu");
    for (const auto& var : shengmu)
        tables.shengmuMap[var.second.at(0)] = var.first;

    auto yunmu = ini.getSection("yunmu");
    for (const auto& entry : yunmu)
        tables.yunmuMap[entry.second.at(0)].push_back(entry.first);

    auto zero_shengmu = ini.getSection("zerosheng");
    for (const auto& var : zero_shengmu) {
        char key = var.first.at(0);
        tables.zeroShengmuMap[key] = var.second;

        auto zero_yun = ini.getSection("zero" + std::string(1, key));
        std::unordered_map<char, std::string> child_table;
        for (const auto& yun : zero_yun)
            child_table[yun.first.at(0)] = yun.second;
        tables.zeroYunmuMap[key] = child_table;
    }

    return tables;
}

namespace {
	const std::string doublepinyinIniPath = (hlp::getInstallPath() / "data" / "doublepinyin").string() + "/";

	struct BuiltinSchemeRegistrar {
		BuiltinSchemeRegistrar() {
			if (!std::filesystem::exists(doublepinyinIniPath)) {
				LOG_ERROR("Doublepinyin config directory not found: " + doublepinyinIniPath);
				return;
			}
			for (const auto& entry : std::filesystem::directory_iterator(doublepinyinIniPath)) {
				if (entry.is_regular_file() && entry.path().extension() == ".ini") {
					std::string filename = entry.path().stem().u8string();
					const auto displayName = coral::config::readDoublepinyinSchemeTitle(entry.path());
					if (!displayName.empty()) {
						const std::string& name_val = filename;
						const std::string& display_val = displayName;
						std::string path_val = entry.path().string();

						DoublepinyinSchemeFactory::instance().registerScheme(
							name_val,
							[name_val, display_val, path_val]() -> std::unique_ptr<IDoublepinyinScheme> {
								return std::make_unique<IniBasedDoublepinyinScheme>(
									name_val,
									display_val,
									path_val
									);
							}
						);
					}
				}
			}
		}
	};
	BuiltinSchemeRegistrar _registerBuiltinSchemes;
}

// 调试函数，后续可删
#ifdef _DEBUG
#include <sstream>
void BaseDoublepinyinScheme::debugPrintState() const noexcept {
    std::ostringstream oss;
    oss << "[Doublepinyin Debug] >>> 当前状态如下：\n";
    oss << "  状态(State) = "
        << (m_state == ParseState::AwaitingShengmu ? "AwaitingShengmu" : "AwaitingYunmu")
        << "\n";
    oss << "  当前待处理声母(pendingShengmu) = \"" << m_pendingShengmu << "\"\n";
    oss << "  上次映射长度(lastEraseLength) = " << static_cast<int>(m_lastEraseLength) << "\n";
    oss << "  输入记录条数 = " << m_inputHistory.size() << "\n";

    oss << "  InputHistory = [\n";
    for (size_t i = 0; i < m_inputHistory.size(); ++i) {
        const auto& rec = m_inputHistory[i];
        oss << "    #" << i
            << " { type=" << (rec.type == InputType::Shengmu ? "Shengmu" : "Yunmu")
            << ", isZero=" << (rec.isZeroShengmu ? "true" : "false")
            << ", length=" << static_cast<int>(rec.length)
            << ", shengmu=\"" << rec.shengmu << "\" }\n";
    }
    oss << "  ]\n";

    OutputDebugStringA(oss.str().c_str());
}
#endif
