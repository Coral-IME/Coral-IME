#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_COREAPI_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_COREAPI_H_INCLUDED

namespace Coreapi
{
	void showSettingMenu();
	void showAboutPage();
	void showAddPhrasePage(const CString& pinyin, const CString& word);
	void sendFeedback();
	void showErrorDialog(const CString& title, const CString& errorMessage,const CString& process_name);

	//和coralpinyinsvc交互的接口
	bool LaunchMainProcessViaService();
	bool LaunchEngineHostViaService();
	bool LaunchRendererViaService();
}

#endif // COREAPI_H
