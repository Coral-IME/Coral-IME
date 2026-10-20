#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TSFAPI_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TSFAPI_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) void setTestTextService(void* pService);
__declspec(dllexport) void clearTestTextService();
__declspec(dllexport) bool setTestInputState(bool isChinese, bool isFullWidth, bool isChinesePunct);
__declspec(dllexport) bool getTestInputState(bool* isChinese, bool* isFullWidth, bool* isChinesePunct);
#ifdef __cplusplus
} // extern "C"
#endif

#endif // TSFAPI_H
