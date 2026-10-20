#include "pch.h"
#include "securityattribute.h"
#include <VersionHelpers.h>
#include <sddl.h>
#include "../../common/search_package_sid.h"

#define LOW_INTEGRITY_SDDL_SACL      SDDL_SACL             \
                                     SDDL_DELIMINATOR      \
                                     SDDL_ACE_BEGIN        \
                                     SDDL_MANDATORY_LABEL  \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_NO_WRITE_UP      \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_ML_LOW           \
                                     SDDL_ACE_END

#define LOCAL_SYSTEM_FILE_ACCESS     SDDL_ACE_BEGIN        \
                                     SDDL_ACCESS_ALLOWED   \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_FILE_ALL         \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_LOCAL_SYSTEM     \
                                     SDDL_ACE_END

#define EVERYONE_FILE_ACCESS         SDDL_ACE_BEGIN        \
                                     SDDL_ACCESS_ALLOWED   \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_FILE_ALL         \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_EVERYONE         \
                                     SDDL_ACE_END

#define ALL_APP_PACKAGES_FILE_ACCESS SDDL_ACE_BEGIN        \
                                     SDDL_ACCESS_ALLOWED   \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_FILE_ALL         \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_SEPERATOR        \
                                     SDDL_ALL_APP_PACKAGES \
                                     SDDL_ACE_END

CSecurityAttribute::CSecurityAttribute()
{
    if (::IsWindowsVistaOrGreater())
    {
        CString strSD;
        if (::IsWindows8OrGreater())
        {
            strSD =
                LOW_INTEGRITY_SDDL_SACL
                SDDL_DACL
                SDDL_DELIMINATOR
                LOCAL_SYSTEM_FILE_ACCESS
                EVERYONE_FILE_ACCESS
                ALL_APP_PACKAGES_FILE_ACCESS;
			for (const auto& packageSid : coral::win32::SearchPackageSids())
				strSD += (L"(A;;GA;;;" + packageSid + L")").c_str();
        }
        else
        {
            strSD =
                LOW_INTEGRITY_SDDL_SACL
                SDDL_DACL
                SDDL_DELIMINATOR
                LOCAL_SYSTEM_FILE_ACCESS
                EVERYONE_FILE_ACCESS;
        }
        ::ConvertStringSecurityDescriptorToSecurityDescriptor(strSD, SDDL_REVISION_1, &m_pd, NULL);
    }
    m_sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    m_sa.lpSecurityDescriptor = m_pd;
    m_sa.bInheritHandle = FALSE;
}

CSecurityAttribute::~CSecurityAttribute()
{
    if (m_pd) ::LocalFree(m_pd);
}

CSecurityAttribute::operator PSECURITY_ATTRIBUTES()
{
    return &m_sa;
}
