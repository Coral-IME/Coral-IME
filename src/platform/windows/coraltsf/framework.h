#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_FRAMEWORK_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_FRAMEWORK_H_INCLUDED

#ifndef STRICT
#define STRICT
#endif

#define _ATL_APARTMENT_THREADED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// 某些 CString 构造函数将是显式的
#define ATL_NO_ASSERT_ON_DESTROY_NONEXISTENT_WINDOW

#include "targetver.h"
#include "resource.h"
#include <Windows.h>
#include <algorithm>

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

#include <atlcom.h>
#include <atlctl.h>

#include <msctf.h>
#include <ole2.h>
#include <olectl.h>
#include <atlcomcli.h>

using namespace ATL;

#if defined _M_IX86
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#endif // FRAMEWORK_H
