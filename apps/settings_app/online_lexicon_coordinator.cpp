#include "pch.h"
#include "online_lexicon_coordinator.h"
#include "core_operations.h"
#include "apphost.h"

namespace coral::lexicon_catalog {
namespace
{
    namespace config
    {
        constexpr auto kRecordName = L"lexicon_record.json";
        constexpr auto kDirectoryName = L"cell";
        constexpr char kXorKey[] = "CoralPinyinCellLex2026";

        constexpr int kGetRequestTimeout = 10000;
    }

    // kcell XOR 混淆/解混淆
    void xor_bytes(char* data, size_t len)
    {
        const size_t key_length = sizeof(config::kXorKey) - 1;
        for (size_t i = 0; i < len; i++)
            data[i] ^= config::kXorKey[i % key_length];
    }

    bool write_cell_file(const std::string& plain_data, const std::filesystem::path& cell_path)
    {
        std::string data = plain_data;
        xor_bytes(data.data(), data.size());
        std::ofstream file(cell_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        return file.good();
    }

    bool read_cell_file(const std::filesystem::path& cell_path, std::string& out_data)
    {
        std::ifstream file(cell_path, std::ios::binary);
        if (!file.is_open())
            return false;
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (data.empty())
            return false;
        xor_bytes(data.data(), data.size());
        out_data = std::move(data);
        return true;
    }

    bool read_all_bytes(const std::filesystem::path& path, std::string& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;
        out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return true;
    }

    std::string temporary_directory_utf8()
    {
        wchar_t temporary_path[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, temporary_path);
        return wstrToUtf8(temporary_path);
    }
}

OnlineLexiconCoordinator& OnlineLexiconCoordinator::instance()
{
    static OnlineLexiconCoordinator instance;
    return instance;
}

OnlineLexiconCoordinator::OnlineLexiconCoordinator(LexiconService service) : service_(std::move(service)) {}
OnlineLexiconCoordinator::~OnlineLexiconCoordinator() { stop(); }
void OnlineLexiconCoordinator::stop()
{
    run_.reset(); // 先使工作线程回调与已排队的应用任务失效。
    downloading_ = false;
    if (catalog_request_) catalog_request_->stop();
    if (location_request_) location_request_->stop();
    if (file_request_) file_request_->stop();
    catalog_request_.reset(); location_request_.reset(); file_request_.reset();
}

void OnlineLexiconCoordinator::check_now()
{
    LOG_INFO("Starting cell lexicon check (manual)");
    fetch_catalog();
}

void OnlineLexiconCoordinator::on_auto_check_timer()
{
    LOG_INFO("Auto-check timer triggered");
    fetch_catalog();
}

void OnlineLexiconCoordinator::fetch_catalog()
{
    if (!service_.available()) return;
    // 上一次流程未结束则跳过
    if (downloading_ || (catalog_request_ && catalog_request_->isRunning()))
        return;

    stop();
    run_ = std::make_shared<int>(0);
    const std::weak_ptr<int> run = run_;
    coralnet::RequestOptions options;
    options.url = service_.catalog_url();
    options.timeoutMs = config::kGetRequestTimeout;

    catalog_request_ = coralnet::asyncRequest(coralnet::HttpMethod::Get, options,
        [this, run](const std::string& body, coralnet::NetResult result) {
            if (run.expired()) return;
            bool success = result.success;
            std::string response = success ? body : std::string();
            appPostTask([this, run, response, success]() {
                if (!run.expired()) on_catalog_response(response, success);
            });
        });
}

// 响应由持有运行令牌检查后的应用线程处理。
void OnlineLexiconCoordinator::on_catalog_response(const std::string& response, bool success)
{
    if (!service_.available() || !run_) return;
    if (!success)
    {
        LOG_ERROR("Failed to fetch from server");
        return;
    }
    LOG_INFO("Server response received");
    std::vector<RemoteLexicon> server_lexicons;
    if (!parse_catalog(response, server_lexicons))
    {
        LOG_ERROR("JSON parse failed");
        return;
    }
    if (server_lexicons.empty())
    {
        LOG_INFO("No lexicons found in server response");
        return;
    }
    LOG_INFO("Retrieved " + std::to_string(server_lexicons.size()) + " lexicons from server");
    remote_lexicons_ = server_lexicons;
    reconcile_catalog(server_lexicons);
}

bool OnlineLexiconCoordinator::parse_catalog(const std::string& response, std::vector<RemoteLexicon>& out_lexicons)
{
    out_lexicons.clear();
    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject())
    {
        LOG_ERROR("Failed to parse JSON response");
        return false;
    }
    if (!doc.HasMember("data") || !doc["data"].IsObject())
    {
        LOG_ERROR("Invalid JSON structure, no 'data' object");
        return false;
    }
    const rapidjson::Value& data_wrapper = doc["data"];
    if (!data_wrapper.HasMember("data") || !data_wrapper["data"].IsArray())
    {
        LOG_ERROR("Invalid JSON structure, no 'data.data' array");
        return false;
    }
    for (const auto& value : data_wrapper["data"].GetArray())
    {
        if (!value.IsObject())
            continue;
        const rapidjson::Value& lexicon_object = value;
        RemoteLexicon info;
        if (lexicon_object.HasMember("id") && lexicon_object["id"].IsInt())
            info.id = lexicon_object["id"].GetInt();
        if (lexicon_object.HasMember("title") && lexicon_object["title"].IsString())
            info.title = lexicon_object["title"].GetString();
        if (lexicon_object.HasMember("description") && lexicon_object["description"].IsString())
            info.description = lexicon_object["description"].GetString();
        if (lexicon_object.HasMember("versionRange") && lexicon_object["versionRange"].IsString())
            info.version_range = lexicon_object["versionRange"].GetString();
        if (lexicon_object.HasMember("fileSize") && lexicon_object["fileSize"].IsInt64())
            info.file_size = lexicon_object["fileSize"].GetInt64();
        else if (lexicon_object.HasMember("fileSize") && lexicon_object["fileSize"].IsInt())
            info.file_size = lexicon_object["fileSize"].GetInt();
        if (lexicon_object.HasMember("filePath") && lexicon_object["filePath"].IsString())
            info.file_path = lexicon_object["filePath"].GetString();
        if (lexicon_object.HasMember("fileSha256") && lexicon_object["fileSha256"].IsString())
            info.file_sha256 = lexicon_object["fileSha256"].GetString();

        if (info.id > 0 && !info.title.empty() && !info.file_sha256.empty())
        {
            out_lexicons.push_back(info);
            LOG_INFO("Found server lexicon: id=" + std::to_string(info.id) + ", title=" + info.title);
        }
    }
    return true;
}

void OnlineLexiconCoordinator::reconcile_catalog(const std::vector<RemoteLexicon>& server_lexicons)
{
    LOG_INFO("Reconciling with server based on hash comparison");

    std::unordered_map<int, CatalogRecord> local_record;
    const bool record_ok = load_record(local_record);

    std::vector<RemoteLexicon> to_download;
    bool has_deleted = false;

    if (!record_ok)
    {
        LOG_ERROR("Record file corrupted or missing, downloading all server lexicons");
        to_download = server_lexicons;
    }
    else
    {
        std::unordered_set<int> server_ids;
        for (const auto& lex : server_lexicons)
        {
            server_ids.insert(lex.id);
            auto it = local_record.find(lex.id);
            if (it == local_record.end())
            {
                LOG_INFO("New lexicon from server: id=" + std::to_string(lex.id) + ", title=" + lex.title);
                to_download.push_back(lex);
            }
            else if (it->second.file_sha256 != lex.file_sha256)
            {
                LOG_INFO("Hash mismatch for id=" + std::to_string(lex.id) + ", title=" + lex.title + ", will update");
                to_download.push_back(lex);
            }
        }

        // 删除 record 中有但 server 中没有的词库文件
        const auto dir = catalog_directory();
        for (const auto& [id, entry] : local_record)
        {
            if (!server_ids.count(id))
            {
                std::error_code ec;
                std::filesystem::path file_path = dir / (std::to_wstring(id) + L".kcell");
                if (std::filesystem::exists(file_path, ec))
                {
                    std::filesystem::remove(file_path, ec);
                    LOG_INFO("Deleted obsolete lexicon file: " + std::to_string(id) + ".kcell");
                }
                has_deleted = true;
            }
        }
    }

    // 检查 combined 二进制文件是否存在
    const auto cell_directory = hlp::getLocalLowCoralPinyinPath() / config::kDirectoryName;
    std::error_code ec;
    const bool need_rebuild_binary = !std::filesystem::exists(cell_directory / "combinedLexicon.trie", ec)
        || !std::filesystem::exists(cell_directory / "combinedLexicon.data", ec);
    if (need_rebuild_binary)
        LOG_INFO("Combined lexicon binary files missing, need rebuild");

    if (!to_download.empty())
    {
        LOG_INFO("Need to download " + std::to_string(to_download.size()) + " lexicons based on hash comparison");
        pending_lexicons_ = to_download;
        download_index_ = 0;
        successful_downloads_ = 0;
        downloading_ = true;
        start_next_download();
    }
    else if (has_deleted || need_rebuild_binary)
    {
        LOG_INFO("No downloads needed, but need rebuild");
        verify_and_rebuild();
    }
    else
    {
        LOG_INFO("No lexicon updates needed after reconciliation");
    }
}

void OnlineLexiconCoordinator::start_next_download()
{
    if (download_index_ >= pending_lexicons_.size())
        return;
    const RemoteLexicon lex = pending_lexicons_[download_index_++];
    LOG_INFO("Queued: id=" + std::to_string(lex.id) + ", title=" + lex.title);
    fetch_location(lex);
}

void OnlineLexiconCoordinator::fetch_location(const RemoteLexicon& lexicon)
{
    if (!service_.available() || !run_) return;
    const auto url = service_.location_url(static_cast<std::uint32_t>(lexicon.id));
    if (url.empty()) { finish_download(); return; }
    const std::weak_ptr<int> run = run_;
    LOG_INFO("Fetching download URL for id=" + std::to_string(lexicon.id) + ", title=" + lexicon.title);

    coralnet::RequestOptions options;
    options.url = url;
    options.timeoutMs = config::kGetRequestTimeout;

    location_request_ = coralnet::asyncRequest(coralnet::HttpMethod::Get, options,
        [this, run, lexicon](const std::string& body, coralnet::NetResult result) {
            if (run.expired()) return;
            bool success = result.success;
            std::string response = success ? body : std::string();
            appPostTask([this, run, lexicon, response, success]() {
                if (!run.expired()) on_location_response(lexicon, response, success);
            });
        });
}

void OnlineLexiconCoordinator::on_location_response(const RemoteLexicon& lexicon, const std::string& response, bool success)
{
    if (!service_.available() || !run_) return;
    if (!success)
    {
        LOG_ERROR("Failed to get download URL for id=" + std::to_string(lexicon.id));
        finish_download();
        return;
    }

    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject()
        || !doc.HasMember("data") || !doc["data"].IsObject())
    {
        LOG_ERROR("Failed to parse download URL response for id=" + std::to_string(lexicon.id));
        finish_download();
        return;
    }
    const rapidjson::Value& data_object = doc["data"];
    std::string download_url;
    if (data_object.HasMember("url") && data_object["url"].IsString())
        download_url = data_object["url"].GetString();
    if (download_url.empty())
    {
        LOG_ERROR("Empty download URL for id=" + std::to_string(lexicon.id));
        finish_download();
        return;
    }

    // 下载 CSV 到临时文件，完成后校验并加密为 kcell
    const auto dir = catalog_directory();
    std::wstring cell_path = (dir / (std::to_wstring(lexicon.id) + L".kcell")).wstring();
    std::wstring csv_path = cell_path + L".tmp";

    const std::weak_ptr<int> run = run_;
    coralnet::DownloadCallbacks callbacks;
    callbacks.onFinish = [this, run, lexicon, cell_path, csv_path](coralnet::NetResult result) {
        if (run.expired()) return;
        bool ok = result.success;
        appPostTask([this, run, lexicon, cell_path, csv_path, ok]() {
            if (!run.expired()) on_file_received(lexicon, cell_path, csv_path, ok);
        });
    };
    file_request_ = coralnet::asyncDownload(download_url, wstrToUtf8(csv_path), std::move(callbacks), 0);
}

void OnlineLexiconCoordinator::on_file_received(const RemoteLexicon& lexicon, const std::wstring& cell_path,
    const std::wstring& csv_path, bool success)
{
    std::error_code ec;
    if (success)
    {
        const std::string hash = file_digest(csv_path);
        if (hash != lexicon.file_sha256)
        {
            LOG_ERROR("Hash mismatch after download");
            std::filesystem::remove(csv_path, ec);
        }
        else
        {
            std::string csvData;
            if (read_all_bytes(csv_path, csvData) && write_cell_file(csvData, cell_path))
            {
                LOG_INFO("Saved to kcell: " + wstrToUtf8(cell_path));
                successful_downloads_++;
            }
            else
            {
                LOG_ERROR("Failed to save kcell: " + wstrToUtf8(cell_path));
            }
            std::filesystem::remove(csv_path, ec);
        }
    }
    else
    {
        LOG_ERROR("Download failed for id=" + std::to_string(lexicon.id));
    }
    finish_download();
}

void OnlineLexiconCoordinator::finish_download()
{
    if (download_index_ < pending_lexicons_.size())
    {
        start_next_download();
        return;
    }
    if (downloading_)
    {
        downloading_ = false;
        const bool all_ok = (successful_downloads_ == static_cast<int>(pending_lexicons_.size()));
        LOG_INFO(all_ok ? "All downloads completed successfully" : "Some downloads failed");
        if (all_ok)
            verify_and_rebuild();
        else
            LOG_ERROR("Download failed, skipping rebuild");
    }
}

std::filesystem::path OnlineLexiconCoordinator::catalog_directory() const
{
    const auto cell_directory = hlp::getLocalLowCoralPinyinPath() / config::kDirectoryName;
    std::error_code ec;
    if (!std::filesystem::exists(cell_directory, ec))
    {
        std::filesystem::create_directories(cell_directory, ec);
        LOG_INFO("Created directory: " + cell_directory.string());
    }
    return cell_directory;
}

std::filesystem::path OnlineLexiconCoordinator::record_path() const
{
    return catalog_directory() / config::kRecordName;
}

bool OnlineLexiconCoordinator::load_record(std::unordered_map<int, CatalogRecord>& records)
{
    records.clear();
    std::string data;
    if (!read_all_bytes(record_path(), data))
        return true; // 没有记录文件视为可解析但为空

    rapidjson::Document doc;
    doc.Parse(data.c_str());
    if (doc.HasParseError() || !doc.IsObject()
        || !doc.HasMember("records") || !doc["records"].IsArray())
        return false;
    for (const auto& record : doc["records"].GetArray())
    {
        if (!record.IsObject())
            continue;
        const int id = (record.HasMember("id") && record["id"].IsInt()) ? record["id"].GetInt() : -1;
        std::string sha;
        if (record.HasMember("fileSha256") && record["fileSha256"].IsString())
            sha = record["fileSha256"].GetString();
        if (id > 0 && !sha.empty())
            records[id] = { sha };
    }
    return true;
}

bool OnlineLexiconCoordinator::save_record(const std::unordered_map<int, CatalogRecord>& records)
{
    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value arr(rapidjson::kArrayType);
    for (const auto& [id, entry] : records)
    {
        rapidjson::Value record(rapidjson::kObjectType);
        record.AddMember("id", id, doc.GetAllocator());
        record.AddMember("fileSha256", rapidjson::Value(entry.file_sha256.c_str(), doc.GetAllocator()), doc.GetAllocator());
        arr.PushBack(record, doc.GetAllocator());
    }
    doc.AddMember("records", arr, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    auto record_file = record_path();
    const auto temporary_path = record_file;
    std::ofstream file(record_file.replace_extension(L".json.tmp"), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    file.close();

    std::error_code ec;
    std::filesystem::remove(record_path(), ec);
    std::filesystem::rename(record_file, record_path(), ec);
    if (ec)
    {
        LOG_ERROR("Failed to rename temp record file");
        std::filesystem::remove(record_file, ec);
        return false;
    }
    LOG_INFO("Record saved (" + std::to_string(records.size()) + " entries)");
    return true;
}

std::string OnlineLexiconCoordinator::file_digest(const std::filesystem::path& file_path)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
        return {};
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return data_digest(data);
}

std::string OnlineLexiconCoordinator::data_digest(const std::string& data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
        || !BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
    {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    std::string result;
    if (BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
        static_cast<ULONG>(data.size()), 0)))
    {
        BYTE digest[32] = { 0 };
        if (BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0)))
        {
            static const char* hex = "0123456789abcdef";
            for (BYTE b : digest)
            {
                result += hex[b >> 4];
                result += hex[b & 0x0F];
            }
        }
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

void OnlineLexiconCoordinator::verify_local_files()
{
    LOG_INFO("Verifying local files against server list");
    if (remote_lexicons_.empty())
    {
        LOG_INFO("Server lexicon list is empty, skip verification");
        return;
    }

    std::error_code ec;
    const auto dir = catalog_directory();
    std::unordered_set<int> server_ids;
    for (const auto& lex : remote_lexicons_)
    {
        server_ids.insert(lex.id);
        const std::filesystem::path file_path = dir / (std::to_wstring(lex.id) + L".kcell");
        if (!std::filesystem::exists(file_path, ec))
        {
            LOG_ERROR("Missing file: " + std::to_string(lex.id) + ".kcell");
            continue;
        }

        std::string decrypted;
        if (!read_cell_file(file_path, decrypted))
        {
            LOG_ERROR("Failed to decrypt kcell: " + std::to_string(lex.id) + ".kcell");
            std::filesystem::remove(file_path, ec);
            continue;
        }

        const std::string actual_hash = data_digest(decrypted);
        if (actual_hash != lex.file_sha256)
        {
            LOG_ERROR("Hash mismatch for id=" + std::to_string(lex.id));
            std::filesystem::remove(file_path, ec);
            LOG_INFO("Deleted corrupted file: " + std::to_string(lex.id) + ".kcell");
        }
    }

    // 删除不在服务器列表中的多余文件
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().extension() != L".kcell")
            continue;
        const std::wstring base_name = entry.path().stem().wstring();
        const int file_id = _wtoi(base_name.c_str());
        if (file_id > 0 && !server_ids.count(file_id))
        {
            std::filesystem::remove(entry.path(), ec);
            LOG_INFO("Deleted extra local lexicon(not on server): " + wstrToUtf8(base_name));
        }
    }
}

void OnlineLexiconCoordinator::verify_and_rebuild()
{
    LOG_INFO("Starting pre-build verification");
    verify_local_files();
    LOG_INFO("All files verified, proceeding to build");
    rebuild_catalog();
}

void OnlineLexiconCoordinator::save_built_record()
{
    LOG_INFO("Saving record after successful build");

    std::unordered_map<int, CatalogRecord> new_record;
    const auto dir = catalog_directory();
    for (const auto& lex : remote_lexicons_)
    {
        const std::filesystem::path file_path = dir / (std::to_wstring(lex.id) + L".kcell");
        if (!std::filesystem::exists(file_path))
        {
            LOG_ERROR("File missing after build: " + std::to_string(lex.id) + ".kcell");
            continue;
        }
        std::string decrypted;
        if (read_cell_file(file_path, decrypted))
        {
            new_record[lex.id] = { data_digest(decrypted) };
        }
        else
        {
            LOG_ERROR("Failed to decrypt kcell for id=" + std::to_string(lex.id));
        }
    }
    save_record(new_record);
}

void OnlineLexiconCoordinator::rebuild_catalog()
{
    LOG_INFO("Triggering lexicon rebuild");
    const auto cell_directory = hlp::getLocalLowCoralPinyinPath() / config::kDirectoryName;
    std::error_code ec;
    if (!std::filesystem::exists(cell_directory, ec))
    {
        LOG_ERROR("Cell directory does not exist");
        return;
    }

    const auto dir = catalog_directory();
    std::vector<std::filesystem::path> csv_files;

    // 解密所有 kcell 为临时 csv
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec) || entry.path().extension() != L".kcell")
            continue;
        const std::filesystem::path cell_path = entry.path();
        std::filesystem::path csv_path = cell_path;
        std::string decrypted;
        if (read_cell_file(cell_path, decrypted))
        {
            std::ofstream csv_file(csv_path.replace_extension(L".csv"), std::ios::binary | std::ios::trunc);
            if (csv_file.is_open())
            {
                csv_file.write(decrypted.data(), static_cast<std::streamsize>(decrypted.size()));
                csv_files.push_back(csv_path);
                LOG_INFO("Decrypted kcell to temp csv: " + csv_path.string());
            }
        }
    }

    const std::string directory_text = cell_directory.string();
    const bool result = coral::runtime::compile_lexicon_directory(directory_text.c_str());

    for (const auto& temporary_csv : csv_files)
    {
        std::filesystem::remove(temporary_csv, ec);
        LOG_INFO("Removed temp csv: " + temporary_csv.string());
    }

    const std::filesystem::path combined_pending = dir / L"combined.kcell.tmp";
    if (std::filesystem::exists(combined_pending, ec))
    {
        std::filesystem::remove(combined_pending, ec);
        LOG_INFO("Removed combined.kcell.tmp");
    }

    if (result)
    {
        LOG_INFO("Lexicon rebuild completed successfully");
        save_built_record();
    }
    else
    {
        LOG_ERROR("Failed to rebuild lexicon");
    }
}

} // 命名空间 coral::lexicon_catalog 结束
