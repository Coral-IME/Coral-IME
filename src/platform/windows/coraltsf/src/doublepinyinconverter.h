#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_DOUBLEPINYINCONVERTER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_DOUBLEPINYINCONVERTER_H_INCLUDED

#include "pch.h"
#include <string>
#include <unordered_map>
#include <memory>

struct MappingTables {
    std::unordered_map<char, std::string> shengmuMap;
    std::unordered_map<char, std::vector<std::string>> yunmuMap;
    std::unordered_map<char, std::string> zeroShengmuMap;  // 零声母
    std::unordered_map<char, std::unordered_map<char,std::string>> zeroYunmuMap;  // 零声母的韵母
};

enum class InputType { Shengmu, Yunmu };
struct InputRecord {
    InputType type;
    bool isZeroShengmu = false;
    uint8_t length = 0;               // 该次映射占据的拼音字符数
    coral::phonetics::TextSlice shengmu;           // 若是声母输入，记录下来
    bool isSign = false;
};

struct DoublepinyinSchemeInfo {
    std::string name;
    std::string displayName;
    std::string iniPath;
};

// 接口
class IDoublepinyinScheme {
public:
    virtual ~IDoublepinyinScheme() = default;

    [[nodiscard]] virtual coral::phonetics::TextSlice get_name() const noexcept = 0;
    [[nodiscard]] virtual CString parseDoublepinyinInput(char key) const = 0;  // 核心双拼接口
    [[nodiscard]] virtual bool zeroProcess(char letter) const = 0;  // addleter暂时用
    virtual void resetState() noexcept = 0;
    virtual void revertLastInput() noexcept = 0;
    virtual uint8_t getLastEraseLength() const noexcept = 0;
    virtual uint8_t getNextMoveLength(int historyIndex, uint8_t& historyIndexMove) const noexcept = 0;
    virtual uint8_t getPrevMoveLength(int historyIndex, uint8_t& historyIndexMove) const noexcept = 0;
};

class BaseDoublepinyinScheme : public IDoublepinyinScheme {
public:
    explicit BaseDoublepinyinScheme(const coral::phonetics::TextSlice name, const coral::phonetics::TextSlice display_name, const MappingTables& tables) noexcept;
	[[nodiscard]] coral::phonetics::TextSlice get_name() const noexcept override;
    [[nodiscard]] CString parseDoublepinyinInput(char key) const override;
    [[nodiscard]] bool zeroProcess(char letter) const override;
    void resetState() noexcept override;
    void revertLastInput() noexcept override;
    uint8_t getLastEraseLength() const noexcept override;
    uint8_t getNextMoveLength(int historyIndex, uint8_t& historyIndexMove) const noexcept override;
    uint8_t getPrevMoveLength(int historyIndex, uint8_t& historyIndexMove) const noexcept override;
#ifdef _DEBUG
    void debugPrintState() const noexcept;
#endif

private:
    [[nodiscard]] const coral::phonetics::TextSlice getShengmuFromKey(char key) const;
    [[nodiscard]] const std::vector<std::string>& getYunmusFromKey(char key) const;
    [[nodiscard]] const coral::phonetics::TextSlice getZeroShengmuFromKey(char key) const;
    [[nodiscard]] const coral::phonetics::TextSlice getZeroYunmuFromKey(char key) const;
    void normalSmRecord(InputRecord& recordS, coral::phonetics::TextSlice shengmu) const;
    void zeroSmRecord(InputRecord& recordS, coral::phonetics::TextSlice shengmu) const;
    void normalYmRecord(InputRecord& recordY, coral::phonetics::TextSlice yunmu) const;
    void zeroYmRecord(InputRecord& recordY, coral::phonetics::TextSlice yunmu) const;
    void signRecord() const;

public:
    enum class ParseState {
        AwaitingShengmu,
        AwaitingYunmu
    };
    mutable ParseState m_state = ParseState::AwaitingShengmu;  // 初始状态==等待声母
    mutable std::vector<InputRecord> m_inputHistory;
    mutable size_t m_lastEraseLength = 0;
    mutable coral::phonetics::TextSlice m_pendingShengmu;  // 暂存上一次解析后的声母

private:
    const std::string m_name;
    const std::string m_displayName;
    const MappingTables m_tables; 
};

class IniBasedDoublepinyinScheme : public BaseDoublepinyinScheme {
public:
    IniBasedDoublepinyinScheme(const std::string& name, const std::string& displayName, const std::string& iniPath);
    static MappingTables loadTables(const std::string& path);
};

using DoublepinyinSchemeCreator = std::function<std::unique_ptr<IDoublepinyinScheme>()>;

class DoublepinyinSchemeFactory {
public:
    static DoublepinyinSchemeFactory& instance() {
        static DoublepinyinSchemeFactory factory;
        return factory;
    }

    void registerScheme(const std::string& name, DoublepinyinSchemeCreator creator) {
        m_registry[name] = std::move(creator);
    }

    std::unique_ptr<IDoublepinyinScheme> create(const std::string& name) const {
        auto it = m_registry.find(name);
        if (it != m_registry.end()) {
            return it->second();
        }
        LOG_ERROR("Doublepinyin scheme not found: " + name + ", total registered: " + std::to_string(m_registry.size()));
        return nullptr;
    }

    std::vector<coral::phonetics::TextSlice> listAvailableSchemes() const {
        std::vector<coral::phonetics::TextSlice> names;
        for (const auto& pair : m_registry) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    std::unordered_map<std::string, DoublepinyinSchemeCreator> m_registry;
};

#endif // DOUBLEPINYINCONVERTER_H
