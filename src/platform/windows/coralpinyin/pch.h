#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_PCH_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_PCH_H_INCLUDED

#ifndef STRICT
#define STRICT
#endif

#define _ATL_APARTMENT_THREADED

#define _ATL_NO_AUTOMATIC_NAMESPACE

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// 某些 CString 构造函数将是显式的

#define ATL_NO_ASSERT_ON_DESTROY_NONEXISTENT_WINDOW

#include <Windows.h>
#include <Windowsx.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <filesystem>
#include <fstream>

namespace Gdiplus
{
    using std::min;
    using std::max;
};

#include <gdiplus.h>
#pragma comment( lib, "gdiplus.lib" )

#include <atlbase.h>
#include <atlstr.h>
#include <atlpath.h>

#define _WTL_NO_CSTRING

#include <atlwin.h>
#include "wtl10/atlapp.h"
#include "wtl10/atlmisc.h"
#include "wtl10/atlcrack.h"
#include "wtl10/atlframe.h"
#include "wtl10/atlctrls.h"
#include "wtl10/atldlgs.h"
#include "wtl10/atlddx.h"
#include "wtl10/atlres.h"
#include <atlcom.h>
#include <atlctl.h>
#include <regex>

using namespace ATL;

#include "resource.h"

#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT ATLASSERT

#define DELETE_P(p)\
do\
{\
	if(p)\
	{\
		delete p;\
		p = nullptr;\
	}\
}while(0)

#define CHECK_ERROR(exp) \
do { \
	if (!(exp)) \
	{ \
		goto Exit0; \
	} \
} while (0)

#define CHECK_ERROR_MSG(exp, process_name, msg) \
do { \
	if (!(exp)) \
	{ \
		hlp::outputDebugView(msg); \
		hlp::logUILogic(hlp::LOGLEVEL_ERROR, process_name, msg); \
		goto Exit0; \
	} \
} while (0)

// 获取当前 Session 的管道名
inline CString getEngineServerName() {
	static const CString name = []() -> CString {
		DWORD session_id = 0;
		if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
			CString n;
			n.Format(_T("_#_coral_pinyin_engineserver_%u_#_"), session_id);
			return n;
		}
		return _T("_#_coral_pinyin_engineserver_#_");
	}();
	return name;
}

#define DEV_LATER_API 1

#if defined _M_IX86
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

/*Author:qipan Log调试宏*/
#define DEBUG_LOGGER//启用调试

#include "preference_registry.h"
#include "call_timing.h"
#include "text_file_helpers.hpp"
#include "trace_log.h"
#include "coralnet.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include "utf8conv.h"

#endif // PCH_H
