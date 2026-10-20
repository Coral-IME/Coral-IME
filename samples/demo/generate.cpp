#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include "syllable_helpers.h"
#include "core_operations.h"
#include "spelling_repair.h"
#include "character_prior.h"
#include <marisa.h>

namespace fs=std::filesystem;
std::vector<std::string> split(const std::string& line,char sep) {
    std::vector<std::string> parts; std::stringstream in(line); std::string part;
    while(std::getline(in,part,sep)) parts.push_back(part);
    return parts;
}
std::vector<std::vector<std::string>> readRows(const fs::path& file) {
    std::ifstream in(file); if(!in) throw std::runtime_error("Missing demo input: "+file.u8string());
    std::string line; std::getline(in,line); std::vector<std::vector<std::string>> rows;
    while(std::getline(in,line)) { if(!line.empty() && line.back()=='\r') line.pop_back(); if(!line.empty()) rows.push_back(split(line,',')); }
    return rows;
}
template<class T> void put(std::ofstream& out,const T& value) { out.write(reinterpret_cast<const char*>(&value),sizeof(value)); }
void models(const std::set<std::string>& syllables,const fs::path& dir) {
    marisa::Keyset keys; keys.push_back("<s>"); keys.push_back("</s>");
    for(const auto& key:syllables) keys.push_back(key.c_str());
    marisa::Trie trie; trie.build(keys); const auto tmp=dir/"demo-syllables.marisa"; trie.save(tmp.string().c_str());
    std::ifstream input(tmp,std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(input)),{}); input.close();
    const uint32_t n=static_cast<uint32_t>(trie.num_keys());
    const uint32_t n2=n*n,n3=n*n*n;
    coral::storage::SpellingModelHeader header{}; std::memcpy(header.magic,"PINYINLM",8); header.version=1;
    header.trie_offset=sizeof(header); header.trie_size=bytes.size();
    header.unigram_offset=header.trie_offset+header.trie_size; header.unigram_size=n*sizeof(uint64_t);
    header.bigram_offset=header.unigram_offset+header.unigram_size; header.bigram_size=4+n2*sizeof(coral::storage::SpellingPairRecord);
    header.trigram_offset=header.bigram_offset+header.bigram_size; header.trigram_size=4+n3*sizeof(coral::storage::SpellingTripleRecord);
    std::ofstream out(dir/"correction_pinyin_model.dat",std::ios::binary);
    put(out,header);out.write(bytes.data(),bytes.size());
    for(uint32_t i=0;i<n;++i) put(out,uint64_t(n*n));
    put(out,n2);
    for(uint32_t i=0;i<n;++i) for(uint32_t j=0;j<n;++j) put(out,coral::storage::SpellingPairRecord{(i<<16)|j,n});
    put(out,n3);
    for(uint32_t i=0;i<n;++i) for(uint32_t j=0;j<n;++j) for(uint32_t k=0;k<n;++k)
        put(out,coral::storage::SpellingTripleRecord{(uint64_t(i)<<32)|(uint64_t(j)<<16)|k,1});
    if(!out) throw std::runtime_error("Cannot write demonstration correction model"); out.close();
    coral::storage::CharacterModelHeader chars{}; std::memcpy(chars.magic,"CHAR_LM",7); chars.version=1; chars.alphabet_size=28;
    std::ofstream lm(dir/"char_lm.dat",std::ios::binary);put(lm,chars);
    // 统一合成词频；不从正式数据复制任何语言模型参数。
    for(size_t i=0;i<29;++i) put(lm,uint64_t(i ? 784 : 0));
    for(size_t i=0;i<785;++i) put(lm,uint64_t(i ? 28 : 0));
    for(size_t i=0;i<21953;++i) put(lm,uint64_t(i ? 1 : 0));
    if(!lm) throw std::runtime_error("Cannot write demonstration character model");
    fs::remove(tmp);
}
int main(int argc,char** argv) {
    try {
        if(argc!=3) { std::cerr<<"usage: coral_demo_generate <demo-fixtures> <output-runtime>\n"; return 2; }
        const auto fixtures=fs::absolute(argv[1]), root=fs::absolute(argv[2]);
        const auto data=root/"data", general=data/"generallexicon", intermediate=root/"intermediate";
        fs::create_directories(general);fs::create_directories(intermediate);fs::create_directories(data/"opencc");
        const auto rows=readRows(fixtures/"lexicon.csv");if(rows.empty()) throw std::runtime_error("Demo lexicon is empty");
        std::map<std::string,unsigned> wordIds;std::set<std::string> syllables;
        std::ofstream index(intermediate/"index.csv");index<<"index,pinyin,frequency,word,fullPinyin,backprob,in_m2\n\n\n";
        for(unsigned i=0;i<rows.size();++i) {
            const auto& r=rows[i];if(r.size()!=6) throw std::runtime_error("Expected six demo lexicon fields");
            wordIds[r[2]]=i;index<<i;for(const auto& value:r) index<<','<<value;index<<'\n';
            for(const auto& value:split(r[3],'\'')) syllables.insert(value);
        }
        index.close();const auto bigrams=readRows(fixtures/"bigram.csv");
        std::ofstream converted(intermediate/"bigram.csv");converted<<bigrams.size()<<"\nprev_index,next_index,prob\n";
        for(const auto& r:bigrams) { if(r.size()!=3) throw std::runtime_error("Expected three demo bigram fields");converted<<wordIds.at(r[0])<<','<<wordIds.at(r[1])<<','<<r[2]<<'\n'; }
        converted.close();
        const std::array<fs::path,19> paths={intermediate/"index.csv",general/"primary.marisa",general/"primary.dat",
            fixtures/"umode_radical.csv",data/"bushou.marisa",data/"bushou.dat",fixtures/"umode_stroke.csv",data/"stroke.marisa",data/"stroke.dat",
            fixtures/"eng_lexicon.csv",general/"english.marisa",general/"english.dat",fs::path{},intermediate/"bigram.csv",general,
            intermediate/"index.csv",general/"cnprimary.marisa",general/"cnprimary.dat",fs::path{}};
        std::array<std::string,19> args;for(size_t i=0;i<paths.size();++i) args[i]=paths[i].string();
        if(!coral::runtime::compile_resource_files(args[0].c_str(),args[1].c_str(),args[2].c_str(),args[3].c_str(),args[4].c_str(),args[5].c_str(),args[6].c_str(),args[7].c_str(),args[8].c_str(),args[9].c_str(),args[10].c_str(),args[11].c_str(),args[12].c_str(),args[13].c_str(),args[14].c_str(),args[15].c_str(),args[16].c_str(),args[17].c_str(),args[18].c_str()))
            throw std::runtime_error("Existing engine data generator failed");
        models(syllables,general);
        for(const auto& entry:fs::directory_iterator(fixtures/"opencc")) fs::copy_file(entry.path(),data/"opencc"/entry.path().filename(),fs::copy_options::overwrite_existing);
        fs::copy_file(fixtures/"dw.coral",data/"dw.coral",fs::copy_options::overwrite_existing);
        std::cout<<"Self-authored demonstration generated: "<<rows.size()<<" entries\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"Demo generation failed: "<<e.what()<<'\n';return 1; }
}
