#include "pch.h"
#include "confirmdialog.h"

bool ConfirmDialog::show(HWND parent, const std::wstring& message,
	const std::wstring& title, bool defaultButtonIsCancel)
{
	const UINT type = MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2;
	const UINT flags = defaultButtonIsCancel ? type : (type & ~MB_DEFBUTTON2);
	return MessageBoxW(parent, message.c_str(), title.c_str(), flags) == IDOK;
}

bool ConfirmDialog::showYesNo(HWND parent, const std::wstring& message,
	const std::wstring& title)
{
	return MessageBoxW(parent, message.c_str(), title.c_str(),
		MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}
