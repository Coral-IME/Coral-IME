#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_SECURITYATTRIBUTE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_SECURITYATTRIBUTE_H_INCLUDED

class CSecurityAttribute
{
public:
	CSecurityAttribute();
	~CSecurityAttribute();
	CSecurityAttribute(const CSecurityAttribute&) = delete;
	CSecurityAttribute& operator=(const CSecurityAttribute&) = delete;
	operator PSECURITY_ATTRIBUTES();
private:
	PSECURITY_DESCRIPTOR m_pd = nullptr;
	SECURITY_ATTRIBUTES m_sa{};
};

#endif // SECURITYATTRIBUTE_H
