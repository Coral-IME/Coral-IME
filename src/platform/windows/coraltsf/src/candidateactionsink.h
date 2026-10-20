#pragma once

#include <Windows.h>
#include <atlstr.h>
#include <cstdint>
#include <vector>
#include "candidatestatemodel.h"

// 候选状态控制器使用的窄化所有者线程回调接口。
// 它刻意不包含渲染器、HWND、字体、主题或绘制类型。
class CandidateActionSink
{
public:
	virtual ~CandidateActionSink() = default;
	virtual bool getIsEngPuncMode() const = 0;
	virtual void setIsEngPuncMode(bool value) = 0;
	virtual HRESULT commitCandidateByIndex(int index) = 0;
	virtual void updateEditSessionAccumulatedPinyin(const CString& pinyin) = 0;
	virtual HRESULT setCursorPos(int pos) = 0;
	virtual void syncCandidateUI(const coral::tsf::CandidateStateModel& model) = 0;
	virtual void setCandidatePresentation(bool expanded, bool hasMore) = 0;
	virtual void updateCandidateUIElement() = 0;
	virtual std::vector<CString>& getCandInput() = 0;
	virtual void endCandidateUIElement() = 0;
	virtual void setKeepPlaceholder(bool keepPlaceholder) = 0;
	virtual void updateCompartmentString(const CString& text) = 0;
	virtual void hideCandidateDlg(BOOL clearKeyFlags = FALSE) = 0;
};
