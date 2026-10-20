#include "agent_actions.h"
#include "crash_reporter.h"
#include "coral/updates/update_service.h"
int wmain(int argc, wchar_t** argv) {
    if (!coral::updates::update_service_available()) return 2;
    try {
        std::vector<std::wstring_view> arguments;
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        auto channel = coral::updates::InheritedUpdateChannel::open(arguments);
        if (!channel) return 3;
        coral::diagnostics::install_crash_reporter(coral::diagnostics::CrashModule::kUpdater);
        coral::updates::AgentActions actions(*channel);
        const auto result = coral::updates::execute_download(channel->request(), actions);
        return result == coral::updates::DownloadError::kNone ? 0 : 1;
    } catch (...) { return 1; }
}
