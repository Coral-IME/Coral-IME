// 由 tools/checks/product_identity.py 生成；请编辑 resources/product_identity.json。
#pragma once

namespace coral::product {
inline constexpr char kSettingsExe[] = "coralpinyin.exe";
inline constexpr wchar_t kSettingsExeWide[] = L"coralpinyin.exe";
inline constexpr char kUpdateExe[] = "coralpinyinupdate.exe";
inline constexpr wchar_t kUpdateExeWide[] = L"coralpinyinupdate.exe";
inline constexpr char kDiagnosticExe[] = "coralcrashdump.exe";
inline constexpr wchar_t kDiagnosticExeWide[] = L"coralcrashdump.exe";
inline constexpr char kServiceExe[] = "coralpinyinsvc.exe";
inline constexpr wchar_t kServiceExeWide[] = L"coralpinyinsvc.exe";
inline constexpr char kSessionHostExe[] = "coral_engine_host.exe";
inline constexpr wchar_t kSessionHostExeWide[] = L"coral_engine_host.exe";
inline constexpr char kRendererExe[] = "coral_renderer.exe";
inline constexpr wchar_t kRendererExeWide[] = L"coral_renderer.exe";
inline constexpr char kTsfX64Dll[] = "coraltsf64.dll";
inline constexpr wchar_t kTsfX64DllWide[] = L"coraltsf64.dll";
inline constexpr char kTsfX86Dll[] = "coraltsf32.dll";
inline constexpr wchar_t kTsfX86DllWide[] = L"coraltsf32.dll";
inline constexpr char kServiceName[] = "coralpinyinsvc";
inline constexpr wchar_t kServiceNameWide[] = L"coralpinyinsvc";
inline constexpr char kServiceBridge[] = "_#_coralpinyin_service_bridge_#_";
inline constexpr wchar_t kServiceBridgeWide[] = L"_#_coralpinyin_service_bridge_#_";
inline constexpr char kAppRegistry[] = "SOFTWARE\\coral\\HermesIME\\engine\\app";
inline constexpr wchar_t kAppRegistryWide[] = L"SOFTWARE\\coral\\HermesIME\\engine\\app";
inline constexpr char kSettingsRegistry[] = "SOFTWARE\\coral\\HermesIME\\settingsconfig";
inline constexpr wchar_t kSettingsRegistryWide[] = L"SOFTWARE\\coral\\HermesIME\\settingsconfig";
inline constexpr char kLauncherRegistry[] = "SOFTWARE\\coralpinyin";
inline constexpr wchar_t kLauncherRegistryWide[] = L"SOFTWARE\\coralpinyin";
inline constexpr char kInstallPathValue[] = "InstallPath";
inline constexpr wchar_t kInstallPathValueWide[] = L"InstallPath";
inline constexpr char kInstallationMutex[] = "CoralPinyinInstalling";
inline constexpr wchar_t kInstallationMutexWide[] = L"CoralPinyinInstalling";
inline constexpr char kSessionPipePrefix[] = "CoralPinyin.Session.v4.";
inline constexpr wchar_t kSessionPipePrefixWide[] = L"CoralPinyin.Session.v4.";
inline constexpr char kSessionMutexPrefix[] = "CoralPinyin.SessionHost.v4.";
inline constexpr wchar_t kSessionMutexPrefixWide[] = L"CoralPinyin.SessionHost.v4.";
inline constexpr char kSessionStopPrefix[] = "CoralPinyin.SessionHost.Stop.v4.";
inline constexpr wchar_t kSessionStopPrefixWide[] = L"CoralPinyin.SessionHost.Stop.v4.";
inline constexpr char kRendererPipePrefix[] = "\\\\.\\pipe\\CoralPinyin.Renderer.v4.";
inline constexpr wchar_t kRendererPipePrefixWide[] = L"\\\\.\\pipe\\CoralPinyin.Renderer.v4.";
inline constexpr char kRendererMutexPrefix[] = "Local\\CoralPinyin.Renderer.v4.";
inline constexpr wchar_t kRendererMutexPrefixWide[] = L"Local\\CoralPinyin.Renderer.v4.";
} // 命名空间 coral::product 结束
