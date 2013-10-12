/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2013 see Authors.txt
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
#include "mplayerc.h"
#include "FGManager.h"
#include "DSUtil.h"
#include "FileVersionInfo.h"
#include "WinAPIUtils.h"
#include "../filters/Filters.h"
#include "AllocatorCommon7.h"
#include "AllocatorCommon.h"
#include "SyncAllocatorPresenter.h"
#include "madVRAllocatorPresenter.h"
#include "DeinterlacerFilter.h"
#include "../DeCSS/VobFile.h"
#include <InitGuid.h>
#include <dmodshow.h>
#include <d3d9.h>
#include <vmr9.h>
#include <evr.h>
#include <evr9.h>
#include <ksproxy.h>
#include "moreuuids.h"

//
// CFGManager
//

CFGManager::CFGManager(LPCTSTR pName, LPUNKNOWN pUnk)
    : CUnknown(pName, pUnk)
    , m_dwRegister(0)
{
    m_pUnkInner.CoCreateInstance(CLSID_FilterGraph, GetOwner());
    m_pFM.CoCreateInstance(CLSID_FilterMapper2);
}

CFGManager::~CFGManager()
{
    CAutoLock cAutoLock(this);
    while (!m_source.IsEmpty()) {
        delete m_source.RemoveHead();
    }
    while (!m_transform.IsEmpty()) {
        delete m_transform.RemoveHead();
    }
    while (!m_override.IsEmpty()) {
        delete m_override.RemoveHead();
    }
    m_pUnks.RemoveAll();
    m_pUnkInner.Release();
}

STDMETHODIMP CFGManager::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);

    return
        QI(IFilterGraph)
        QI(IGraphBuilder)
        QI(IFilterGraph2)
        QI(IGraphBuilder2)
        QI(IGraphBuilderDeadEnd)
        m_pUnkInner && (riid != IID_IUnknown && SUCCEEDED(m_pUnkInner->QueryInterface(riid, ppv))) ? S_OK :
        __super::NonDelegatingQueryInterface(riid, ppv);
}

//

void CFGManager::CStreamPath::Append(IBaseFilter* pBF, IPin* pPin)
{
    path_t p;
    p.clsid = GetCLSID(pBF);
    p.filter = GetFilterName(pBF);
    p.pin = GetPinName(pPin);
    AddTail(p);
}

bool CFGManager::CStreamPath::Compare(const CStreamPath& path)
{
    POSITION pos1 = GetHeadPosition();
    POSITION pos2 = path.GetHeadPosition();

    while (pos1 && pos2) {
        const path_t& p1 = GetNext(pos1);
        const path_t& p2 = path.GetNext(pos2);

        if (p1.filter != p2.filter) {
            return true;
        } else if (p1.pin != p2.pin) {
            return false;
        }
    }

    return true;
}

//

bool CFGManager::CheckBytes(HANDLE hFile, CString chkbytes)
{
    CAtlList<CString> sl;
    Explode(chkbytes, sl, ',');

    if (sl.GetCount() < 4) {
        return false;
    }

    ASSERT(!(sl.GetCount() & 3));

    LARGE_INTEGER size = {0, 0};
    GetFileSizeEx(hFile, &size);

    while (sl.GetCount() >= 4) {
        CString offsetstr = sl.RemoveHead();
        CString cbstr = sl.RemoveHead();
        CString maskstr = sl.RemoveHead();
        CString valstr = sl.RemoveHead();

        long cb = _ttol(cbstr);

        if (offsetstr.IsEmpty() || cbstr.IsEmpty()
                || valstr.IsEmpty() || (valstr.GetLength() & 1)
                || cb * 2 != valstr.GetLength()) {
            return false;
        }

        LARGE_INTEGER offset;
        offset.QuadPart = _ttoi64(offsetstr);
        if (offset.QuadPart < 0) {
            offset.QuadPart = size.QuadPart - offset.QuadPart;
        }
        SetFilePointerEx(hFile, offset, &offset, FILE_BEGIN);

        // LAME
        while (maskstr.GetLength() < valstr.GetLength()) {
            maskstr += 'F';
        }

        CAtlArray<BYTE> mask, val;
        CStringToBin(maskstr, mask);
        CStringToBin(valstr, val);

        for (size_t i = 0; i < val.GetCount(); i++) {
            BYTE b;
            DWORD r;
            if (!ReadFile(hFile, &b, 1, &r, nullptr) || (b & mask[i]) != val[i]) {
                return false;
            }
        }
    }

    return sl.IsEmpty();
}

CFGFilter* LookupFilterRegistry(const GUID& guid, CAtlList<CFGFilter*>& list, UINT64 fallback_merit = MERIT64_DO_USE)
{
    POSITION pos = list.GetHeadPosition();
    CFGFilter* pFilter = nullptr;
    while (pos) {
        CFGFilter* pFGF = list.GetNext(pos);
        if (pFGF->GetCLSID() == guid) {
            pFilter = pFGF;
            break;
        }
    }
    if (pFilter) {
        return DEBUG_NEW CFGFilterRegistry(guid, pFilter->GetMerit());
    } else {
        return DEBUG_NEW CFGFilterRegistry(guid, fallback_merit);
    }
}

HRESULT CFGManager::EnumSourceFilters(LPCWSTR lpcwstrFileName, CFGFilterList& fl)
{
    // TODO: use overrides

    CheckPointer(lpcwstrFileName, E_POINTER);

    fl.RemoveAll();

    CStringW fn = CStringW(lpcwstrFileName).TrimLeft();
    CStringW protocol = fn.Left(fn.Find(':') + 1).TrimRight(':').MakeLower();
    CStringW ext = CPathW(fn).GetExtension().MakeLower();

    HANDLE hFile = INVALID_HANDLE_VALUE;

    if (protocol.GetLength() <= 1 || protocol == L"file") {
        hFile = CreateFile(CString(fn), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, (HANDLE)nullptr);

        if (hFile == INVALID_HANDLE_VALUE) {
            return VFW_E_NOT_FOUND;
        }
    }

    // exceptions first

    if (ext == _T(".dvr-ms") || ext == _T(".wtv")) { // doh, this is stupid
        fl.Insert(LookupFilterRegistry(CLSID_StreamBufferSource, m_override, MERIT64_PREFERRED), 0);
    }

    TCHAR buff[256];
    ULONG len;

    if (hFile == INVALID_HANDLE_VALUE) {
        // internal / protocol

        POSITION pos = m_source.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = m_source.GetNext(pos);
            if (pFGF->m_protocols.Find(CString(protocol))) {
                fl.Insert(pFGF, 0, false, false);
            }
        }
    } else {
        // internal / check bytes

        POSITION pos = m_source.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = m_source.GetNext(pos);

            POSITION pos2 = pFGF->m_chkbytes.GetHeadPosition();
            while (pos2) {
                if (CheckBytes(hFile, pFGF->m_chkbytes.GetNext(pos2))) {
                    fl.Insert(pFGF, 1, false, false);
                    break;
                }
            }
        }
    }

    if (!ext.IsEmpty()) {
        // internal / file extension

        POSITION pos = m_source.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = m_source.GetNext(pos);
            if (pFGF->m_extensions.Find(CString(ext))) {
                fl.Insert(pFGF, 2, false, false);
            }
        }
    }

    {
        // internal / the rest

        POSITION pos = m_source.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = m_source.GetNext(pos);
            if (pFGF->m_protocols.IsEmpty() && pFGF->m_chkbytes.IsEmpty() && pFGF->m_extensions.IsEmpty()) {
                fl.Insert(pFGF, 3, false, false);
            }
        }
    }

    if (hFile == INVALID_HANDLE_VALUE) {
        // protocol

        CRegKey key;
        if (ERROR_SUCCESS == key.Open(HKEY_CLASSES_ROOT, CString(protocol), KEY_READ)) {
            CRegKey exts;
            if (ERROR_SUCCESS == exts.Open(key, _T("Extensions"), KEY_READ)) {
                len = _countof(buff);
                if (ERROR_SUCCESS == exts.QueryStringValue(CString(ext), buff, &len)) {
                    fl.Insert(LookupFilterRegistry(GUIDFromCString(buff), m_override), 4);
                }
            }

            len = _countof(buff);
            if (ERROR_SUCCESS == key.QueryStringValue(_T("Source Filter"), buff, &len)) {
                fl.Insert(LookupFilterRegistry(GUIDFromCString(buff), m_override), 5);
            }
        }

        fl.Insert(DEBUG_NEW CFGFilterRegistry(CLSID_URLReader), 6);
    } else {
        // check bytes

        CRegKey key;
        if (ERROR_SUCCESS == key.Open(HKEY_CLASSES_ROOT, _T("Media Type"), KEY_READ)) {
            FILETIME ft;
            len = _countof(buff);
            for (DWORD i = 0; ERROR_SUCCESS == key.EnumKey(i, buff, &len, &ft); i++, len = _countof(buff)) {
                GUID majortype;
                if (FAILED(GUIDFromCString(buff, majortype))) {
                    continue;
                }

                CRegKey majorkey;
                if (ERROR_SUCCESS == majorkey.Open(key, buff, KEY_READ)) {
                    len = _countof(buff);
                    for (DWORD j = 0; ERROR_SUCCESS == majorkey.EnumKey(j, buff, &len, &ft); j++, len = _countof(buff)) {
                        GUID subtype;
                        if (FAILED(GUIDFromCString(buff, subtype))) {
                            continue;
                        }

                        CRegKey subkey;
                        if (ERROR_SUCCESS == subkey.Open(majorkey, buff, KEY_READ)) {
                            len = _countof(buff);
                            if (ERROR_SUCCESS != subkey.QueryStringValue(_T("Source Filter"), buff, &len)) {
                                continue;
                            }

                            GUID clsid = GUIDFromCString(buff);
                            TCHAR buff2[256];
                            ULONG len2;

                            len = _countof(buff);
                            len2 = sizeof(buff2);
                            for (DWORD k = 0, type;
                                    clsid != GUID_NULL && ERROR_SUCCESS == RegEnumValue(subkey, k, buff2, &len2, 0, &type, (BYTE*)buff, &len);
                                    k++, len = _countof(buff), len2 = sizeof(buff2)) {
                                if (CheckBytes(hFile, CString(buff))) {
                                    CFGFilter* pFGF = LookupFilterRegistry(clsid, m_override);
                                    pFGF->AddType(majortype, subtype);
                                    fl.Insert(pFGF, 9);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (!ext.IsEmpty()) {
        // file extension

        CRegKey key;
        if (ERROR_SUCCESS == key.Open(HKEY_CLASSES_ROOT, _T("Media Type\\Extensions\\") + CString(ext), KEY_READ)) {
            ULONG len = _countof(buff);
            ZeroMemory(buff, sizeof(buff));
            LONG ret = key.QueryStringValue(_T("Source Filter"), buff, &len); // QueryStringValue can return ERROR_INVALID_DATA on bogus strings (radlight mpc v1003, fixed in v1004)
            if (ERROR_SUCCESS == ret || ERROR_INVALID_DATA == ret && GUIDFromCString(buff) != GUID_NULL) {
                GUID clsid = GUIDFromCString(buff);
                GUID majortype = GUID_NULL;
                GUID subtype = GUID_NULL;

                len = _countof(buff);
                if (ERROR_SUCCESS == key.QueryStringValue(_T("Media Type"), buff, &len)) {
                    majortype = GUIDFromCString(buff);
                }

                len = _countof(buff);
                if (ERROR_SUCCESS == key.QueryStringValue(_T("Subtype"), buff, &len)) {
                    subtype = GUIDFromCString(buff);
                }

                CFGFilter* pFGF = LookupFilterRegistry(clsid, m_override);
                pFGF->AddType(majortype, subtype);
                fl.Insert(pFGF, 7);
            }
        }
    }

    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }

    CFGFilter* pFGF = LookupFilterRegistry(CLSID_AsyncReader, m_override);
    pFGF->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_NULL);
    fl.Insert(pFGF, 9);

    return S_OK;
}

HRESULT CFGManager::AddSourceFilter(CFGFilter* pFGF, LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrFilterName, IBaseFilter** ppBF)
{
    TRACE(_T("FGM: AddSourceFilter trying '%s'\n"), CStringFromGUID(pFGF->GetCLSID()));

    CheckPointer(lpcwstrFileName, E_POINTER);
    CheckPointer(ppBF, E_POINTER);

    ASSERT(*ppBF == nullptr);

    HRESULT hr;

    CComPtr<IBaseFilter> pBF;
    CInterfaceList<IUnknown, &IID_IUnknown> pUnks;
    if (FAILED(hr = pFGF->Create(&pBF, pUnks))) {
        return hr;
    }

    CComQIPtr<IFileSourceFilter> pFSF = pBF;
    if (!pFSF) {
        return E_NOINTERFACE;
    }

    if (FAILED(hr = AddFilter(pBF, lpcwstrFilterName))) {
        return hr;
    }

    const AM_MEDIA_TYPE* pmt = nullptr;

    CMediaType mt;
    const CAtlList<GUID>& types = pFGF->GetTypes();
    if (types.GetCount() == 2 && (types.GetHead() != GUID_NULL || types.GetTail() != GUID_NULL)) {
        mt.majortype = types.GetHead();
        mt.subtype = types.GetTail();
        pmt = &mt;
    }

    // sometimes looping with AviSynth
    if (FAILED(hr = pFSF->Load(lpcwstrFileName, pmt))) {
        RemoveFilter(pBF);
        return hr;
    }

    // doh :P
    BeginEnumMediaTypes(GetFirstPin(pBF, PINDIR_OUTPUT), pEMT, pmt) {
        static const GUID guid1 =
        { 0x640999A0, 0xA946, 0x11D0, { 0xA5, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
        static const GUID guid2 =
        { 0x640999A1, 0xA946, 0x11D0, { 0xA5, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
        static const GUID guid3 =
        { 0xD51BD5AE, 0x7548, 0x11CF, { 0xA5, 0x20, 0x00, 0x80, 0xC7, 0x7E, 0xF5, 0x8A } };

        if (pmt->subtype == guid1 || pmt->subtype == guid2 || pmt->subtype == guid3) {
            RemoveFilter(pBF);
            pFGF = DEBUG_NEW CFGFilterRegistry(CLSID_NetShowSource);
            hr = AddSourceFilter(pFGF, lpcwstrFileName, lpcwstrFilterName, ppBF);
            delete pFGF;
            return hr;
        }
    }
    EndEnumMediaTypes(pmt);

    *ppBF = pBF.Detach();

    m_pUnks.AddTailList(&pUnks);

    return S_OK;
}

// IFilterGraph

STDMETHODIMP CFGManager::AddFilter(IBaseFilter* pFilter, LPCWSTR pName)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    HRESULT hr;

    if (FAILED(hr = CComQIPtr<IFilterGraph2>(m_pUnkInner)->AddFilter(pFilter, pName))) {
        return hr;
    }

    // TODO
    hr = pFilter->JoinFilterGraph(nullptr, nullptr);
    hr = pFilter->JoinFilterGraph(this, pName);

    return hr;
}

STDMETHODIMP CFGManager::RemoveFilter(IBaseFilter* pFilter)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->RemoveFilter(pFilter);
}

STDMETHODIMP CFGManager::EnumFilters(IEnumFilters** ppEnum)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    // Not locking here fixes a deadlock involving ReClock
    //CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->EnumFilters(ppEnum);
}

STDMETHODIMP CFGManager::FindFilterByName(LPCWSTR pName, IBaseFilter** ppFilter)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->FindFilterByName(pName, ppFilter);
}

STDMETHODIMP CFGManager::ConnectDirect(IPin* pPinOut, IPin* pPinIn, const AM_MEDIA_TYPE* pmt)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    CComPtr<IBaseFilter> pBF = GetFilterFromPin(pPinIn);
    CLSID clsid = GetCLSID(pBF);

    // TODO: GetUpStreamFilter goes up on the first input pin only
    for (CComPtr<IBaseFilter> pBFUS = GetFilterFromPin(pPinOut); pBFUS; pBFUS = GetUpStreamFilter(pBFUS)) {
        if (pBFUS == pBF) {
            return VFW_E_CIRCULAR_GRAPH;
        }
        if (clsid != CLSID_Proxy && GetCLSID(pBFUS) == clsid) {
            return VFW_E_CANNOT_CONNECT;
        }
    }

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->ConnectDirect(pPinOut, pPinIn, pmt);
}

STDMETHODIMP CFGManager::Reconnect(IPin* ppin)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->Reconnect(ppin);
}

STDMETHODIMP CFGManager::Disconnect(IPin* ppin)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->Disconnect(ppin);
}

STDMETHODIMP CFGManager::SetDefaultSyncSource()
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->SetDefaultSyncSource();
}

// IGraphBuilder

STDMETHODIMP CFGManager::Connect(IPin* pPinOut, IPin* pPinIn)
{
    return Connect(pPinOut, pPinIn, true);
}

HRESULT CFGManager::Connect(IPin* pPinOut, IPin* pPinIn, bool bContinueRender)
{
    CAutoLock cAutoLock(this);

    CheckPointer(pPinOut, E_POINTER);

    HRESULT hr;

    if (S_OK != IsPinDirection(pPinOut, PINDIR_OUTPUT)
            || pPinIn && S_OK != IsPinDirection(pPinIn, PINDIR_INPUT)) {
        return VFW_E_INVALID_DIRECTION;
    }

    if (S_OK == IsPinConnected(pPinOut)
            || pPinIn && S_OK == IsPinConnected(pPinIn)) {
        return VFW_E_ALREADY_CONNECTED;
    }

    bool fDeadEnd = true;

    if (pPinIn) {
        // 1. Try a direct connection between the filters, with no intermediate filters

        if (SUCCEEDED(hr = ConnectDirect(pPinOut, pPinIn, nullptr))) {
            return hr;
        }
    } else {
        // 1. Use IStreamBuilder

        if (CComQIPtr<IStreamBuilder> pSB = pPinOut) {
            if (SUCCEEDED(hr = pSB->Render(pPinOut, this))) {
                return hr;
            }

            pSB->Backout(pPinOut, this);
        }
    }

    // 2. Try cached filters

    if (CComQIPtr<IGraphConfig> pGC = (IGraphBuilder2*)this) {
        BeginEnumCachedFilters(pGC, pEF, pBF) {
            if (pPinIn && GetFilterFromPin(pPinIn) == pBF) {
                continue;
            }

            hr = pGC->RemoveFilterFromCache(pBF);

            // does RemoveFilterFromCache call AddFilter like AddFilterToCache calls RemoveFilter ?

            if (SUCCEEDED(hr = ConnectFilterDirect(pPinOut, pBF, nullptr))) {
                if (!IsStreamEnd(pBF)) {
                    fDeadEnd = false;
                }

                if (SUCCEEDED(hr = ConnectFilter(pBF, pPinIn))) {
                    return hr;
                }
            }

            hr = pGC->AddFilterToCache(pBF);
        }
        EndEnumCachedFilters;
    }

    // 3. Try filters in the graph

    {
        CInterfaceList<IBaseFilter> pBFs;

        BeginEnumFilters(this, pEF, pBF) {
            if (pPinIn && GetFilterFromPin(pPinIn) == pBF
                    || GetFilterFromPin(pPinOut) == pBF) {
                continue;
            }

            // HACK: ffdshow - audio capture filter
            if (GetCLSID(pPinOut) == GUIDFromCString(_T("{04FE9017-F873-410E-871E-AB91661A4EF7}"))
                    && GetCLSID(pBF) == GUIDFromCString(_T("{E30629D2-27E5-11CE-875D-00608CB78066}"))) {
                continue;
            }

            pBFs.AddTail(pBF);
        }
        EndEnumFilters;

        POSITION pos = pBFs.GetHeadPosition();
        while (pos) {
            IBaseFilter* pBF = pBFs.GetNext(pos);

            if (SUCCEEDED(hr = ConnectFilterDirect(pPinOut, pBF, nullptr))) {
                if (!IsStreamEnd(pBF)) {
                    fDeadEnd = false;
                }

                if (SUCCEEDED(hr = ConnectFilter(pBF, pPinIn))) {
                    return hr;
                }
            }

            EXECUTE_ASSERT(Disconnect(pPinOut));
        }
    }

    // 4. Look up filters in the registry

    {
        CFGFilterList fl;

        CAtlArray<GUID> types;
        ExtractMediaTypes(pPinOut, types);

        POSITION pos = m_transform.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = m_transform.GetNext(pos);
            if (pFGF->GetMerit() < MERIT64_DO_USE || pFGF->CheckTypes(types, false)) {
                fl.Insert(pFGF, 0, pFGF->CheckTypes(types, true), false);
            }
        }

        pos = m_override.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = m_override.GetNext(pos);
            if (pFGF->GetMerit() < MERIT64_DO_USE || pFGF->CheckTypes(types, false)) {
                fl.Insert(pFGF, 0, pFGF->CheckTypes(types, true), false);
            }
        }

        CComPtr<IEnumMoniker> pEM;
        if (!types.IsEmpty()
                && SUCCEEDED(m_pFM->EnumMatchingFilters(
                                 &pEM, 0, FALSE, MERIT_DO_NOT_USE + 1,
                                 TRUE, (DWORD)types.GetCount() / 2, types.GetData(), nullptr, nullptr, FALSE,
                                 !!pPinIn, 0, nullptr, nullptr, nullptr))) {
            for (CComPtr<IMoniker> pMoniker; S_OK == pEM->Next(1, &pMoniker, nullptr); pMoniker = nullptr) {
                CFGFilterRegistry* pFGF = DEBUG_NEW CFGFilterRegistry(pMoniker);
                fl.Insert(pFGF, 0, pFGF->CheckTypes(types, true));
            }
        }

        // let's check whether the madVR allocator presenter is in our list
        // it should be if madVR is selected as the video renderer
        CFGFilter* pMadVRAllocatorPresenter = nullptr;
        pos = fl.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = fl.GetNext(pos);
            if (pFGF->GetCLSID() == CLSID_madVRAllocatorPresenter) {
                // found it!
                pMadVRAllocatorPresenter = pFGF;
                break;
            }
        }

        pos = fl.GetHeadPosition();
        while (pos) {
            CFGFilter* pFGF = fl.GetNext(pos);

            // Checks if madVR is already in the graph to avoid two instances at the same time
            CComPtr<IBaseFilter> pBFmadVR;
            FindFilterByName(_T("madVR Renderer"), &pBFmadVR);
            if (pBFmadVR && (pFGF->GetName() == _T("madVR Renderer"))) {
                continue;
            }

            if (pMadVRAllocatorPresenter && (pFGF->GetCLSID() == CLSID_madVR)) {
                // the pure madVR filter was selected (without the allocator presenter)
                // subtitles, OSD etc don't work correctly without the allocator presenter
                // so we prefer the allocator presenter over the pure filter
                pFGF = pMadVRAllocatorPresenter;
            }

            TRACE(_T("FGM: Connecting '%s'\n"), pFGF->GetName());

            CComPtr<IBaseFilter> pBF;
            CInterfaceList<IUnknown, &IID_IUnknown> pUnks;
            if (FAILED(pFGF->Create(&pBF, pUnks))) {
                TRACE(_T("     --> Filter creation failed\n"));
                continue;
            }

            if (FAILED(hr = AddFilter(pBF, pFGF->GetName()))) {
                TRACE(_T("     --> Adding the filter failed\n"));
                pUnks.RemoveAll();
                pBF.Release();
                continue;
            }

            hr = ConnectFilterDirect(pPinOut, pBF, nullptr);
            /*
            if (FAILED(hr))
            {
                if (types.GetCount() >= 2 && types[0] == MEDIATYPE_Stream && types[1] != GUID_NULL)
                {
                    CMediaType mt;

                    mt.majortype = types[0];
                    mt.subtype = types[1];
                    mt.formattype = FORMAT_None;
                    if (FAILED(hr)) hr = ConnectFilterDirect(pPinOut, pBF, &mt);

                    mt.formattype = GUID_NULL;
                    if (FAILED(hr)) hr = ConnectFilterDirect(pPinOut, pBF, &mt);
                }
            }
            */
            if (SUCCEEDED(hr)) {
                if (!IsStreamEnd(pBF)) {
                    fDeadEnd = false;
                }

                if (bContinueRender) {
                    hr = ConnectFilter(pBF, pPinIn);
                }

                if (SUCCEEDED(hr)) {
                    m_pUnks.AddTailList(&pUnks);

                    // maybe the application should do this...

                    POSITION pos = pUnks.GetHeadPosition();
                    while (pos) {
                        if (CComQIPtr<IMixerPinConfig, &IID_IMixerPinConfig> pMPC = pUnks.GetNext(pos)) {
                            pMPC->SetAspectRatioMode(AM_ARMODE_STRETCHED);
                        }
                    }

                    if (CComQIPtr<IVMRAspectRatioControl> pARC = pBF) {
                        pARC->SetAspectRatioMode(VMR_ARMODE_NONE);
                    }

                    if (CComQIPtr<IVMRAspectRatioControl9> pARC = pBF) {
                        pARC->SetAspectRatioMode(VMR_ARMODE_NONE);
                    }

                    if (CComQIPtr<IVMRMixerControl9> pMC = pBF) {
                        m_pUnks.AddTail(pMC);
                    }

                    if (CComQIPtr<IVMRMixerBitmap9> pMB = pBF) {
                        m_pUnks.AddTail(pMB);
                    }

                    if (CComQIPtr<IMFGetService, &__uuidof(IMFGetService)> pMFGS = pBF) {
                        CComPtr<IMFVideoDisplayControl> pMFVDC;
                        CComPtr<IMFVideoMixerBitmap>    pMFMB;
                        CComPtr<IMFVideoProcessor>      pMFVP;

                        if (SUCCEEDED(pMFGS->GetService(MR_VIDEO_RENDER_SERVICE, IID_IMFVideoDisplayControl, (void**)&pMFVDC))) {
                            m_pUnks.AddTail(pMFVDC);
                        }

                        if (SUCCEEDED(pMFGS->GetService(MR_VIDEO_MIXER_SERVICE, IID_IMFVideoMixerBitmap, (void**)&pMFMB))) {
                            m_pUnks.AddTail(pMFMB);
                        }

                        if (SUCCEEDED(pMFGS->GetService(MR_VIDEO_MIXER_SERVICE, IID_IMFVideoProcessor, (void**)&pMFVP))) {
                            m_pUnks.AddTail(pMFVP);
                        }

                        //CComPtr<IMFWorkQueueServices> pMFWQS;
                        //pMFGS->GetService (MF_WORKQUEUE_SERVICES, IID_IMFWorkQueueServices, (void**)&pMFWQS);
                        //pMFWQS->BeginRegisterPlatformWorkQueueWithMMCSS(

                    }

                    return hr;
                }
            }

            EXECUTE_ASSERT(SUCCEEDED(RemoveFilter(pBF)));
            TRACE(_T("     --> Failed to connect\n"));
            pUnks.RemoveAll();
            pBF.Release();
        }
    }

    if (fDeadEnd) {
        CAutoPtr<CStreamDeadEnd> psde(DEBUG_NEW CStreamDeadEnd());
        psde->AddTailList(&m_streampath);
        int skip = 0;
        BeginEnumMediaTypes(pPinOut, pEM, pmt) {
            if (pmt->majortype == MEDIATYPE_Stream && pmt->subtype == MEDIASUBTYPE_NULL) {
                skip++;
            }
            psde->mts.AddTail(CMediaType(*pmt));
        }
        EndEnumMediaTypes(pmt);
        if (skip < (int)psde->mts.GetCount()) {
            m_deadends.Add(psde);
        }
    }

    return pPinIn ? VFW_E_CANNOT_CONNECT : VFW_E_CANNOT_RENDER;
}

STDMETHODIMP CFGManager::Render(IPin* pPinOut)
{
    CAutoLock cAutoLock(this);

    return RenderEx(pPinOut, 0, nullptr);
}

STDMETHODIMP CFGManager::RenderFile(LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrPlayList)
{
    TRACE(_T("--> CFGManager::RenderFile on thread: %d\n"), GetCurrentThreadId());
    CAutoLock cAutoLock(this);

    m_streampath.RemoveAll();
    m_deadends.RemoveAll();

    HRESULT hr;
    HRESULT hrRFS = S_OK;

    /*CComPtr<IBaseFilter> pBF;
    if (FAILED(hr = AddSourceFilter(lpcwstrFile, lpcwstrFile, &pBF)))
        return hr;

    return ConnectFilter(pBF, nullptr);*/

    CFGFilterList fl;
    if (FAILED(hr = EnumSourceFilters(lpcwstrFileName, fl))) {
        return hr;
    }

    CAutoPtrArray<CStreamDeadEnd> deadends;

    hr = VFW_E_CANNOT_RENDER;

    POSITION pos = fl.GetHeadPosition();
    while (pos) {
        CComPtr<IBaseFilter> pBF;
        CFGFilter* pFG = fl.GetNext(pos);

        if (SUCCEEDED(hr = AddSourceFilter(pFG, lpcwstrFileName, pFG->GetName(), &pBF))) {
            m_streampath.RemoveAll();
            m_deadends.RemoveAll();

            if (SUCCEEDED(hr = ConnectFilter(pBF, nullptr))) {
                return hr;
            }

            NukeDownstream(pBF);
            RemoveFilter(pBF);

            deadends.Append(m_deadends);
        } else if (pFG->GetCLSID() == __uuidof(CRARFileSource) && HRESULT_FACILITY(hr) == FACILITY_ITF) {
            hrRFS = hr;
        }
    }

    m_deadends.Copy(deadends);

    // If RFS was part of the graph, return its error code instead of the last error code.
    // TODO: Improve filter error reporting to graph manager.
    return hrRFS != S_OK ? hrRFS : hr;
}

STDMETHODIMP CFGManager::AddSourceFilter(LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrFilterName, IBaseFilter** ppFilter)
{
    CAutoLock cAutoLock(this);

    HRESULT hr;
    CFGFilterList fl;

    if (FAILED(hr = EnumSourceFilters(lpcwstrFileName, fl))) {
        return hr;
    }

    POSITION pos = fl.GetHeadPosition();
    while (pos) {
        if (SUCCEEDED(hr = AddSourceFilter(fl.GetNext(pos), lpcwstrFileName, lpcwstrFilterName, ppFilter))) {
            return hr;
        }
    }

    return VFW_E_CANNOT_LOAD_SOURCE_FILTER;
}

STDMETHODIMP CFGManager::SetLogFile(DWORD_PTR hFile)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->SetLogFile(hFile);
}

STDMETHODIMP CFGManager::Abort()
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->Abort();
}

STDMETHODIMP CFGManager::ShouldOperationContinue()
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->ShouldOperationContinue();
}

// IFilterGraph2

STDMETHODIMP CFGManager::AddSourceFilterForMoniker(IMoniker* pMoniker, IBindCtx* pCtx, LPCWSTR lpcwstrFilterName, IBaseFilter** ppFilter)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->AddSourceFilterForMoniker(pMoniker, pCtx, lpcwstrFilterName, ppFilter);
}

STDMETHODIMP CFGManager::ReconnectEx(IPin* ppin, const AM_MEDIA_TYPE* pmt)
{
    if (!m_pUnkInner) {
        return E_UNEXPECTED;
    }

    CAutoLock cAutoLock(this);

    return CComQIPtr<IFilterGraph2>(m_pUnkInner)->ReconnectEx(ppin, pmt);
}

STDMETHODIMP CFGManager::RenderEx(IPin* pPinOut, DWORD dwFlags, DWORD* pvContext)
{
    CAutoLock cAutoLock(this);

    m_streampath.RemoveAll();
    m_deadends.RemoveAll();

    if (!pPinOut || dwFlags > AM_RENDEREX_RENDERTOEXISTINGRENDERERS || pvContext) {
        return E_INVALIDARG;
    }

    if (dwFlags & AM_RENDEREX_RENDERTOEXISTINGRENDERERS) {
        CInterfaceList<IBaseFilter> pBFs;

        BeginEnumFilters(this, pEF, pBF) {
            if (CComQIPtr<IAMFilterMiscFlags> pAMMF = pBF) {
                if (pAMMF->GetMiscFlags() & AM_FILTER_MISC_FLAGS_IS_RENDERER) {
                    pBFs.AddTail(pBF);
                }
            } else {
                BeginEnumPins(pBF, pEP, pPin) {
                    CComPtr<IPin> pPinIn;
                    DWORD size = 1;
                    if (SUCCEEDED(pPin->QueryInternalConnections(&pPinIn, &size)) && size == 0) {
                        pBFs.AddTail(pBF);
                        break;
                    }
                }
                EndEnumPins;
            }
        }
        EndEnumFilters;

        HRESULT hr;

        while (!pBFs.IsEmpty()) {
            if (SUCCEEDED(hr = ConnectFilter(pPinOut, pBFs.RemoveHead()))) {
                return hr;
            }
        }

        return VFW_E_CANNOT_RENDER;
    }

    return Connect(pPinOut, (IPin*)nullptr);
}

// IGraphBuilder2

STDMETHODIMP CFGManager::IsPinDirection(IPin* pPin, PIN_DIRECTION dir1)
{
    CAutoLock cAutoLock(this);

    CheckPointer(pPin, E_POINTER);

    PIN_DIRECTION dir2;
    if (FAILED(pPin->QueryDirection(&dir2))) {
        return E_FAIL;
    }

    return dir1 == dir2 ? S_OK : S_FALSE;
}

STDMETHODIMP CFGManager::IsPinConnected(IPin* pPin)
{
    CAutoLock cAutoLock(this);

    CheckPointer(pPin, E_POINTER);

    CComPtr<IPin> pPinTo;
    return SUCCEEDED(pPin->ConnectedTo(&pPinTo)) && pPinTo ? S_OK : S_FALSE;
}

STDMETHODIMP CFGManager::ConnectFilter(IBaseFilter* pBF, IPin* pPinIn)
{
    CAutoLock cAutoLock(this);

    CheckPointer(pBF, E_POINTER);

    if (pPinIn && S_OK != IsPinDirection(pPinIn, PINDIR_INPUT)) {
        return VFW_E_INVALID_DIRECTION;
    }

    int nTotal = 0, nRendered = 0;

    const CAppSettings& s = AfxGetAppSettings();

    BeginEnumPins(pBF, pEP, pPin) {
        if (S_OK == IsPinDirection(pPin, PINDIR_OUTPUT)
                && S_OK != IsPinConnected(pPin)
                && !((s.iDSVideoRendererType != VIDRNDT_DS_EVR_CUSTOM && s.iDSVideoRendererType != VIDRNDT_DS_EVR && s.iDSVideoRendererType != VIDRNDT_DS_SYNC) && GetPinName(pPin)[0] == '~')) {

            CLSID clsid;
            pBF->GetClassID(&clsid);
            // Disable DVD subtitle mixing in EVR (CP) and Sync Renderer for Microsoft DTV-DVD Video Decoder, it corrupts DVD playback.
            if (clsid == CLSID_CMPEG2VidDecoderDS) {
                if (s.iDSVideoRendererType == VIDRNDT_DS_EVR_CUSTOM || s.iDSVideoRendererType == VIDRNDT_DS_SYNC) {
                    if (GetPinName(pPin)[0] == '~') {
                        continue;
                    }
                }
            }
            // No multiple pin for Internal MPEG2 Software Decoder, Nvidia PureVideo Decoder, Sonic Cinemaster VideoDecoder
            else if (clsid == CLSID_CMpeg2DecFilter
                     || clsid == CLSID_NvidiaVideoDecoder
                     || clsid == CLSID_SonicCinemasterVideoDecoder) {
                if (GetPinName(pPin)[0] == '~') {
                    continue;
                }
                //TODO: enable multiple pins for the renderer, if the video decoder supports DXVA
            }

            m_streampath.Append(pBF, pPin);

            HRESULT hr = Connect(pPin, pPinIn);

            if (SUCCEEDED(hr)) {
                for (ptrdiff_t i = m_deadends.GetCount() - 1; i >= 0; i--) {
                    if (m_deadends[i]->Compare(m_streampath)) {
                        m_deadends.RemoveAt(i);
                    }
                }
                nRendered++;
            }

            nTotal++;

            m_streampath.RemoveTail();

            if (SUCCEEDED(hr) && pPinIn) {
                return S_OK;
            }
        }
    }
    EndEnumPins;

    return
        nRendered == nTotal ? (nRendered > 0 ? S_OK : S_FALSE) :
            nRendered > 0 ? VFW_S_PARTIAL_RENDER :
            VFW_E_CANNOT_RENDER;
}

STDMETHODIMP CFGManager::ConnectFilter(IPin* pPinOut, IBaseFilter* pBF)
{
    CAutoLock cAutoLock(this);

    CheckPointer(pPinOut, E_POINTER);
    CheckPointer(pBF, E_POINTER);

    if (S_OK != IsPinDirection(pPinOut, PINDIR_OUTPUT)) {
        return VFW_E_INVALID_DIRECTION;
    }

    const CAppSettings& s = AfxGetAppSettings();

    BeginEnumPins(pBF, pEP, pPin) {
        if (S_OK == IsPinDirection(pPin, PINDIR_INPUT)
                && S_OK != IsPinConnected(pPin)
                && !((s.iDSVideoRendererType != VIDRNDT_DS_EVR_CUSTOM && s.iDSVideoRendererType != VIDRNDT_DS_EVR && s.iDSVideoRendererType != VIDRNDT_DS_SYNC) && GetPinName(pPin)[0] == '~')) {
            HRESULT hr = Connect(pPinOut, pPin);
            if (SUCCEEDED(hr)) {
                return hr;
            }
        }
    }
    EndEnumPins;

    return VFW_E_CANNOT_CONNECT;
}

STDMETHODIMP CFGManager::ConnectFilterDirect(IPin* pPinOut, IBaseFilter* pBF, const AM_MEDIA_TYPE* pmt)
{
    CAutoLock cAutoLock(this);

    CheckPointer(pPinOut, E_POINTER);
    CheckPointer(pBF, E_POINTER);

    if (S_OK != IsPinDirection(pPinOut, PINDIR_OUTPUT)) {
        return VFW_E_INVALID_DIRECTION;
    }

    const CAppSettings& s = AfxGetAppSettings();

    BeginEnumPins(pBF, pEP, pPin) {
        if (S_OK == IsPinDirection(pPin, PINDIR_INPUT)
                && S_OK != IsPinConnected(pPin)
                && !((s.iDSVideoRendererType != VIDRNDT_DS_EVR_CUSTOM && s.iDSVideoRendererType != VIDRNDT_DS_EVR && s.iDSVideoRendererType != VIDRNDT_DS_SYNC) && GetPinName(pPin)[0] == '~')) {
            HRESULT hr = ConnectDirect(pPinOut, pPin, pmt);
            if (SUCCEEDED(hr)) {
                return hr;
            }
        }
    }
    EndEnumPins;

    return VFW_E_CANNOT_CONNECT;
}

STDMETHODIMP CFGManager::NukeDownstream(IUnknown* pUnk)
{
    CAutoLock cAutoLock(this);

    if (CComQIPtr<IBaseFilter> pBF = pUnk) {
        BeginEnumPins(pBF, pEP, pPin) {
            NukeDownstream(pPin);
        }
        EndEnumPins;
    } else if (CComQIPtr<IPin> pPin = pUnk) {
        CComPtr<IPin> pPinTo;
        if (S_OK == IsPinDirection(pPin, PINDIR_OUTPUT)
                && SUCCEEDED(pPin->ConnectedTo(&pPinTo)) && pPinTo) {
            if (CComPtr<IBaseFilter> pBF = GetFilterFromPin(pPinTo)) {
                NukeDownstream(pBF);
                Disconnect(pPinTo);
                Disconnect(pPin);
                RemoveFilter(pBF);
            }
        }
    } else {
        return E_INVALIDARG;
    }

    return S_OK;
}

STDMETHODIMP CFGManager::FindInterface(REFIID iid, void** ppv, BOOL bRemove)
{
    CAutoLock cAutoLock(this);

    CheckPointer(ppv, E_POINTER);

    for (POSITION pos = m_pUnks.GetHeadPosition(); pos; m_pUnks.GetNext(pos)) {
        if (SUCCEEDED(m_pUnks.GetAt(pos)->QueryInterface(iid, ppv))) {
            if (bRemove) {
                m_pUnks.RemoveAt(pos);
            }
            return S_OK;
        }
    }

    return E_NOINTERFACE;
}

STDMETHODIMP CFGManager::AddToROT()
{
    CAutoLock cAutoLock(this);

    HRESULT hr;

    if (m_dwRegister) {
        return S_FALSE;
    }

    CComPtr<IRunningObjectTable> pROT;
    CComPtr<IMoniker> pMoniker;
    WCHAR wsz[256];
    swprintf_s(wsz, _countof(wsz), L"FilterGraph %08p pid %08x (MPC-HC)", this, GetCurrentProcessId());
    if (SUCCEEDED(hr = GetRunningObjectTable(0, &pROT))
            && SUCCEEDED(hr = CreateItemMoniker(L"!", wsz, &pMoniker))) {
        hr = pROT->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, (IGraphBuilder2*)this, pMoniker, &m_dwRegister);
    }

    return hr;
}

STDMETHODIMP CFGManager::RemoveFromROT()
{
    CAutoLock cAutoLock(this);

    HRESULT hr;

    if (!m_dwRegister) {
        return S_FALSE;
    }

    CComPtr<IRunningObjectTable> pROT;
    if (SUCCEEDED(hr = GetRunningObjectTable(0, &pROT))
            && SUCCEEDED(hr = pROT->Revoke(m_dwRegister))) {
        m_dwRegister = 0;
    }

    return hr;
}

// IGraphBuilderDeadEnd

STDMETHODIMP_(size_t) CFGManager::GetCount()
{
    CAutoLock cAutoLock(this);

    return m_deadends.GetCount();
}

STDMETHODIMP CFGManager::GetDeadEnd(int iIndex, CAtlList<CStringW>& path, CAtlList<CMediaType>& mts)
{
    CAutoLock cAutoLock(this);

    if (iIndex < 0 || iIndex >= (int)m_deadends.GetCount()) {
        return E_FAIL;
    }

    path.RemoveAll();
    mts.RemoveAll();

    POSITION pos = m_deadends[iIndex]->GetHeadPosition();
    while (pos) {
        const path_t& p = m_deadends[iIndex]->GetNext(pos);

        CStringW str;
        str.Format(L"%s::%s", p.filter, p.pin);
        path.AddTail(str);
    }

    mts.AddTailList(&m_deadends[iIndex]->mts);

    return S_OK;
}

//
//  CFGManagerCustom
//

CFGManagerCustom::CFGManagerCustom(LPCTSTR pName, LPUNKNOWN pUnk)
    : CFGManager(pName, pUnk)
{
    const CAppSettings& s = AfxGetAppSettings();

    bool bOverrideBroadcom = false;
    CFGFilter* pFGF;

    const bool* src = s.SrcFilters;
    const bool* tra = s.TraFilters;

    // Reset LAVFilters internal instances
    CFGFilterLAV::ResetInternalInstances();

    // Prepare LAVFilters wrappers
    CAutoPtr<CFGFilterLAVSplitterBase> pFGLAVSplitterSource(static_cast<CFGFilterLAVSplitterBase*>(CFGFilterLAV::CreateFilter(CFGFilterLAV::SPLITTER_SOURCE)));
    CAutoPtr<CFGFilterLAVSplitterBase> pFGLAVSplitter(static_cast<CFGFilterLAVSplitterBase*>(CFGFilterLAV::CreateFilter(CFGFilterLAV::SPLITTER, MERIT64_ABOVE_DSHOW)));
    CAutoPtr<CFGFilterLAVSplitterBase> pFGLAVSplitterLM(static_cast<CFGFilterLAVSplitterBase*>(CFGFilterLAV::CreateFilter(CFGFilterLAV::SPLITTER, MERIT64_DO_USE, true)));
    CAutoPtr<CFGFilterLAV> pFGLAVVideo(CFGFilterLAV::CreateFilter(CFGFilterLAV::VIDEO_DECODER, MERIT64_ABOVE_DSHOW));
    CAutoPtr<CFGFilterLAV> pFGLAVVideoLM(CFGFilterLAV::CreateFilter(CFGFilterLAV::VIDEO_DECODER, MERIT64_DO_USE, true));
    CAutoPtr<CFGFilterLAV> pFGLAVAudio(CFGFilterLAV::CreateFilter(CFGFilterLAV::AUDIO_DECODER, MERIT64_ABOVE_DSHOW));
    CAutoPtr<CFGFilterLAV> pFGLAVAudioLM(CFGFilterLAV::CreateFilter(CFGFilterLAV::AUDIO_DECODER, MERIT64_DO_USE, true));

    // Source filters

#if INTERNAL_SOURCEFILTER_RFS
    if (src[SRC_RFS]) {
        pFGF = DEBUG_NEW CFGFilterInternal<CRARFileSource>();
        pFGF->m_chkbytes.AddTail(_T("0,7,,526172211A0700"));
        pFGF->m_extensions.AddTail(_T(".rar"));
        m_source.AddTail(pFGF);
    }
#endif

#if INTERNAL_SOURCEFILTER_CDDA
    if (src[SRC_CDDA]) {
        pFGF = DEBUG_NEW CFGFilterInternal<CCDDAReader>();
        pFGF->m_extensions.AddTail(_T(".cda"));
        m_source.AddTail(pFGF);
    }
#endif

#if INTERNAL_SOURCEFILTER_CDXA
    if (src[SRC_CDXA]) {
        pFGF = DEBUG_NEW CFGFilterInternal<CCDXAReader>();
        pFGF->m_chkbytes.AddTail(_T("0,4,,52494646,8,4,,43445841"));
        m_source.AddTail(pFGF);
    }
#endif

#if INTERNAL_SOURCEFILTER_VTS
    if (src[SRC_VTS]) {
        pFGF = DEBUG_NEW CFGFilterInternal<CVTSReader>();
        pFGF->m_chkbytes.AddTail(_T("0,12,,445644564944454F2D565453"));
        m_source.AddTail(pFGF);
    }
#endif

#if INTERNAL_SOURCEFILTER_DSM
    if (src[SRC_DSM]) {
        pFGF = DEBUG_NEW CFGFilterInternal<CDSMSourceFilter>();
        pFGF->m_chkbytes.AddTail(_T("0,4,,44534D53"));
        m_source.AddTail(pFGF);
    }
#endif

#if INTERNAL_SOURCEFILTER_AVI
    if (src[SRC_AVI]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,52494646,8,4,,41564920"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,52494646,8,4,,41564958"));
        pFGLAVSplitterSource->AddFormat("avi");
    }
#endif

#if INTERNAL_SOURCEFILTER_MP4
    if (src[SRC_MP4]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,4,,66747970")); // ftyp
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,4,,6d6f6f76")); // moov
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,4,,6d646174")); // mdat
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,4,,736b6970")); // skip
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,12,ffffffff00000000ffffffff,77696465027fe3706d646174")); // wide ? mdat
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("3,3,,000001")); // raw mpeg4 video
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".mov"));
        pFGLAVSplitterSource->AddFormat("mp4");
    }
#endif

#if INTERNAL_SOURCEFILTER_FLV
    if (src[SRC_FLV]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,464C5601")); // FLV (v1)
        pFGLAVSplitterSource->AddFormat("flv");
    }
#endif

#if INTERNAL_SOURCEFILTER_MATROSKA
    if (src[SRC_MATROSKA]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,1A45DFA3"));
        pFGLAVSplitterSource->AddFormat("matroska");
    }
#endif

#if INTERNAL_SOURCEFILTER_REALMEDIA
    if (src[SRC_REALMEDIA]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,2E524D46"));
        pFGLAVSplitterSource->AddFormat("rm");
    }
#endif

#if INTERNAL_SOURCEFILTER_FLIC
    if (src[SRC_FLIC]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,2,,11AF"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,2,,12AF"));
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".fli"));
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".flc"));
        pFGLAVSplitterSource->AddFormat("flic");
    }
#endif

#if INTERNAL_SOURCEFILTER_FLAC
    if (src[SRC_FLAC]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,664C6143"));
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".flac"));
        pFGLAVSplitterSource->AddFormat("flac");
    }
#endif

#if INTERNAL_SOURCEFILTER_OGG
    if (src[SRC_OGG]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,4F676753"));
        pFGLAVSplitterSource->AddFormat("ogg");
    }
#endif

#if INTERNAL_SOURCEFILTER_MPEG
    if (src[SRC_MPEG]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,16,FFFFFFFFF100010001800001FFFFFFFF,000001BA2100010001800001000001BB"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,5,FFFFFFFFC0,000001BA40"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,1,,47,188,1,,47,376,1,,47"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,1,,47,196,1,,47,388,1,,47"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,54467263,1660,1,,47"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,8,fffffc00ffe00000,4156000055000000"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,8,,4D504C5330323030"));  // MPLS0200
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,8,,4D504C5330313030"));  // MPLS0100
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".ts")); // for some broken .ts
        pFGLAVSplitterSource->AddFormat("mpeg");
        pFGLAVSplitterSource->AddFormat("mpegts");
        pFGLAVSplitterSource->AddFormat("mpegraw");
    }
#endif

#if INTERNAL_SOURCEFILTER_DTSAC3
    if (src[SRC_DTSAC3]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,7FFE8001"));                      // DTS
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,fE7f0180"));                      // DTS LE
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,2,,0B77"));                          // AC3, E-AC3
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,4,,52494646,8,8,,57415645666D7420"));// RIFFxxxxWAVEfmt_ for DTSWAV
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("4,4,,F8726FBB"));                      // MLP
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".ac3"));
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".dts"));
        pFGLAVSplitterSource->m_extensions.AddTail(_T(".eac3"));
        pFGLAVSplitterSource->AddFormat("ac3");
        pFGLAVSplitterSource->AddFormat("dts");
        pFGLAVSplitterSource->AddFormat("dtshd");
        pFGLAVSplitterSource->AddFormat("eac3");
    }
#endif

#if INTERNAL_SOURCEFILTER_MPEGAUDIO
    if (src[SRC_MPA]) {
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,2,FFE0,FFE0"));
        pFGLAVSplitterSource->m_chkbytes.AddTail(_T("0,10,FFFFFF00000080808080,49443300000000000000"));
        pFGLAVSplitterSource->AddFormat("mp3");
    }
#endif

#if INTERNAL_SOURCEFILTER_HTTP
    if (src[SRC_HTTP]) {
        pFGLAVSplitterSource->m_protocols.AddTail(_T("http"));
        pFGLAVSplitterSource->AddFormat("http");
    }
#endif

#if INTERNAL_SOURCEFILTER_RTSP
    if (src[SRC_RTSP]) {
        pFGLAVSplitterSource->m_protocols.AddTail(_T("rtsp"));
        pFGLAVSplitterSource->AddFormat("rtsp");
    }
#endif

#if INTERNAL_SOURCEFILTER_UDP
    if (src[SRC_UDP]) {
        pFGLAVSplitterSource->m_protocols.AddTail(_T("udp"));
        pFGLAVSplitterSource->AddFormat("udp");
    }
#endif

#if INTERNAL_SOURCEFILTER_RTP
    if (src[SRC_RTP]) {
        pFGLAVSplitterSource->m_protocols.AddTail(_T("rtp"));
        pFGLAVSplitterSource->AddFormat("rtp");
    }
#endif

#if INTERNAL_SOURCEFILTER_MMS
    if (src[SRC_MMS]) {
        pFGLAVSplitterSource->m_protocols.AddTail(_T("mms"));
        pFGLAVSplitterSource->m_protocols.AddTail(_T("mmsh"));
        pFGLAVSplitterSource->m_protocols.AddTail(_T("mmst"));
        pFGLAVSplitterSource->AddFormat("mms");
    }
#endif

    // Always register the pipe protocol to allow handling standard input
    pFGLAVSplitterSource->m_protocols.AddTail(_T("pipe"));

    // Add LAV Source Filter if needed
    if (!pFGLAVSplitterSource->m_extensions.IsEmpty()
            || !pFGLAVSplitterSource->m_chkbytes.IsEmpty()
            || !pFGLAVSplitterSource->m_protocols.IsEmpty()) {
        m_source.AddTail(pFGLAVSplitterSource.Detach());
    }

#if INTERNAL_SOURCEFILTER_AVI2AC3
    // hmmm, shouldn't there be an option in the GUI to enable/disable this filter?
    pFGF = DEBUG_NEW CFGFilterInternal<CAVI2AC3Filter>(AVI2AC3FilterName, MERIT64(0x00680000) + 1);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_WAVE_DOLBY_AC3);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_WAVE_DTS);
    m_transform.AddTail(pFGF);
#endif

#if INTERNAL_SOURCEFILTER_DSM
    if (src[SRC_DSM]) {
        pFGF = DEBUG_NEW CFGFilterInternal<CDSMSplitterFilter>(DSMSplitterName, MERIT64_ABOVE_DSHOW);
    } else {
        pFGF = DEBUG_NEW CFGFilterInternal<CDSMSplitterFilter>(LowMerit(DSMSplitterName), MERIT64_DO_USE);
    }
    pFGF->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_DirectShowMedia);
    pFGF->AddType(MEDIATYPE_Stream, GUID_NULL);
    m_transform.AddTail(pFGF);
#endif

#if INTERNAL_SOURCEFILTER_MATROSKA
    if (src[SRC_MATROSKA]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_Matroska);
        pFGLAVSplitter->AddFormat("matroska");
    }
#endif

#if INTERNAL_SOURCEFILTER_REALMEDIA
    if (src[SRC_REALMEDIA]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_RealMedia);
        pFGLAVSplitter->AddFormat("rm");
    }
#endif

#if INTERNAL_SOURCEFILTER_AVI
    if (src[SRC_AVI]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_Avi);
        pFGLAVSplitter->AddFormat("avi");
    }
#endif

#if INTERNAL_SOURCEFILTER_OGG
    if (src[SRC_OGG]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_Ogg);
        pFGLAVSplitter->AddFormat("ogg");
    }
#endif

#if INTERNAL_SOURCEFILTER_MPEG
    if (src[SRC_MPEG]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_MPEG1System);
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_MPEG2_PROGRAM);
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_MPEG2_TRANSPORT);
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_MPEG2_PVA);
        pFGLAVSplitter->AddFormat("mpeg");
        pFGLAVSplitter->AddFormat("mpegts");
        pFGLAVSplitter->AddFormat("mpegraw");
    }
#endif

#if INTERNAL_SOURCEFILTER_MPEGAUDIO
    if (src[SRC_MPA]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_MPEG1Audio);
        pFGLAVSplitter->AddFormat("mp3");
    }
#endif

#if INTERNAL_SOURCEFILTER_MP4
    if (src[SRC_MP4]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_MP4);
        pFGLAVSplitter->AddFormat("mp4");
    }
#endif

#if INTERNAL_SOURCEFILTER_FLV
    if (src[SRC_FLV]) {
        pFGLAVSplitter->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_FLV);
        pFGLAVSplitter->AddFormat("flv");
    }
#endif

    // Add LAV Splitter if needed
    if (!pFGLAVSplitter->GetTypes().IsEmpty()) {
        m_transform.AddTail(pFGLAVSplitter.Detach());
    }

    // Add low merit LAV Splitter
    pFGLAVSplitterLM->AddType(MEDIATYPE_Stream, MEDIASUBTYPE_NULL);
    pFGLAVSplitterLM->AddFormat("*");
    m_transform.AddTail(pFGLAVSplitterLM.Detach());

    // Transform filters

#if INTERNAL_DECODER_MPEGAUDIO
    pFGF = tra[TRA_MPA] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MP3);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MPEG1AudioPayload);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MPEG1Payload);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MPEG1Packet);

    pFGF->AddType(MEDIATYPE_DVD_ENCRYPTED_PACK, MEDIASUBTYPE_MPEG2_AUDIO);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_MPEG2_AUDIO);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_MPEG2_AUDIO);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MPEG2_AUDIO);
#endif

#if INTERNAL_DECODER_AMR
    pFGF = tra[TRA_AMR] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_SAMR);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_AMR);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_SAWB);
#endif

#if INTERNAL_DECODER_LPCM
    pFGF = tra[TRA_LPCM] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_DVD_ENCRYPTED_PACK, MEDIASUBTYPE_DVD_LPCM_AUDIO);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_DVD_LPCM_AUDIO);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_DVD_LPCM_AUDIO);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_DVD_LPCM_AUDIO);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_HDMV_LPCM_AUDIO);
#endif

#if INTERNAL_DECODER_AC3
    pFGF = tra[TRA_AC3] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_DVD_ENCRYPTED_PACK, MEDIASUBTYPE_DOLBY_AC3);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_DOLBY_AC3);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_DOLBY_AC3);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_DOLBY_AC3);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_WAVE_DOLBY_AC3);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_DOLBY_TRUEHD);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_DOLBY_DDPLUS);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MLP);
#endif

#if INTERNAL_DECODER_DTS
    pFGF = tra[TRA_DTS] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_DVD_ENCRYPTED_PACK, MEDIASUBTYPE_DTS);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_DTS);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_DTS);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_DTS);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_WAVE_DTS);
#endif

#if INTERNAL_DECODER_AAC
    pFGF = tra[TRA_AAC] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_AAC);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_AAC);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_AAC);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_LATM_AAC);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_AAC_ADTS);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_MP4A);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_MP4A);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_MP4A);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_mp4a);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_mp4a);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_mp4a);
#endif

#if INTERNAL_DECODER_PS2AUDIO
    pFGF = tra[TRA_PS2AUD] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_PS2_PCM);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_PS2_PCM);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PS2_PCM);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_PS2_ADPCM);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_PS2_ADPCM);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PS2_ADPCM);
#endif

#if INTERNAL_DECODER_REALVIDEO
    pFGF = tra[TRA_RV] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_RV10);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_RV20);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_RV30);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_RV40);
#endif

#if INTERNAL_DECODER_REALAUDIO
    pFGF = tra[TRA_RA] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_14_4);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_28_8);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_ATRC);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_COOK);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_DNET);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_SIPR);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_RAAC);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_RACP);
#endif

#if INTERNAL_DECODER_VORBIS
    pFGF = tra[TRA_VORBIS] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_Vorbis2);
#endif

#if INTERNAL_DECODER_FLAC
    pFGF = tra[TRA_FLAC] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_FLAC_FRAMED);
#endif

#if INTERNAL_DECODER_NELLYMOSER
    pFGF = tra[TRA_NELLY] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NELLYMOSER);
#endif

#if INTERNAL_DECODER_ALAC
    pFGF = tra[TRA_ALAC] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_ALAC);
#endif

#if INTERNAL_DECODER_ALS
    pFGF = tra[TRA_ALS] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_ALS);
#endif

#if INTERNAL_DECODER_PCM
    pFGF = tra[TRA_PCM] ? pFGLAVAudio : pFGLAVAudioLM;
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_NONE);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_RAW);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_TWOS);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_SOWT);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_IN24);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_IN32);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_FL32);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_PCM_FL64);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_IEEE_FLOAT); // only for 64-bit float PCM
    /* todo: this should not depend on PCM */
#if INTERNAL_DECODER_ADPCM
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_IMA4);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_ADPCM_SWF);
    pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_ADPCM_AMV);
#endif
#endif

    // Add LAV Audio if needed
    if (!pFGLAVAudio->GetTypes().IsEmpty()) {
        m_transform.AddTail(pFGLAVAudio.Detach());
    }
    // Add low merit LAV Audio
    pFGLAVAudioLM->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NULL);
    m_transform.AddTail(pFGLAVAudioLM.Detach());

#if INTERNAL_DECODER_MPEG1
    pFGF = tra[TRA_MPEG1] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPEG1Packet);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPEG1Payload);
#endif

#if HAS_VIDEO_DECODERS
#if INTERNAL_DECODER_FLV
    pFGF = tra[TRA_FLV4] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_FLV1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_flv1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_FLV4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_flv4);
#endif
#if INTERNAL_DECODER_VP356
    pFGF = tra[TRA_VP356] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP30);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP31);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP50);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vp50);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP60);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vp60);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP61);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vp61);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP62);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vp62);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP6F);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vp6f);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP6A);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vp6a);
#endif
#if INTERNAL_DECODER_H264
    pFGF = tra[TRA_H264] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_H264);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_h264);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_X264);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_x264);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VSSH);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vssh);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DAVC);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_davc);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_PAVC);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_pavc);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_AVC1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_avc1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_H264_bis);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_CCV1);
#endif
#if INTERNAL_DECODER_VC1
    pFGF = tra[TRA_VC1] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_WVC1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_wvc1);
#endif
#if INTERNAL_DECODER_XVID
    pFGF = tra[TRA_XVID] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_XVID);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_xvid);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_XVIX);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_xvix);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MP4V);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mp4v);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_M4S2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_m4s2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MP4S);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mp4s);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_3IV1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_3iv1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_3IV2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_3iv2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_3IVX);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_3ivx);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_BLZ0);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_blz0);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DM4V);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_dm4v);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DXGM);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_dxgm);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_FMP4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_fmp4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_HDX4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_hdx4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_LMP4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_lmp4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_NDIG);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_ndig);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_RMP4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_rmp4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_SMP4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_smp4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_SEDG);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_sedg);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_UMP4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_ump4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_WV1F);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_wv1f);
#endif
#if INTERNAL_DECODER_DIVX
    pFGF = tra[TRA_DIVX] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIVX);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_divx);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DX50);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_dx50);
#endif
#if INTERNAL_DECODER_WMV
    pFGF = tra[TRA_WMV] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_WMV1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_wmv1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_WMV2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_wmv2);
#endif
#if INTERNAL_DECODER_WMV
    pFGF = tra[TRA_WMV] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_WMV3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_wmv3);
#endif
#if INTERNAL_DECODER_MSMPEG4
    pFGF = tra[TRA_MSMPEG4] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIV3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_div3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DVX3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_dvx3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MP43);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mp43);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_COL1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_col1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIV4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_div4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIV5);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_div5);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIV6);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_div6);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_AP41);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_ap41);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPG3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mpg3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIV2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_div2);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MP42);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mp42);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPG4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mpg4);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DIV1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_div1);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MP41);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_mp41);
#endif
#if INTERNAL_DECODER_SVQ
    pFGF = tra[TRA_SVQ3] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_SVQ3);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_SVQ1);
#endif
#if INTERNAL_DECODER_H263
    pFGF = tra[TRA_H263] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_H263);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_h263);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_S263);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_s263);
#endif
#if INTERNAL_DECODER_THEORA
    pFGF = tra[TRA_THEORA] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_THEORA);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_theora);
#endif
#if INTERNAL_DECODER_AMVV
    pFGF = tra[TRA_AMVV] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_AMVV);
#endif
#if INTERNAL_DECODER_VP8
    pFGF = tra[TRA_VP8] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VP80);
#endif
#if INTERNAL_DECODER_MJPEG
    pFGF = tra[TRA_MJPEG] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MJPG);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_QTJpeg);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MJPA);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MJPB);
#endif
#if INTERNAL_DECODER_INDEO
    pFGF = tra[TRA_INDEO] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_IV31);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_IV32);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_IV41);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_IV50);
#endif
#if INTERNAL_DECODER_SCREEN
    pFGF = tra[TRA_SCREEN] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_TSCC);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VMnc);
#endif
#if INTERNAL_DECODER_FLIC
    pFGF = tra[TRA_FLIC] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_FLIC);
#endif
#endif /* #if HAS_VIDEO_DECODERS */

#if INTERNAL_DECODER_MPEG2
    pFGF = tra[TRA_MPEG2] ? pFGLAVVideo : pFGLAVVideoLM;
    pFGF->AddType(MEDIATYPE_DVD_ENCRYPTED_PACK, MEDIASUBTYPE_MPEG2_VIDEO);
    pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_MPEG2_VIDEO);
    pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_MPEG2_VIDEO);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPEG2_VIDEO);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPG2);
#endif

    // Add LAV Video if needed
    if (!pFGLAVVideo->GetTypes().IsEmpty()) {
        m_transform.AddTail(pFGLAVVideo.Detach());
    }
    // Add low merit LAV video
    pFGLAVVideoLM->AddType(MEDIATYPE_Video, MEDIASUBTYPE_NULL);
    m_transform.AddTail(pFGLAVVideoLM.Detach());

    pFGF = DEBUG_NEW CFGFilterInternal<CNullTextRenderer>(L"NullTextRenderer", MERIT64_DO_USE);
    pFGF->AddType(MEDIATYPE_Text, MEDIASUBTYPE_NULL);
    pFGF->AddType(MEDIATYPE_ScriptCommand, MEDIASUBTYPE_NULL);
    pFGF->AddType(MEDIATYPE_Subtitle, MEDIASUBTYPE_NULL);
    pFGF->AddType(MEDIATYPE_Text, MEDIASUBTYPE_NULL);
    pFGF->AddType(MEDIATYPE_NULL, MEDIASUBTYPE_DVD_SUBPICTURE);
    pFGF->AddType(MEDIATYPE_NULL, MEDIASUBTYPE_CVD_SUBPICTURE);
    pFGF->AddType(MEDIATYPE_NULL, MEDIASUBTYPE_SVCD_SUBPICTURE);
    m_transform.AddTail(pFGF);

    // Blocked filters

    // "Subtitle Mixer" makes an access violation around the
    // 11-12th media type when enumerating them on its output.
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{00A95963-3BE5-48C0-AD9F-3356D67EA09D}")), MERIT64_DO_NOT_USE));

    // DiracSplitter.ax is crashing MPC-HC when opening invalid files...
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{09E7F58E-71A1-419D-B0A0-E524AE1454A9}")), MERIT64_DO_NOT_USE));
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{5899CFB9-948F-4869-A999-5544ECB38BA5}")), MERIT64_DO_NOT_USE));
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{F78CF248-180E-4713-B107-B13F7B5C31E1}")), MERIT64_DO_NOT_USE));

    // ISCR suxx
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{48025243-2D39-11CE-875D-00608CB78066}")), MERIT64_DO_NOT_USE));

    // Samsung's "mpeg-4 demultiplexor" can even open matroska files, amazing...
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{99EC0C72-4D1B-411B-AB1F-D561EE049D94}")), MERIT64_DO_NOT_USE));

    // LG Video Renderer (lgvid.ax) just crashes when trying to connect it
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{9F711C60-0668-11D0-94D4-0000C02BA972}")), MERIT64_DO_NOT_USE));

    // palm demuxer crashes (even crashes graphedit when dropping an .ac3 onto it)
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{BE2CF8A7-08CE-4A2C-9A25-FD726A999196}")), MERIT64_DO_NOT_USE));

    // DCDSPFilter (early versions crash mpc)
    {
        CRegKey key;

        TCHAR buff[256];
        ULONG len = sizeof(buff);
        ZeroMemory(buff, sizeof(buff));

        CString clsid = _T("{B38C58A0-1809-11D6-A458-EDAE78F1DF12}");

        if (ERROR_SUCCESS == key.Open(HKEY_CLASSES_ROOT, _T("CLSID\\") + clsid + _T("\\InprocServer32"), KEY_READ)
                && ERROR_SUCCESS == key.QueryStringValue(nullptr, buff, &len)
                && CFileVersionInfo::GetFileVersionNum(buff) < 0x0001000000030000ui64) {
            m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(clsid), MERIT64_DO_NOT_USE));
        }
    }

    /*
        // NVIDIA Transport Demux crashed for someone, I could not reproduce it
        m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{735823C1-ACC4-11D3-85AC-006008376FB8}")), MERIT64_DO_NOT_USE));
    */

    // mainconcept color space converter
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{272D77A0-A852-4851-ADA4-9091FEAD4C86}")), MERIT64_DO_NOT_USE));

    // VSFilter blocking routines
    if (s.fBlockVSFilter) {
        // If the internal renderer is enabled or XySubFilter is registered and we are using a compatible renderer
        if (s.IsISREnabled() || (IsCLSIDRegistered(CLSID_XySubFilter) && s.iDSVideoRendererType == VIDRNDT_DS_MADVR)) {
            // Disable VSFilter as a workaround for its aggressive loading
            m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(CLSID_VSFilter, MERIT64_DO_NOT_USE));
        }
    }

    // Blacklist Accusoft PICVideo M-JPEG Codec 2.1 since causes a DEP crash
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{4C4CD9E1-F876-11D2-962F-00500471FDDC}")), MERIT64_DO_NOT_USE));

    // Overrides

    WORD merit_low = 1;

    POSITION pos = s.m_filters.GetTailPosition();
    while (pos) {
        FilterOverride* fo = s.m_filters.GetPrev(pos);

        if (!fo->fDisabled && fo->name == _T("Broadcom Video Decoder")) {
            bOverrideBroadcom = true;
        }

        if (fo->fDisabled || fo->type == FilterOverride::EXTERNAL && !CPath(MakeFullPath(fo->path)).FileExists()) {
            continue;
        }

        ULONGLONG merit =
            fo->iLoadType == FilterOverride::PREFERRED ? MERIT64_ABOVE_DSHOW :
            fo->iLoadType == FilterOverride::MERIT ? MERIT64(fo->dwMerit) :
            MERIT64_DO_NOT_USE; // fo->iLoadType == FilterOverride::BLOCKED

        merit += merit_low++;

        CFGFilter* pFGF = nullptr;

        if (fo->type == FilterOverride::REGISTERED) {
            pFGF = DEBUG_NEW CFGFilterRegistry(fo->dispname, merit);
        } else if (fo->type == FilterOverride::EXTERNAL) {
            pFGF = DEBUG_NEW CFGFilterFile(fo->clsid, fo->path, CStringW(fo->name), merit);
        }

        if (pFGF) {
            pFGF->SetTypes(fo->guids);
            m_override.AddTail(pFGF);
        }
    }

    /* Use Broadcom decoder (if installed) for VC-1, H.264 and MPEG-2 */
    if (!bOverrideBroadcom) {
        pFGF = DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{2DE1D17E-46B1-42A8-9AEC-E20E80D9B1A9}")), MERIT64_ABOVE_DSHOW);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_H264);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_h264);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_X264);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_x264);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_VSSH);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_vssh);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_DAVC);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_davc);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_PAVC);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_pavc);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_AVC1);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_avc1);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_H264_bis);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_CCV1);

        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_WVC1);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_wvc1);

        pFGF->AddType(MEDIATYPE_DVD_ENCRYPTED_PACK, MEDIASUBTYPE_MPEG2_VIDEO);
        pFGF->AddType(MEDIATYPE_MPEG2_PACK, MEDIASUBTYPE_MPEG2_VIDEO);
        pFGF->AddType(MEDIATYPE_MPEG2_PES, MEDIASUBTYPE_MPEG2_VIDEO);
        pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_MPEG2_VIDEO);
        m_transform.AddHead(pFGF);
    }
}

STDMETHODIMP CFGManagerCustom::AddFilter(IBaseFilter* pBF, LPCWSTR pName)
{
    CAutoLock cAutoLock(this);

    HRESULT hr;

    if (FAILED(hr = __super::AddFilter(pBF, pName))) {
        return hr;
    }

    CAppSettings& s = AfxGetAppSettings();

    if (GetCLSID(pBF) == CLSID_DMOWrapperFilter) {
        if (CComQIPtr<IPropertyBag> pPB = pBF) {
            CComVariant var(true);
            pPB->Write(CComBSTR(L"_HIRESOUTPUT"), &var);
        }
    }

    if (CComQIPtr<IAudioSwitcherFilter> pASF = pBF) {
        pASF->EnableDownSamplingTo441(s.fDownSampleTo441);
        pASF->SetSpeakerConfig(s.fCustomChannelMapping, s.pSpeakerToChannelMap);
        pASF->SetAudioTimeShift(s.fAudioTimeShift ? 10000i64 * s.iAudioTimeShift : 0);
        pASF->SetNormalizeBoost2(s.fAudioNormalize, s.nAudioMaxNormFactor, s.fAudioNormalizeRecover, s.nAudioBoost);
    }

    return hr;
}

//
//  CFGManagerPlayer
//

CFGManagerPlayer::CFGManagerPlayer(LPCTSTR pName, LPUNKNOWN pUnk, HWND hWnd)
    : CFGManagerCustom(pName, pUnk)
    , m_hWnd(hWnd)
    , m_vrmerit(MERIT64(MERIT_PREFERRED))
    , m_armerit(MERIT64(MERIT_PREFERRED))
{
    TRACE(_T("--> CFGManagerPlayer::CFGManagerPlayer on thread: %d\n"), GetCurrentThreadId());
    CFGFilter* pFGF;

    const CAppSettings& s = AfxGetAppSettings();

    if (m_pFM) {
        CComPtr<IEnumMoniker> pEM;

        GUID guidsVideo[] = {MEDIATYPE_Video, MEDIASUBTYPE_NULL};

        if (SUCCEEDED(m_pFM->EnumMatchingFilters(&pEM, 0, FALSE, MERIT_DO_NOT_USE + 1,
                      TRUE, 1, guidsVideo, nullptr, nullptr, TRUE, FALSE, 0, nullptr, nullptr, nullptr))) {
            for (CComPtr<IMoniker> pMoniker; S_OK == pEM->Next(1, &pMoniker, nullptr); pMoniker = nullptr) {
                CFGFilterRegistry f(pMoniker);
                // RDP DShow Redirection Filter's merit is so high that it flaws the graph building process so we ignore it.
                // Without doing that the renderer selected in MPC-HC is given a so high merit that filters that normally
                // should connect between the video decoder and the renderer can't (e.g. VSFilter).
                if (f.GetCLSID() != CLSID_RDPDShowRedirectionFilter) {
                    m_vrmerit = max(m_vrmerit, f.GetMerit());
                }
            }
        }

        m_vrmerit += 0x100;

        pEM.Release();

        GUID guidsAudio[] = {MEDIATYPE_Audio, MEDIASUBTYPE_NULL};

        if (SUCCEEDED(m_pFM->EnumMatchingFilters(&pEM, 0, FALSE, MERIT_DO_NOT_USE + 1,
                      TRUE, 1, guidsAudio, nullptr, nullptr, TRUE, FALSE, 0, nullptr, nullptr, nullptr))) {
            for (CComPtr<IMoniker> pMoniker; S_OK == pEM->Next(1, &pMoniker, nullptr); pMoniker = nullptr) {
                CFGFilterRegistry f(pMoniker);
                m_armerit = max(m_armerit, f.GetMerit());
            }
        }

        BeginEnumSysDev(CLSID_AudioRendererCategory, pMoniker) {
            CFGFilterRegistry f(pMoniker);
            m_armerit = max(m_armerit, f.GetMerit());
        }
        EndEnumSysDev;

        m_armerit += 0x100;
    }

    // Switchers

    if (s.fEnableAudioSwitcher) {
        pFGF = DEBUG_NEW CFGFilterInternal<CAudioSwitcherFilter>(L"Audio Switcher", m_armerit + 0x100);
        pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NULL);
        m_transform.AddTail(pFGF);

        // Blacklist Morgan's Stream Switcher
        m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(CLSID_MorganStreamSwitcher, MERIT64_DO_NOT_USE));
    }

    // Renderers

    switch (s.iDSVideoRendererType) {
        case VIDRNDT_DS_OLDRENDERER:
            m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(CLSID_VideoRenderer, m_vrmerit));
            break;
        case VIDRNDT_DS_OVERLAYMIXER:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_OverlayMixer, ResStr(IDS_PPAGE_OUTPUT_OVERLAYMIXER), m_vrmerit));
            break;
        case VIDRNDT_DS_VMR7WINDOWED:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_VideoMixingRenderer, ResStr(IDS_PPAGE_OUTPUT_VMR7WINDOWED), m_vrmerit));
            break;
        case VIDRNDT_DS_VMR9WINDOWED:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_VideoMixingRenderer9, ResStr(IDS_PPAGE_OUTPUT_VMR9WINDOWED), m_vrmerit));
            break;
        case VIDRNDT_DS_VMR7RENDERLESS:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_VMR7AllocatorPresenter, ResStr(IDS_PPAGE_OUTPUT_VMR7RENDERLESS), m_vrmerit));
            break;
        case VIDRNDT_DS_VMR9RENDERLESS:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_VMR9AllocatorPresenter, ResStr(IDS_PPAGE_OUTPUT_VMR9RENDERLESS), m_vrmerit));
            break;
        case VIDRNDT_DS_EVR:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_EnhancedVideoRenderer, ResStr(IDS_PPAGE_OUTPUT_EVR), m_vrmerit));
            break;
        case VIDRNDT_DS_EVR_CUSTOM:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_EVRAllocatorPresenter, ResStr(IDS_PPAGE_OUTPUT_EVR_CUSTOM), m_vrmerit));
            break;
        case VIDRNDT_DS_DXR:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_DXRAllocatorPresenter, ResStr(IDS_PPAGE_OUTPUT_DXR), m_vrmerit));
            break;
        case VIDRNDT_DS_MADVR:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_madVRAllocatorPresenter, ResStr(IDS_PPAGE_OUTPUT_MADVR), m_vrmerit));
            break;
        case VIDRNDT_DS_SYNC:
            m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_SyncAllocatorPresenter, ResStr(IDS_PPAGE_OUTPUT_SYNC), m_vrmerit));
            break;
        case VIDRNDT_DS_NULL_COMP:
            pFGF = DEBUG_NEW CFGFilterInternal<CNullVideoRenderer>(ResStr(IDS_PPAGE_OUTPUT_NULL_COMP), MERIT64_ABOVE_DSHOW + 2);
            pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_NULL);
            m_transform.AddTail(pFGF);
            break;
        case VIDRNDT_DS_NULL_UNCOMP:
            pFGF = DEBUG_NEW CFGFilterInternal<CNullUVideoRenderer>(ResStr(IDS_PPAGE_OUTPUT_NULL_UNCOMP), MERIT64_ABOVE_DSHOW + 2);
            pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_NULL);
            m_transform.AddTail(pFGF);
            break;
    }

    CString SelAudioRenderer = s.SelectedAudioRenderer();
    if (SelAudioRenderer == AUDRNDT_NULL_COMP) {
        pFGF = DEBUG_NEW CFGFilterInternal<CNullAudioRenderer>(AUDRNDT_NULL_COMP, MERIT64_ABOVE_DSHOW + 2);
        pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NULL);
        m_transform.AddTail(pFGF);
    } else if (SelAudioRenderer == AUDRNDT_NULL_UNCOMP) {
        pFGF = DEBUG_NEW CFGFilterInternal<CNullUAudioRenderer>(AUDRNDT_NULL_UNCOMP, MERIT64_ABOVE_DSHOW + 2);
        pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NULL);
        m_transform.AddTail(pFGF);
    } else if (SelAudioRenderer == AUDRNDT_MPC) {
        pFGF = DEBUG_NEW CFGFilterInternal<CMpcAudioRenderer>(AUDRNDT_MPC, MERIT64_ABOVE_DSHOW + 2);
        pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NULL);
        m_transform.AddTail(pFGF);
    } else if (!SelAudioRenderer.IsEmpty()) {
        pFGF = DEBUG_NEW CFGFilterRegistry(SelAudioRenderer, m_armerit);
        pFGF->AddType(MEDIATYPE_Audio, MEDIASUBTYPE_NULL);
        m_transform.AddTail(pFGF);
    }
}

STDMETHODIMP CFGManagerPlayer::ConnectDirect(IPin* pPinOut, IPin* pPinIn, const AM_MEDIA_TYPE* pmt)
{
    CAutoLock cAutoLock(this);

    if (GetCLSID(pPinOut) == CLSID_MPEG2Demultiplexer) {
        CComQIPtr<IMediaSeeking> pMS = pPinOut;
        REFERENCE_TIME rtDur = 0;
        if (!pMS || FAILED(pMS->GetDuration(&rtDur)) || rtDur <= 0) {
            return E_FAIL;
        }
    }

    return __super::ConnectDirect(pPinOut, pPinIn, pmt);
}

//
// CFGManagerDVD
//

CFGManagerDVD::CFGManagerDVD(LPCTSTR pName, LPUNKNOWN pUnk, HWND hWnd)
    : CFGManagerPlayer(pName, pUnk, hWnd)
{
    const CAppSettings& s = AfxGetAppSettings();

    // have to avoid the old video renderer
    if (s.iDSVideoRendererType == VIDRNDT_DS_OLDRENDERER) {
        m_transform.AddTail(DEBUG_NEW CFGFilterVideoRenderer(m_hWnd, CLSID_OverlayMixer, L"Overlay Mixer", m_vrmerit - 1));
    }

    // elecard's decoder isn't suited for dvd playback (atm)
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(GUIDFromCString(_T("{F50B3F13-19C4-11CF-AA9A-02608C9BABA2}")), MERIT64_DO_NOT_USE));
}

class CResetDVD : public CDVDSession
{
public:
    CResetDVD(LPCTSTR path) {
        if (Open(path)) {
            if (BeginSession()) {
                Authenticate(); /*GetDiscKey();*/
                EndSession();
            }
            Close();
        }
    }
};

STDMETHODIMP CFGManagerDVD::RenderFile(LPCWSTR lpcwstrFile, LPCWSTR lpcwstrPlayList)
{
    CAutoLock cAutoLock(this);

    HRESULT hr;

    CComPtr<IBaseFilter> pBF;
    if (FAILED(hr = AddSourceFilter(lpcwstrFile, lpcwstrFile, &pBF))) {
        return hr;
    }

    return ConnectFilter(pBF, nullptr);
}

STDMETHODIMP CFGManagerDVD::AddSourceFilter(LPCWSTR lpcwstrFileName, LPCWSTR lpcwstrFilterName, IBaseFilter** ppFilter)
{
    CAutoLock cAutoLock(this);

    CheckPointer(lpcwstrFileName, E_POINTER);
    CheckPointer(ppFilter, E_POINTER);

    HRESULT hr;

    CStringW fn = CStringW(lpcwstrFileName).TrimLeft();

    GUID clsid = CLSID_DVDNavigator;

    CComPtr<IBaseFilter> pBF;
    if (FAILED(hr = pBF.CoCreateInstance(clsid))
            || FAILED(hr = AddFilter(pBF, L"DVD Navigator"))) {
        return VFW_E_CANNOT_LOAD_SOURCE_FILTER;
    }

    CComQIPtr<IDvdControl2> pDVDC;
    CComQIPtr<IDvdInfo2> pDVDI;

    if (!((pDVDC = pBF) && (pDVDI = pBF))) {
        return E_NOINTERFACE;
    }

    WCHAR buff[MAX_PATH];
    ULONG len;
    if ((!fn.IsEmpty()
            && FAILED(hr = pDVDC->SetDVDDirectory(fn))
            && FAILED(hr = pDVDC->SetDVDDirectory(fn + L"VIDEO_TS"))
            && FAILED(hr = pDVDC->SetDVDDirectory(fn + L"\\VIDEO_TS")))
            || FAILED(hr = pDVDI->GetDVDDirectory(buff, _countof(buff), &len)) || len == 0) {
        return E_INVALIDARG;
    }

    pDVDC->SetOption(DVD_ResetOnStop, FALSE);
    pDVDC->SetOption(DVD_HMSF_TimeCodeEvents, TRUE);

    if (clsid == CLSID_DVDNavigator) {
        CResetDVD(CString(buff));
    }

    *ppFilter = pBF.Detach();

    return S_OK;
}

//
// CFGManagerCapture
//

CFGManagerCapture::CFGManagerCapture(LPCTSTR pName, LPUNKNOWN pUnk, HWND hWnd)
    : CFGManagerPlayer(pName, pUnk, hWnd)
{
    CFGFilter* pFGF = DEBUG_NEW CFGFilterInternal<CDeinterlacerFilter>(L"Deinterlacer", m_vrmerit + 0x100);
    pFGF->AddType(MEDIATYPE_Video, MEDIASUBTYPE_NULL);
    m_transform.AddTail(pFGF);

    // Blacklist Morgan's Stream Switcher
    m_transform.AddTail(DEBUG_NEW CFGFilterRegistry(CLSID_MorganStreamSwitcher, MERIT64_DO_NOT_USE));
}

//
// CFGManagerMuxer
//

CFGManagerMuxer::CFGManagerMuxer(LPCTSTR pName, LPUNKNOWN pUnk)
    : CFGManagerCustom(pName, pUnk)
{
    m_source.AddTail(DEBUG_NEW CFGFilterInternal<CSubtitleSourceASS>());
}

//
// CFGAggregator
//

CFGAggregator::CFGAggregator(const CLSID& clsid, LPCTSTR pName, LPUNKNOWN pUnk, HRESULT& hr)
    : CUnknown(pName, pUnk)
{
    hr = m_pUnkInner.CoCreateInstance(clsid, GetOwner());
}

CFGAggregator::~CFGAggregator()
{
    m_pUnkInner.Release();
}

STDMETHODIMP CFGAggregator::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);

    return
        m_pUnkInner && (riid != IID_IUnknown && SUCCEEDED(m_pUnkInner->QueryInterface(riid, ppv))) ? S_OK :
        __super::NonDelegatingQueryInterface(riid, ppv);
}
