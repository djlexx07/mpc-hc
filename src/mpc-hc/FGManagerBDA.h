/*
 * (C) 2009-2014 see Authors.txt
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

#include <bdatypes.h>
#include <bdamedia.h>
#include <bdaiface.h>
#include "FGManager.h"


class CDVBStream
{
public:
    CDVBStream()
        : m_pmt(0)
        , m_bFindExisting(false)
        , m_Name(L"")
        , m_nMsc(MEDIA_TRANSPORT_PACKET)
        , m_ulMappedPID(0) {
    }

    CDVBStream(LPWSTR strName, const AM_MEDIA_TYPE* pmt, bool bFindExisting = false, MEDIA_SAMPLE_CONTENT nMsc = MEDIA_ELEMENTARY_STREAM)
        : m_pmt(pmt)
        , m_bFindExisting(bFindExisting)
        , m_Name(strName)
        , m_nMsc(nMsc)
        , m_ulMappedPID(0) {
    }

    LPWSTR GetName() { return m_Name; };
    const AM_MEDIA_TYPE* GetMediaType() { return m_pmt; };
    bool GetFindExisting() const { return m_bFindExisting; };
    IBaseFilter* GetFilter() { return m_pFilter; };

    void SetPin(IPin* pPin) {
        CComPtr<IPin> pPinOut;
        PIN_INFO PinInfo;

        m_pMap = pPin;
        if (m_pMap &&
                SUCCEEDED(pPin->ConnectedTo(&pPinOut)) &&
                SUCCEEDED(pPinOut->QueryPinInfo(&PinInfo))) {
            m_pFilter.Attach(PinInfo.pFilter);
        }
    }

    HRESULT Map(ULONG ulPID) {
        CheckPointer(m_pMap, E_UNEXPECTED);
        ClearMaps();
        m_ulMappedPID = ulPID;
        return m_pMap->MapPID(1, &ulPID, m_nMsc);
    }

    HRESULT Unmap(ULONG ulPID) {
        CheckPointer(m_pMap, E_UNEXPECTED);
        m_ulMappedPID = 0;
        return m_pMap->UnmapPID(1, &ulPID);
    }

    ULONG GetMappedPID() const { return m_ulMappedPID; }

private:
    CComQIPtr<IMPEG2PIDMap> m_pMap;
    CComPtr<IBaseFilter>    m_pFilter;
    const AM_MEDIA_TYPE*    m_pmt;
    bool                    m_bFindExisting;
    LPWSTR                  m_Name;
    MEDIA_SAMPLE_CONTENT    m_nMsc;
    ULONG                   m_ulMappedPID;

    void ClearMaps() {
        CComPtr<IEnumPIDMap> pEnumMap;

        if (SUCCEEDED(m_pMap->EnumPIDMap(&pEnumMap))) {
            PID_MAP maps[8];
            ULONG   nbPids = 0;
            ZeroMemory(maps, sizeof(maps));

            if (pEnumMap->Next(_countof(maps), maps, &nbPids) == S_OK) {
                for (ULONG i = 0; i < nbPids; i++) {
                    ULONG pid = maps[i].ulPID;

                    m_pMap->UnmapPID(1, &pid);
                }
            }
        }
    }
};

class CFGManagerBDA : public CFGManagerPlayer, IBDATuner, IAMStreamSelect
{
public:
    CFGManagerBDA(LPCTSTR pName, LPUNKNOWN pUnk, HWND hWnd);
    ~CFGManagerBDA();

    // IGraphBuilder
    STDMETHODIMP RenderFile(LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList);

    // IFilterGraph
    STDMETHODIMP ConnectDirect(IPin* pPinOut, IPin* pPinIn, const AM_MEDIA_TYPE* pmt);

    // IBDATuner
    STDMETHODIMP SetChannel(int nChannelPrefNumber);
    STDMETHODIMP SetAudio(int nAudioIndex);
    STDMETHODIMP SetFrequency(ULONG freq);
    STDMETHODIMP Scan(ULONG ulFrequency, HWND hWnd);
    STDMETHODIMP GetStats(BOOLEAN& bPresent, BOOLEAN& bLocked, LONG& lDbStrength, LONG& lPercentQuality);

    // IAMStreamSelect
    STDMETHODIMP Count(DWORD* pcStreams);
    STDMETHODIMP Enable(long lIndex, DWORD dwFlags);
    STDMETHODIMP Info(long lIndex, AM_MEDIA_TYPE** ppmt, DWORD* pdwFlags, LCID* plcid, DWORD* pdwGroup, WCHAR** ppszName, IUnknown** ppObject, IUnknown** ppUnk);

    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP UpdatePSI(const CDVBChannel* pChannel, EventDescriptor& NowNext);

private:

    CComQIPtr<IBDA_DeviceControl>        m_pBDAControl;
    CComPtr<IBDA_FrequencyFilter>        m_pBDAFreq;
    CComQIPtr<IBDA_SignalStatistics>     m_pBDATunerStats;
    CComPtr<IBDA_DigitalDemodulator>     m_pBDADemodulator;
    CComQIPtr<IBDA_SignalStatistics>     m_pBDADemodStats;
    CComPtr<IBDA_AutoDemodulate>         m_pBDAAutoDemulate;
    DVB_RebuildFilterGraph m_nDVBRebuildFilterGraph;
    CAtlMap<DVB_STREAM_TYPE, CDVBStream> m_DVBStreams;

    DVB_STREAM_TYPE m_nCurVideoType;
    DVB_STREAM_TYPE m_nCurAudioType;
    bool            m_fHideWindow;
    CComPtr<IBaseFilter> m_pDemux;

    HRESULT         CreateKSFilter(IBaseFilter** ppBF, CLSID KSCategory, const CStringW& DisplayName);
    HRESULT         ConnectFilters(IBaseFilter* pOutFiter, IBaseFilter* pInFilter);
    HRESULT         CreateMicrosoftDemux(CComPtr<IBaseFilter>& pMpeg2Demux);
    HRESULT         SetChannelInternal(CDVBChannel* pChannel);
    HRESULT         SwitchStream(DVB_STREAM_TYPE nOldType, DVB_STREAM_TYPE nNewType);
    HRESULT         ChangeState(FILTER_STATE nRequested);
    HRESULT         ClearMaps();
    FILTER_STATE    GetState();
    void UpdateMediaType(VIDEOINFOHEADER2* NewVideoHeader, CDVBChannel* pChannel);
    HRESULT Flush(DVB_STREAM_TYPE nVideoType, DVB_STREAM_TYPE nAudioType);

    template <class ITF>
    HRESULT SearchIBDATopology(const CComPtr<IBaseFilter>& pTuner, CComPtr<ITF>& pItf) {
        CComPtr<IUnknown> pUnk;
        HRESULT hr = SearchIBDATopology(pTuner, __uuidof(ITF), pUnk);

        if (SUCCEEDED(hr)) {
            hr = pUnk.QueryInterface(&pItf);
        }
        return !pItf ? E_NOINTERFACE : hr;
    }

    HRESULT SearchIBDATopology(const CComPtr<IBaseFilter>& pTuner, REFIID iid, CComPtr<IUnknown>& pUnk);
};

#ifdef _DEBUG
#include <sys/types.h>
#include <sys/timeb.h>

#define CheckAndLogBDA(x, msg)  hr = ##x; if (FAILED(hr)) { LOG(msg _T(": 0x%08x\n"), hr); return hr; }
#define CheckAndLogBDANoRet(x, msg)  hr = ##x; if (FAILED(hr)) { LOG(msg _T(": 0x%08x\n"), hr); }

#define LOG_FILE _T("bda.log")

static void LOG(LPCTSTR fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    TCHAR buff[3000];
    FILE* f;
    _timeb timebuffer;
    TCHAR time1[8];
    TCHAR wbuf[26];

    _ftime_s(&timebuffer);
    _tctime_s(wbuf, _countof(wbuf), &timebuffer.time);

    for (size_t i = 0; i < _countof(time1); i++) {
        time1[i] = wbuf[i + 11];
    }

    _vstprintf_s(buff, _countof(buff), fmt, args);
    if (_tfopen_s(&f, LOG_FILE, _T("at")) == 0) {
        fseek(f, 0, 2);
        _ftprintf_s(f, _T("%.8s.%03hu - %s\n"), time1, timebuffer.millitm, buff);
        fclose(f);
    }

    va_end(args);
}
#else
inline void LOG(LPCTSTR fmt, ...) {}
#define CheckAndLogBDA(x, msg)  hr = ##x; if (FAILED(hr)) { TRACE(msg _T(": 0x%08x\n"), hr); return hr; }
#define CheckAndLogBDANoRet(x, msg)  hr = ##x; if (FAILED(hr)) { TRACE(msg _T(": 0x%08x\n"), hr); }
#endif
