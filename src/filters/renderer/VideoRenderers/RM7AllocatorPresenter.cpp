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
#include "RM7AllocatorPresenter.h"

using namespace DSObjects;

//
// CRM7AllocatorPresenter
//

CRM7AllocatorPresenter::CRM7AllocatorPresenter(HWND hWnd, HRESULT& hr)
    : CDX7AllocatorPresenter(hWnd, hr)
{
    ZeroMemory(&m_bitmapInfo, sizeof(m_bitmapInfo));
    ZeroMemory(&m_lastBitmapInfo, sizeof(m_lastBitmapInfo));
}

STDMETHODIMP CRM7AllocatorPresenter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);

    return
        QI2(IRMAVideoSurface)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CRM7AllocatorPresenter::AllocSurfaces()
{
    CAutoLock cAutoLock(this);

    CheckPointer(m_pDD, E_POINTER);

    m_pVideoSurfaceOff  = nullptr;
    m_pVideoSurfaceYUY2 = nullptr;

    DDSURFACEDESC2 ddsd;
    DDBLTFX fx;

    INITDDSTRUCT(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = m_nativeVideoSize.cx;
    ddsd.dwHeight = m_nativeVideoSize.cy;
    ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
    ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
    ddsd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
    ddsd.ddpfPixelFormat.dwRBitMask        = 0x00FF0000;
    ddsd.ddpfPixelFormat.dwGBitMask        = 0x0000FF00;
    ddsd.ddpfPixelFormat.dwBBitMask        = 0x000000FF;

    if (FAILED(m_pDD->CreateSurface(&ddsd, &m_pVideoSurfaceOff, nullptr))) {
        return E_FAIL;
    }

    INITDDSTRUCT(fx);
    fx.dwFillColor = 0;
    m_pVideoSurfaceOff->Blt(nullptr, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, &fx);

    INITDDSTRUCT(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = m_nativeVideoSize.cx;
    ddsd.dwHeight = m_nativeVideoSize.cy;
    ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    ddsd.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
    ddsd.ddpfPixelFormat.dwYUVBitCount = 16;
    ddsd.ddpfPixelFormat.dwFourCC = '2YUY';

    m_pDD->CreateSurface(&ddsd, &m_pVideoSurfaceYUY2, nullptr);

    if (FAILED(m_pVideoSurfaceOff->Blt(nullptr, m_pVideoSurfaceYUY2, nullptr, DDBLT_WAIT, nullptr))) {
        m_pVideoSurfaceYUY2 = nullptr;
    }

    if (m_pVideoSurfaceYUY2) {
        INITDDSTRUCT(fx);
        fx.dwFillColor = 0x80108010;
        m_pVideoSurfaceYUY2->Blt(nullptr, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, &fx);
    }

    return __super::AllocSurfaces();
}

void CRM7AllocatorPresenter::DeleteSurfaces()
{
    CAutoLock cAutoLock(this);

    m_pVideoSurfaceOff  = nullptr;
    m_pVideoSurfaceYUY2 = nullptr;

    __super::DeleteSurfaces();
}

// IRMAVideoSurface

STDMETHODIMP CRM7AllocatorPresenter::Blt(UCHAR* pImageData, RMABitmapInfoHeader* pBitmapInfo, REF(PNxRect) inDestRect, REF(PNxRect) inSrcRect)
{
    if (!m_pVideoSurface || !m_pVideoSurfaceOff) {
        return E_FAIL;
    }

    bool fRGB = false;
    bool fYUY2 = false;

    CRect src((RECT*)&inSrcRect), dst((RECT*)&inDestRect), src2(CPoint(0, 0), src.Size());
    if (src.Width() > dst.Width() || src.Height() > dst.Height()) {
        return E_FAIL;
    }

    DDSURFACEDESC2 ddsd;

    if (pBitmapInfo->biCompression == '024I') {
        DWORD pitch = pBitmapInfo->biWidth;
        DWORD size = pitch * abs(pBitmapInfo->biHeight);

        BYTE* y = pImageData                   + src.top * pitch + src.left;
        BYTE* u = pImageData + size            + src.top * (pitch / 2) + src.left / 2;
        BYTE* v = pImageData + size + size / 4 + src.top * (pitch / 2) + src.left / 2;

        if (m_pVideoSurfaceYUY2) {
            INITDDSTRUCT(ddsd);
            if (SUCCEEDED(m_pVideoSurfaceYUY2->Lock(src2, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR | DDLOCK_WRITEONLY, nullptr))) {
                BitBltFromI420ToYUY2(src.Width(), src.Height(), (BYTE*)ddsd.lpSurface, ddsd.lPitch, y, u, v, pitch);
                m_pVideoSurfaceYUY2->Unlock(src2);
                fYUY2 = true;
            }
        } else {
            INITDDSTRUCT(ddsd);
            if (SUCCEEDED(m_pVideoSurfaceOff->Lock(src2, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR | DDLOCK_WRITEONLY, nullptr))) {
                BitBltFromI420ToRGB(src.Width(), src.Height(), (BYTE*)ddsd.lpSurface, ddsd.lPitch, ddsd.ddpfPixelFormat.dwRGBBitCount, y, u, v, pitch);
                m_pVideoSurfaceOff->Unlock(src2);
                fRGB = true;
            }
        }
    } else if (pBitmapInfo->biCompression == '2YUY') {
        DWORD w = pBitmapInfo->biWidth;
        DWORD h = abs(pBitmapInfo->biHeight);
        DWORD pitch = pBitmapInfo->biWidth * 2;
        UNREFERENCED_PARAMETER(w);
        UNREFERENCED_PARAMETER(h);

        BYTE* yvyu = pImageData + src.top * pitch + src.left * 2;

        if (m_pVideoSurfaceYUY2) {
            INITDDSTRUCT(ddsd);
            if (SUCCEEDED(m_pVideoSurfaceYUY2->Lock(src2, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR | DDLOCK_WRITEONLY, nullptr))) {
                BitBltFromYUY2ToYUY2(src.Width(), src.Height(), (BYTE*)ddsd.lpSurface, ddsd.lPitch, yvyu, pitch);
                m_pVideoSurfaceYUY2->Unlock(src2);
                fYUY2 = true;
            }
        } else {
            INITDDSTRUCT(ddsd);
            if (SUCCEEDED(m_pVideoSurfaceOff->Lock(src2, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR | DDLOCK_WRITEONLY, nullptr))) {
                BitBltFromYUY2ToRGB(src.Width(), src.Height(), (BYTE*)ddsd.lpSurface, ddsd.lPitch, ddsd.ddpfPixelFormat.dwRGBBitCount, yvyu, pitch);
                m_pVideoSurfaceOff->Unlock(src2);
                fRGB = true;
            }
        }
    } else if (pBitmapInfo->biCompression == 0 || pBitmapInfo->biCompression == 3
               || pBitmapInfo->biCompression == 'BGRA') {
        DWORD w = pBitmapInfo->biWidth;
        DWORD h = abs(pBitmapInfo->biHeight);
        DWORD pitch = pBitmapInfo->biWidth * pBitmapInfo->biBitCount >> 3;
        UNREFERENCED_PARAMETER(w);
        UNREFERENCED_PARAMETER(h);

        BYTE* rgb = pImageData + src.top * pitch + src.left * (pBitmapInfo->biBitCount >> 3);

        INITDDSTRUCT(ddsd);
        if (SUCCEEDED(m_pVideoSurfaceOff->Lock(src2, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR | DDLOCK_WRITEONLY, nullptr))) {
            BYTE* lpSurface = (BYTE*)ddsd.lpSurface;
            if (pBitmapInfo->biHeight > 0) {
                lpSurface += ddsd.lPitch * (src.Height() - 1);
                ddsd.lPitch = -ddsd.lPitch;
            }
            BitBltFromRGBToRGB(src.Width(), src.Height(), lpSurface, ddsd.lPitch, ddsd.ddpfPixelFormat.dwRGBBitCount, rgb, pitch, pBitmapInfo->biBitCount);
            fRGB = true;
            m_pVideoSurfaceOff->Unlock(src2);
        }
    }

    if (!fRGB && !fYUY2) {
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

            fRGB = true;
        }
    }

    if (fRGB) {
        m_pVideoSurface->Blt(dst, m_pVideoSurfaceOff, src2, DDBLT_WAIT, nullptr);
    }
    if (fYUY2) {
        m_pVideoSurface->Blt(dst, m_pVideoSurfaceYUY2, src2, DDBLT_WAIT, nullptr);
    }

    Paint(true);

    return PNR_OK;
}

STDMETHODIMP CRM7AllocatorPresenter::BeginOptimizedBlt(RMABitmapInfoHeader* pBitmapInfo)
{
    CAutoLock cAutoLock(this);
    DeleteSurfaces();
    SetVideoSize(CSize(pBitmapInfo->biWidth, abs(pBitmapInfo->biHeight)));
    if (FAILED(AllocSurfaces())) {
        return E_FAIL;
    }
    return PNR_NOTIMPL;
}

STDMETHODIMP CRM7AllocatorPresenter::OptimizedBlt(UCHAR* pImageBits, REF(PNxRect) rDestRect, REF(PNxRect) rSrcRect)
{
    return PNR_NOTIMPL;
}

STDMETHODIMP CRM7AllocatorPresenter::EndOptimizedBlt()
{
    return PNR_NOTIMPL;
}

STDMETHODIMP CRM7AllocatorPresenter::GetOptimizedFormat(REF(RMA_COMPRESSION_TYPE) ulType)
{
    return PNR_NOTIMPL;
}

STDMETHODIMP CRM7AllocatorPresenter::GetPreferredFormat(REF(RMA_COMPRESSION_TYPE) ulType)
{
    ulType = RMA_I420;
    return PNR_OK;
}
