#include "zip_archive.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    namespace fs=std::filesystem;
    const fs::path dir=fs::absolute(argv[1]);fs::create_directories(dir);
    const auto text=dir/fs::u8path(u8"日志.txt"),empty=dir/"empty.txt";
    { std::ofstream out(text,std::ios::binary);out<<u8"自制日志\n"<<std::string(100000,'a'); }
    { std::ofstream out(empty,std::ios::binary); }
    std::string error;const auto zip=dir/"result.zip";
    // 只删除本测试已知的上一次输出文件。
    std::error_code ec;fs::remove(zip,ec);
    if(!coral::archive::writeZip(zip,{{u8"日志.txt",text},{"nested/empty.txt",empty}},error)) {
        std::cerr<<error<<'\n';return 1;
    }
    if(coral::archive::writeZip(zip,{{"empty.txt",empty}},error))return 1;
    if(coral::archive::writeZip(dir/"missing.zip",{{"missing",dir/"no-such-file"}},error))return 1;
    if(coral::archive::writeZip(dir/"escape.zip",{{"../escape",empty}},error))return 1;
    if(coral::archive::writeZip(dir/"duplicate.zip",{{"same",empty},{"same",empty}},error))return 1;
    for(const auto& entry:fs::directory_iterator(dir))
        // 使用明确的 UTF-8，避免英文 Windows 将中文文件名转换到本地代码页时抛异常。
        if(entry.path().filename().u8string().find(".partial.")!=std::string::npos)return 1;
    return 0;
}
