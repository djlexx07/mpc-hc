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
#include "mplayerc.h"
#include "PlayerStatusBar.h"
#include "MainFrm.h"
#include "DSUtil.h"

// CPlayerStatusBar

IMPLEMENT_DYNAMIC(CPlayerStatusBar, CDialogBar)

CPlayerStatusBar::CPlayerStatusBar(CMainFrame* pMainFrame)
    : m_pMainFrame(pMainFrame)
    , m_status(pMainFrame->m_dpi, false, true)
    , m_time(pMainFrame->m_dpi, true, false)
    , m_bmid(0)
    , m_hIcon(0)
    , m_time_rect(-1, -1, -1, -1)
{
    EventRouter::EventSelection fires;
    fires.insert(MpcEvent::STREAM_POS_UPDATE_REQUEST);
    EventRouter::EventSelection receives;
    receives.insert(MpcEvent::DPI_CHANGED);
    GetEventd().Connect(m_eventc, receives, std::bind(&CPlayerStatusBar::EventCallback, this, std::placeholders::_1), fires);
}

CPlayerStatusBar::~CPlayerStatusBar()
{
    if (m_hIcon) {
        DestroyIcon(m_hIcon);
    }
}

BOOL CPlayerStatusBar::Create(CWnd* pParentWnd)
{
    BOOL ret = CDialogBar::Create(pParentWnd, IDD_PLAYERSTATUSBAR, WS_CHILD | WS_VISIBLE | CBRS_ALIGN_BOTTOM, IDD_PLAYERSTATUSBAR);

    // Should never be RTLed
    ModifyStyleEx(WS_EX_LAYOUTRTL, WS_EX_NOINHERITLAYOUT);

    m_tooltip.Create(this, TTS_NOPREFIX | TTS_ALWAYSTIP);
    m_tooltip.SetDelayTime(TTDT_INITIAL, 0);
    m_tooltip.SetDelayTime(TTDT_AUTOPOP, 2500);
    m_tooltip.SetDelayTime(TTDT_RESHOW, 0);
    m_tooltip.AddTool(&m_time, IDS_TOOLTIP_REMAINING_TIME);
    m_tooltip.AddTool(&m_status);

    return ret;
}

BOOL CPlayerStatusBar::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CDialogBar::PreCreateWindow(cs)) {
        return FALSE;
    }

    m_dwStyle &= ~CBRS_BORDER_TOP;
    m_dwStyle &= ~CBRS_BORDER_BOTTOM;

    return TRUE;
}

CSize CPlayerStatusBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz)
{
    CSize ret = __super::CalcFixedLayout(bStretch, bHorz);
    ret.cy = std::max<long>(ret.cy, 24);
    ret.cy = m_pMainFrame->m_dpi.ScaleSystemToOverrideY(ret.cy);
    return ret;
}

int CPlayerStatusBar::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CDialogBar::OnCreate(lpCreateStruct) == -1) {
        return -1;
    }

    CRect r;
    r.SetRectEmpty();

    m_type.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
                  r, this, IDC_STATIC1);

    m_status.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
                    r, this, IDC_PLAYERSTATUS);

    m_time.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
                  r, this, IDC_PLAYERTIME);
    // Should never be RTLed
    m_time.ModifyStyleEx(WS_EX_LAYOUTRTL, WS_EX_NOINHERITLAYOUT);

    m_status.SetWindowPos(&m_time, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    Relayout();

    return 0;
}

void CPlayerStatusBar::EventCallback(MpcEvent ev)
{
    switch (ev) {
        case MpcEvent::DPI_CHANGED:
            m_status.ScaleFont(m_pMainFrame->m_dpi);
            m_time.ScaleFont(m_pMainFrame->m_dpi);
            SetMediaTypeIcon();
            break;

        default:
            ASSERT(FALSE);
    }
}

void CPlayerStatusBar::Relayout()
{
    BITMAP bm {};
    if (m_bm.m_hObject) {
        m_bm.GetBitmap(&bm);
    }

    CString str;
    CRect r, r2;

    GetClientRect(r);

    if (m_type.GetIcon()) {
        r2.SetRect(6, r.top + 4, 6 + m_pMainFrame->m_dpi.ScaleX(16), r.bottom - 4);
        m_type.MoveWindow(r2);
    }

    r.DeflateRect(11 + m_pMainFrame->m_dpi.ScaleX(16), 5, bm.bmWidth + 8, 4);

    if (CDC* pDC = m_time.GetDC()) {
        CFont* pOld = pDC->SelectObject(&m_time.GetFont());
        m_time.GetWindowText(str);
        r2 = r;
        r2.left = r2.right - pDC->GetTextExtent(str).cx;
        m_time.MoveWindow(&r2, FALSE);
        m_time_rect = r2;
        pDC->SelectObject(pOld);
        m_time.ReleaseDC(pDC);
    } else {
        ASSERT(FALSE);
    }

    if (CDC* pDC = m_status.GetDC()) {
        CFont* pOld = pDC->SelectObject(&m_status.GetFont());
        m_status.GetWindowText(str);
        r2 = r;
        r2.right = r2.left + pDC->GetTextExtent(str).cx;
        // If the text is too long, ensure it won't overlap
        // with the timer. Ellipses will be added if needed.
        if (r2.right >= m_time_rect.left) {
            r2.right = m_time_rect.left - 1;
        }
        m_status.MoveWindow(&r2, FALSE);
        pDC->SelectObject(pOld);
        m_status.ReleaseDC(pDC);
    } else {
        ASSERT(FALSE);
    }

    InvalidateRect(r);
    UpdateWindow();
}

void CPlayerStatusBar::Clear()
{
    m_status.SetWindowText(_T(""));
    m_time.SetWindowText(_T(""));
    m_typeExt.Empty();
    SetMediaTypeIcon();
    SetStatusBitmap(0);
}

void CPlayerStatusBar::SetStatusBitmap(UINT id)
{
    if (m_bmid == id) {
        return;
    }

    if (m_bm.m_hObject) {
        m_bm.DeleteObject();
    }
    if (id) {
        // We can't use m_bm.LoadBitmap(id) directly since we want to load the bitmap from the main executable
        CImage img;
        img.LoadFromResource(AfxGetInstanceHandle(), id);
        m_bm.Attach(img.Detach());
    }
    m_bmid = id;

    Invalidate();
    Relayout();
}

void CPlayerStatusBar::SetMediaType(CString ext)
{
    if (ext != m_typeExt) {
        m_typeExt = ext;
        SetMediaTypeIcon();
    }
}

CString CPlayerStatusBar::GetStatusMessage() const
{
    CString strResult;

    m_status.GetWindowText(strResult);

    return strResult;
}

void CPlayerStatusBar::SetStatusMessage(CString str)
{
    str.Trim();
    if (GetStatusMessage() != str) {
        m_status.SetRedraw(FALSE);
        m_status.SetWindowText(str);
        m_status.SetRedraw(TRUE);
        Relayout();
    }
}

CString CPlayerStatusBar::PreparePathStatusMessage(CPath path)
{
    if (CDC* pDC = m_status.GetDC()) {
        CRect r;
        m_status.GetClientRect(r);
        path.CompactPath(pDC->m_hDC, m_time_rect.left - r.left - 1);
        m_status.ReleaseDC(pDC);
    } else {
        ASSERT(FALSE);
    }

    return path;
}

CString CPlayerStatusBar::GetStatusTimer() const
{
    CString strResult;

    m_time.GetWindowText(strResult);

    return strResult;
}

void CPlayerStatusBar::SetStatusTimer(CString str)
{
    str.Trim();
    if (GetStatusTimer() != str) {
        m_time.SetRedraw(FALSE);
        m_time.SetWindowText(str);
        m_time.SetRedraw(TRUE);
        Relayout();
    }
}

void CPlayerStatusBar::SetStatusTimer(REFERENCE_TIME rtNow, REFERENCE_TIME rtDur, bool fHighPrecision, const GUID& timeFormat/* = TIME_FORMAT_MEDIA_TIME*/)
{
    CString str;
    CString posstr, durstr, rstr;
    const CAppSettings& s = AfxGetAppSettings();

    if (timeFormat == TIME_FORMAT_MEDIA_TIME) {
        DVD_HMSF_TIMECODE tcNow, tcDur, tcRt;

        if (fHighPrecision || s.bHighPrecisionTimer) {
            tcNow = RT2HMSF(rtNow);
            tcDur = RT2HMSF(rtDur);
            tcRt  = RT2HMSF(rtDur - rtNow);
        } else {
            tcNow = RT2HMS_r(rtNow);
            tcDur = RT2HMS_r(rtDur);
            tcRt  = RT2HMS_r(rtDur - rtNow);
        }

        if (tcDur.bHours > 0 || (rtNow >= rtDur && tcNow.bHours > 0)) {
            posstr.Format(_T("%02u:%02u:%02u"), tcNow.bHours, tcNow.bMinutes, tcNow.bSeconds);
            rstr.Format(_T("%02u:%02u:%02u"), tcRt.bHours, tcRt.bMinutes, tcRt.bSeconds);
        } else {
            posstr.Format(_T("%02u:%02u"), tcNow.bMinutes, tcNow.bSeconds);
            rstr.Format(_T("%02u:%02u"), tcRt.bMinutes, tcRt.bSeconds);
        }

        if (tcDur.bHours > 0) {
            durstr.Format(_T("%02u:%02u:%02u"), tcDur.bHours, tcDur.bMinutes, tcDur.bSeconds);
        } else {
            durstr.Format(_T("%02u:%02u"), tcDur.bMinutes, tcDur.bSeconds);
        }

        if (fHighPrecision || s.bHighPrecisionTimer) {
            posstr.AppendFormat(_T(".%03d"), int((rtNow / 10000) % 1000));
            durstr.AppendFormat(_T(".%03d"), int((rtDur / 10000) % 1000));
            rstr.AppendFormat(_T(".%03d"), int(((rtDur - rtNow) / 10000) % 1000));
        }
    } else if (timeFormat == TIME_FORMAT_FRAME) {
        posstr.Format(_T("%I64d"), rtNow);
        durstr.Format(_T("%I64d"), rtDur);
        rstr.Format(_T("%I64d"), rtDur - rtNow);
    }

    if (!s.fRemainingTime) {
        str = ((rtDur <= 0) || (rtDur < rtNow)) ? posstr : posstr + _T(" / ") + durstr;
    } else {
        str = ((rtDur <= 0) || (rtDur < rtNow)) ? posstr : _T("- ") + rstr + _T(" / ") + durstr;
    }

    SetStatusTimer(str);
}

void CPlayerStatusBar::ShowTimer(bool fShow)
{
    m_time.ShowWindow(fShow ? SW_SHOW : SW_HIDE);

    Relayout();
}

BEGIN_MESSAGE_MAP(CPlayerStatusBar, CDialogBar)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_CREATE()
    ON_WM_LBUTTONDOWN()
    ON_WM_SETCURSOR()
    ON_WM_CTLCOLOR()
    ON_WM_CONTEXTMENU()
    ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnToolTipNotify)
END_MESSAGE_MAP()


// CPlayerStatusBar message handlers

void CPlayerStatusBar::SetMediaTypeIcon()
{
    if (m_hIcon) {
        DestroyIcon(m_hIcon);
    }

    m_hIcon = m_typeExt.IsEmpty() ? NULL : LoadIcon(m_typeExt, true, &m_pMainFrame->m_dpi);

    m_type.SetIcon(m_hIcon);

    Relayout();
}

BOOL CPlayerStatusBar::OnEraseBkgnd(CDC* pDC)
{
    return TRUE;
}

void CPlayerStatusBar::OnPaint()
{
    CPaintDC dc(this); // device context for painting

    for (CWnd* pChild = GetWindow(GW_CHILD); pChild; pChild = pChild->GetNextWindow()) {
        if (!pChild->IsWindowVisible()) {
            continue;
        }

        CRect r;
        pChild->GetClientRect(&r);
        pChild->MapWindowPoints(this, &r);
        dc.ExcludeClipRect(&r);
    }

    CRect r;
    GetClientRect(&r);

    if (m_pMainFrame->m_pLastBar != this || m_pMainFrame->m_fFullScreen) {
        r.InflateRect(0, 0, 0, 1);
    }

    if (m_pMainFrame->m_fFullScreen) {
        r.InflateRect(1, 0, 1, 0);
    }

    dc.Draw3dRect(&r, GetSysColor(COLOR_3DSHADOW), GetSysColor(COLOR_3DHILIGHT));

    r.DeflateRect(1, 1);

    dc.FillSolidRect(&r, 0);

    if (m_bm.m_hObject) {
        BITMAP bm;
        m_bm.GetBitmap(&bm);
        CDC memdc;
        memdc.CreateCompatibleDC(&dc);
        memdc.SelectObject(&m_bm);
        CRect clientRect;
        GetClientRect(&clientRect);
        CRect statusRect;
        m_status.GetWindowRect(statusRect);
        ScreenToClient(statusRect);
        dc.BitBlt(clientRect.right - bm.bmWidth - 1,
                  statusRect.CenterPoint().y - bm.bmHeight / 2,
                  bm.bmWidth, bm.bmHeight, &memdc, 0, 0, SRCCOPY);
    }
}

void CPlayerStatusBar::OnSize(UINT nType, int cx, int cy)
{
    CDialogBar::OnSize(nType, cx, cy);

    Invalidate();
    Relayout();
}

void CPlayerStatusBar::OnLButtonDown(UINT nFlags, CPoint point)
{
    CMainFrame* pFrame = ((CMainFrame*)GetParentFrame());

    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    pFrame->GetWindowPlacement(&wp);

    if (m_time_rect.PtInRect(point)) {
        OnTimeDisplayClicked();
    } else if (!pFrame->m_fFullScreen && wp.showCmd != SW_SHOWMAXIMIZED && AfxGetAppSettings().bClientDragWindow) {
        CRect r;
        GetClientRect(r);
        CPoint p = point;

        MapWindowPoints(pFrame, &point, 1);

        pFrame->PostMessage(WM_NCLBUTTONDOWN,
                            (p.x >= r.Width() - r.Height() && !pFrame->IsCaptionHidden()) ? HTBOTTOMRIGHT :
                            HTCAPTION,
                            MAKELPARAM(point.x, point.y));
    }
}

BOOL CPlayerStatusBar::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    CMainFrame* pFrame = ((CMainFrame*)GetParentFrame());

    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    pFrame->GetWindowPlacement(&wp);

    CPoint p;
    GetCursorPos(&p);
    ScreenToClient(&p);

    if (m_time_rect.PtInRect(p) && !IsMenu(m_timerMenu)) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }

    if (!pFrame->m_fFullScreen && wp.showCmd != SW_SHOWMAXIMIZED) {
        CRect r;
        GetClientRect(r);
        if (p.x >= r.Width() - r.Height() && !pFrame->IsCaptionHidden()) {
            SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
            return TRUE;
        }
    }

    return CDialogBar::OnSetCursor(pWnd, nHitTest, message);
}

HBRUSH CPlayerStatusBar::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr = CDialogBar::OnCtlColor(pDC, pWnd, nCtlColor);

    if (*pWnd == m_type) {
        hbr = GetStockBrush(BLACK_BRUSH);
    }

    // TODO:  Return a different brush if the default is not desired
    return hbr;
}

BOOL CPlayerStatusBar::PreTranslateMessage(MSG* pMsg)
{
    m_tooltip.RelayEvent(pMsg);

    return __super::PreTranslateMessage(pMsg);
}

void CPlayerStatusBar::OnTimeDisplayClicked()
{
    CAppSettings& s = AfxGetAppSettings();

    s.fRemainingTime = !s.fRemainingTime;
    m_eventc.FireEvent(MpcEvent::STREAM_POS_UPDATE_REQUEST);
}

void CPlayerStatusBar::OnContextMenu(CWnd* pWnd, CPoint point)
{
    CPoint clientPoint = point;
    ScreenToClient(&clientPoint);
    if (!m_time_rect.PtInRect(clientPoint)) {
        return __super::OnContextMenu(pWnd, point);
    }

    CAppSettings& s = AfxGetAppSettings();

    enum {
        REMAINING_TIME = 1,
        HIGH_PRECISION
    };

    m_timerMenu.CreatePopupMenu();
    m_timerMenu.AppendMenu(MF_STRING | MF_ENABLED | (s.fRemainingTime ? MF_CHECKED : MF_UNCHECKED), REMAINING_TIME, ResStr(IDS_TIMER_REMAINING_TIME));
    UINT nFlags = MF_STRING;
    if (m_pMainFrame->IsSubresyncBarVisible()) {
        nFlags |= MF_DISABLED | MF_CHECKED;
    } else {
        nFlags |= MF_ENABLED | (s.bHighPrecisionTimer ? MF_CHECKED : MF_UNCHECKED);
    }
    m_timerMenu.AppendMenu(nFlags, HIGH_PRECISION, ResStr(IDS_TIMER_HIGH_PRECISION));

    switch (m_timerMenu.TrackPopupMenu(TPM_LEFTBUTTON | TPM_RETURNCMD, point.x, point.y, this)) {
        case REMAINING_TIME:
            s.fRemainingTime = !s.fRemainingTime;
            m_eventc.FireEvent(MpcEvent::STREAM_POS_UPDATE_REQUEST);
            break;
        case HIGH_PRECISION:
            s.bHighPrecisionTimer = !s.bHighPrecisionTimer;
            m_eventc.FireEvent(MpcEvent::STREAM_POS_UPDATE_REQUEST);
            break;
    }
    m_timerMenu.DestroyMenu();
}

BOOL CPlayerStatusBar::OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
    TOOLTIPTEXT* pTTT = reinterpret_cast<LPTOOLTIPTEXT>(pNMHDR);
    if (pTTT->uFlags & TTF_IDISHWND) {
        UINT_PTR nID = pNMHDR->idFrom;
        if (::GetDlgCtrlID((HWND)nID) == IDC_PLAYERSTATUS) {
            CString type;
            const CString msg = GetStatusMessage();
            if (m_pMainFrame->GetDecoderType(type)
                    && (msg.Find(ResStr(IDS_CONTROLS_PAUSED)) == 0 || msg.Find(ResStr(IDS_CONTROLS_PLAYING)) == 0)) {
                _tcscpy_s(pTTT->szText, type);
                *pResult = 0;
                return TRUE;
            }
        }
    }
    return FALSE;
}
