#include "pch.h"
#include "update_coordinator.h"
#include "apphost.h"
#include "package_acceptance.h"
#include "coral/updates/update_service.h"
#include "utf8conv.h"
#include <chrono>

namespace coral::updates {
namespace {
std::int64_t now_utc() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}
UpdateCoordinator::~UpdateCoordinator() { stop(); }
UpdateCoordinator& UpdateCoordinator::instance() {
    static UpdateCoordinator coordinator;
    return coordinator;
}
void UpdateCoordinator::stop() {
    lifetime_.reset();
    check_request_.reset();
    update_window_.reset();
    checking_ = false;
    release_.reset();
}
void UpdateCoordinator::on_auto_check_timer() {
    if (checking_ && check_request_ && check_request_->finished() && check_request_->callback_failed())
        checking_ = false;
    if (!lifetime_ || !sConfig->getIsAutoCheck() || checking_ ||
        (update_window_ && update_window_->is_alive()) || !update_service_available() ||
        !auto_check_due(now_utc(), sConfig->get_update_last_check_utc())) return;
    upgrade_mode_ = UpgradeMode::Auto;
    start_check();
}
void UpdateCoordinator::check_update(bool manual) {
    if (!manual) { on_auto_check_timer(); return; }
    if (!lifetime_) return;
    // 把进行中的自动检查提升为可见的手动结果。
    upgrade_mode_ = UpgradeMode::Manual;
    start_check();
}
void UpdateCoordinator::start_check() {
    if (!update_service_available()) {
        if (upgrade_mode_ == UpgradeMode::Manual) notify_manual_result(L"此构建尚未配置更新检查地址");
        return;
    }
    if (update_window_ && update_window_->is_alive()) {
        if (upgrade_mode_ == UpgradeMode::Manual) update_window_->show(true);
        return;
    }
    if (checking_) {
        if (!check_request_ || !check_request_->finished() || !check_request_->callback_failed()) return;
        checking_ = false;
    }
    checking_ = true;
    const std::weak_ptr<int> lifetime = lifetime_;
    try {
        check_request_ = MetadataRequest::start(release_check_url(), [this, lifetime](HttpReply reply) {
            if (lifetime.expired()) return;
            appPostTask([this, lifetime, reply = std::move(reply)] {
                if (!lifetime.expired()) on_release_response(reply.body, reply.error == DownloadError::kNone);
            });
        });
        // 持久化尝试（包括失败），避免重启触发请求风暴。
        sConfig->set_update_last_check_utc(now_utc());
    } catch (...) {
        checking_ = false;
        if (upgrade_mode_ == UpgradeMode::Manual) notify_manual_result(L"无法启动更新检查，请稍后重试");
    }
}
void UpdateCoordinator::on_release_response(const std::string& body, bool success) {
    checking_ = false;
    if (!success) {
        if (upgrade_mode_ == UpgradeMode::Manual) notify_manual_result(L"暂时无法检查更新，请稍后重试");
        return;
    }
    UpdateNotice notice;
    if (!parse_update_notice(body, notice)) {
        if (upgrade_mode_ == UpgradeMode::Manual) notify_manual_result(L"更新信息无效，请稍后重试");
        return;
    }
    const auto installed = read_running_package_version();
    if (!installed) {
        if (upgrade_mode_ == UpgradeMode::Manual) notify_manual_result(L"读取本地版本失败");
        return;
    }
    if (!is_newer_version(notice.version, *installed)) {
        if (upgrade_mode_ == UpgradeMode::Manual) notify_manual_result(L"当前已是最新版本");
        return;
    }
    const auto version = format_release_version(notice.version);
    // 请求进行期间尊重开关被禁用的状态。
    if (upgrade_mode_ == UpgradeMode::Auto && (!sConfig->getIsAutoCheck() ||
        sConfig->get_update_last_notified_version() == version)) return;
    release_ = std::move(notice);
    if (show_update_window()) sConfig->set_update_last_notified_version(version);
}
void UpdateCoordinator::notify_manual_result(const wchar_t* message) {
    MessageBoxW(GetActiveWindow(), message, L"CORAL拼音", MB_OK | MB_ICONINFORMATION);
}
bool UpdateCoordinator::show_update_window() {
    if (!release_) return false;
    update_window_ = std::make_unique<CoralUpdateWindow>();
    UpdateInfoLite info;
    info.serverVersion = format_release_version(release_->version);
    info.updateDesc = release_->description;
    if (!update_window_->create(info, upgrade_mode_)) {
        update_window_.reset();
        return false;
    }
    const std::weak_ptr<int> lifetime = lifetime_;
    update_window_->onOpenRelease = [this, lifetime] {
        if (!lifetime.expired()) open_release_page();
    };
    update_window_->onClosed = [this, lifetime] {
        if (lifetime.expired()) return;
        // 关闭在窗口过程内运行。把删除推迟到其返回之后。
        appPostTask([this, lifetime] {
            if (!lifetime.expired() && update_window_ && !update_window_->is_alive()) update_window_.reset();
        });
    };
    update_window_->show();
    return true;
}
void UpdateCoordinator::open_release_page() {
    if (!release_ || !is_release_page_url(release_->release_url)) return;
    const auto url = utf8ToWstr(release_->release_url);
    const auto result = ShellExecuteW(update_window_->hwnd(), L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        update_window_->showFailure(L"无法打开浏览器，请稍后重试。");
}
}
