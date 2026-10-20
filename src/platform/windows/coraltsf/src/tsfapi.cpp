#include "pch.h"
#include "tsfapi.h"
#include "textservice.h"
#include <atomic>

static std::atomic<CTextService*> g_pTestTextService = nullptr;
extern "C" {

__declspec(dllexport) void setTestTextService(void* pService)
{
	g_pTestTextService = static_cast<CTextService*>(pService);
}

__declspec(dllexport) void clearTestTextService()
{
	g_pTestTextService = nullptr;
}

__declspec(dllexport) bool setTestInputState(bool isChinese, bool isFullWidth, bool isChinesePunct)
{
	CTextService* pService = g_pTestTextService.load();
	if (!pService)
		return false;

	return pService->setTestInputState(isChinese, isFullWidth, isChinesePunct);
}

__declspec(dllexport) bool getTestInputState(bool* isChinese, bool* isFullWidth, bool* isChinesePunct)
{
	if (!isChinese || !isFullWidth || !isChinesePunct)
		return false;

	CTextService* pService = g_pTestTextService.load();
	if (!pService)
		return false;

	return pService->getTestInputState(isChinese, isFullWidth, isChinesePunct);
}

} // extern "C"


