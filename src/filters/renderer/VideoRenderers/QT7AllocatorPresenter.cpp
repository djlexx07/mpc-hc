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

#include "stdafx.h"
#include "QT7AllocatorPresenter.h"

using namespace DSObjects;

//
// CQT7AllocatorPresenter
//

CQT7AllocatorPresenter::CQT7AllocatorPresenter(HWND hWnd, HRESULT& hr)
    : CDX7AllocatorPresenter(hWnd, hr)
{
}

STDMETHODIMP CQT7AllocatorPresenter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);

    return
        QI(IQTVideoSurface)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CQT7AllocatorPresenter::AllocSurfaces()
{
    CAutoLock cAutoLock(this);

    CheckPointer(m_pDD, E_POINTER);

    m_pVideoSurfaceOff = nullptr;

    DDSURFACEDESC2 ddsd;
    INITDDSTRUCT(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = m_nativeVideoSize.cx;
    ddsd.dwHeight = m_nativeVideoSize.cy;
    ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
    ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
    ddsd.ddpfPixelFormat.dwRGBAlphaBitMask  = 0xFF000000;
    ddsd.ddpfPixelFormat.dwRBitMask         = 0x00FF0000;
    ddsd.ddpfPixelFormat.dwGBitMask         = 0x0000FF00;
    ddsd.ddpfPixelFormat.dwBBitMask         = 0x000000FF;

    HRESULT hr = m_pDD->CreateSurface(&ddsd, &m_pVideoSurfaceOff, nullptr);
    if (FAILED(hr)) {
        return E_FAIL;
    }

    DDBLTFX fx;
    INITDDSTRUCT(fx);
    fx.dwFillColor = 0;
    m_pVideoSurfaceOff->Blt(nullptr, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, &fx);

    return __super::AllocSurfaces();
}

void CQT7AllocatorPresenter::DeleteSurfaces()
{
    CAutoLock cAutoLock(this);

    m_pVideoSurfaceOff = nullptr;

    __super::DeleteSurfaces();
}

// IQTVideoSurface

STDMETHODIMP CQT7AllocatorPresenter::BeginBlt(const BITMAP& bm)
{
    CAutoLock cAutoLock(this);

    DeleteSurfaces();

    SetVideoSize(CSize(bm.bmWidth, abs(bm.bmHeight)));

    HRESULT hr;
    if (FAILED(hr = AllocSurfaces())) {
        return hr;
    }

    return S_OK;
}

STDMETHODIMP CQT7AllocatorPresenter::DoBlt(const BITMAP& bm)
{
    if (!m_pVideoSurface || !m_pVideoSurfaceOff) {
        return E_FAIL;
    }

    bool fOk = false;

    DDSURFACEDESC2 ddsd;
    INITDDSTRUCT(ddsd);
    if (FAILED(m_pVideoSurfaceOff->GetSurfaceDesc(&ddsd))) {
        return E_FAIL;
    }

    UINT w = (UINT)bm.bmWidth;
    UINT h = abs(bm.bmHeight);
    int bpp = bm.bmBitsPixel;

    if ((bpp == 16 || bpp == 24 || bpp == 32) && w == ddsd.dwWidth && h == ddsd.dwHeight) {
        INITDDSTRUCT(ddsd);
        if (SUCCEEDED(m_pVideoSurfaceOff->Lock(nullptr, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR | DDLOCK_WRITEONLY, nullptr))) {
            BitBltFromRGBToRGB(
                w, h,
                (BYTE*)ddsd.lpSurface, ddsd.lPitch, ddsd.ddpfPixelFormat.dwRGBBitCount,
                (BYTE*)bm.bmBits, bm.bmWidthBytes, bm.bmBitsPixel);
            m_pVideoSurfaceOff->Unlock(nullptr);
            fOk = true;
        }
    }

    if (!fOk) {
        DDBLTFX fx;
        INITDDSTRUCT(fx);
        fx.dwFillColor = 0;
        m_pVideoSurfaceOff->Blt(nullptr, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, &fx);

        HDC hDC;
        if (SUCCEEDED(m_pVideoSurfaceOff->GetDC(&hDC))) {
            CString str;
            str.Format(_T("Sorry, this format is not supported"));

            SetBkColor(hDC, 0);
            SetTextColor(hDC, 0x404040);
            TextOut(hDC, 10, 10, str, str.GetLength());

            m_pVideoSurfaceOff->ReleaseDC(hDC);
        }
    }

    m_pVideoSurface->Blt(nullptr, m_pVideoSurfaceOff, nullptr, DDBLT_WAIT, nullptr);

    Paint(true);

    return S_OK;
}
