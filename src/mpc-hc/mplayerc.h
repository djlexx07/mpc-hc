/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2014 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

#include "EventDispatcher.h"
#include "RenderersSettings.h"
#include "resource.h"

#include <atlsync.h>
#include <d3d9.h> // needed for dxva2api.h and vmr9.h
#include <dxva2api.h>
#include <vmr9.h>

#include <map>
#include <memory>
#include <mutex>

#define MPC_WND_CLASS_NAME L"MediaPlayerClassicW"

// define the default logo we use
#define DEF_LOGO IDF_LOGO3

extern HICON LoadIcon(CString fn, bool bSmallIcon);
extern bool LoadType(CString fn, CString& type);
extern bool LoadResource(UINT resid, CStringA& str, LPCTSTR restype);
extern CStringA GetContentType(CString fn, CAtlList<CString>* redir = nullptr);
extern WORD AssignedToCmd(UINT keyOrMouseValue, bool bIsFullScreen = false, bool bCheckMouse = true);
extern void SetAudioRenderer(int AudioDevNo);
extern void SetHandCursor(HWND m_hWnd, UINT nID);

__inline DXVA2_Fixed32 IntToFixed(__in const int _int_, __in const short divisor = 1)
{
    // special converter that is resistant to MS bugs
    DXVA2_Fixed32 _fixed_;
    _fixed_.Value = SHORT(_int_ / divisor);
    _fixed_.Fraction = USHORT((_int_ % divisor * 0x10000 + divisor / 2) / divisor);
    return _fixed_;
}

__inline int FixedToInt(__in const DXVA2_Fixed32& _fixed_, __in const short factor = 1)
{
    // special converter that is resistant to MS bugs
    return (int)_fixed_.Value * factor + ((int)_fixed_.Fraction * factor + 0x8000) / 0x10000;
}

enum {
    WM_GRAPHNOTIFY = WM_RESET_DEVICE + 1,
    WM_POSTOPEN,
    WM_OPENFAILED,
    WM_SAVESETTINGS,
    WM_TUNER_SCAN_PROGRESS,
    WM_TUNER_SCAN_END,
    WM_TUNER_STATS,
    WM_TUNER_NEW_CHANNEL,
    WM_DVB_EIT_DATA_READY,
    WM_LOADSUBTITLES,
    WM_GETSUBTITLES,
};

enum ControlType {
    ProcAmp_Brightness = 0x1,
    ProcAmp_Contrast   = 0x2,
    ProcAmp_Hue        = 0x4,
    ProcAmp_Saturation = 0x8,
    ProcAmp_All = ProcAmp_Brightness | ProcAmp_Contrast | ProcAmp_Hue | ProcAmp_Saturation
};

struct COLORPROPERTY_RANGE {
    DWORD dwProperty;
    int   MinValue;
    int   MaxValue;
    int   DefaultValue;
    int   StepSize;
};

class CAppSettings;

class CMPlayerCApp : public CWinApp
{
    HMODULE m_hNTDLL;

    ATL::CMutex m_mutexOneInstance;

    CAtlList<CString> m_cmdln;
    void PreProcessCommandLine();
    bool SendCommandLine(HWND hWnd);
    UINT GetVKFromAppCommand(UINT nAppCommand);

    COLORPROPERTY_RANGE     m_ColorControl[4];
    VMR9ProcAmpControlRange m_VMR9ColorControl[4];
    DXVA2_ValueRange        m_EVRColorControl[4];

    static UINT GetRemoteControlCodeMicrosoft(UINT nInputcode, HRAWINPUT hRawInput);
    static UINT GetRemoteControlCodeSRM7500(UINT nInputcode, HRAWINPUT hRawInput);

    bool m_bDelayingIdle;
    void DelayedIdle();
    virtual BOOL IsIdleMessage(MSG* pMsg) override;
    virtual BOOL OnIdle(LONG lCount) override;
    virtual BOOL PumpMessage() override;

public:
    CMPlayerCApp();
    ~CMPlayerCApp();

    EventRouter m_eventd;

    void ShowCmdlnSwitches() const;

    bool StoreSettingsToIni();
    bool StoreSettingsToRegistry();
    CString GetIniPath() const;
    bool IsIniValid() const;
    bool ChangeSettingsLocation(bool useIni);
    bool ExportSettings(CString savePath, CString subKey = _T(""));

private:
    std::map<CString, std::map<CString, CString, CStringUtils::IgnoreCaseLess>, CStringUtils::IgnoreCaseLess> m_ProfileMap;
    bool m_bProfileInitialized;
    bool m_bQueuedProfileFlush;
    void InitProfile();
    std::recursive_mutex m_profileMutex;
    DWORD m_dwProfileLastAccessTick;

public:
    void FlushProfile(bool bForce = true);
    virtual BOOL GetProfileBinary(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPBYTE* ppData, UINT* pBytes) override;
    virtual UINT GetProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nDefault) override;
    virtual CString GetProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault = nullptr) override;
    virtual BOOL WriteProfileBinary(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPBYTE pData, UINT nBytes) override;
    virtual BOOL WriteProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nValue) override;
    virtual BOOL WriteProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszValue) override;
    bool HasProfileEntry(LPCTSTR lpszSection, LPCTSTR lpszEntry);

    bool GetAppSavePath(CString& path);
    bool GetAppDataPath(CString& path);

    bool m_fClosingState;
    CRenderersData m_Renderers;
    CString     m_strVersion;
    CString     m_AudioRendererDisplayName_CL;

    std::unique_ptr<CAppSettings> m_s;

    typedef UINT(*PTR_GetRemoteControlCode)(UINT nInputcode, HRAWINPUT hRawInput);

    PTR_GetRemoteControlCode    GetRemoteControlCode;
    COLORPROPERTY_RANGE*        GetColorControl(ControlType nFlag);
    void                        ResetColorControlRange();
    void                        UpdateColorControlRange(bool isEVR);
    VMR9ProcAmpControlRange*    GetVMR9ColorControl(ControlType nFlag);
    DXVA2_ValueRange*           GetEVRColorControl(ControlType nFlag);

    static void RunAsAdministrator(LPCTSTR strCommand, LPCTSTR strArgs, bool bWaitProcess);

    void RegisterHotkeys();
    void UnregisterHotkeys();

public:
    virtual BOOL InitInstance() override;
    virtual int ExitInstance() override;

public:
    DECLARE_MESSAGE_MAP()
    afx_msg void OnAppAbout();
    afx_msg void OnFileExit();
    afx_msg void OnHelpShowcommandlineswitches();
};

class DPI final
{
    int m_dpix, m_dpiy;
public:
    DPI() {
        HDC hDC = ::GetDC(nullptr);
        m_dpix = GetDeviceCaps(hDC, LOGPIXELSX);
        m_dpiy = GetDeviceCaps(hDC, LOGPIXELSY);
        ::ReleaseDC(nullptr, hDC);
    }
    int ScaleFloorX(int x) { return x * m_dpix / 96; }
    int ScaleFloorY(int y) { return y * m_dpiy / 96; }
    int ScaleX(int x) { return MulDiv(x, m_dpix, 96); }
    int ScaleY(int y) { return MulDiv(y, m_dpiy, 96); }
    int TransposeScaledX(int x) { return MulDiv(x, m_dpiy, m_dpix); }
    int TransposeScaledY(int y) { return MulDiv(y, m_dpix, m_dpiy); }
};

#define AfxGetAppSettings() (*static_cast<CMPlayerCApp*>(AfxGetApp())->m_s.get())
#define AfxGetMyApp()       static_cast<CMPlayerCApp*>(AfxGetApp())
#define AfxGetMainFrame()   dynamic_cast<CMainFrame*>(AfxGetMainWnd())

#define GetEventd() AfxGetMyApp()->m_eventd
