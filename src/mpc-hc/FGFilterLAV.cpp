/*
 * (C) 2013-2014 see Authors.txt
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
#include <mpconfig.h>
#include "FGFilterLAV.h"
#include "MainFrm.h"
#include "DSUtil.h"
#include "FileVersionInfo.h"
#include "PathUtils.h"
#include "SysVersion.h"
#include "AllocatorCommon7.h"
#include "AllocatorCommon.h"
#include "SyncAllocatorPresenter.h"
#include "moreuuids.h"
#include "../filters/InternalPropertyPage.h"
#include "../filters/PinInfoWnd.h"

#define LAV_FILTERS_VERSION_MAJOR      0
#define LAV_FILTERS_VERSION_MINOR      62
#define LAV_FILTERS_VERSION_REVISION   0
#define LAV_FILTERS_VERSION_COMMIT_NUM 0
#define LAV_FILTERS_VERSION ((QWORD)LAV_FILTERS_VERSION_MAJOR << 48 | (QWORD)LAV_FILTERS_VERSION_MINOR << 32 | (QWORD)LAV_FILTERS_VERSION_REVISION << 16 | LAV_FILTERS_VERSION_COMMIT_NUM)

#define IDS_R_INTERNAL_LAVSPLITTER           IDS_R_INTERNAL_FILTERS  _T("\\LAVSplitter")
#define IDS_R_INTERNAL_LAVVIDEO              IDS_R_INTERNAL_FILTERS  _T("\\LAVVideo")
#define IDS_R_INTERNAL_LAVVIDEO_OUTPUTFORMAT IDS_R_INTERNAL_LAVVIDEO _T("\\OutputFormat")
#define IDS_R_INTERNAL_LAVVIDEO_HWACCEL      IDS_R_INTERNAL_LAVVIDEO _T("\\HWAccel")
#define IDS_R_INTERNAL_LAVAUDIO              IDS_R_INTERNAL_FILTERS  _T("\\LAVAudio")

#ifndef _WIN64
#define LAVFILTERS_DIR _T("LAVFilters\\")
#else
#define LAVFILTERS_DIR _T("LAVFilters64\\")
#endif

//
// CFGFilterLAV
//

CList<const IBaseFilter*> CFGFilterLAV::s_instances;

CFGFilterLAV::CFGFilterLAV(const CLSID& clsid, CString path, CStringW name, bool bAddLowMeritSuffix, UINT64 merit)
    : CFGFilterFile(clsid, path, name + (bAddLowMeritSuffix ? LowMeritSuffix : L""), merit)
{
}

CString CFGFilterLAV::GetFilterPath(LAVFILTER_TYPE filterType)
{
    // Default path
    CString filterPath = PathUtils::CombinePaths(PathUtils::GetProgramPath(), LAVFILTERS_DIR);
    CLSID filterCLSID;

    switch (filterType) {
        case SPLITTER:
        case SPLITTER_SOURCE:
            filterPath  = PathUtils::CombinePaths(filterPath, CFGFilterLAVSplitterBase::filename);
            filterCLSID = GUID_LAVSplitter;
            break;
        case VIDEO_DECODER:
            filterPath  = PathUtils::CombinePaths(filterPath, CFGFilterLAVVideo::filename);
            filterCLSID = GUID_LAVVideo;
            break;
        case AUDIO_DECODER:
            filterPath  = PathUtils::CombinePaths(filterPath, CFGFilterLAVAudio::filename);
            filterCLSID = GUID_LAVAudio;
            break;
        default:
            ASSERT(FALSE); // This should never happen
            break;
    }

#if ENABLE_LOAD_EXTERNAL_LAVF_AS_INTERNAL
    // Check that the filter's version is correct
    if (!CheckVersion(filterPath)) {
        // If not, check if a registered version of the filter is available.
        filterPath = ::GetFilterPath(filterCLSID);
        // and if it can be used
        if (!CheckVersion(filterPath)) {
            filterPath = _T("");
        }
    }
#endif

    return filterPath;
}

bool CFGFilterLAV::CheckVersion(CString filterPath)
{
    QWORD version = FileVersionInfo::GetFileVersionNum(filterPath);

    return (version >= LAV_FILTERS_VERSION);
}

CString CFGFilterLAV::GetVersion(LAVFILTER_TYPE filterType /*= INVALID*/)
{
    CStringList paths;

    if (filterType == INVALID) {
        paths.AddTail(GetFilterPath(SPLITTER));
        paths.AddTail(GetFilterPath(VIDEO_DECODER));
        paths.AddTail(GetFilterPath(AUDIO_DECODER));
    } else {
        paths.AddTail(GetFilterPath(filterType));
    }

    QWORD uiVersionMin = UINT64_MAX;
    QWORD uiVersionMax = 0ui64;
    CString strVersionMin, strVersionMax;
    POSITION pos = paths.GetHeadPosition();
    while (pos) {
        CString& path = paths.GetNext(pos);

        QWORD version = FileVersionInfo::GetFileVersionNum(path);
        if (version) {
            if (version < uiVersionMin) {
                uiVersionMin = version;
                strVersionMin = FileVersionInfo::FormatVersionString(version);
            }
            if (version > uiVersionMax) {
                uiVersionMax = version;
                strVersionMax = FileVersionInfo::FormatVersionString(version);
            }
        }
    }

    CString version;
    if (uiVersionMin != UINT64_MAX) {
        version = strVersionMin;
        if (uiVersionMax != uiVersionMin) {
            version.AppendFormat(_T(" - %s"), strVersionMax);
        }
    }

    return version;
}

CFGFilterLAV* CFGFilterLAV::CreateFilter(LAVFILTER_TYPE filterType, UINT64 merit /*= MERIT64_DO_USE*/, bool bAddLowMeritSuffix /*= false*/)
{
    CFGFilterLAV* filter = nullptr;

    CString filterPath = GetFilterPath(filterType);

    switch (filterType) {
        case SPLITTER:
            filter = DEBUG_NEW CFGFilterLAVSplitter(filterPath, merit, bAddLowMeritSuffix);
            break;
        case SPLITTER_SOURCE:
            filter = DEBUG_NEW CFGFilterLAVSplitterSource(filterPath, merit, bAddLowMeritSuffix);
            break;
        case VIDEO_DECODER:
            filter = DEBUG_NEW CFGFilterLAVVideo(filterPath, merit, bAddLowMeritSuffix);
            break;
        case AUDIO_DECODER:
            filter = DEBUG_NEW CFGFilterLAVAudio(filterPath, merit, bAddLowMeritSuffix);
            break;
        default:
            ASSERT(FALSE); // This should never happen
            break;
    }

    return filter;
}

bool CFGFilterLAV::IsInternalInstance(IBaseFilter* pBF, LAVFILTER_TYPE* pLAVFilterType /*= nullptr*/)
{
    bool bIsInternalInstance = (s_instances.Find(pBF) != nullptr);

    if (bIsInternalInstance && pLAVFilterType) {
        CLSID clsid;
        *pLAVFilterType = INVALID;

        if (SUCCEEDED(pBF->GetClassID(&clsid))) {
            if (clsid == GUID_LAVSplitter) {
                *pLAVFilterType = SPLITTER;
            } else if (clsid == GUID_LAVSplitterSource) {
                *pLAVFilterType = SPLITTER_SOURCE;
            } else if (clsid == GUID_LAVVideo) {
                *pLAVFilterType = VIDEO_DECODER;
            } else if (clsid == GUID_LAVAudio) {
                *pLAVFilterType = AUDIO_DECODER;
            }
        }
    }

    return bIsInternalInstance;
}

HRESULT CFGFilterLAV::PropertyPageCallback(IBaseFilter* pBF)
{
    CheckPointer(pBF, E_POINTER);

    CComPropertySheet ps(ResStr(IDS_PROPSHEET_PROPERTIES), AfxGetMyApp()->GetMainWnd());

    // Find out which internal filter we are opening the property page for
    CFGFilterLAV::LAVFILTER_TYPE LAVFilterType = CFGFilterLAV::INVALID;
    if (!CFGFilterLAV::IsInternalInstance(pBF, &LAVFilterType)) {
        return E_UNEXPECTED;
    }

    HRESULT hr = E_FAIL;
    if (CComQIPtr<ISpecifyPropertyPages> pSPP = pBF) {
        ps.AddPages(pSPP, (LAVFilterType != CFGFilterLAV::AUDIO_DECODER) ? 1 : 2);
    }

    CComPtr<IPropertyPage> pPP = DEBUG_NEW CInternalPropertyPageTempl<CPinInfoWnd>(nullptr, &hr);
    ps.AddPage(pPP, pBF);

    if (ps.GetPageCount() > 1) {
        ps.DoModal();

        if (CComQIPtr<ILAVFSettings> pLAVFSettings = pBF) {
            CFGFilterLAVSplitterBase::Settings settings;
            if (settings.GetSettings(pLAVFSettings)) { // Get current settings from LAVSplitter
                settings.SaveSettings(); // Save them to the registry/ini
            }
        } else if (CComQIPtr<ILAVVideoSettings> pLAVFSettings = pBF) {
            CFGFilterLAVVideo::Settings settings;
            if (settings.GetSettings(pLAVFSettings)) { // Get current settings from LAVVideo
                settings.SaveSettings(); // Save them to the registry/ini
            }
        } else if (CComQIPtr<ILAVAudioSettings> pLAVFSettings = pBF) {
            CFGFilterLAVAudio::Settings settings;
            if (settings.GetSettings(pLAVFSettings)) { // Get current settings from LAVAudio
                settings.SaveSettings(); // Save them to the registry/ini
            }
        }

        hr = S_OK;
    }

    return hr;
}

//
// CFGFilterLAVSplitterBase
//

const CString CFGFilterLAVSplitterBase::filename = _T("LAVSplitter.ax");

CFGFilterLAVSplitterBase::CFGFilterLAVSplitterBase(CString path, const CLSID& clsid, CStringW name, bool bAddLowMeritSuffix, UINT64 merit)
    : CFGFilterLAV(clsid, path, name, bAddLowMeritSuffix, merit)
{
}

HRESULT CFGFilterLAVSplitterBase::Create(IBaseFilter** ppBF, CInterfaceList<IUnknown, &IID_IUnknown>& pUnks)
{
    HRESULT hr = __super::Create(ppBF, pUnks);

    if (SUCCEEDED(hr)) {
        if (!CheckVersion(m_path)) {
            hr = E_FAIL;
        } else if (CComQIPtr<ILAVFSettings> pLAVFSettings = *ppBF) {
            // Take control over LAVSplitter's settings, the settings are reseted to default values
            hr = pLAVFSettings->SetRuntimeConfig(TRUE);

            if (SUCCEEDED(hr)) {
                Settings settings;
                if (settings.GetSettings(pLAVFSettings)) { // Get default settings from LAVSplitter
                    settings.LoadSettings(); // Load our current settings from registry/ini
                    settings.SetSettings(pLAVFSettings); // Set our settings in LAVSplitter
                }

                SetEnabledDisabledFormats(pLAVFSettings);

                // Keep track of LAVFilters instances in runtime mode
                s_instances.AddTail(*ppBF);
            }
        } else {
            hr = E_NOINTERFACE;
        }
    }

    return hr;
}

void CFGFilterLAVSplitterBase::SetEnabledDisabledFormats(CComQIPtr<ILAVFSettings> pLAVFSettings)
{
    // "*" is a special case and means all formats are enabled
    if (m_enabledFormats.IsEmpty() || m_enabledFormats.GetHead() != "*") {
        // We turn off all formats by default to ensure that we won't hijack other filters
        LPSTR* formats;
        UINT nFormats;
        if (SUCCEEDED(pLAVFSettings->GetFormats(&formats, &nFormats))) {
            for (UINT i = 0; i < nFormats; i++) {
                pLAVFSettings->SetFormatEnabled(formats[i], FALSE);
                // Free the memory immediately since we won't need it later
                CoTaskMemFree(formats[i]);
            }
            CoTaskMemFree(formats);
        }
        // We turn on only the formats specified explicitly
        POSITION pos = m_enabledFormats.GetHeadPosition();
        while (pos) {
            const CStringA& format = m_enabledFormats.GetNext(pos);
            pLAVFSettings->SetFormatEnabled(format, TRUE);
        }
    }

    // Explicitly disabled formats
    POSITION pos = m_disabledFormats.GetHeadPosition();
    while (pos) {
        const CStringA& format = m_disabledFormats.GetNext(pos);
        pLAVFSettings->SetFormatEnabled(format, FALSE);
    }
}

void CFGFilterLAVSplitterBase::ShowPropertyPages(CWnd* pParendWnd)
{
    CFGFilterLAV::ShowPropertyPages<SPLITTER, CFGFilterLAVSplitterBase, ILAVFSettings, 1>(pParendWnd);
}

void CFGFilterLAVSplitterBase::Settings::LoadSettings()
{
    CMPlayerCApp* pApp = AfxGetMyApp();
    ASSERT(pApp);

    bTrayIcon = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("TrayIcon"), bTrayIcon);

    prefAudioLangs = pApp->GetProfileString(IDS_R_INTERNAL_LAVSPLITTER, _T("prefAudioLangs"), prefAudioLangs.c_str());
    prefSubLangs = pApp->GetProfileString(IDS_R_INTERNAL_LAVSPLITTER, _T("prefSubLangs"), prefSubLangs.c_str());
    subtitleAdvanced = pApp->GetProfileString(IDS_R_INTERNAL_LAVSPLITTER, _T("subtitleAdvanced"), subtitleAdvanced.c_str());

    subtitleMode = (LAVSubtitleMode)pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("subtitleMode"), subtitleMode);

    bPGSForcedStream = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("PGSForcedStream"), bPGSForcedStream);

    bPGSOnlyForced = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("PGSOnlyForced"), bPGSOnlyForced);

    iVC1Mode = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("vc1TimestampMode"), iVC1Mode);

    bSubstreams = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("substreams"), bSubstreams);

    bMatroskaExternalSegments = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("MatroskaExternalSegments"), bMatroskaExternalSegments);

    bStreamSwitchRemoveAudio = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("StreamSwitchRemoveAudio"), bStreamSwitchRemoveAudio);

    bPreferHighQualityAudio = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("PreferHighQualityAudio"), bPreferHighQualityAudio);

    bImpairedAudio = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("ImpairedAudio"), bImpairedAudio);

    dwQueueMaxSize = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("QueueMaxSize"), dwQueueMaxSize);

    dwNetworkAnalysisDuration = pApp->GetProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("NetworkAnalysisDuration"), dwNetworkAnalysisDuration);
}

void CFGFilterLAVSplitterBase::Settings::SaveSettings()
{
    CMPlayerCApp* pApp = AfxGetMyApp();
    ASSERT(pApp);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("TrayIcon"), bTrayIcon);

    pApp->WriteProfileString(IDS_R_INTERNAL_LAVSPLITTER, _T("prefAudioLangs"), prefAudioLangs.c_str());
    pApp->WriteProfileString(IDS_R_INTERNAL_LAVSPLITTER, _T("prefSubLangs"), prefSubLangs.c_str());
    pApp->WriteProfileString(IDS_R_INTERNAL_LAVSPLITTER, _T("subtitleAdvanced"), subtitleAdvanced.c_str());

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("subtitleMode"), subtitleMode);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("PGSForcedStream"), bPGSForcedStream);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("PGSOnlyForced"), bPGSOnlyForced);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("vc1TimestampMode"), iVC1Mode);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("substreams"), bSubstreams);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("MatroskaExternalSegments"), bMatroskaExternalSegments);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("StreamSwitchRemoveAudio"), bStreamSwitchRemoveAudio);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("PreferHighQualityAudio"), bPreferHighQualityAudio);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("ImpairedAudio"), bImpairedAudio);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("QueueMaxSize"), dwQueueMaxSize);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVSPLITTER, _T("NetworkAnalysisDuration"), dwNetworkAnalysisDuration);
}

bool CFGFilterLAVSplitterBase::Settings::GetSettings(CComQIPtr<ILAVFSettings> pLAVFSettings)
{
    if (!pLAVFSettings) {
        return false;
    }

    bTrayIcon = pLAVFSettings->GetTrayIcon();

    HRESULT hr;
    LPWSTR lpwstr = nullptr;
    hr = pLAVFSettings->GetPreferredLanguages(&lpwstr);
    if (SUCCEEDED(hr) && lpwstr) {
        prefAudioLangs = lpwstr;
        CoTaskMemFree(lpwstr);
    }
    lpwstr = nullptr;
    hr = pLAVFSettings->GetPreferredSubtitleLanguages(&lpwstr);
    if (SUCCEEDED(hr) && lpwstr) {
        prefSubLangs = lpwstr;
        CoTaskMemFree(lpwstr);
    }
    lpwstr = nullptr;
    hr = pLAVFSettings->GetAdvancedSubtitleConfig(&lpwstr);
    if (SUCCEEDED(hr) && lpwstr) {
        subtitleAdvanced = lpwstr;
        CoTaskMemFree(lpwstr);
    }

    subtitleMode = pLAVFSettings->GetSubtitleMode();

    bPGSForcedStream = pLAVFSettings->GetPGSForcedStream();

    bPGSOnlyForced = pLAVFSettings->GetPGSOnlyForced();

    iVC1Mode = pLAVFSettings->GetVC1TimestampMode();

    bSubstreams = pLAVFSettings->GetSubstreamsEnabled();

    bMatroskaExternalSegments = pLAVFSettings->GetLoadMatroskaExternalSegments();

    bStreamSwitchRemoveAudio = pLAVFSettings->GetStreamSwitchRemoveAudio();

    bImpairedAudio = pLAVFSettings->GetUseAudioForHearingVisuallyImpaired();

    bPreferHighQualityAudio = pLAVFSettings->GetPreferHighQualityAudioStreams();

    dwQueueMaxSize = pLAVFSettings->GetMaxQueueMemSize();

    dwNetworkAnalysisDuration = pLAVFSettings->GetNetworkStreamAnalysisDuration();

    return true;
}

bool CFGFilterLAVSplitterBase::Settings::SetSettings(CComQIPtr<ILAVFSettings> pLAVFSettings)
{
    if (!pLAVFSettings) {
        return false;
    }

    pLAVFSettings->SetTrayIcon(bTrayIcon);

    pLAVFSettings->SetPreferredLanguages(prefAudioLangs.c_str());
    pLAVFSettings->SetPreferredSubtitleLanguages(prefSubLangs.c_str());
    pLAVFSettings->SetAdvancedSubtitleConfig(subtitleAdvanced.c_str());

    pLAVFSettings->SetSubtitleMode(subtitleMode);

    pLAVFSettings->SetPGSForcedStream(bPGSForcedStream);

    pLAVFSettings->SetPGSOnlyForced(bPGSOnlyForced);

    pLAVFSettings->SetVC1TimestampMode(iVC1Mode);

    pLAVFSettings->SetSubstreamsEnabled(bSubstreams);

    pLAVFSettings->SetLoadMatroskaExternalSegments(bMatroskaExternalSegments);

    pLAVFSettings->SetStreamSwitchRemoveAudio(bStreamSwitchRemoveAudio);

    pLAVFSettings->SetUseAudioForHearingVisuallyImpaired(bImpairedAudio);

    pLAVFSettings->SetPreferHighQualityAudioStreams(bPreferHighQualityAudio);

    pLAVFSettings->SetMaxQueueMemSize(dwQueueMaxSize);

    pLAVFSettings->SetNetworkStreamAnalysisDuration(dwNetworkAnalysisDuration);

    // Custom interface available only in patched build, will be removed after it's upstreamed
    if (CComQIPtr<ILAVFSettingsMPCHCCustom> pLAVFSettingsMPCHCCustom = pLAVFSettings) {
        pLAVFSettingsMPCHCCustom->SetPropertyPageCallback(PropertyPageCallback);
    }

    return true;
}

//
// CFGFilterLAVSplitter
//

CFGFilterLAVSplitter::CFGFilterLAVSplitter(CString path, UINT64 merit /*= MERIT64_DO_USE*/, bool bAddLowMeritSuffix /*= false*/)
    : CFGFilterLAVSplitterBase(path, GUID_LAVSplitter, L"LAV Splitter (internal)", bAddLowMeritSuffix, merit)
{
}

//
// CFGFilterLAVSplitterSource
//

CFGFilterLAVSplitterSource::CFGFilterLAVSplitterSource(CString path, UINT64 merit /*= MERIT64_DO_USE*/, bool bAddLowMeritSuffix /*= false*/)
    : CFGFilterLAVSplitterBase(path, GUID_LAVSplitterSource, L"LAV Splitter Source (internal)", bAddLowMeritSuffix, merit)
{
}

//
// CFGFilterLAVVideo
//

const CString CFGFilterLAVVideo::filename = _T("LAVVideo.ax");

CFGFilterLAVVideo::CFGFilterLAVVideo(CString path, UINT64 merit /*= MERIT64_DO_USE*/, bool bAddLowMeritSuffix /*= false*/)
    : CFGFilterLAV(GUID_LAVVideo, path, L"LAV Video Decoder (internal)", bAddLowMeritSuffix, merit)
{
}

HRESULT CFGFilterLAVVideo::Create(IBaseFilter** ppBF, CInterfaceList<IUnknown, &IID_IUnknown>& pUnks)
{
    HRESULT hr = __super::Create(ppBF, pUnks);

    if (SUCCEEDED(hr)) {
        if (!CheckVersion(m_path)) {
            hr = E_FAIL;
        } else if (CComQIPtr<ILAVVideoSettings> pLAVFSettings = *ppBF) {
            // Take control over LAVVideo's settings, the settings are reseted to default values
            hr = pLAVFSettings->SetRuntimeConfig(TRUE);

            if (SUCCEEDED(hr)) {
                Settings settings;
                if (settings.GetSettings(pLAVFSettings)) { // Get default settings from LAVVideo
                    settings.LoadSettings(); // Load our current settings from registry/ini
                    settings.SetSettings(pLAVFSettings); // Set our settings in LAVVideo
                }

                // Keep track of LAVFilters instances in runtime mode
                s_instances.AddTail(*ppBF);
            }
        } else {
            hr = E_NOINTERFACE;
        }
    }

    return hr;
}

void CFGFilterLAVVideo::ShowPropertyPages(CWnd* pParendWnd)
{
    CFGFilterLAV::ShowPropertyPages<VIDEO_DECODER, CFGFilterLAVVideo, ILAVVideoSettings, 1>(pParendWnd);
}

LPCTSTR CFGFilterLAVVideo::GetUserFriendlyDecoderName(const CString& decoderName)
{
    static std::pair<LPCTSTR, LPCTSTR> userFriendlyDecoderNames[] = {
        make_pair(_T("avcodec"), _T("FFmpeg")),
        make_pair(_T("dxva2n"), _T("DXVA2 Native")),
        make_pair(_T("dxva2cb"), _T("DXVA2 Copy-back")),
        make_pair(_T("cuvid"), _T("NVIDIA CUVID")),
        make_pair(_T("quicksync"), _T("Intel QuickSync"))
    };

    for (int i = 0; i < _countof(userFriendlyDecoderNames); i++) {
        if (decoderName == userFriendlyDecoderNames[i].first) {
            return userFriendlyDecoderNames[i].second;
        }
    }

    return _T("None");
}

static LPCTSTR pixFmtSettingsMap[LAVOutPixFmt_NB] = {
    _T("yv12"), _T("nv12"), _T("yuy2"), _T("uyvy"), _T("ayuv"), _T("p010"), _T("p210"), _T("y410"),
    _T("p016"), _T("p216"), _T("y416"), _T("rgb32"), _T("rgb24"), _T("v210"), _T("v410"), _T("yv16"),
    _T("yv24"), _T("rgb48")
};

void CFGFilterLAVVideo::Settings::LoadSettings()
{
    CMPlayerCApp* pApp = AfxGetMyApp();
    ASSERT(pApp);

    bTrayIcon = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("TrayIcon"), bTrayIcon);

    dwStreamAR = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("StreamAR"), dwStreamAR);

    dwNumThreads = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("NumThreads"), dwNumThreads);

    dwDeintFieldOrder = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("DeintFieldOrder"), dwDeintFieldOrder);

    deintMode = (LAVDeintMode)pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("DeintMode"), deintMode);

    dwRGBRange = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("RGBRange"), dwRGBRange);

    dwSWDeintMode = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("SWDeintMode"), dwSWDeintMode);

    dwSWDeintOutput = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("SWDeintOutput"), dwSWDeintOutput);

    dwDitherMode = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("DitherMode"), dwDitherMode);

    for (int i = 0; i < LAVOutPixFmt_NB; ++i) {
        bPixFmts[i] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_OUTPUTFORMAT, pixFmtSettingsMap[i], bPixFmts[i]);
    }
    // Force disable, for future use
    bPixFmts[LAVOutPixFmt_YV16] = FALSE;

    dwHWAccel = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWAccel"), -1);
    if (dwHWAccel == DWORD(-1)) {
        dwHWAccel = HWAccel_None; // Ensure that a valid state is selected if no HW acceleration is available

        // We enable by default DXVA2 native on Vista+ and CUVID on XP if an NVIDIA adapter is found
        if (SysVersion::IsVistaOrLater()) {
            dwHWAccel = HWAccel_DXVA2Native;
        } else {
            CComPtr<IDirect3D9> pD3D9 = Direct3DCreate9(D3D_SDK_VERSION);
            if (pD3D9) {
                D3DADAPTER_IDENTIFIER9 adapterIdentifier;

                for (UINT adp = 0, num_adp = pD3D9->GetAdapterCount(); adp < num_adp; ++adp) {
                    if (pD3D9->GetAdapterIdentifier(adp, 0, &adapterIdentifier) == S_OK
                            && adapterIdentifier.VendorId == 0x10DE) {
                        dwHWAccel = HWAccel_CUDA;
                        break;
                    }
                }
            }
        }
    }

    bHWFormats[HWCodec_H264] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("h264"), bHWFormats[HWCodec_H264]);

    bHWFormats[HWCodec_VC1] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("vc1"), bHWFormats[HWCodec_VC1]);

    bHWFormats[HWCodec_MPEG2] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("mpeg2"), bHWFormats[HWCodec_MPEG2]);

    bHWFormats[HWCodec_MPEG4] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("mpeg4"), bHWFormats[HWCodec_MPEG4]);

    bHWFormats[HWCodec_MPEG2DVD] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("dvd"), bHWFormats[HWCodec_MPEG2DVD]);

    bHWFormats[HWCodec_HEVC] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("hevc"), bHWFormats[HWCodec_HEVC]);

    dwHWAccelResFlags = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWResFlags"), dwHWAccelResFlags);

    dwHWDeintMode = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWDeintMode"), dwHWDeintMode);

    dwHWDeintOutput = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWDeintOutput"), dwHWDeintOutput);

    bHWDeintHQ = pApp->GetProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWDeintHQ"), bHWDeintHQ);
}

void CFGFilterLAVVideo::Settings::SaveSettings()
{
    CMPlayerCApp* pApp = AfxGetMyApp();
    ASSERT(pApp);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("TrayIcon"), bTrayIcon);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("StreamAR"), dwStreamAR);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("NumThreads"), dwNumThreads);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("DeintFieldOrder"), dwDeintFieldOrder);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("DeintMode"), deintMode);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("RGBRange"), dwRGBRange);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("SWDeintMode"), dwSWDeintMode);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("SWDeintOutput"), dwSWDeintOutput);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO, _T("DitherMode"), dwDitherMode);

    for (int i = 0; i < LAVOutPixFmt_NB; ++i) {
        pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_OUTPUTFORMAT, pixFmtSettingsMap[i], bPixFmts[i]);
    }

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWAccel"), dwHWAccel);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("h264"), bHWFormats[HWCodec_H264]);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("vc1"), bHWFormats[HWCodec_VC1]);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("mpeg2"), bHWFormats[HWCodec_MPEG2]);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("mpeg4"), bHWFormats[HWCodec_MPEG4]);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("dvd"), bHWFormats[HWCodec_MPEG2DVD]);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("hevc"), bHWFormats[HWCodec_HEVC]);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWResFlags"), dwHWAccelResFlags);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWDeintMode"), dwHWDeintMode);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWDeintOutput"), dwHWDeintOutput);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVVIDEO_HWACCEL, _T("HWDeintHQ"), bHWDeintHQ);
}

bool CFGFilterLAVVideo::Settings::GetSettings(CComQIPtr<ILAVVideoSettings> pLAVFSettings)
{
    if (!pLAVFSettings) {
        return false;
    }

    bTrayIcon = pLAVFSettings->GetTrayIcon();

    dwStreamAR = pLAVFSettings->GetStreamAR();

    dwNumThreads = pLAVFSettings->GetNumThreads();

    dwDeintFieldOrder = pLAVFSettings->GetDeintFieldOrder();

    deintMode = pLAVFSettings->GetDeinterlacingMode();

    dwRGBRange = pLAVFSettings->GetRGBOutputRange();

    dwSWDeintMode = pLAVFSettings->GetSWDeintMode();

    dwSWDeintOutput = pLAVFSettings->GetSWDeintOutput();

    dwDitherMode = pLAVFSettings->GetDitherMode();

    for (int i = 0; i < LAVOutPixFmt_NB; ++i) {
        bPixFmts[i] = pLAVFSettings->GetPixelFormat((LAVOutPixFmts)i);
    }

    dwHWAccel = pLAVFSettings->GetHWAccel();

    for (int i = 0; i < HWCodec_NB; ++i) {
        bHWFormats[i] = pLAVFSettings->GetHWAccelCodec((LAVVideoHWCodec)i);
    }

    dwHWAccelResFlags = pLAVFSettings->GetHWAccelResolutionFlags();

    dwHWDeintMode = pLAVFSettings->GetHWAccelDeintMode();

    dwHWDeintOutput = pLAVFSettings->GetHWAccelDeintOutput();

    bHWDeintHQ = pLAVFSettings->GetHWAccelDeintHQ();

    return true;
}

bool CFGFilterLAVVideo::Settings::SetSettings(CComQIPtr<ILAVVideoSettings> pLAVFSettings)
{
    if (!pLAVFSettings) {
        return false;
    }

    pLAVFSettings->SetTrayIcon(bTrayIcon);

    pLAVFSettings->SetStreamAR(dwStreamAR);

    pLAVFSettings->SetNumThreads(dwNumThreads);

    pLAVFSettings->SetDeintFieldOrder((LAVDeintFieldOrder)dwDeintFieldOrder);

    pLAVFSettings->SetDeinterlacingMode(deintMode);

    pLAVFSettings->SetRGBOutputRange(dwRGBRange);

    pLAVFSettings->SetSWDeintMode((LAVSWDeintModes)dwSWDeintMode);

    pLAVFSettings->SetSWDeintOutput((LAVDeintOutput)dwSWDeintOutput);

    pLAVFSettings->SetDitherMode((LAVDitherMode)dwDitherMode);

    for (int i = 0; i < LAVOutPixFmt_NB; ++i) {
        pLAVFSettings->SetPixelFormat((LAVOutPixFmts)i, bPixFmts[i]);
    }

    pLAVFSettings->SetHWAccel((LAVHWAccel)dwHWAccel);

    for (int i = 0; i < HWCodec_NB; ++i) {
        pLAVFSettings->SetHWAccelCodec((LAVVideoHWCodec)i, bHWFormats[i]);
    }

    pLAVFSettings->SetHWAccelResolutionFlags(dwHWAccelResFlags);

    pLAVFSettings->SetHWAccelDeintMode((LAVHWDeintModes)dwHWDeintMode);

    pLAVFSettings->SetHWAccelDeintOutput((LAVDeintOutput)dwHWDeintOutput);

    pLAVFSettings->SetHWAccelDeintHQ(bHWDeintHQ);

    // Force RV1/2 and v210/v410 enabled, the user can control it from our own options
    pLAVFSettings->SetFormatConfiguration(Codec_RV12, TRUE);
    pLAVFSettings->SetFormatConfiguration(Codec_v210, TRUE);
    // Enable Cinepack and QPEG so that they can be used in low-merit mode
    pLAVFSettings->SetFormatConfiguration(Codec_Cinepak, TRUE);
    pLAVFSettings->SetFormatConfiguration(Codec_QPEG, TRUE);

    // Custom interface available only in patched build, will be removed after it's upstreamed
    if (CComQIPtr<ILAVVideoSettingsMPCHCCustom> pLAVFSettingsMPCHCCustom = pLAVFSettings) {
        pLAVFSettingsMPCHCCustom->SetPropertyPageCallback(PropertyPageCallback);
    }

    return true;
}


//
// CFGFilterLAVAudio
//

const CString CFGFilterLAVAudio::filename = _T("LAVAudio.ax");

CFGFilterLAVAudio::CFGFilterLAVAudio(CString path, UINT64 merit /*= MERIT64_DO_USE*/, bool bAddLowMeritSuffix /*= false*/)
    : CFGFilterLAV(GUID_LAVAudio, path, L"LAV Audio Decoder (internal)", bAddLowMeritSuffix, merit)
{
}

HRESULT CFGFilterLAVAudio::Create(IBaseFilter** ppBF, CInterfaceList<IUnknown, &IID_IUnknown>& pUnks)
{
    HRESULT hr = __super::Create(ppBF, pUnks);

    if (SUCCEEDED(hr)) {
        if (!CheckVersion(m_path)) {
            hr = E_FAIL;
        } else if (CComQIPtr<ILAVAudioSettings> pLAVFSettings = *ppBF) {
            // Take control over LAVAudio's settings, the settings are reseted to default values
            hr = pLAVFSettings->SetRuntimeConfig(TRUE);

            if (SUCCEEDED(hr)) {
                Settings settings;
                if (settings.GetSettings(pLAVFSettings)) { // Get default settings from LAVAudio
                    settings.LoadSettings(); // Load our current settings from registry/ini
                    settings.SetSettings(pLAVFSettings); // Set our settings in LAVAudio
                }

                // Keep track of LAVFilters instances in runtime mode
                s_instances.AddTail(*ppBF);
            }
        } else {
            hr = E_NOINTERFACE;
        }
    }

    return hr;
}

void CFGFilterLAVAudio::ShowPropertyPages(CWnd* pParendWnd)
{
    CFGFilterLAV::ShowPropertyPages<AUDIO_DECODER, CFGFilterLAVAudio, ILAVAudioSettings, 2>(pParendWnd);
}

static LPCTSTR bitstreamingCodecs[Bitstream_NB] = {
    _T("ac3"), _T("eac3"), _T("truehd"), _T("dts"), _T("dtshd")
};

static LPCTSTR sampleFormats[SampleFormat_Bitstream] = {
    _T("s16"), _T("s24"), _T("s32"), _T("u8"), _T("fp32")
};

void CFGFilterLAVAudio::Settings::LoadSettings()
{
    CMPlayerCApp* pApp = AfxGetMyApp();
    ASSERT(pApp);

    bTrayIcon = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("TrayIcon"), bTrayIcon);

    bDRCEnabled = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("DRCEnabled"), bDRCEnabled);

    iDRCLevel = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("DRCLevel"), iDRCLevel);

    bDTSHDFraming = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("DTSHDFraming"), bDTSHDFraming);

    bAutoAVSync = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("AutoAVSync"), bAutoAVSync);

    bExpandMono = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("ExpandMono"), bExpandMono);

    bExpand61 = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("Expand61"), bExpand61);

    bOutputStandardLayout = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("OutputStandardLayout"), bOutputStandardLayout);

    bMixingEnabled = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("Mixing"), bMixingEnabled);

    dwMixingLayout = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingLayout"), dwMixingLayout);

    dwMixingFlags = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingFlags"), dwMixingFlags);

    dwMixingMode = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingMode"), dwMixingMode);

    dwMixingCenterLevel = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingCenterLevel"), dwMixingCenterLevel);

    dwMixingSurroundLevel = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingSurroundLevel"), dwMixingSurroundLevel);

    dwMixingLFELevel = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingLFELevel"), dwMixingLFELevel);

    bAudioDelayEnabled = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("AudioDelayEnabled"), bAudioDelayEnabled);

    iAudioDelay = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("AudioDelay"), iAudioDelay);

    for (int i = 0; i < Bitstream_NB; ++i) {
        CString key = CString(_T("Bitstreaming_")) + bitstreamingCodecs[i];
        bBitstream[i] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, key, bBitstream[i]);
    }

    for (int i = 0; i < SampleFormat_Bitstream; ++i) {
        CString key = CString(_T("SampleFormat_")) + sampleFormats[i];
        bSampleFormats[i] = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, key, bSampleFormats[i]);
    }

    bSampleConvertDither = pApp->GetProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("SampleConvertDither"), bSampleConvertDither);
}

void CFGFilterLAVAudio::Settings::SaveSettings()
{
    CMPlayerCApp* pApp = AfxGetMyApp();
    ASSERT(pApp);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("TrayIcon"), bTrayIcon);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("DRCEnabled"), bDRCEnabled);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("DRCLevel"), iDRCLevel);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("DTSHDFraming"), bDTSHDFraming);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("AutoAVSync"), bAutoAVSync);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("ExpandMono"), bExpandMono);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("Expand61"), bExpand61);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("OutputStandardLayout"), bOutputStandardLayout);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("Mixing"), bMixingEnabled);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingLayout"), dwMixingLayout);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingFlags"), dwMixingFlags);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingMode"), dwMixingMode);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingCenterLevel"), dwMixingCenterLevel);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingSurroundLevel"), dwMixingSurroundLevel);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("MixingLFELevel"), dwMixingLFELevel);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("AudioDelayEnabled"), bAudioDelayEnabled);

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("AudioDelay"), iAudioDelay);

    for (int i = 0; i < Bitstream_NB; ++i) {
        CString key = CString(_T("Bitstreaming_")) + bitstreamingCodecs[i];
        pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, key, bBitstream[i]);
    }

    for (int i = 0; i < SampleFormat_Bitstream; ++i) {
        CString key = CString(_T("SampleFormat_")) + sampleFormats[i];
        pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, key, bSampleFormats[i]);
    }

    pApp->WriteProfileInt(IDS_R_INTERNAL_LAVAUDIO, _T("SampleConvertDither"), bSampleConvertDither);
}

bool CFGFilterLAVAudio::Settings::GetSettings(CComQIPtr<ILAVAudioSettings> pLAVFSettings)
{
    if (!pLAVFSettings) {
        return false;
    }

    bTrayIcon = pLAVFSettings->GetTrayIcon();

    pLAVFSettings->GetDRC(&bDRCEnabled, &iDRCLevel);

    bDTSHDFraming = pLAVFSettings->GetDTSHDFraming();

    bAutoAVSync = pLAVFSettings->GetAutoAVSync();

    bExpandMono = pLAVFSettings->GetExpandMono();

    bExpand61 = pLAVFSettings->GetExpand61();

    bOutputStandardLayout = pLAVFSettings->GetOutputStandardLayout();

    bMixingEnabled = pLAVFSettings->GetMixingEnabled();

    dwMixingLayout = pLAVFSettings->GetMixingLayout();

    dwMixingFlags = pLAVFSettings->GetMixingFlags();

    dwMixingMode = pLAVFSettings->GetMixingMode();

    pLAVFSettings->GetMixingLevels(&dwMixingCenterLevel, &dwMixingSurroundLevel, &dwMixingLFELevel);

    pLAVFSettings->GetAudioDelay(&bAudioDelayEnabled, &iAudioDelay);

    for (int i = 0; i < Bitstream_NB; ++i) {
        bBitstream[i] = pLAVFSettings->GetBitstreamConfig((LAVBitstreamCodec)i);
    }

    for (int i = 0; i < SampleFormat_Bitstream; ++i) {
        bSampleFormats[i] = pLAVFSettings->GetSampleFormat((LAVAudioSampleFormat)i);
    }

    bSampleConvertDither = pLAVFSettings->GetSampleConvertDithering();

    return true;
}

bool CFGFilterLAVAudio::Settings::SetSettings(CComQIPtr<ILAVAudioSettings> pLAVFSettings)
{
    if (!pLAVFSettings) {
        return false;
    }

    pLAVFSettings->SetTrayIcon(bTrayIcon);

    pLAVFSettings->SetDRC(bDRCEnabled, iDRCLevel);

    pLAVFSettings->SetDTSHDFraming(bDTSHDFraming);

    pLAVFSettings->SetAutoAVSync(bAutoAVSync);

    pLAVFSettings->SetExpandMono(bExpandMono);

    pLAVFSettings->SetExpand61(bExpand61);

    pLAVFSettings->SetOutputStandardLayout(bOutputStandardLayout);

    pLAVFSettings->SetMixingEnabled(bMixingEnabled);

    pLAVFSettings->SetMixingLayout(dwMixingLayout);

    pLAVFSettings->SetMixingFlags(dwMixingFlags);

    pLAVFSettings->SetMixingMode((LAVAudioMixingMode)dwMixingMode);

    pLAVFSettings->SetMixingLevels(dwMixingCenterLevel, dwMixingSurroundLevel, dwMixingLFELevel);

    pLAVFSettings->SetAudioDelay(bAudioDelayEnabled, iAudioDelay);

    for (int i = 0; i < Bitstream_NB; ++i) {
        pLAVFSettings->SetBitstreamConfig((LAVBitstreamCodec)i, bBitstream[i]);
    }

    for (int i = 0; i < SampleFormat_Bitstream; ++i) {
        pLAVFSettings->SetSampleFormat((LAVAudioSampleFormat)i, bSampleFormats[i]);
    }

    pLAVFSettings->SetSampleConvertDithering(bSampleConvertDither);

    // The internal LAV Audio Decoder will not be registered to handle WMA formats
    // since the system decoder is preferred. However we can still enable those
    // formats internally so that they are used in low-merit mode.
    pLAVFSettings->SetFormatConfiguration(Codec_WMA2, TRUE);
    pLAVFSettings->SetFormatConfiguration(Codec_WMAPRO, TRUE);
    pLAVFSettings->SetFormatConfiguration(Codec_WMALL, TRUE);

    // Custom interface available only in patched build, will be removed after it's upstreamed
    if (CComQIPtr<ILAVAudioSettingsMPCHCCustom> pLAVFSettingsMPCHCCustom = pLAVFSettings) {
        pLAVFSettingsMPCHCCustom->SetPropertyPageCallback(PropertyPageCallback);
    }

    return true;
}
