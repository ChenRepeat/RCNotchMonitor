#pragma once
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <wincodec.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

// WinRT ABI COM Interfaces for Windows.Media.Control (GSMTC)
// Verified against C:\Windows\System32\WinMetadata\Windows.Media.winmd

// {00000036-0000-0000-C000-000000000046} IAsyncInfo
static const GUID IID_WinRT_IAsyncInfo =
    { 0x00000036, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

// {2050c4ee-11a0-57de-aed7-c97c70338245} IGlobalSystemMediaTransportControlsSessionManagerStatics
static const GUID IID_IGSMTCSessionManagerStatics =
    { 0x2050c4ee, 0x11a0, 0x57de, { 0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45 } };

// {cace8eac-e86e-504a-ab31-5ff8ff1bce49} IGlobalSystemMediaTransportControlsSessionManager
static const GUID IID_IGSMTCSessionManager =
    { 0xcace8eac, 0xe86e, 0x504a, { 0xab, 0x31, 0x5f, 0xf8, 0xff, 0x1b, 0xce, 0x49 } };

// {7148c835-9b14-5ae2-ab85-dc9b1c14e1a8} IGlobalSystemMediaTransportControlsSession
static const GUID IID_IGSMTCSession =
    { 0x7148c835, 0x9b14, 0x5ae2, { 0xab, 0x85, 0xdc, 0x9b, 0x1c, 0x14, 0xe1, 0xa8 } };

// {68856cf6-adb4-54b2-ac16-05837907acb6} IGlobalSystemMediaTransportControlsSessionMediaProperties
static const GUID IID_IGSMTCMediaProperties =
    { 0x68856cf6, 0xadb4, 0x54b2, { 0xac, 0x16, 0x05, 0x83, 0x79, 0x07, 0xac, 0xb6 } };

// {94b4b6cf-e8ba-51ad-87a7-c10ade106127} IGlobalSystemMediaTransportControlsSessionPlaybackInfo
static const GUID IID_IGSMTCPlaybackInfo =
    { 0x94b4b6cf, 0xe8ba, 0x51ad, { 0x87, 0xa7, 0xc1, 0x0a, 0xde, 0x10, 0x61, 0x27 } };

// {ede34136-6f25-588d-8ecf-ea5b6735aaa5} IGlobalSystemMediaTransportControlsSessionTimelineProperties
static const GUID IID_IGSMTCTimelineProperties =
    { 0xede34136, 0x6f25, 0x588d, { 0x8e, 0xcf, 0xea, 0x5b, 0x67, 0x35, 0xaa, 0xa5 } };

// {33ee3134-1dd6-4e3a-8067-d1c162e8642b} IRandomAccessStreamReference
static const GUID IID_IRandomAccessStreamReference =
    { 0x33ee3134, 0x1dd6, 0x4e3a, { 0x80, 0x67, 0xd1, 0xc1, 0x62, 0xe8, 0x64, 0x2b } };

// Generic vtable wrappers for WinRT IInspectable-derived interfaces
struct WinRT_IAsyncInfo : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* status) = 0; // 0=Started, 1=Completed, 2=Canceled, 3=Error
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

struct WinRT_IAsyncOperationPtr : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void** results) = 0;
};

struct WinRT_IAsyncOperationBool : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(unsigned char* results) = 0;
};

struct WinRT_IVectorViewPtr : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, void** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
};

struct WinRT_IRandomAccessStreamReference : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(WinRT_IAsyncOperationPtr** operation) = 0;
};

struct WinRT_IGSMTCTimelineProperties : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(INT64* value) = 0;
};

struct WinRT_IGSMTCPlaybackInfo : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(INT32* value) = 0; // 4 = Playing, 5 = Paused
};

struct WinRT_IGSMTCMediaProperties : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(WinRT_IRandomAccessStreamReference** value) = 0;
};

struct WinRT_IGSMTCSession : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(WinRT_IAsyncOperationPtr** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(WinRT_IGSMTCTimelineProperties** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(WinRT_IGSMTCPlaybackInfo** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPlayAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPauseAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryStopAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRecordAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryFastForwardAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRewindAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipNextAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipPreviousAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelUpAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelDownAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryTogglePlayPauseAsync(WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeAutoRepeatModeAsync(INT32 mode, WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackRateAsync(double rate, WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeShuffleActiveAsync(unsigned char active, WinRT_IAsyncOperationBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackPositionAsync(INT64 requestedPlaybackPosition, WinRT_IAsyncOperationBool** operation) = 0;
};

struct WinRT_IGSMTCSessionManager : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(WinRT_IGSMTCSession** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(WinRT_IVectorViewPtr** result) = 0;
};

struct WinRT_IGSMTCSessionManagerStatics : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(WinRT_IAsyncOperationPtr** operation) = 0;
};

// Dynamic bindings to combase.dll and shcore.dll so no extra import libs are needed
struct CombaseRuntime {
    HMODULE hCombase = NULL;
    HMODULE hShcore = NULL;
    HRESULT(WINAPI* pRoInitialize)(RO_INIT_TYPE) = nullptr;
    HRESULT(WINAPI* pRoGetActivationFactory)(HSTRING, REFIID, void**) = nullptr;
    HRESULT(WINAPI* pWindowsCreateString)(PCNZWCH, UINT32, HSTRING*) = nullptr;
    HRESULT(WINAPI* pWindowsDeleteString)(HSTRING) = nullptr;
    PCWSTR(WINAPI* pWindowsGetStringRawBuffer)(HSTRING, UINT32*) = nullptr;
    HRESULT(WINAPI* pCreateStreamOverRandomAccessStream)(IUnknown*, REFIID, void**) = nullptr;

    bool Init() {
        if (!hCombase) {
            hCombase = LoadLibraryW(L"combase.dll");
            if (hCombase) {
                pRoInitialize = (decltype(pRoInitialize))GetProcAddress(hCombase, "RoInitialize");
                pRoGetActivationFactory = (decltype(pRoGetActivationFactory))GetProcAddress(hCombase, "RoGetActivationFactory");
                pWindowsCreateString = (decltype(pWindowsCreateString))GetProcAddress(hCombase, "WindowsCreateString");
                pWindowsDeleteString = (decltype(pWindowsDeleteString))GetProcAddress(hCombase, "WindowsDeleteString");
                pWindowsGetStringRawBuffer = (decltype(pWindowsGetStringRawBuffer))GetProcAddress(hCombase, "WindowsGetStringRawBuffer");
            }
        }
        if (!hShcore) {
            hShcore = LoadLibraryW(L"shcore.dll");
            if (hShcore) {
                pCreateStreamOverRandomAccessStream = (decltype(pCreateStreamOverRandomAccessStream))GetProcAddress(hShcore, "CreateStreamOverRandomAccessStream");
            }
        }
        return (pRoGetActivationFactory && pWindowsCreateString && pWindowsDeleteString && pWindowsGetStringRawBuffer);
    }

    std::wstring HStringToWide(HSTRING hs) {
        if (!hs || !pWindowsGetStringRawBuffer) return L"";
        UINT32 len = 0;
        PCWSTR raw = pWindowsGetStringRawBuffer(hs, &len);
        std::wstring res = (raw && len > 0) ? std::wstring(raw, len) : L"";
        pWindowsDeleteString(hs);
        return res;
    }

    bool WaitAsyncOp(IInspectable* op, int timeoutMs = 250) {
        if (!op) return false;
        WinRT_IAsyncInfo* info = nullptr;
        if (FAILED(op->QueryInterface(IID_WinRT_IAsyncInfo, (void**)&info)) || !info) return false;
        int elapsed = 0;
        INT32 status = 0;
        while (elapsed < timeoutMs) {
            if ( FAILED(info->get_Status(&status)) || status != 0 ) break;
            Sleep(4);
            elapsed += 4;
        }
        info->Release();
        return (status == 1); // Completed
    }
};
