/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2015 see Authors.txt
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

#include "stdafx.h"
#include <algorithm>
#include "RenderersSettings.h"
#include "DX9AllocatorPresenter.h"
#include <InitGuid.h>
#include <utility>
#include "../../../SubPic/DX9SubPic.h"
#include "../../../SubPic/SubPicQueueImpl.h"
#include "IPinHook.h"
#include "version.h"
#include "FocusThread.h"

CCritSec g_ffdshowReceive;
bool queue_ffdshow_support = false;

// only for debugging
//#define DISABLE_USING_D3D9EX

#define FRAMERATE_MAX_DELTA 3000

using namespace DSObjects;

#pragma warning(push)
#pragma warning(disable: 4351) // new behavior: elements of array 'array' will be default initialized
// CDX9AllocatorPresenter
CDX9AllocatorPresenter::CDX9AllocatorPresenter(HWND hWnd, bool bFullscreen, HRESULT& hr, bool bIsEVR, CString& _Error)
    : CDX9RenderingEngine(hWnd, hr, &_Error)
    , m_bAlternativeVSync(false)
    , m_bCompositionEnabled(false)
    , m_bIsEVR(bIsEVR)
    , m_OrderedPaint(0)
    , m_VSyncMode(0)
    , m_bDesktopCompositionDisabled(false)
    , m_bIsFullscreen(bFullscreen)
    , m_bNeedCheckSample(true)
    , m_MainThreadId(0)
    , m_bIsRendering(false)
    , m_hDWMAPI(nullptr)
    , m_hD3D9(nullptr)
    , m_pDwmIsCompositionEnabled(nullptr)
    , m_pDwmEnableComposition(nullptr)
    , m_pDirect3DCreate9Ex(nullptr)
    , m_pDirectDraw(nullptr)
    , m_LastAdapterCheck(0)
    , m_nTearingPos(0)
    , m_VMR9AlphaBitmapWidthBytes(0)
    , m_pD3DXLoadSurfaceFromMemory(nullptr)
    , m_pD3DXLoadSurfaceFromSurface(nullptr)
    , m_pD3DXCreateLine(nullptr)
    , m_pD3DXCreateFont(nullptr)
    , m_pD3DXCreateSprite(nullptr)
    , m_nVMR9Surfaces(0)
    , m_iVMR9Surface(0)
    , m_nUsedBuffer(0)
    , m_fAvrFps(0.0)
    , m_fJitterStdDev(0.0)
    , m_fJitterMean(0.0)
    , m_fSyncOffsetStdDev(0.0)
    , m_fSyncOffsetAvr(0.0)
    , m_DetectedRefreshRate(0)
    , m_DetectedRefreshTime(0.0)
    , m_DetectedRefreshTimePrim(0)
    , m_DetectedScanlineTime(0)
    , m_DetectedScanlineTimePrim(0)
    , m_DetectedScanlinesPerFrame(0.0)
    , m_ldDetectedRefreshRateList()
    , m_ldDetectedScanlineRateList()
    , m_DetectedRefreshRatePos(0)
    , m_bSyncStatsAvailable(false)
    , m_pllJitter()
    , m_pllSyncOffset()
    , m_llLastPerf(0)
    , m_JitterStdDev(0)
    , m_MaxJitter(MINLONG64)
    , m_MinJitter(MAXLONG64)
    , m_MaxSyncOffset(0)
    , m_MinSyncOffset(0)
    , m_nNextJitter(0)
    , m_nNextSyncOffset(0)
    , m_rtTimePerFrame(0)
    , m_DetectedFrameRate(0.0)
    , m_DetectedFrameTime(0.0)
    , m_DetectedFrameTimeStdDev(0.0)
    , m_DetectedLock(false)
    , m_DetectedFrameTimeHistory()
    , m_DetectedFrameTimeHistoryHistory()
    , m_DetectedFrameTimePos(0)
    , m_bInterlaced(false)
    , m_nFrameType(PICT_NONE)
    , m_TextScale(1.0)
    , m_VBlankEndWait(0)
    , m_VBlankStartWait(0)
    , m_VBlankWaitTime(0)
    , m_VBlankLockTime(0)
    , m_VBlankMin(300000)
    , m_VBlankMinCalc(300000)
    , m_VBlankMax(0)
    , m_VBlankEndPresent(-100000)
    , m_VBlankStartMeasureTime(0)
    , m_VBlankStartMeasure(0)
    , m_PresentWaitTime(0)
    , m_PresentWaitTimeMin(3000000000)
    , m_PresentWaitTimeMax(0)
    , m_PaintTime(0)
    , m_PaintTimeMin(3000000000)
    , m_PaintTimeMax(0)
    , m_WaitForGPUTime(0)
    , m_RasterStatusWaitTime(0)
    , m_RasterStatusWaitTimeMin(3000000000)
    , m_RasterStatusWaitTimeMax(0)
    , m_RasterStatusWaitTimeMaxCalc(0)
    , m_ClockDiffCalc(0.0)
    , m_ClockDiffPrim(0.0)
    , m_ClockDiff(0.0)
    , m_TimeChangeHistory()
    , m_ClockChangeHistory()
    , m_ClockTimeChangeHistoryPos(0)
    , m_ModeratedTimeSpeed(1.0)
    , m_ModeratedTimeSpeedPrim(0.0)
    , m_ModeratedTimeSpeedDiff(0.0)
    , m_bCorrectedFrameTime(0)
    , m_FrameTimeCorrection(0)
    , m_LastFrameDuration(0)
    , m_LastSampleTime(0)
    , m_FocusThread(nullptr)
{
    ZeroMemory(&m_VMR9AlphaBitmap, sizeof(m_VMR9AlphaBitmap));

    if (FAILED(hr)) {
        _Error += _T("ISubPicAllocatorPresenterImpl failed\n");
        return;
    }

    HINSTANCE hDll                = GetRenderersData()->GetD3X9Dll();

    if (hDll) {
        (FARPROC&)m_pD3DXLoadSurfaceFromMemory  = GetProcAddress(hDll, "D3DXLoadSurfaceFromMemory");
        (FARPROC&)m_pD3DXLoadSurfaceFromSurface = GetProcAddress(hDll, "D3DXLoadSurfaceFromSurface");
        (FARPROC&)m_pD3DXCreateLine             = GetProcAddress(hDll, "D3DXCreateLine");
        (FARPROC&)m_pD3DXCreateFont             = GetProcAddress(hDll, "D3DXCreateFontW");
        (FARPROC&)m_pD3DXCreateSprite           = GetProcAddress(hDll, "D3DXCreateSprite");
    } else {
        _Error += _T("The installed DirectX End-User Runtime is outdated. Please download and install the ");
        _Error += MPC_DX_SDK_MONTH _T(" ") MAKE_STR(MPC_DX_SDK_YEAR);
        _Error += _T(" release or newer in order for MPC-HC to function properly.\n");
    }

    m_hDWMAPI = LoadLibrary(L"dwmapi.dll");
    if (m_hDWMAPI) {
        (FARPROC&)m_pDwmIsCompositionEnabled = GetProcAddress(m_hDWMAPI, "DwmIsCompositionEnabled");
        (FARPROC&)m_pDwmEnableComposition = GetProcAddress(m_hDWMAPI, "DwmEnableComposition");
    }

    m_hD3D9 = LoadLibrary(L"d3d9.dll");
#ifndef DISABLE_USING_D3D9EX
    if (m_hD3D9) {
        (FARPROC&)m_pDirect3DCreate9Ex = GetProcAddress(m_hD3D9, "Direct3DCreate9Ex");
    }
#endif

    if (m_pDirect3DCreate9Ex) {
        m_pDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
        if (!m_pD3DEx) {
            m_pDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
        }
    }
    if (!m_pD3DEx) {
        m_pD3D.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!m_pD3D) {
            m_pD3D.Attach(Direct3DCreate9(D3D9b_SDK_VERSION));
        }
    } else {
        m_pD3D = m_pD3DEx;
    }

    const CRenderersSettings& r = GetRenderersSettings();

    if (r.m_AdvRendSets.bVMRDisableDesktopComposition) {
        m_bDesktopCompositionDisabled = true;
        if (m_pDwmEnableComposition) {
            m_pDwmEnableComposition(0);
        }
    } else {
        m_bDesktopCompositionDisabled = false;
    }

    hr = CreateDevice(_Error);
}
#pragma warning(pop)

CDX9AllocatorPresenter::~CDX9AllocatorPresenter()
{
    if (m_bDesktopCompositionDisabled) {
        m_bDesktopCompositionDisabled = false;
        if (m_pDwmEnableComposition) {
            m_pDwmEnableComposition(1);
        }
    }

    m_pFont       = nullptr;
    m_pLine       = nullptr;
    m_pSprite     = nullptr;
    m_pOSDTexture = nullptr;
    m_pOSDSurface = nullptr;
    m_pD3DDev     = nullptr;
    m_pD3DDevEx   = nullptr;

    CleanupRenderingEngine();

    m_pD3D   = nullptr;
    m_pD3DEx = nullptr;
    if (m_hDWMAPI) {
        FreeLibrary(m_hDWMAPI);
        m_hDWMAPI = nullptr;
    }
    if (m_hD3D9) {
        FreeLibrary(m_hD3D9);
        m_hD3D9 = nullptr;
    }

    if (m_FocusThread) {
        m_FocusThread->PostThreadMessage(WM_QUIT, 0, 0);
        if (WaitForSingleObject(m_FocusThread->m_hThread, 10000) == WAIT_TIMEOUT) {
            ASSERT(FALSE);
            TerminateThread(m_FocusThread->m_hThread, 0xDEAD);
        }
    }
}

void ModerateFloat(double& Value, double Target, double& ValuePrim, double ChangeSpeed);

#if 0
class CRandom31
{
public:

    CRandom31() {
        m_Seed = 12164;
    }

    void f_SetSeed(int32 _Seed) {
        m_Seed = _Seed;
    }
    int32 f_GetSeed() {
        return m_Seed;
    }
    /*
    Park and Miller's psuedo-random number generator.
    */
    int32 m_Seed;
    int32 f_Get() {
        static const int32 A = 16807;
        static const int32 M = 2147483647;      // 2^31 - 1
        static const int32 q = M / A;           // M / A
        static const int32 r = M % A;           // M % A
        m_Seed = A * (m_Seed % q) - r * (m_Seed / q);
        if (m_Seed < 0) {
            m_Seed += M;
        }
        return m_Seed;
    }

    static int32 fs_Max() {
        return 2147483646;
    }

    double f_GetFloat() {
        return double(f_Get()) * (1.0 / double(fs_Max()));
    }
};

class CVSyncEstimation
{
private:
    class CHistoryEntry
    {
    public:
        CHistoryEntry() {
            m_Time = 0;
            m_ScanLine = -1;
        }
        LONGLONG m_Time;
        int m_ScanLine;
    };

    class CSolution
    {
    public:
        CSolution() {
            m_ScanLines = 1000;
            m_ScanLinesPerSecond = m_ScanLines * 100;
        }
        int m_ScanLines;
        double m_ScanLinesPerSecond;
        double m_SqrSum;

        void f_Mutate(double _Amount, CRandom31& _Random, int _MinScans) {
            int ToDo = _Random.f_Get() % 10;
            if (ToDo == 0) {
                m_ScanLines = m_ScanLines / 2;
            } else if (ToDo == 1) {
                m_ScanLines = m_ScanLines * 2;
            }

            m_ScanLines = m_ScanLines * (1.0 + (_Random.f_GetFloat() * _Amount) - _Amount * 0.5);
            m_ScanLines = max(m_ScanLines, _MinScans);

            if (ToDo == 2) {
                m_ScanLinesPerSecond /= (_Random.f_Get() % 4) + 1;
            } else if (ToDo == 3) {
                m_ScanLinesPerSecond *= (_Random.f_Get() % 4) + 1;
            }

            m_ScanLinesPerSecond *= 1.0 + (_Random.f_GetFloat() * _Amount) - _Amount * 0.5;
        }

        void f_SpawnInto(CSolution& _Other, CRandom31& _Random, int _MinScans) {
            _Other = *this;
            _Other.f_Mutate(_Random.f_GetFloat() * 0.1, _Random, _MinScans);
        }

        static int fs_Compare(const void* _pFirst, const void* _pSecond) {
            const CSolution* pFirst = (const CSolution*)_pFirst;
            const CSolution* pSecond = (const CSolution*)_pSecond;
            if (pFirst->m_SqrSum < pSecond->m_SqrSum) {
                return -1;
            } else if (pFirst->m_SqrSum > pSecond->m_SqrSum) {
                return 1;
            }
            return 0;
        }


    };

    enum {
        ENumHistory = 128
    };

    CHistoryEntry m_History[ENumHistory];
    int m_iHistory;
    CSolution m_OldSolutions[2];

    CRandom31 m_Random;


    double fp_GetSquareSum(double _ScansPerSecond, double _ScanLines) {
        double SquareSum = 0;
        int nHistory = min(m_nHistory, ENumHistory);
        int iHistory = m_iHistory - nHistory;
        if (iHistory < 0) {
            iHistory += ENumHistory;
        }
        for (int i = 1; i < nHistory; ++i) {
            int iHistory0 = iHistory + i - 1;
            int iHistory1 = iHistory + i;
            if (iHistory0 < 0) {
                iHistory0 += ENumHistory;
            }
            iHistory0 = iHistory0 % ENumHistory;
            iHistory1 = iHistory1 % ENumHistory;
            ASSERT(m_History[iHistory0].m_Time != 0);
            ASSERT(m_History[iHistory1].m_Time != 0);

            double DeltaTime = (m_History[iHistory1].m_Time - m_History[iHistory0].m_Time) / 10000000.0;
            double PredictedScanLine = m_History[iHistory0].m_ScanLine + DeltaTime * _ScansPerSecond;
            PredictedScanLine = fmod(PredictedScanLine, _ScanLines);
            double Delta = (m_History[iHistory1].m_ScanLine - PredictedScanLine);
            double DeltaSqr = Delta * Delta;
            SquareSum += DeltaSqr;
        }
        return SquareSum;
    }

    int m_nHistory;
public:

    CVSyncEstimation() {
        m_iHistory = 0;
        m_nHistory = 0;
    }

    void f_AddSample(int _ScanLine, LONGLONG _Time) {
        m_History[m_iHistory].m_ScanLine = _ScanLine;
        m_History[m_iHistory].m_Time = _Time;
        ++m_nHistory;
        m_iHistory = (m_iHistory + 1) % ENumHistory;
    }

    void f_GetEstimation(double& _RefreshRate, int& _ScanLines, int _ScreenSizeY, int _WindowsRefreshRate) {
        _RefreshRate = 0;
        _ScanLines = 0;

        int iHistory = m_iHistory;
        // We have a full history
        if (m_nHistory > 10) {
            for (int l = 0; l < 5; ++l) {
                const int nSol = 3 + 5 + 5 + 3;
                CSolution Solutions[nSol];

                Solutions[0] = m_OldSolutions[0];
                Solutions[1] = m_OldSolutions[1];
                Solutions[2].m_ScanLines = _ScreenSizeY;
                Solutions[2].m_ScanLinesPerSecond = _ScreenSizeY * _WindowsRefreshRate;

                int iStart = 3;
                for (int i = iStart; i < iStart + 5; ++i) {
                    Solutions[0].f_SpawnInto(Solutions[i], m_Random, _ScreenSizeY);
                }
                iStart += 5;
                for (int i = iStart; i < iStart + 5; ++i) {
                    Solutions[1].f_SpawnInto(Solutions[i], m_Random, _ScreenSizeY);
                }
                iStart += 5;
                for (int i = iStart; i < iStart + 3; ++i) {
                    Solutions[2].f_SpawnInto(Solutions[i], m_Random, _ScreenSizeY);
                }

                int Start = 2;
                if (l == 0) {
                    Start = 0;
                }
                for (int i = Start; i < nSol; ++i) {
                    Solutions[i].m_SqrSum = fp_GetSquareSum(Solutions[i].m_ScanLinesPerSecond, Solutions[i].m_ScanLines);
                }

                qsort(Solutions, nSol, sizeof(Solutions[0]), &CSolution::fs_Compare);
                for (int i = 0; i < 2; ++i) {
                    m_OldSolutions[i] = Solutions[i];
                }
            }

            _ScanLines = m_OldSolutions[0].m_ScanLines + 0.5;
            _RefreshRate = 1.0 / (m_OldSolutions[0].m_ScanLines / m_OldSolutions[0].m_ScanLinesPerSecond);
        } else {
            m_OldSolutions[0].m_ScanLines = _ScreenSizeY;
            m_OldSolutions[1].m_ScanLines = _ScreenSizeY;
        }
    }
};
#endif

bool CDX9AllocatorPresenter::SettingsNeedResetDevice()
{
    CRenderersSettings& r = GetRenderersSettings();
    CRenderersSettings::CAdvRendererSettings& New = r.m_AdvRendSets;
    CRenderersSettings::CAdvRendererSettings& Current = m_LastRendererSettings;

    bool bRet = false;

    bRet = bRet || New.bVMR9AlterativeVSync != Current.bVMR9AlterativeVSync;
    bRet = bRet || New.bVMR9VSyncAccurate != Current.bVMR9VSyncAccurate;

    if (m_bIsFullscreen) {
        bRet = bRet || New.bVMR9FullscreenGUISupport != Current.bVMR9FullscreenGUISupport;
    } else {
        if (Current.bVMRDisableDesktopComposition) {
            if (!m_bDesktopCompositionDisabled) {
                m_bDesktopCompositionDisabled = true;
                if (m_pDwmEnableComposition) {
                    m_pDwmEnableComposition(0);
                }
            }
        } else {
            if (m_bDesktopCompositionDisabled) {
                m_bDesktopCompositionDisabled = false;
                if (m_pDwmEnableComposition) {
                    m_pDwmEnableComposition(1);
                }
            }
        }
    }

    if (m_bIsEVR) {
        bRet = bRet || New.bEVRHighColorResolution != Current.bEVRHighColorResolution;
        bRet = bRet || New.bEVRForceInputHighColorResolution != Current.bEVRForceInputHighColorResolution;
    }

    m_LastRendererSettings = r.m_AdvRendSets;

    return bRet;
}

HRESULT CDX9AllocatorPresenter::CreateDevice(CString& _Error)
{
    const CRenderersSettings& r = GetRenderersSettings();
    CRenderersData* rd = GetRenderersData();

    m_VBlankEndWait = 0;
    m_VBlankMin = 300000;
    m_VBlankMinCalc = 300000;
    m_VBlankMax = 0;
    m_VBlankStartWait = 0;
    m_VBlankWaitTime = 0;
    m_VBlankLockTime = 0;
    m_PresentWaitTime = 0;
    m_PresentWaitTimeMin = 3000000000;
    m_PresentWaitTimeMax = 0;

    m_LastRendererSettings = r.m_AdvRendSets;

    m_VBlankEndPresent = -100000;
    m_VBlankStartMeasureTime = 0;
    m_VBlankStartMeasure = 0;

    m_PaintTime = 0;
    m_PaintTimeMin = 3000000000;
    m_PaintTimeMax = 0;

    m_RasterStatusWaitTime = 0;
    m_RasterStatusWaitTimeMin = 3000000000;
    m_RasterStatusWaitTimeMax = 0;
    m_RasterStatusWaitTimeMaxCalc = 0;

    m_ClockDiff = 0.0;
    m_ClockDiffPrim = 0.0;
    m_ClockDiffCalc = 0.0;

    m_ModeratedTimeSpeed = 1.0;
    m_ModeratedTimeSpeedDiff = 0.0;
    m_ModeratedTimeSpeedPrim = 0;
    ZeroMemory(m_TimeChangeHistory, sizeof(m_TimeChangeHistory));
    ZeroMemory(m_ClockChangeHistory, sizeof(m_ClockChangeHistory));
    m_ClockTimeChangeHistoryPos = 0;

    m_pSprite = nullptr;
    m_pLine = nullptr;
    m_pFont = nullptr;
    m_pOSDTexture = nullptr;
    m_pOSDSurface = nullptr;
    m_pDirectDraw = nullptr;

    CleanupRenderingEngine();

    UINT currentAdapter = GetAdapter(m_pD3D);
    bool bTryToReset = (currentAdapter == m_CurrentAdapter);

    if (!bTryToReset) {
        m_pD3DDev = nullptr;
        m_pD3DDevEx = nullptr;
        m_CurrentAdapter = currentAdapter;
    }

    if (!m_pD3D) {
        _Error += L"Failed to create D3D9\n";
        return E_UNEXPECTED;
    }

    HRESULT hr = S_OK;

    /*// TODO : add NVIDIA PerfHUD !!!

    // Set default settings
    UINT AdapterToUse=D3DADAPTER_DEFAULT;
    D3DDEVTYPE DeviceType=D3DDEVTYPE_HAL;

    #if SHIPPING_VERSION
    // When building a shipping version, disable PerfHUD (opt-out)
    #else
    // Look for 'NVIDIA PerfHUD' adapter
    // If it is present, override default settings
    for (UINT Adapter=0;Adapter<g_pD3D->GetAdapterCount();Adapter++)
    {
      D3DADAPTER_IDENTIFIER9 Identifier;
      HRESULT Res;

    Res = g_pD3D->GetAdapterIdentifier(Adapter,0,&Identifier);
      if (strstr(Identifier.Description,"PerfHUD") != 0)
     {
      AdapterToUse=Adapter;
      DeviceType=D3DDEVTYPE_REF;
      break;
     }
    }
    #endif

    if (FAILED(g_pD3D->CreateDevice( AdapterToUse, DeviceType, hWnd,
      D3DCREATE_HARDWARE_VERTEXPROCESSING,
    &d3dpp, &g_pd3dDevice) ) )
    {
     return E_FAIL;
    }
    */


    //#define ENABLE_DDRAWSYNC
#ifdef ENABLE_DDRAWSYNC
    hr = DirectDrawCreate(nullptr, &m_pDirectDraw, nullptr);
    if (hr == S_OK) {
        hr = m_pDirectDraw->SetCooperativeLevel(m_hWnd, DDSCL_NORMAL);
    }
#endif

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));

    BOOL bCompositionEnabled = false;
    if (m_pDwmIsCompositionEnabled) {
        m_pDwmIsCompositionEnabled(&bCompositionEnabled);
    }

    m_bCompositionEnabled = !!bCompositionEnabled;
    m_bAlternativeVSync = r.m_AdvRendSets.bVMR9AlterativeVSync;

    // detect FP textures support
    rd->m_bFP16Support = SUCCEEDED(m_pD3D->CheckDeviceFormat(m_CurrentAdapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_FILTER, D3DRTYPE_VOLUMETEXTURE, D3DFMT_A32B32G32R32F));

    // detect 10-bit textures support
    rd->m_b10bitSupport = SUCCEEDED(m_pD3D->CheckDeviceFormat(m_CurrentAdapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A2R10G10B10));

    // detect 10-bit device support
    bool bHighColorSupport = SUCCEEDED(m_pD3D->CheckDeviceType(m_CurrentAdapter, D3DDEVTYPE_HAL, D3DFMT_A2R10G10B10, D3DFMT_A2R10G10B10, FALSE));

    // set settings that depend on hardware feature support
    m_bForceInputHighColorResolution = r.m_AdvRendSets.bEVRForceInputHighColorResolution && m_bIsEVR && rd->m_b10bitSupport;
    m_bHighColorResolution = r.m_AdvRendSets.bEVRHighColorResolution && m_bIsEVR && rd->m_b10bitSupport && bHighColorSupport;
    m_bFullFloatingPointProcessing = r.m_AdvRendSets.bVMR9FullFloatingPointProcessing && rd->m_bFP16Support;
    m_bHalfFloatingPointProcessing = r.m_AdvRendSets.bVMR9HalfFloatingPointProcessing && rd->m_bFP16Support && !m_bFullFloatingPointProcessing;

    // set color formats
    if (m_bFullFloatingPointProcessing) {
        m_SurfaceType = D3DFMT_A32B32G32R32F;
    } else if (m_bHalfFloatingPointProcessing) {
        m_SurfaceType = D3DFMT_A16B16G16R16F;
    } else if (m_bForceInputHighColorResolution || m_bHighColorResolution) {
        m_SurfaceType = D3DFMT_A2R10G10B10;
    } else {
        m_SurfaceType = D3DFMT_X8R8G8B8;
    }

    D3DDISPLAYMODEEX DisplayMode;
    ZeroMemory(&DisplayMode, sizeof(DisplayMode));
    DisplayMode.Size = sizeof(DisplayMode);
    D3DDISPLAYMODE d3ddm;
    ZeroMemory(&d3ddm, sizeof(d3ddm));

    CSize szDesktopSize(GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));

    if (m_bIsFullscreen) {
        if (m_bHighColorResolution) {
            pp.BackBufferFormat = D3DFMT_A2R10G10B10;
        } else {
            pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        }
        pp.Windowed = false;
        pp.BackBufferCount = 3;
        pp.SwapEffect = D3DSWAPEFFECT_FLIP;
        // there's no Desktop composition to take care of alternative vSync in exclusive mode, alternative vSync is therefore unused
        pp.hDeviceWindow = m_hWnd;
        pp.Flags = D3DPRESENTFLAG_VIDEO;
        if (r.m_AdvRendSets.bVMR9FullscreenGUISupport && !m_bHighColorResolution) {
            pp.Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        }
        m_D3DDevExError = L"No m_pD3DEx";

        if (!m_FocusThread) {
            m_FocusThread = (CFocusThread*)AfxBeginThread(RUNTIME_CLASS(CFocusThread), 0, 0, 0);
        }

        if (m_pD3DEx) {
            m_pD3DEx->GetAdapterDisplayModeEx(m_CurrentAdapter, &DisplayMode, nullptr);

            DisplayMode.Format = pp.BackBufferFormat;
            m_ScreenSize.SetSize(DisplayMode.Width, DisplayMode.Height);
            pp.FullScreen_RefreshRateInHz = m_refreshRate = DisplayMode.RefreshRate;
            pp.BackBufferWidth = m_ScreenSize.cx;
            pp.BackBufferHeight = m_ScreenSize.cy;

            if (bTryToReset) {
                if (!m_pD3DDevEx || FAILED(hr = m_pD3DDevEx->ResetEx(&pp, &DisplayMode))) {
                    bTryToReset = false;
                    m_pD3DDev = nullptr;
                    m_pD3DDevEx = nullptr;
                }
            }
            if (!bTryToReset) {
                hr = m_pD3DEx->CreateDeviceEx(
                         m_CurrentAdapter, D3DDEVTYPE_HAL, m_FocusThread->GetFocusWindow(),
                         GetVertexProcessing() | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS | D3DCREATE_NOWINDOWCHANGES, //D3DCREATE_MANAGED
                         &pp, &DisplayMode, &m_pD3DDevEx);
            }

            m_D3DDevExError = GetWindowsErrorMessage(hr, m_hD3D9);
            if (m_pD3DDevEx) {
                m_pD3DDev = m_pD3DDevEx;
                m_BackbufferType = pp.BackBufferFormat;
                m_DisplayType = DisplayMode.Format;
            }
        }
        if (bTryToReset && m_pD3DDev && !m_pD3DDevEx) {
            if (FAILED(hr = m_pD3DDev->Reset(&pp))) {
                m_pD3DDev = nullptr;
            }
        }
        if (!m_pD3DDev) {
            m_pD3D->GetAdapterDisplayMode(m_CurrentAdapter, &d3ddm);
            d3ddm.Format = pp.BackBufferFormat;
            m_ScreenSize.SetSize(d3ddm.Width, d3ddm.Height);
            pp.FullScreen_RefreshRateInHz = m_refreshRate = d3ddm.RefreshRate;
            pp.BackBufferWidth = m_ScreenSize.cx;
            pp.BackBufferHeight = m_ScreenSize.cy;

            hr = m_pD3D->CreateDevice(
                     m_CurrentAdapter, D3DDEVTYPE_HAL, m_FocusThread->GetFocusWindow(),
                     GetVertexProcessing() | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_NOWINDOWCHANGES, //D3DCREATE_MANAGED
                     &pp, &m_pD3DDev);
            m_DisplayType = d3ddm.Format;
            m_BackbufferType = pp.BackBufferFormat;
        }
        if (m_pD3DDev && r.m_AdvRendSets.bVMR9FullscreenGUISupport && !m_bHighColorResolution) {
            m_pD3DDev->SetDialogBoxMode(true);
            //if (m_pD3DDev->SetDialogBoxMode(true) != S_OK)
            //  ExitProcess(0);
        }
    } else {
        pp.Windowed = TRUE;
        pp.hDeviceWindow = m_hWnd;
        pp.SwapEffect = D3DSWAPEFFECT_COPY;
        pp.Flags = D3DPRESENTFLAG_VIDEO;
        pp.BackBufferCount = 1;
        if (bCompositionEnabled || m_bAlternativeVSync) {
            // Desktop composition takes care of the VSYNC
            pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        }

        if (m_pD3DEx) {
            m_pD3DEx->GetAdapterDisplayModeEx(m_CurrentAdapter, &DisplayMode, nullptr);
            m_ScreenSize.SetSize(DisplayMode.Width, DisplayMode.Height);
            m_refreshRate = DisplayMode.RefreshRate;
            pp.BackBufferWidth = szDesktopSize.cx;
            pp.BackBufferHeight = szDesktopSize.cy;

            if (bTryToReset) {
                if (!m_pD3DDevEx || FAILED(hr = m_pD3DDevEx->ResetEx(&pp, nullptr))) {
                    bTryToReset = false;
                    m_pD3DDev = nullptr;
                    m_pD3DDevEx = nullptr;
                }
            }
            if (!bTryToReset) {
                // We can get 0x8876086a here when switching from two displays to one display using Win + P (Windows 7)
                // Cause: We might not reinitialize dx correctly during the switch
                hr = m_pD3DEx->CreateDeviceEx(
                         m_CurrentAdapter, D3DDEVTYPE_HAL, m_hWnd,
                         GetVertexProcessing() | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS, //D3DCREATE_MANAGED
                         &pp, nullptr, &m_pD3DDevEx);
            }
            if (m_pD3DDevEx) {
                m_pD3DDev = m_pD3DDevEx;
                m_DisplayType = DisplayMode.Format;
            }
        }
        if (bTryToReset && m_pD3DDev && !m_pD3DDevEx) {
            if (FAILED(hr = m_pD3DDev->Reset(&pp))) {
                m_pD3DDev = nullptr;
            }
        }
        if (!m_pD3DDev) {
            m_pD3D->GetAdapterDisplayMode(m_CurrentAdapter, &d3ddm);
            m_ScreenSize.SetSize(d3ddm.Width, d3ddm.Height);
            m_refreshRate = d3ddm.RefreshRate;
            pp.BackBufferWidth = szDesktopSize.cx;
            pp.BackBufferHeight = szDesktopSize.cy;

            hr = m_pD3D->CreateDevice(
                     m_CurrentAdapter, D3DDEVTYPE_HAL, m_hWnd,
                     GetVertexProcessing() | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, //D3DCREATE_MANAGED
                     &pp, &m_pD3DDev);
            m_DisplayType = d3ddm.Format;
        }
        m_BackbufferType = pp.BackBufferFormat;
    }

    if (m_pD3DDev) {
        while (hr == D3DERR_DEVICELOST) {
            TRACE(_T("D3DERR_DEVICELOST. Trying to Reset.\n"));
            hr = m_pD3DDev->TestCooperativeLevel();
        }
        if (hr == D3DERR_DEVICENOTRESET) {
            TRACE(_T("D3DERR_DEVICENOTRESET\n"));
            hr = m_pD3DDev->Reset(&pp);
        }

        if (m_pD3DDevEx) {
            m_pD3DDevEx->SetGPUThreadPriority(7);
        }
    }

    if (FAILED(hr)) {
        _Error += L"CreateDevice failed\n";
        CStringW str;
        str.Format(L"Error code: 0x%X\n", hr);
        _Error += str;

        return hr;
    }

    ASSERT(m_pD3DDev);

    m_MainThreadId = GetCurrentThreadId();

    // Get the device caps
    ZeroMemory(&m_Caps, sizeof(m_Caps));
    m_pD3DDev->GetDeviceCaps(&m_Caps);

    // Initialize the rendering engine
    InitRenderingEngine();

    CComPtr<ISubPicProvider> pSubPicProvider;
    if (m_pSubPicQueue) {
        m_pSubPicQueue->GetSubPicProvider(&pSubPicProvider);
    }

    InitMaxSubtitleTextureSize(r.subPicQueueSettings.nMaxRes, m_bIsFullscreen ? m_ScreenSize : szDesktopSize);

    if (m_pAllocator) {
        m_pAllocator->ChangeDevice(m_pD3DDev);
    } else {
        m_pAllocator = DEBUG_NEW CDX9SubPicAllocator(m_pD3DDev, m_maxSubtitleTextureSize, false);
    }

    hr = S_OK;
    if (!m_pSubPicQueue) {
        CAutoLock cAutoLock(this);
        m_pSubPicQueue = r.subPicQueueSettings.nSize > 0
                         ? (ISubPicQueue*)DEBUG_NEW CSubPicQueue(r.subPicQueueSettings, m_pAllocator, &hr)
                         : (ISubPicQueue*)DEBUG_NEW CSubPicQueueNoThread(r.subPicQueueSettings, m_pAllocator, &hr);
    } else {
        m_pSubPicQueue->Invalidate();
    }

    if (FAILED(hr)) {
        _Error += L"m_pSubPicQueue failed\n";

        return hr;
    }

    if (pSubPicProvider) {
        m_pSubPicQueue->SetSubPicProvider(pSubPicProvider);
    }

    if (m_pD3DXCreateFont) {
        long MinSize = 1600;
        long CurrentSize = std::min(m_ScreenSize.cx, MinSize);
        double Scale = double(CurrentSize) / double(MinSize);
        m_TextScale = Scale;
        m_pD3DXCreateFont(m_pD3DDev,                   // D3D device
                          (int)(-24.0 * Scale),        // Height
                          (UINT)(-11.0 * Scale),       // Width
                          CurrentSize < 800 ? FW_NORMAL : FW_BOLD,  // Weight
                          0,                           // MipLevels, 0 = autogen mipmaps
                          FALSE,                       // Italic
                          DEFAULT_CHARSET,             // CharSet
                          OUT_DEFAULT_PRECIS,          // OutputPrecision
                          ANTIALIASED_QUALITY,         // Quality
                          FIXED_PITCH | FF_DONTCARE,   // PitchAndFamily
                          L"Lucida Console",           // pFaceName
                          &m_pFont);                   // ppFont
    }

    if (m_pD3DXCreateSprite) {
        m_pD3DXCreateSprite(m_pD3DDev,                 // D3D device
                            &m_pSprite);
    }

    if (m_pD3DXCreateLine) {
        m_pD3DXCreateLine(m_pD3DDev, &m_pLine);
    }

    m_LastAdapterCheck = rd->GetPerfCounter();

    return S_OK;
}

HRESULT CDX9AllocatorPresenter::AllocSurfaces()
{
    CAutoLock cAutoLock(this);
    CAutoLock cRenderLock(&m_RenderLock);

    return CreateVideoSurfaces();
}

void CDX9AllocatorPresenter::DeleteSurfaces()
{
    CAutoLock cAutoLock(this);
    CAutoLock cRenderLock(&m_RenderLock);

    FreeVideoSurfaces();
}

UINT CDX9AllocatorPresenter::GetAdapter(IDirect3D9* pD3D, bool bGetAdapter)
{
    if (m_hWnd == nullptr || pD3D == nullptr) {
        return D3DADAPTER_DEFAULT;
    }

    m_D3D9Device = _T("");

    const CRenderersSettings& r = GetRenderersSettings();
    if (bGetAdapter && (pD3D->GetAdapterCount() > 1) && !r.D3D9RenderDevice.IsEmpty()) {
        TCHAR strGUID[50];
        D3DADAPTER_IDENTIFIER9 adapterIdentifier;

        for (UINT adp = 0, num_adp = pD3D->GetAdapterCount(); adp < num_adp; ++adp) {
            if (pD3D->GetAdapterIdentifier(adp, 0, &adapterIdentifier) == S_OK) {
                if ((::StringFromGUID2(adapterIdentifier.DeviceIdentifier, strGUID, 50) > 0) && (r.D3D9RenderDevice == strGUID)) {
                    m_D3D9Device = adapterIdentifier.Description;
                    return  adp;
                }
            }
        }
    }

    HMONITOR hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor == nullptr) {
        return D3DADAPTER_DEFAULT;
    }

    for (UINT adp = 0, num_adp = pD3D->GetAdapterCount(); adp < num_adp; ++adp) {
        HMONITOR hAdpMon = pD3D->GetAdapterMonitor(adp);
        if (hAdpMon == hMonitor) {
            if (bGetAdapter) {
                D3DADAPTER_IDENTIFIER9 adapterIdentifier;
                if (pD3D->GetAdapterIdentifier(adp, 0, &adapterIdentifier) == S_OK) {
                    m_D3D9Device = adapterIdentifier.Description;
                }
            }
            return adp;
        }
    }

    return D3DADAPTER_DEFAULT;
}

DWORD CDX9AllocatorPresenter::GetVertexProcessing()
{
    HRESULT hr;
    D3DCAPS9 caps;

    hr = m_pD3D->GetDeviceCaps(m_CurrentAdapter, D3DDEVTYPE_HAL, &caps);
    if (FAILED(hr)) {
        return D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    }

    if ((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == 0 ||
            caps.VertexShaderVersion < D3DVS_VERSION(2, 0)) {
        return D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    }

    return D3DCREATE_HARDWARE_VERTEXPROCESSING;
}

// ISubPicAllocatorPresenter

STDMETHODIMP CDX9AllocatorPresenter::CreateRenderer(IUnknown** ppRenderer)
{
    return E_NOTIMPL;
}

void CDX9AllocatorPresenter::CalculateJitter(LONGLONG PerfCounter)
{
    // Calculate the jitter!
    LONGLONG llPerf = PerfCounter;
    if ((m_rtTimePerFrame != 0) && (labs((long)(llPerf - m_llLastPerf)) < m_rtTimePerFrame * 3)) {
        m_nNextJitter = (m_nNextJitter + 1) % NB_JITTER;
        m_pllJitter[m_nNextJitter] = llPerf - m_llLastPerf;

        m_MaxJitter = MINLONG64;
        m_MinJitter = MAXLONG64;

        // Calculate the real FPS
        LONGLONG llJitterSum = 0;
        LONGLONG llJitterSumAvg = 0;
        for (int i = 0; i < NB_JITTER; i++) {
            LONGLONG Jitter = m_pllJitter[i];
            llJitterSum += Jitter;
            llJitterSumAvg += Jitter;
        }
        double FrameTimeMean = double(llJitterSumAvg) / NB_JITTER;
        m_fJitterMean = FrameTimeMean;
        double DeviationSum = 0;
        for (int i = 0; i < NB_JITTER; i++) {
            LONGLONG DevInt = m_pllJitter[i] - (LONGLONG)FrameTimeMean;
            double Deviation = (double)DevInt;
            DeviationSum += Deviation * Deviation;
            m_MaxJitter = std::max(m_MaxJitter, DevInt);
            m_MinJitter = std::min(m_MinJitter, DevInt);
        }
        double StdDev = sqrt(DeviationSum / NB_JITTER);

        m_fJitterStdDev = StdDev;
        m_fAvrFps = 10000000.0 / (double(llJitterSum) / NB_JITTER);
    }

    m_llLastPerf = llPerf;
}

bool CDX9AllocatorPresenter::GetVBlank(int& _ScanLine, int& _bInVBlank, bool _bMeasureTime)
{
    LONGLONG llPerf = 0;
    if (_bMeasureTime) {
        llPerf = GetRenderersData()->GetPerfCounter();
    }

    int ScanLine = 0;
    _ScanLine = 0;
    _bInVBlank = 0;

    if (m_pDirectDraw) {
        DWORD ScanLineGet = 0;
        m_pDirectDraw->GetScanLine(&ScanLineGet);
        BOOL InVBlank;
        if (m_pDirectDraw->GetVerticalBlankStatus(&InVBlank) != S_OK) {
            return false;
        }
        ScanLine = ScanLineGet;
        _bInVBlank = InVBlank;
        if (InVBlank) {
            ScanLine = 0;
        }
    } else {
        D3DRASTER_STATUS RasterStatus;
        if (m_pD3DDev->GetRasterStatus(0, &RasterStatus) != S_OK) {
            return false;
        }
        ScanLine = RasterStatus.ScanLine;
        _bInVBlank = RasterStatus.InVBlank;
    }
    if (_bMeasureTime) {
        m_VBlankMax = std::max(m_VBlankMax, ScanLine);
        if (ScanLine != 0 && !_bInVBlank) {
            m_VBlankMinCalc = std::min(m_VBlankMinCalc, ScanLine);
        }
        m_VBlankMin = m_VBlankMax - m_ScreenSize.cy;
    }
    if (_bInVBlank) {
        _ScanLine = 0;
    } else if (m_VBlankMin != 300000) {
        _ScanLine = ScanLine - m_VBlankMin;
    } else {
        _ScanLine = ScanLine;
    }

    if (_bMeasureTime) {
        LONGLONG Time = GetRenderersData()->GetPerfCounter() - llPerf;
        if (Time > 5000000) { // 0.5 sec
            TRACE(_T("GetVBlank too long (%f sec)\n"), Time / 10000000.0);
        }
        m_RasterStatusWaitTimeMaxCalc = std::max(m_RasterStatusWaitTimeMaxCalc, Time);
    }

    return true;
}

bool CDX9AllocatorPresenter::WaitForVBlankRange(int& _RasterStart, int _RasterSize, bool _bWaitIfInside, bool _bNeedAccurate, bool _bMeasure, bool& _bTakenLock)
{
    if (_bMeasure) {
        m_RasterStatusWaitTimeMaxCalc = 0;
    }
    bool bWaited = false;
    int ScanLine = 0;
    int InVBlank = 0;
    LONGLONG llPerf = 0;
    if (_bMeasure) {
        llPerf = GetRenderersData()->GetPerfCounter();
    }
    GetVBlank(ScanLine, InVBlank, _bMeasure);
    if (_bMeasure) {
        m_VBlankStartWait = ScanLine;
    }

    static bool bOneWait = true;
    if (bOneWait && _bMeasure) {
        bOneWait = false;
        // If we are already in the wanted interval we need to wait until we aren't, this improves sync when for example you are playing 23.976 Hz material on a 24 Hz refresh rate
        int nInVBlank = 0;
        for (;;) {
            if (!GetVBlank(ScanLine, InVBlank, _bMeasure)) {
                break;
            }

            if (InVBlank && nInVBlank == 0) {
                nInVBlank = 1;
            } else if (!InVBlank && nInVBlank == 1) {
                nInVBlank = 2;
            } else if (InVBlank && nInVBlank == 2) {
                nInVBlank = 3;
            } else if (!InVBlank && nInVBlank == 3) {
                break;
            }
        }
    }
    if (_bWaitIfInside) {
        int ScanLineDiff = long(ScanLine) - _RasterStart;
        if (ScanLineDiff > m_ScreenSize.cy / 2) {
            ScanLineDiff -= m_ScreenSize.cy;
        } else if (ScanLineDiff < -m_ScreenSize.cy / 2) {
            ScanLineDiff += m_ScreenSize.cy;
        }

        if (ScanLineDiff >= 0 && ScanLineDiff <= _RasterSize) {
            bWaited = true;
            // If we are already in the wanted interval we need to wait until we aren't, this improves sync when for example you are playing 23.976 Hz material on a 24 Hz refresh rate
            int LastLineDiff = ScanLineDiff;
            for (;;) {
                if (!GetVBlank(ScanLine, InVBlank, _bMeasure)) {
                    break;
                }
                int ScanLineDiff = long(ScanLine) - _RasterStart;
                if (ScanLineDiff > m_ScreenSize.cy / 2) {
                    ScanLineDiff -= m_ScreenSize.cy;
                } else if (ScanLineDiff < -m_ScreenSize.cy / 2) {
                    ScanLineDiff += m_ScreenSize.cy;
                }
                if (!(ScanLineDiff >= 0 && ScanLineDiff <= _RasterSize) || (LastLineDiff < 0 && ScanLineDiff > 0)) {
                    break;
                }
                LastLineDiff = ScanLineDiff;
                Sleep(1); // Just sleep
            }
        }
    }
    double RefreshRate = GetRefreshRate();
    LONG ScanLines = GetScanLines();
    int MinRange = std::max(std::min(long(0.0015 * ScanLines * RefreshRate + 0.5), ScanLines / 3l), 5l); // 1.5 ms or max 33 % of Time
    int NoSleepStart = _RasterStart - MinRange;
    int NoSleepRange = MinRange;
    if (NoSleepStart < 0) {
        NoSleepStart += m_ScreenSize.cy;
    }

    int MinRange2 = std::max(std::min(long(0.0050 * ScanLines * RefreshRate + 0.5), ScanLines / 3l), 5l); // 5 ms or max 33 % of Time
    int D3DDevLockStart = _RasterStart - MinRange2;
    int D3DDevLockRange = MinRange2;
    if (D3DDevLockStart < 0) {
        D3DDevLockStart += m_ScreenSize.cy;
    }

    int ScanLineDiff = ScanLine - _RasterStart;
    if (ScanLineDiff > m_ScreenSize.cy / 2) {
        ScanLineDiff -= m_ScreenSize.cy;
    } else if (ScanLineDiff < -m_ScreenSize.cy / 2) {
        ScanLineDiff += m_ScreenSize.cy;
    }
    int LastLineDiff = ScanLineDiff;


    int ScanLineDiffSleep = long(ScanLine) - NoSleepStart;
    if (ScanLineDiffSleep > m_ScreenSize.cy / 2) {
        ScanLineDiffSleep -= m_ScreenSize.cy;
    } else if (ScanLineDiffSleep < -m_ScreenSize.cy / 2) {
        ScanLineDiffSleep += m_ScreenSize.cy;
    }
    int LastLineDiffSleep = ScanLineDiffSleep;


    int ScanLineDiffLock = long(ScanLine) - D3DDevLockStart;
    if (ScanLineDiffLock > m_ScreenSize.cy / 2) {
        ScanLineDiffLock -= m_ScreenSize.cy;
    } else if (ScanLineDiffLock < -m_ScreenSize.cy / 2) {
        ScanLineDiffLock += m_ScreenSize.cy;
    }
    int LastLineDiffLock = ScanLineDiffLock;

    LONGLONG llPerfLock = 0;

    for (;;) {
        if (!GetVBlank(ScanLine, InVBlank, _bMeasure)) {
            break;
        }
        int ScanLineDiff = long(ScanLine) - _RasterStart;
        if (ScanLineDiff > m_ScreenSize.cy / 2) {
            ScanLineDiff -= m_ScreenSize.cy;
        } else if (ScanLineDiff < -m_ScreenSize.cy / 2) {
            ScanLineDiff += m_ScreenSize.cy;
        }
        if ((ScanLineDiff >= 0 && ScanLineDiff <= _RasterSize) || (LastLineDiff < 0 && ScanLineDiff > 0)) {
            break;
        }

        LastLineDiff = ScanLineDiff;

        bWaited = true;

        int ScanLineDiffLock = long(ScanLine) - D3DDevLockStart;
        if (ScanLineDiffLock > m_ScreenSize.cy / 2) {
            ScanLineDiffLock -= m_ScreenSize.cy;
        } else if (ScanLineDiffLock < -m_ScreenSize.cy / 2) {
            ScanLineDiffLock += m_ScreenSize.cy;
        }

        if (((ScanLineDiffLock >= 0 && ScanLineDiffLock <= D3DDevLockRange) || (LastLineDiffLock < 0 && ScanLineDiffLock > 0))) {
            if (!_bTakenLock && _bMeasure) {
                _bTakenLock = true;
                llPerfLock = GetRenderersData()->GetPerfCounter();
                LockD3DDevice();
            }
        }
        LastLineDiffLock = ScanLineDiffLock;


        int ScanLineDiffSleep = long(ScanLine) - NoSleepStart;
        if (ScanLineDiffSleep > m_ScreenSize.cy / 2) {
            ScanLineDiffSleep -= m_ScreenSize.cy;
        } else if (ScanLineDiffSleep < -m_ScreenSize.cy / 2) {
            ScanLineDiffSleep += m_ScreenSize.cy;
        }

        if (!((ScanLineDiffSleep >= 0 && ScanLineDiffSleep <= NoSleepRange) || (LastLineDiffSleep < 0 && ScanLineDiffSleep > 0)) || !_bNeedAccurate) {
            //TRACE(_T("%d\n"), RasterStatus.ScanLine);
            Sleep(1); // Don't sleep for the last 1.5 ms scan lines, so we get maximum precision
        }
        LastLineDiffSleep = ScanLineDiffSleep;
    }
    _RasterStart = ScanLine;
    if (_bMeasure) {
        m_VBlankEndWait = ScanLine;
        m_VBlankWaitTime = GetRenderersData()->GetPerfCounter() - llPerf;

        if (_bTakenLock) {
            m_VBlankLockTime = GetRenderersData()->GetPerfCounter() - llPerfLock;
        } else {
            m_VBlankLockTime = 0;
        }

        m_RasterStatusWaitTime = m_RasterStatusWaitTimeMaxCalc;
        m_RasterStatusWaitTimeMin = std::min(m_RasterStatusWaitTimeMin, m_RasterStatusWaitTime);
        m_RasterStatusWaitTimeMax = std::max(m_RasterStatusWaitTimeMax, m_RasterStatusWaitTime);
    }

    return bWaited;
}

int CDX9AllocatorPresenter::GetVBlackPos()
{
    const CRenderersSettings& r = GetRenderersSettings();
    BOOL bCompositionEnabled = m_bCompositionEnabled;

    int WaitRange = std::max(m_ScreenSize.cy / 40l, 5l);
    if (!bCompositionEnabled) {
        if (m_bAlternativeVSync) {
            return r.m_AdvRendSets.iVMR9VSyncOffset;
        } else {
            int MinRange = std::max(std::min(long(0.005 * m_ScreenSize.cy * GetRefreshRate() + 0.5), m_ScreenSize.cy / 3l), 5l); // 5  ms or max 33 % of Time
            int WaitFor = m_ScreenSize.cy - (MinRange + WaitRange);
            return WaitFor;
        }
    } else {
        int WaitFor = m_ScreenSize.cy / 2;
        return WaitFor;
    }
}

bool CDX9AllocatorPresenter::WaitForVBlank(bool& _Waited, bool& _bTakenLock)
{
    const CRenderersSettings& r = GetRenderersSettings();
    if (!r.m_AdvRendSets.bVMR9VSync) {
        _Waited = true;
        m_VBlankWaitTime = 0;
        m_VBlankLockTime = 0;
        m_VBlankEndWait = 0;
        m_VBlankStartWait = 0;
        return true;
    }
    //_Waited = true;
    //return false;

    BOOL bCompositionEnabled = m_bCompositionEnabled;
    int WaitFor = GetVBlackPos();

    if (!bCompositionEnabled) {
        if (m_bAlternativeVSync) {
            _Waited = WaitForVBlankRange(WaitFor, 0, false, true, true, _bTakenLock);
            return false;
        } else {
            _Waited = WaitForVBlankRange(WaitFor, 0, false, r.m_AdvRendSets.bVMR9VSyncAccurate, true, _bTakenLock);
            return true;
        }
    } else {
        // Instead we wait for VBlack after the present, this seems to fix the stuttering problem. It'r also possible to fix by removing the Sleep above, but that isn't an option.
        WaitForVBlankRange(WaitFor, 0, false, r.m_AdvRendSets.bVMR9VSyncAccurate, true, _bTakenLock);

        return false;
    }
}

void CDX9AllocatorPresenter::UpdateAlphaBitmap()
{
    m_VMR9AlphaBitmapData.Free();

    if ((m_VMR9AlphaBitmap.dwFlags & VMRBITMAP_DISABLE) == 0) {
        HBITMAP hBitmap = (HBITMAP)GetCurrentObject(m_VMR9AlphaBitmap.hdc, OBJ_BITMAP);
        if (!hBitmap) {
            return;
        }
        DIBSECTION info;
        ZeroMemory(&info, sizeof(DIBSECTION));
        if (!::GetObject(hBitmap, sizeof(DIBSECTION), &info)) {
            return;
        }

        m_VMR9AlphaBitmapRect = CRect(0, 0, info.dsBm.bmWidth, info.dsBm.bmHeight);
        m_VMR9AlphaBitmapWidthBytes = info.dsBm.bmWidthBytes;

        if (m_VMR9AlphaBitmapData.Allocate(info.dsBm.bmWidthBytes * info.dsBm.bmHeight)) {
            memcpy((BYTE*)m_VMR9AlphaBitmapData, info.dsBm.bmBits, info.dsBm.bmWidthBytes * info.dsBm.bmHeight);
        }
    }
}

STDMETHODIMP_(bool) CDX9AllocatorPresenter::Paint(bool bAll)
{
    if (m_bPendingResetDevice) {
        SendResetRequest();
        return false;
    }

    const CRenderersSettings& r = GetRenderersSettings();

    //TRACE(_T("Thread: %d\n"), (LONG)((CRITICAL_SECTION &)m_RenderLock).OwningThread);

#if 0
    if (TryEnterCriticalSection(&(CRITICAL_SECTION&)(*((CCritSec*)this)))) {
        LeaveCriticalSection((&(CRITICAL_SECTION&)(*((CCritSec*)this))));
    } else {
        __debugbreak();
    }
#endif

    CRenderersData* rd = GetRenderersData();

    LONGLONG StartPaint = rd->GetPerfCounter();
    CAutoLock cRenderLock(&m_RenderLock);

    if (m_windowRect.right <= m_windowRect.left || m_windowRect.bottom <= m_windowRect.top
            || m_nativeVideoSize.cx <= 0 || m_nativeVideoSize.cy <= 0
            || !m_pVideoSurface[m_nCurSurface]) {
        if (m_OrderedPaint) {
            --m_OrderedPaint;
        } else {
            //TRACE(_T("UNORDERED PAINT!!!!!!\n"));
        }


        return false;
    }

    HRESULT hr;

    m_pD3DDev->BeginScene();

    CComPtr<IDirect3DSurface9> pBackBuffer;
    m_pD3DDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

    // Clear the backbuffer
    m_pD3DDev->SetRenderTarget(0, pBackBuffer);
    hr = m_pD3DDev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);

    CRect rSrcVid(CPoint(0, 0), GetVisibleVideoSize());
    CRect rDstVid(m_videoRect);

    CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
    CRect rDstPri(rSrcPri);

    // Render the current video frame
    hr = RenderVideo(pBackBuffer, rSrcVid, rDstVid);

    if (FAILED(hr)) {
        if (m_RenderingPath == RENDERING_PATH_STRETCHRECT) {
            // Support ffdshow queueing
            // m_pD3DDev->StretchRect may fail if ffdshow is using queue output samples.
            // Here we don't want to show the black buffer.
            if (m_OrderedPaint) {
                --m_OrderedPaint;
            } else {
                //TRACE(_T("UNORDERED PAINT!!!!!!\n"));
            }

            return false;
        }
    }

    // paint the text on the backbuffer
    AlphaBltSubPic(rDstPri, rDstVid);

    // Casimir666 : show OSD
    if (m_VMR9AlphaBitmap.dwFlags & VMRBITMAP_UPDATE) {
        CAutoLock BitMapLock(&m_VMR9AlphaBitmapLock);
        CRect rcSrc(m_VMR9AlphaBitmap.rSrc);
        m_pOSDTexture = nullptr;
        m_pOSDSurface = nullptr;
        if ((m_VMR9AlphaBitmap.dwFlags & VMRBITMAP_DISABLE) == 0 && (BYTE*)m_VMR9AlphaBitmapData) {
            if ((m_pD3DXLoadSurfaceFromMemory != nullptr) &&
                    SUCCEEDED(hr = m_pD3DDev->CreateTexture(rcSrc.Width(), rcSrc.Height(), 1,
                                                            D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                                            D3DPOOL_DEFAULT, &m_pOSDTexture, nullptr))) {
                if (SUCCEEDED(hr = m_pOSDTexture->GetSurfaceLevel(0, &m_pOSDSurface))) {
                    hr = m_pD3DXLoadSurfaceFromMemory(m_pOSDSurface,
                                                      nullptr,
                                                      nullptr,
                                                      (BYTE*)m_VMR9AlphaBitmapData,
                                                      D3DFMT_A8R8G8B8,
                                                      m_VMR9AlphaBitmapWidthBytes,
                                                      nullptr,
                                                      &m_VMR9AlphaBitmapRect,
                                                      D3DX_FILTER_NONE,
                                                      m_VMR9AlphaBitmap.clrSrcKey);
                }
                if (FAILED(hr)) {
                    m_pOSDTexture = nullptr;
                    m_pOSDSurface = nullptr;
                }
            }
        }
        m_VMR9AlphaBitmap.dwFlags ^= VMRBITMAP_UPDATE;

    }

    if (rd->m_bResetStats) {
        ResetStats();
        rd->m_bResetStats = false;
    }

    if (rd->m_iDisplayStats) {
        DrawStats();
    }

    if (m_pOSDTexture) {
        AlphaBlt(rSrcPri, rDstPri, m_pOSDTexture);
    }

    m_pD3DDev->EndScene();

    BOOL bCompositionEnabled = m_bCompositionEnabled;

    bool bDoVSyncInPresent = (!bCompositionEnabled && !m_bAlternativeVSync) || !r.m_AdvRendSets.bVMR9VSync;

    CComPtr<IDirect3DQuery9> pEventQuery;

    m_pD3DDev->CreateQuery(D3DQUERYTYPE_EVENT, &pEventQuery);
    if (pEventQuery) {
        pEventQuery->Issue(D3DISSUE_END);
    }

    if (r.m_AdvRendSets.bVMRFlushGPUBeforeVSync && pEventQuery) {
        LONGLONG llPerf = rd->GetPerfCounter();
        BOOL Data;
        //Sleep(5);
        LONGLONG FlushStartTime = rd->GetPerfCounter();
        while (S_FALSE == pEventQuery->GetData(&Data, sizeof(Data), D3DGETDATA_FLUSH)) {
            if (!r.m_AdvRendSets.bVMRFlushGPUWait) {
                break;
            }
            Sleep(1);
            if (rd->GetPerfCounter() - FlushStartTime > 500000) {
                break;    // timeout after 50 ms
            }
        }
        if (r.m_AdvRendSets.bVMRFlushGPUWait) {
            m_WaitForGPUTime = rd->GetPerfCounter() - llPerf;
        } else {
            m_WaitForGPUTime = 0;
        }
    } else {
        m_WaitForGPUTime = 0;
    }

    if (bAll) {
        m_PaintTime = (rd->GetPerfCounter() - StartPaint);
        m_PaintTimeMin = std::min(m_PaintTimeMin, m_PaintTime);
        m_PaintTimeMax = std::max(m_PaintTimeMax, m_PaintTime);
    }

    bool bWaited = false;
    bool bTakenLock = false;
    if (bAll) {
        // Only sync to refresh when redrawing all
        bool bTest = WaitForVBlank(bWaited, bTakenLock);
        ASSERT(bTest == bDoVSyncInPresent);
        if (!bDoVSyncInPresent) {
            LONGLONG Time = rd->GetPerfCounter();
            OnVBlankFinished(bAll, Time);
            if (!m_bIsEVR || m_OrderedPaint) {
                CalculateJitter(Time);
            }
        }
    }

    // Create a device pointer m_pd3dDevice

    // Create a query object
    {
        CComPtr<IDirect3DQuery9> pEventQuery;
        m_pD3DDev->CreateQuery(D3DQUERYTYPE_EVENT, &pEventQuery);

        LONGLONG llPerf = rd->GetPerfCounter();
        CRect presentationSrcRect(rDstPri), presentationDestRect(m_windowRect);
        // PresentEx() / Present() performs the clipping
        // TODO: fix the race and uncomment the assert
        //ASSERT(presentationSrcRect.Size() == presentationDestRect.Size());
        if (m_pD3DDevEx) {
            if (m_bIsFullscreen) {
                hr = m_pD3DDevEx->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
            } else {
                hr = m_pD3DDevEx->PresentEx(presentationSrcRect, presentationDestRect, nullptr, nullptr, 0);
            }
        } else {
            if (m_bIsFullscreen) {
                hr = m_pD3DDev->Present(nullptr, nullptr, nullptr, nullptr);
            } else {
                hr = m_pD3DDev->Present(presentationSrcRect, presentationDestRect, nullptr, nullptr);
            }
        }
        // Issue an End event
        if (pEventQuery) {
            pEventQuery->Issue(D3DISSUE_END);
        }

        BOOL Data;

        if (r.m_AdvRendSets.bVMRFlushGPUAfterPresent && pEventQuery) {
            LONGLONG FlushStartTime = rd->GetPerfCounter();
            while (S_FALSE == pEventQuery->GetData(&Data, sizeof(Data), D3DGETDATA_FLUSH)) {
                if (!r.m_AdvRendSets.bVMRFlushGPUWait) {
                    break;
                }
                if (rd->GetPerfCounter() - FlushStartTime > 500000) {
                    break;    // timeout after 50 ms
                }
            }
        }

        int ScanLine;
        int bInVBlank;
        GetVBlank(ScanLine, bInVBlank, false);

        if (bAll && (!m_bIsEVR || m_OrderedPaint)) {
            m_VBlankEndPresent = ScanLine;
        }

        while (ScanLine == 0 || bInVBlank) {
            GetVBlank(ScanLine, bInVBlank, false);

        }
        m_VBlankStartMeasureTime = rd->GetPerfCounter();
        m_VBlankStartMeasure = ScanLine;

        if (bAll && bDoVSyncInPresent) {
            m_PresentWaitTime = rd->GetPerfCounter() - llPerf;
            m_PresentWaitTimeMin = std::min(m_PresentWaitTimeMin, m_PresentWaitTime);
            m_PresentWaitTimeMax = std::max(m_PresentWaitTimeMax, m_PresentWaitTime);
        } else {
            m_PresentWaitTime = 0;
            m_PresentWaitTimeMin = std::min(m_PresentWaitTimeMin, m_PresentWaitTime);
            m_PresentWaitTimeMax = std::max(m_PresentWaitTimeMax, m_PresentWaitTime);
        }
    }

    if (bDoVSyncInPresent) {
        LONGLONG Time = rd->GetPerfCounter();
        if (!m_bIsEVR || m_OrderedPaint) {
            CalculateJitter(Time);
        }
        OnVBlankFinished(bAll, Time);
    }

    if (bTakenLock) {
        UnlockD3DDevice();
    }

    /*if (!bWaited)
    {
        bWaited = true;
        WaitForVBlank(bWaited);
        TRACE(_T("Double VBlank\n"));
        ASSERT(bWaited);
        if (!bDoVSyncInPresent)
        {
            CalculateJitter();
            OnVBlankFinished(bAll);
        }
    }*/

    if (!m_bPendingResetDevice) {
        bool fResetDevice = false;

        if (hr == D3DERR_DEVICELOST && m_pD3DDev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            TRACE(_T("Reset Device: D3D Device Lost\n"));
            fResetDevice = true;
        }

        if (hr == S_PRESENT_MODE_CHANGED) {
            TRACE(_T("Reset Device: D3D Device mode changed\n"));
            fResetDevice = true;
        }

        if (SettingsNeedResetDevice()) {
            TRACE(_T("Reset Device: settings changed\n"));
            fResetDevice = true;
        }

        bCompositionEnabled = false;
        if (m_pDwmIsCompositionEnabled) {
            m_pDwmIsCompositionEnabled(&bCompositionEnabled);
        }
        if ((bCompositionEnabled != 0) != m_bCompositionEnabled) {
            if (m_bIsFullscreen) {
                m_bCompositionEnabled = (bCompositionEnabled != 0);
            } else {
                TRACE(_T("Reset Device: DWM composition changed\n"));
                fResetDevice = true;
            }
        }

        if (r.fResetDevice) {
            LONGLONG time = rd->GetPerfCounter();
            if (time > m_LastAdapterCheck + 20000000) { // check every 2 sec.
                m_LastAdapterCheck = time;
#ifdef _DEBUG
                D3DDEVICE_CREATION_PARAMETERS Parameters;
                if (SUCCEEDED(m_pD3DDev->GetCreationParameters(&Parameters))) {
                    ASSERT(Parameters.AdapterOrdinal == m_CurrentAdapter);
                }
#endif
                if (m_CurrentAdapter != GetAdapter(m_pD3D)) {
                    TRACE(_T("Reset Device: D3D adapter changed\n"));
                    fResetDevice = true;
                }
#ifdef _DEBUG
                else {
                    ASSERT(m_pD3D->GetAdapterMonitor(m_CurrentAdapter) == m_pD3D->GetAdapterMonitor(GetAdapter(m_pD3D)));
                }
#endif
            }
        }

        if (fResetDevice) {
            m_bPendingResetDevice = true;
            SendResetRequest();
        }
    }

    if (m_OrderedPaint) {
        --m_OrderedPaint;
    } else {
        //if (m_bIsEVR)
        //  TRACE(_T("UNORDERED PAINT!!!!!!\n"));
    }
    return true;
}

double CDX9AllocatorPresenter::GetFrameTime() const
{
    if (m_DetectedLock) {
        return m_DetectedFrameTime;
    }

    return m_rtTimePerFrame / 10000000.0;
}

double CDX9AllocatorPresenter::GetFrameRate() const
{
    if (m_DetectedLock) {
        return m_DetectedFrameRate;
    }

    return 10000000.0 / m_rtTimePerFrame;
}

void CDX9AllocatorPresenter::SendResetRequest()
{
    if (!m_bDeviceResetRequested) {
        m_bDeviceResetRequested = true;
        AfxGetApp()->m_pMainWnd->PostMessage(WM_RESET_DEVICE);
    }
}

STDMETHODIMP_(bool) CDX9AllocatorPresenter::ResetDevice()
{
    TRACE(_T("ResetDevice\n"));
    ASSERT(m_MainThreadId == GetCurrentThreadId());

    // In VMR-9 deleting the surfaces before we are told to is bad !
    // Can't comment out this because CDX9AllocatorPresenter is used by EVR Custom
    // Why is EVR using a presenter for DX9 anyway ?!
    DeleteSurfaces();

    HRESULT hr;
    CString Error;
    // TODO: Report error messages here

    // In VMR-9 'AllocSurfaces' call is redundant afaik because
    // 'CreateDevice' calls 'm_pIVMRSurfAllocNotify->ChangeD3DDevice' which in turn calls
    // 'CVMR9AllocatorPresenter::InitializeDevice' which calls 'AllocSurfaces'
    if (FAILED(hr = CreateDevice(Error)) || FAILED(hr = AllocSurfaces())) {
        // TODO: We should probably pause player
#ifdef _DEBUG
        Error += GetWindowsErrorMessage(hr, nullptr);
        TRACE(_T("D3D Reset Error\n%ws\n\n"), Error.GetBuffer());
#endif
        m_bDeviceResetRequested = false;
        return false;
    }
    OnResetDevice();
    m_bDeviceResetRequested = false;
    m_bPendingResetDevice = false;

    return true;
}

STDMETHODIMP_(bool) CDX9AllocatorPresenter::DisplayChange()
{
    m_bPendingResetDevice = true;
    SendResetRequest();
    return true;
}

void CDX9AllocatorPresenter::DrawText(const RECT& rc, const CString& strText, int _Priority)
{
    if (_Priority < 1) {
        return;
    }

    D3DXCOLOR Color1(1.0f, 0.2f, 0.2f, 1.0f);
    D3DXCOLOR Color0(0.0f, 0.0f, 0.0f, 1.0f);
    RECT Rect1 = rc;
    RECT Rect2 = rc;
    OffsetRect(&Rect2 , 2, 2);

    // Draw shadow
    m_pFont->DrawText(m_pSprite, strText, -1, &Rect2, DT_NOCLIP, Color0);
    // Draw text
    m_pFont->DrawText(m_pSprite, strText, -1, &Rect1, DT_NOCLIP, Color1);
}

void CDX9AllocatorPresenter::ResetStats()
{
    CRenderersData* rd = GetRenderersData();
    LONGLONG Time = rd->GetPerfCounter();

    m_PaintTime = 0;
    m_PaintTimeMin = 3000000000;
    m_PaintTimeMax = 0;

    m_RasterStatusWaitTime = 0;
    m_RasterStatusWaitTimeMin = 3000000000;
    m_RasterStatusWaitTimeMax = 0;

    m_MinSyncOffset = 0;
    m_MaxSyncOffset = 0;
    m_fSyncOffsetAvr = 0;
    m_fSyncOffsetStdDev = 0;

    CalculateJitter(Time);
}

void CDX9AllocatorPresenter::DrawStats()
{
    const CRenderersSettings& r = GetRenderersSettings();
    const CRenderersData* rd = GetRenderersData();
    int iDetailedStats = 2;

    switch (rd->m_iDisplayStats) {
        case 1:
            iDetailedStats = 2;
            break;
        case 2:
            iDetailedStats = 1;
            break;
        case 3:
            iDetailedStats = 0;
            break;
    }

    LONGLONG llMaxJitter = m_MaxJitter;
    LONGLONG llMinJitter = m_MinJitter;
    LONGLONG llMaxSyncOffset = m_MaxSyncOffset;
    LONGLONG llMinSyncOffset = m_MinSyncOffset;
    RECT rc = {40, 40, 0, 0 };
    if (m_pFont && m_pSprite) {
        m_pSprite->Begin(D3DXSPRITE_ALPHABLEND);
        CString strText;
        int TextHeight = int(25.0 * m_TextScale + 0.5);
        //strText.Format(L"Frame rate   : %7.03f   (%7.3f ms = %.03f, %s)   (%7.3f ms = %.03f%s)    Clock: %7.3f ms %+1.4f %%  %+1.9f  %+1.9f", m_fAvrFps, double(m_rtTimePerFrame) / 10000.0, 10000000.0 / (double)(m_rtTimePerFrame), m_bInterlaced ? L"I" : L"P", GetFrameTime() * 1000.0, GetFrameRate(), m_DetectedLock ? L" L" : L"", m_ClockDiff/10000.0, m_ModeratedTimeSpeed*100.0 - 100.0, m_ModeratedTimeSpeedDiff, m_ClockDiffCalc/10000.0);
        if (iDetailedStats > 1) {
            if (m_bIsEVR) {
                if (m_nFrameType != PICT_NONE) {
                    strText.Format(L"Frame rate   : %7.03f   (%7.3f ms = %.03f, %s)   (%7.3f ms = %.03f%s, %2.03f StdDev)  Clock: %1.4f %%", m_fAvrFps, double(m_rtTimePerFrame) / 10000.0, 10000000.0 / (double)(m_rtTimePerFrame), m_nFrameType == PICT_FRAME ? L"P" : L"I", GetFrameTime() * 1000.0, GetFrameRate(), m_DetectedLock ? L" L" : L"", m_DetectedFrameTimeStdDev / 10000.0, m_ModeratedTimeSpeed * 100.0);
                } else {
                    strText.Format(L"Frame rate   : %7.03f   (%7.3f ms = %.03f, %s)   (%7.3f ms = %.03f%s, %2.03f StdDev)  Clock: %1.4f %%", m_fAvrFps, double(m_rtTimePerFrame) / 10000.0, 10000000.0 / (double)(m_rtTimePerFrame), m_bInterlaced ? L"I" : L"P", GetFrameTime() * 1000.0, GetFrameRate(), m_DetectedLock ? L" L" : L"", m_DetectedFrameTimeStdDev / 10000.0, m_ModeratedTimeSpeed * 100.0);
                }
            } else {
                strText.Format(L"Frame rate   : %7.03f   (%7.3f ms = %.03f, %s)", m_fAvrFps, double(m_rtTimePerFrame) / 10000.0, 10000000.0 / (double)(m_rtTimePerFrame), m_bInterlaced ? L"I" : L"P");
            }
        }
        //strText.Format(L"Frame rate   : %7.03f   (%7.3f ms = %.03f, %s)   (%7.3f ms = %.03f%s, %2.03f StdDev)", m_fAvrFps, double(m_rtTimePerFrame) / 10000.0, 10000000.0 / (double)(m_rtTimePerFrame), m_bInterlaced ? L"I" : L"P", GetFrameTime() * 1000.0, GetFrameRate(), m_DetectedLock ? L" L" : L"", m_DetectedFrameTimeStdDev / 10000.0);
        else {
            strText.Format(L"Frame rate   : %7.03f   (%.03f%s)", m_fAvrFps, GetFrameRate(), m_DetectedLock ? L" L" : L"");
        }
        DrawText(rc, strText, 1);
        OffsetRect(&rc, 0, TextHeight);

        if (m_nFrameType != PICT_NONE) {
            strText.Format(L"Frame type   : %s", m_nFrameType == PICT_FRAME ? L"Progressive" : m_nFrameType == PICT_BOTTOM_FIELD ? L"Interlaced : Bottom field first" : L"Interlaced : Top field first");
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
        }

        if (iDetailedStats > 1) {
            strText = L"Settings     : ";

            if (m_bIsEVR) {
                strText += "EVR ";
            } else {
                strText += "VMR9 ";
            }

            if (m_bIsFullscreen) {
                strText += "FS ";
            }
            if (r.m_AdvRendSets.bVMR9FullscreenGUISupport) {
                strText += "FSGui ";
            }

            if (r.m_AdvRendSets.bVMRDisableDesktopComposition) {
                strText += "DisDC ";
            }

            if (m_bColorManagement) {
                strText += "ColorMan ";
            }

            if (r.m_AdvRendSets.bVMRFlushGPUBeforeVSync) {
                strText += "GPUFlushBV ";
            }
            if (r.m_AdvRendSets.bVMRFlushGPUAfterPresent) {
                strText += "GPUFlushAP ";
            }

            if (r.m_AdvRendSets.bVMRFlushGPUWait) {
                strText += "GPUFlushWt ";
            }

            if (r.m_AdvRendSets.bVMR9VSync) {
                strText += "VS ";
            }
            if (r.m_AdvRendSets.bVMR9AlterativeVSync) {
                strText += "AltVS ";
            }
            if (r.m_AdvRendSets.bVMR9VSyncAccurate) {
                strText += "AccVS ";
            }
            if (r.m_AdvRendSets.iVMR9VSyncOffset) {
                strText.AppendFormat(L"VSOfst(%d)", r.m_AdvRendSets.iVMR9VSyncOffset);
            }

            if (m_bFullFloatingPointProcessing) {
                strText += "FullFP ";
            }

            if (m_bHalfFloatingPointProcessing) {
                strText += "HalfFP ";
            }

            if (m_bIsEVR) {
                if (m_bHighColorResolution) {
                    strText += "10bitOut ";
                }
                if (m_bForceInputHighColorResolution) {
                    strText += "For10bitIn ";
                }
                if (r.m_AdvRendSets.bEVREnableFrameTimeCorrection) {
                    strText += "FTC ";
                }
                if (r.m_AdvRendSets.iEVROutputRange == 0) {
                    strText += "0-255 ";
                } else if (r.m_AdvRendSets.iEVROutputRange == 1) {
                    strText += "16-235 ";
                }
            }


            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);

            strText.Format(L"Formats      : Surface %s    Backbuffer %s    Display %s     Device %s      D3DExError: %s", GetD3DFormatStr(m_SurfaceType), GetD3DFormatStr(m_BackbufferType), GetD3DFormatStr(m_DisplayType), m_pD3DDevEx ? L"D3DDevEx" : L"D3DDev", m_D3DDevExError.GetString());
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);

            if (m_bIsEVR) {
                if (r.m_AdvRendSets.bVMR9VSync) {
                    strText.Format(L"Refresh rate : %.05f Hz    SL: %4d     (%3u Hz)      ", m_DetectedRefreshRate, int(m_DetectedScanlinesPerFrame + 0.5), m_refreshRate);
                } else {
                    strText.Format(L"Refresh rate : %3u Hz      ", m_refreshRate);
                }
                strText.AppendFormat(L"Last Duration: %10.6f      Corrected Frame Time: %s", double(m_LastFrameDuration) / 10000.0, m_bCorrectedFrameTime ? L"Yes" : L"No");
                DrawText(rc, strText, 1);
                OffsetRect(&rc, 0, TextHeight);
            }
        }

        if (m_bSyncStatsAvailable) {
            if (iDetailedStats > 1) {
                strText.Format(L"Sync offset  : Min = %+8.3f ms, Max = %+8.3f ms, StdDev = %7.3f ms, Avr = %7.3f ms, Mode = %d", (double(llMinSyncOffset) / 10000.0), (double(llMaxSyncOffset) / 10000.0), m_fSyncOffsetStdDev / 10000.0, m_fSyncOffsetAvr / 10000.0, m_VSyncMode);
            } else {
                strText.Format(L"Sync offset  : Mode = %d", m_VSyncMode);
            }
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
        }

        if (iDetailedStats > 1) {
            strText.Format(L"Jitter       : Min = %+8.3f ms, Max = %+8.3f ms, StdDev = %7.3f ms", (double(llMinJitter) / 10000.0), (double(llMaxJitter) / 10000.0), m_fJitterStdDev / 10000.0);
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
        }

        if (m_pAllocator && iDetailedStats > 1) {
            CDX9SubPicAllocator* pAlloc = (CDX9SubPicAllocator*)m_pAllocator.p;
            int nFree = 0;
            int nAlloc = 0;
            int nSubPic = 0;
            REFERENCE_TIME rtQueueStart = 0;
            REFERENCE_TIME rtQueueEnd = 0;

            if (m_pSubPicQueue) {
                REFERENCE_TIME rtQueueNow = 0;
                m_pSubPicQueue->GetStats(nSubPic, rtQueueNow, rtQueueStart, rtQueueEnd);
            }

            pAlloc->GetStats(nFree, nAlloc);
            strText.Format(L"Subtitles    : Free %d     Allocated %d     Buffered %d     QueueStart %7.3f     QueueEnd %7.3f",
                           nFree, nAlloc, nSubPic, (double(rtQueueStart) / 10000000.0), (double(rtQueueEnd) / 10000000.0));
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
        }

        if (iDetailedStats > 1) {
            if (m_VBlankEndPresent == -100000) {
                strText.Format(L"VBlank Wait  : Start %4d   End %4d   Wait %7.3f ms   Lock %7.3f ms   Offset %4d   Max %4d", m_VBlankStartWait, m_VBlankEndWait, (double(m_VBlankWaitTime) / 10000.0), (double(m_VBlankLockTime) / 10000.0), m_VBlankMin, m_VBlankMax - m_VBlankMin);
            } else {
                strText.Format(L"VBlank Wait  : Start %4d   End %4d   Wait %7.3f ms   Lock %7.3f ms   Offset %4d   Max %4d   EndPresent %4d", m_VBlankStartWait, m_VBlankEndWait, (double(m_VBlankWaitTime) / 10000.0), (double(m_VBlankLockTime) / 10000.0), m_VBlankMin, m_VBlankMax - m_VBlankMin, m_VBlankEndPresent);
            }
        } else {
            if (m_VBlankEndPresent == -100000) {
                strText.Format(L"VBlank Wait  : Start %4d   End %4d", m_VBlankStartWait, m_VBlankEndWait);
            } else {
                strText.Format(L"VBlank Wait  : Start %4d   End %4d   EP %4d", m_VBlankStartWait, m_VBlankEndWait, m_VBlankEndPresent);
            }
        }
        DrawText(rc, strText, 1);
        OffsetRect(&rc, 0, TextHeight);

        BOOL bCompositionEnabled = m_bCompositionEnabled;

        bool bDoVSyncInPresent = (!bCompositionEnabled && !m_bAlternativeVSync) || !r.m_AdvRendSets.bVMR9VSync;

        if (iDetailedStats > 1 && bDoVSyncInPresent) {
            strText.Format(L"Present Wait : Wait %7.3f ms   Min %7.3f ms   Max %7.3f ms", (double(m_PresentWaitTime) / 10000.0), (double(m_PresentWaitTimeMin) / 10000.0), (double(m_PresentWaitTimeMax) / 10000.0));
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
        }

        if (iDetailedStats > 1) {
            if (m_WaitForGPUTime) {
                strText.Format(L"Paint Time   : Draw %7.3f ms   Min %7.3f ms   Max %7.3f ms   GPU %7.3f ms", (double(m_PaintTime - m_WaitForGPUTime) / 10000.0), (double(m_PaintTimeMin) / 10000.0), (double(m_PaintTimeMax) / 10000.0), (double(m_WaitForGPUTime) / 10000.0));
            } else {
                strText.Format(L"Paint Time   : Draw %7.3f ms   Min %7.3f ms   Max %7.3f ms", (double(m_PaintTime - m_WaitForGPUTime) / 10000.0), (double(m_PaintTimeMin) / 10000.0), (double(m_PaintTimeMax) / 10000.0));
            }
        } else {
            if (m_WaitForGPUTime) {
                strText.Format(L"Paint Time   : Draw %7.3f ms   GPU %7.3f ms", (double(m_PaintTime - m_WaitForGPUTime) / 10000.0), (double(m_WaitForGPUTime) / 10000.0));
            } else {
                strText.Format(L"Paint Time   : Draw %7.3f ms", (double(m_PaintTime - m_WaitForGPUTime) / 10000.0));
            }
        }
        DrawText(rc, strText, 2);
        OffsetRect(&rc, 0, TextHeight);

        if (iDetailedStats > 1 && r.m_AdvRendSets.bVMR9VSync) {
            strText.Format(L"Raster Status: Wait %7.3f ms   Min %7.3f ms   Max %7.3f ms", (double(m_RasterStatusWaitTime) / 10000.0), (double(m_RasterStatusWaitTimeMin) / 10000.0), (double(m_RasterStatusWaitTimeMax) / 10000.0));
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
        }

        if (iDetailedStats > 1) {
            if (m_bIsEVR) {
                strText.Format(L"Buffering    : Buffered %3ld    Free %3d    Current Surface %3d", m_nUsedBuffer, m_nNbDXSurface - m_nUsedBuffer, m_nCurSurface);
            } else {
                strText.Format(L"Buffering    : VMR9Surfaces %3d   VMR9Surface %3d", m_nVMR9Surfaces, m_iVMR9Surface);
            }
        } else {
            strText.Format(L"Buffered     : %3ld", m_nUsedBuffer);
        }
        DrawText(rc, strText, 1);
        OffsetRect(&rc, 0, TextHeight);

        if (iDetailedStats > 1) {
            strText.Format(L"Video size   : %ld x %ld  (AR = %ld : %ld)", m_nativeVideoSize.cx, m_nativeVideoSize.cy, m_aspectRatio.cx, m_aspectRatio.cy);
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);
            if (m_pVideoTexture[0] || m_pVideoSurface[0]) {
                D3DSURFACE_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                if (m_pVideoTexture[0]) {
                    m_pVideoTexture[0]->GetLevelDesc(0, &desc);
                } else if (m_pVideoSurface[0]) {
                    m_pVideoSurface[0]->GetDesc(&desc);
                }

                if (desc.Width != (UINT)m_nativeVideoSize.cx || desc.Height != (UINT)m_nativeVideoSize.cy) {
                    strText.Format(L"Texture size : %u x %u", desc.Width, desc.Height);
                    DrawText(rc, strText, 1);
                    OffsetRect(&rc, 0, TextHeight);
                }
            }


            DrawText(rc, rd->m_strDXVAInfo, 1);
            OffsetRect(&rc, 0, TextHeight);

            strText.Format(L"DirectX SDK  : %u", rd->GetDXSdkRelease());
            DrawText(rc, strText, 1);
            OffsetRect(&rc, 0, TextHeight);

            if (!m_D3D9Device.IsEmpty()) {
                strText = "Render device: " + m_D3D9Device;
                DrawText(rc, strText, 1);
                OffsetRect(&rc, 0, TextHeight);
            }

            if (!m_Decoder.IsEmpty()) {
                strText = "Decoder      : " + m_Decoder;
                DrawText(rc, strText, 1);
                OffsetRect(&rc, 0, TextHeight);
            }

            for (int i = 0; i < 6; i++) {
                if (m_strStatsMsg[i][0]) {
                    DrawText(rc, m_strStatsMsg[i], 1);
                    OffsetRect(&rc, 0, TextHeight);
                }
            }
        }
        m_pSprite->End();
        OffsetRect(&rc, 0, TextHeight); // Extra "line feed"
    }

    if (m_pLine && iDetailedStats) {
        D3DXVECTOR2 Points[NB_JITTER];

        int ScaleX = 1;
        int ScaleY = 1;
        int DrawWidth = 625 * ScaleX + 50;
        int DrawHeight = 250 * ScaleY;
        int Alpha = 80;
        int StartX = m_windowRect.Width() - (DrawWidth + 20);
        int StartY = m_windowRect.Height() - (DrawHeight + 20);

        DrawRect(RGB(0, 0, 0), Alpha, CRect(StartX, StartY, StartX + DrawWidth, StartY + DrawHeight));
        // === Jitter Graduation
        //m_pLine->SetWidth(2.2);          // Width
        //m_pLine->SetAntialias(1);
        m_pLine->SetWidth(2.5);          // Width
        m_pLine->SetAntialias(1);
        //m_pLine->SetGLLines(1);
        m_pLine->Begin();

        for (int i = 10; i < 250 * ScaleY; i += 10 * ScaleY) {
            Points[0].x = (float)StartX;
            Points[0].y = (float)(StartY + i);
            Points[1].x = (float)(StartX + (((i - 10) % 40) ? 50 : 625 * ScaleX));
            Points[1].y = (float)(StartY + i);
            if (i == 130) {
                Points[1].x += 50;
            }
            m_pLine->Draw(Points, 2, D3DCOLOR_XRGB(100, 100, 255));
        }

        // === Jitter curve
        if (m_rtTimePerFrame) {
            for (int i = 0; i < NB_JITTER; i++) {
                int nIndex = (m_nNextJitter + 1 + i) % NB_JITTER;
                if (nIndex < 0) {
                    nIndex += NB_JITTER;
                }
                double Jitter = m_pllJitter[nIndex] - m_fJitterMean;
                Points[i].x  = (float)(StartX + (i * 5 * ScaleX + 5));
                Points[i].y  = (float)(StartY + ((Jitter * ScaleY) / 5000.0 + 125.0 * ScaleY));
            }
            m_pLine->Draw(Points, NB_JITTER, D3DCOLOR_XRGB(255, 100, 100));

            if (m_bSyncStatsAvailable) {
                for (int i = 0; i < NB_JITTER; i++) {
                    int nIndex = (m_nNextSyncOffset + 1 + i) % NB_JITTER;
                    if (nIndex < 0) {
                        nIndex += NB_JITTER;
                    }
                    Points[i].x = (float)(StartX + (i * 5 * ScaleX + 5));
                    Points[i].y = (float)(StartY + ((m_pllSyncOffset[nIndex] * ScaleY) / 5000 + 125 * ScaleY));
                }
                m_pLine->Draw(Points, NB_JITTER, D3DCOLOR_XRGB(100, 200, 100));
            }
        }
        m_pLine->End();
    }

    // === Text

}

STDMETHODIMP CDX9AllocatorPresenter::GetDIB(BYTE* lpDib, DWORD* size)
{
    CheckPointer(size, E_POINTER);

    // Keep a reference so that we can safely work on the surface
    // without having to lock everything
    CComPtr<IDirect3DSurface9> pVideoSurface;
    {
        CAutoLock cAutoLock(this);
        CheckPointer(m_pVideoSurface[m_nCurSurface], E_FAIL);
        pVideoSurface = m_pVideoSurface[m_nCurSurface];
    }

    HRESULT hr;

    D3DSURFACE_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    if (FAILED(hr = pVideoSurface->GetDesc(&desc))) {
        return hr;
    }

    DWORD required = sizeof(BITMAPINFOHEADER) + (desc.Width * desc.Height * 32 >> 3);
    if (!lpDib) {
        *size = required;
        return S_OK;
    }
    if (*size < required) {
        return E_OUTOFMEMORY;
    }
    *size = required;

    CComPtr<IDirect3DSurface9> pSurface;
    // Convert to 8-bit when using 10-bit or full/half processing modes
    if (desc.Format != D3DFMT_X8R8G8B8) {
        if (FAILED(hr = m_pD3DDev->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &pSurface, nullptr))
                || FAILED(hr = m_pD3DXLoadSurfaceFromSurface(pSurface, nullptr, nullptr, pVideoSurface, nullptr, nullptr, D3DX_DEFAULT, 0))) {
            return hr;
        }
    } else {
        pSurface = pVideoSurface;
    }
    D3DLOCKED_RECT r;
    if (FAILED(hr = pSurface->LockRect(&r, nullptr, D3DLOCK_READONLY))) {
        // If this fails, we try to use a surface allocated from the system memory
        CComPtr<IDirect3DSurface9> pInputSurface = pSurface;
        pSurface = nullptr;
        if (FAILED(hr = m_pD3DDev->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &pSurface, nullptr))
                || FAILED(hr = m_pD3DDev->GetRenderTargetData(pInputSurface, pSurface))
                || FAILED(hr = pSurface->LockRect(&r, nullptr, D3DLOCK_READONLY))) {
            return hr;
        }
    }

    BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)lpDib;
    ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = desc.Width;
    bih->biHeight = desc.Height;
    bih->biBitCount = 32;
    bih->biPlanes = 1;
    bih->biSizeImage = bih->biWidth * bih->biHeight * bih->biBitCount >> 3;

    BitBltFromRGBToRGB(bih->biWidth, bih->biHeight,
                       (BYTE*)(bih + 1), bih->biWidth * bih->biBitCount >> 3, bih->biBitCount,
                       (BYTE*)r.pBits + r.Pitch * (desc.Height - 1), -(int)r.Pitch, 32);

    pSurface->UnlockRect();

    return S_OK;
}

STDMETHODIMP CDX9AllocatorPresenter::SetPixelShader(LPCSTR pSrcData, LPCSTR pTarget)
{
    return SetPixelShader2(pSrcData, pTarget, false);
}

STDMETHODIMP CDX9AllocatorPresenter::SetPixelShader2(LPCSTR pSrcData, LPCSTR pTarget, bool bScreenSpace)
{
    CAutoLock cRenderLock(&m_RenderLock);

    return SetCustomPixelShader(pSrcData, pTarget, bScreenSpace);
}

STDMETHODIMP CDX9AllocatorPresenter::SetD3DFullscreen(bool fEnabled)
{
    m_bIsFullscreen = fEnabled;
    return S_OK;
}

STDMETHODIMP CDX9AllocatorPresenter::GetD3DFullscreen(bool* pfEnabled)
{
    CheckPointer(pfEnabled, E_POINTER);
    *pfEnabled = m_bIsFullscreen;
    return S_OK;
}
