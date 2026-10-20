#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_COMMON_SHAREDDATA_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_COMMON_SHAREDDATA_H_INCLUDED

#define EVENTFROMTSF L"EventFromTSF"
#define EVENTTOTSF  L"EventAckToTSF"
#define SHAREDMEMORY L"Local\\SharedMemory"
constexpr int maxSharedText = 256;

struct SharedData
{
    int pressKey = -1;
    int preservedKeyStatus = -1;
    int inputType = -1;
    int isCandidateShow = -1;
    int request_id = -1;
    int responseId = -1;
    wchar_t rc[maxSharedText] = { 0 };
    wchar_t selected_text[maxSharedText] = { 0 };

    void reset()
    {
       inputType = -1;
       isCandidateShow = -1;
       selected_text[0] = _T('\0');
       responseId = -1;
    }
};

#endif // KSHAREDDATA_H
