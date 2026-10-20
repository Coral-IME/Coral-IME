#include "zip_archive.h"
#include <windows.h>
#include <atomic>
#include <process.h>
#include <set>
#include "miniz.h"

namespace coral::archive {
bool writeZip(const std::filesystem::path& destination,
              const std::vector<Entry>& entries, std::string& error) {
    error.clear();
    if (entries.empty()) { error = "No files to archive"; return false; }
    std::set<std::string> names;
    for (const auto& entry : entries) {
        const auto name = std::filesystem::u8path(entry.name);
        if (entry.name.empty() || entry.name.find('\0') != std::string::npos ||
            entry.name.find(':') != std::string::npos || name.is_absolute() ||
            !names.insert(entry.name).second) {
            error = "Invalid or duplicate archive entry name"; return false;
        }
        for (const auto& part : name)
            if (part == "..") { error = "Archive path escapes its root"; return false; }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(entry.source, ec)) {
            error = "Archive input is missing: " + entry.source.u8string(); return false;
        }
    }
    static std::atomic<unsigned> sequence{0};
    auto temporary = destination;
    temporary += ".partial." + std::to_string(_getpid()) + "." + std::to_string(++sequence);
    struct Writer {
        mz_zip_archive zip{};
        bool initialized = false;
        ~Writer() { if (initialized) mz_zip_writer_end(&zip); }
    } writer;
    std::error_code ec;
    if (std::filesystem::exists(temporary, ec) || ec) {
        error = "Archive temporary path is already occupied"; return false;
    }
    const auto cleanup = [&] {
        if (writer.initialized) { mz_zip_writer_end(&writer.zip); writer.initialized = false; }
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    };
    if (!mz_zip_writer_init_file(&writer.zip, temporary.u8string().c_str(), 0)) {
        error = "Cannot create archive"; cleanup(); return false;
    }
    writer.initialized = true;
    for (const auto& entry : entries) {
        if (!mz_zip_writer_add_file(&writer.zip, entry.name.c_str(), entry.source.u8string().c_str(),
                                    nullptr, 0, MZ_DEFAULT_COMPRESSION)) {
            error = mz_zip_get_error_string(mz_zip_get_last_error(&writer.zip));
            cleanup(); return false;
        }
    }
    if (!mz_zip_writer_finalize_archive(&writer.zip)) {
        error = "Cannot finalize archive"; cleanup(); return false;
    }
    const bool closed = mz_zip_writer_end(&writer.zip) != 0;
    writer.initialized = false;
    if (!closed) { error = "Cannot close archive"; cleanup(); return false; }
    if (!::MoveFileW(temporary.c_str(), destination.c_str())) {
        error = std::system_category().message(::GetLastError()); cleanup(); return false;
    }
    return true;
}
}
