#pragma once
#include "pch.h"
#include "upgrademode.h"
#include "updatewindow.h"
#include "coral/updates/update_notice.h"
#include "update_http.h"
#include <optional>

namespace coral::updates {
class UpdateCoordinator {
public:
    static UpdateCoordinator& instance();
    void check_update(bool manual);
    void on_auto_check_timer();
    void stop(); // 在应用窗口/日志关闭之前取消并汇合。
private:
    UpdateCoordinator() = default;
    ~UpdateCoordinator();
    UpdateCoordinator(const UpdateCoordinator&) = delete;
    UpdateCoordinator& operator=(const UpdateCoordinator&) = delete;
    void start_check();
    void on_release_response(const std::string& body, bool success);
    bool show_update_window();
    void open_release_page();
    void notify_manual_result(const wchar_t* message);
    std::unique_ptr<MetadataRequest> check_request_;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    std::unique_ptr<CoralUpdateWindow> update_window_;
    UpgradeMode upgrade_mode_ = UpgradeMode::Manual;
    bool checking_ = false;
    std::optional<UpdateNotice> release_;
};
}
