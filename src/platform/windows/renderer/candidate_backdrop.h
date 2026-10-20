#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_RENDERER_CANDIDATE_BACKDROP_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_RENDERER_CANDIDATE_BACKDROP_H_INCLUDED

#include <windows.h>

namespace coral::win32
{
// 位于既有分层候选窗口之下的非激活 DWM 表面。
// 只有产品候选宿主选择启用；预览与其他窗口不启用。
class CandidateBackdrop final
{
public:
    ~CandidateBackdrop() { reset(); }
    CandidateBackdrop() = default;
    CandidateBackdrop(const CandidateBackdrop&) = delete;
    CandidateBackdrop& operator=(const CandidateBackdrop&) = delete;

    static bool supported();
    bool update(HWND foreground, RECT panel_screen, bool enabled, bool dark);
    void hide();
    void reset();
    HWND window() const { return window_; }

private:
    HWND window_ = nullptr;
    bool dark_ = false;
};
}
#endif
