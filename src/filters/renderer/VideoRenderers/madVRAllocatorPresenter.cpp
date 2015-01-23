/*
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

#include "stdafx.h"
#include "madVRAllocatorPresenter.h"
#include "../../../SubPic/DX9SubPic.h"
#include "../../../SubPic/SubPicQueueImpl.h"
#include "RenderersSettings.h"
#include "moreuuids.h"

using namespace DSObjects;

#define ShaderStage_PreScale 0
#define ShaderStage_PostScale 1

extern bool g_bExternalSubtitleTime;

//
// CmadVRAllocatorPresenter
//

CmadVRAllocatorPresenter::CmadVRAllocatorPresenter(HWND hWnd, HRESULT& hr, CString& _Error)
    : CSubPicAllocatorPresenterImpl(hWnd, hr, &_Error)
    , m_ScreenSize(0, 0)
    , m_bIsFullscreen(false)
{
    if (FAILED(hr)) {
        _Error += L"ISubPicAllocatorPresenterImpl failed\n";
        return;
    }

    hr = S_OK;
}

CmadVRAllocatorPresenter::~CmadVRAllocatorPresenter()
{
    if (m_pSRCB) {
        // nasty, but we have to let it know about our death somehow
        ((CSubRenderCallback*)(ISubRenderCallback2*)m_pSRCB)->SetDXRAP(nullptr);
    }

    // the order is important here
    m_pSubPicQueue = nullptr;
    m_pAllocator = nullptr;
    m_pDXR = nullptr;
}

STDMETHODIMP CmadVRAllocatorPresenter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    /*
    if (riid == __uuidof(IVideoWindow))
        return GetInterface((IVideoWindow*)this, ppv);
    if (riid == __uuidof(IBasicVideo))
        return GetInterface((IBasicVideo*)this, ppv);
    if (riid == __uuidof(IBasicVideo2))
        return GetInterface((IBasicVideo2*)this, ppv);
    */
    /*
    if (riid == __uuidof(IVMRWindowlessControl))
        return GetInterface((IVMRWindowlessControl*)this, ppv);
    */

    if (riid != IID_IUnknown && m_pDXR) {
        if (SUCCEEDED(m_pDXR->QueryInterface(riid, ppv))) {
            return S_OK;
        }
    }

    return __super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CmadVRAllocatorPresenter::SetDevice(IDirect3DDevice9* pD3DDev)
{
    const CRenderersSettings& r = GetRenderersSettings();

    if (!pD3DDev) {
        // release all resources
        m_pSubPicQueue = nullptr;
        m_pAllocator = nullptr;
        return S_OK;
    }

    InitMaxSubtitleTextureSize(r.subPicQueueSettings.nMaxRes, m_ScreenSize);

    if (m_pAllocator) {
        m_pAllocator->ChangeDevice(pD3DDev);
    } else {
        m_pAllocator = DEBUG_NEW CDX9SubPicAllocator(pD3DDev, m_maxSubtitleTextureSize, true);
    }

    HRESULT hr = S_OK;
    if (!m_pSubPicQueue) {
        CAutoLock cAutoLock(this);
        m_pSubPicQueue = r.subPicQueueSettings.nSize > 0
                         ? (ISubPicQueue*)DEBUG_NEW CSubPicQueue(r.subPicQueueSettings, m_pAllocator, &hr)
                         : (ISubPicQueue*)DEBUG_NEW CSubPicQueueNoThread(r.subPicQueueSettings, m_pAllocator, &hr);
    } else {
        m_pSubPicQueue->Invalidate();
    }

    if (SUCCEEDED(hr) && m_pSubPicProvider) {
        m_pSubPicQueue->SetSubPicProvider(m_pSubPicProvider);
    }

    return hr;
}

HRESULT CmadVRAllocatorPresenter::Render(
    REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, REFERENCE_TIME atpf,
    int left, int top, int right, int bottom, int width, int height)
{
    CRect wndRect(0, 0, width, height);
    CRect videoRect(left, top, right, bottom);
    __super::SetPosition(wndRect, videoRect); // needed? should be already set by the player
    if (!g_bExternalSubtitleTime) {
        SetTime(rtStart);
    }
    if (atpf > 0 && m_pSubPicQueue) {
        m_fps = 10000000.0 / atpf;
        m_pSubPicQueue->SetFPS(m_fps);
    }
    AlphaBltSubPic(wndRect, videoRect);
    return S_OK;
}

// ISubPicAllocatorPresenter

STDMETHODIMP CmadVRAllocatorPresenter::CreateRenderer(IUnknown** ppRenderer)
{
    CheckPointer(ppRenderer, E_POINTER);

    if (m_pDXR) {
        return E_UNEXPECTED;
    }
    m_pDXR.CoCreateInstance(CLSID_madVR, GetOwner());
    if (!m_pDXR) {
        return E_FAIL;
    }

    CComQIPtr<ISubRender> pSR = m_pDXR;
    if (!pSR) {
        m_pDXR = nullptr;
        return E_FAIL;
    }

    m_pSRCB = DEBUG_NEW CSubRenderCallback(this);
    if (FAILED(pSR->SetCallback(m_pSRCB))) {
        m_pDXR = nullptr;
        return E_FAIL;
    }

    (*ppRenderer = (IUnknown*)(INonDelegatingUnknown*)(this))->AddRef();

    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        m_ScreenSize.SetSize(mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
    }

    return S_OK;
}

STDMETHODIMP_(void) CmadVRAllocatorPresenter::SetPosition(RECT w, RECT v)
{
    if (CComQIPtr<IBasicVideo> pBV = m_pDXR) {
        pBV->SetDefaultSourcePosition();
        pBV->SetDestinationPosition(v.left, v.top, v.right - v.left, v.bottom - v.top);
    }

    if (CComQIPtr<IVideoWindow> pVW = m_pDXR) {
        pVW->SetWindowPosition(w.left, w.top, w.right - w.left, w.bottom - w.top);
    }
}

STDMETHODIMP_(SIZE) CmadVRAllocatorPresenter::GetVideoSize(bool bCorrectAR) const
{
    SIZE size = {0, 0};

    if (!bCorrectAR) {
        if (CComQIPtr<IBasicVideo> pBV = m_pDXR) {
            pBV->GetVideoSize(&size.cx, &size.cy);
        }
    } else {
        if (CComQIPtr<IBasicVideo2> pBV2 = m_pDXR) {
            pBV2->GetPreferredAspectRatio(&size.cx, &size.cy);
        }
    }

    return size;
}

STDMETHODIMP_(bool) CmadVRAllocatorPresenter::Paint(bool bAll)
{
    return false; // TODO
}

STDMETHODIMP CmadVRAllocatorPresenter::GetDIB(BYTE* lpDib, DWORD* size)
{
    HRESULT hr = E_NOTIMPL;
    if (CComQIPtr<IBasicVideo> pBV = m_pDXR) {
        hr = pBV->GetCurrentImage((long*)size, (long*)lpDib);
    }
    return hr;
}

STDMETHODIMP CmadVRAllocatorPresenter::SetPixelShader(LPCSTR pSrcData, LPCSTR pTarget)
{
    HRESULT hr = E_NOTIMPL;
    if (CComQIPtr<IMadVRExternalPixelShaders> pEPS = m_pDXR) {
        if (!pSrcData && !pTarget) {
            hr = pEPS->ClearPixelShaders(false);
        } else {
            hr = pEPS->AddPixelShader(pSrcData, pTarget, ShaderStage_PreScale, nullptr);
        }
    }
    return hr;
}

STDMETHODIMP CmadVRAllocatorPresenter::SetPixelShader2(LPCSTR pSrcData, LPCSTR pTarget, bool bScreenSpace)
{
    HRESULT hr = E_NOTIMPL;
    if (CComQIPtr<IMadVRExternalPixelShaders> pEPS = m_pDXR) {
        if (!pSrcData && !pTarget) {
            hr = pEPS->ClearPixelShaders(bScreenSpace);
        } else {
            hr = pEPS->AddPixelShader(pSrcData, pTarget, bScreenSpace ? ShaderStage_PostScale : ShaderStage_PreScale, nullptr);
        }
    }
    return hr;
}
