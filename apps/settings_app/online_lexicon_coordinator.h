#pragma once
#ifndef CORAL_APPS_SETTINGS_APP_ONLINE_LEXICON_COORDINATOR_H_INCLUDED
#define CORAL_APPS_SETTINGS_APP_ONLINE_LEXICON_COORDINATOR_H_INCLUDED

#include "coral/lexicon_catalog/service_endpoint.h"
#include "coralnet.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 保留既有的目录/文件算法；服务与请求生命周期是显式的。
namespace coral::lexicon_catalog {
class OnlineLexiconCoordinator
{
public:
    static OnlineLexiconCoordinator& instance();

    void stop();
    void check_now();

    void on_auto_check_timer(); // 可调用适配器；不注册生产定时器。

    explicit OnlineLexiconCoordinator(LexiconService service = configured_lexicon_service());
    ~OnlineLexiconCoordinator();
    OnlineLexiconCoordinator(const OnlineLexiconCoordinator&) = delete;
    OnlineLexiconCoordinator& operator=(const OnlineLexiconCoordinator&) = delete;

private:
    struct RemoteLexicon
    {
        int id = -1;
        std::string title;
        std::string description;
        std::string version_range;
        int64_t file_size = 0;
        std::string file_path;    // 服务器相对路径，需二次请求获取真实下载URL
        std::string file_sha256;  // 明文 CSV 的 SHA256
    };
    struct CatalogRecord
    {
        std::string file_sha256;
    };

    void fetch_catalog();
    void on_catalog_response(const std::string& response, bool success);
    bool parse_catalog(const std::string& response, std::vector<RemoteLexicon>& out_lexicons);
    void reconcile_catalog(const std::vector<RemoteLexicon>& server_lexicons);

    void start_next_download();
    void fetch_location(const RemoteLexicon& lexicon);
    void on_location_response(const RemoteLexicon& lexicon, const std::string& response, bool success);
    void on_file_received(const RemoteLexicon& lexicon, const std::wstring& cell_path,
        const std::wstring& csv_path, bool success);
    void finish_download();

    std::filesystem::path catalog_directory() const;
    std::filesystem::path record_path() const;
    bool load_record(std::unordered_map<int, CatalogRecord>& records);
    bool save_record(const std::unordered_map<int, CatalogRecord>& records);
    std::string file_digest(const std::filesystem::path& file_path);
    std::string data_digest(const std::string& data);

    void verify_and_rebuild();
    void verify_local_files();
    void save_built_record();
    void rebuild_catalog();

private:
    std::unique_ptr<coralnet::HttpRequest> catalog_request_;
    std::unique_ptr<coralnet::HttpRequest> location_request_;
    std::unique_ptr<coralnet::HttpRequest> file_request_;

    std::vector<RemoteLexicon> remote_lexicons_;
    std::vector<RemoteLexicon> pending_lexicons_;
    size_t download_index_ = 0;
    int successful_downloads_ = 0;
    bool downloading_ = false;
    LexiconService service_;
    std::shared_ptr<int> run_;
};

} // 命名空间 coral::lexicon_catalog 结束

#endif // CORAL_APPS_SETTINGS_APP_ONLINE_LEXICON_COORDINATOR_H_INCLUDED
