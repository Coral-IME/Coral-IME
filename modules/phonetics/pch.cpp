/*
* @author: huangzhishou
* @date: 2021-8-19 10:27:54
*/
#include "pch.h"

#if defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sstream>

int memcpy_s(void *dest, size_t dest_size, const void *src, size_t count) {
    if (dest == nullptr || src == nullptr) {
        return EINVAL;
    }
    if (count > dest_size) {
        return ERANGE;
    }
    memcpy(dest, src, count);
    return 0;
}

bool UnmapViewOfFile(const char* baseAddress, size_t size) {
    return (munmap((void*)baseAddress, size) == 0);  // Android/Linux 平台
}

bool map_file_to_memory(const std::filesystem::path& filePath, const char*& pFileData, DWORD& file_size) {
    // 1. 打开文件
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd == -1) {
        return false; // 打开文件失败filePath = {const std::__ndk1::__fs::filesystem::path}
    }

    // 2. 获取文件大小
    struct stat fileStat;
    if (fstat(fd, &fileStat) == -1) {
        close(fd);
        return false; // 获取文件信息失败
    }
    file_size = static_cast<size_t>(fileStat.st_size);

    // 3. 映射文件到内存
    const void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return false; // 内存映射失败
    }

    // 4. 关闭文件描述符（映射后不再需要）
    close(fd);

    // 5. 返回映射指针
    pFileData = static_cast<const char*>(mapped);
    return true;
}

std::string float2_string(float value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

#elif defined(_WIN32) || defined(_MSC_VER)

// 文件映射函数，返回文件指针和文件大小
bool map_file_to_memory(const filesystem::path& filePath, const char*& pFileData, DWORD& file_size)
{
    // 打开文件
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    // 获取文件大小
    file_size = GetFileSize(hFile, nullptr);
    if (file_size == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return false;
    }

    // 创建文件映射
    HANDLE hMapping = CreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, file_size, nullptr);
    if (hMapping == nullptr)
    {
        CloseHandle(hFile);
        return false;
    }

    // 映射文件到内存
    pFileData = static_cast<const char*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0));

    if (pFileData == nullptr)
    {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    // 关闭文件句柄，映射不需要文件句柄了
    CloseHandle(hFile);
    CloseHandle(hMapping);
    return true;
}
#endif