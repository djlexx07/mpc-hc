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
#include "SubtitleDlDlg.h"
#include "SubtitlesProvider.h"

// User Defined Window Messages
enum {
    UWM_SEARCH = WM_USER + 100,
    UWM_SEARCHING,
    UWM_DOWNLOADING,
    UWM_DOWNLOADED,
    UWM_COMPLETED,
    UWM_FINISHED,
    UWM_FAILED,
    UWM_CLEAR,
};

CSubtitleDlDlg::CSubtitleDlDlg(CWnd* pParentWnd)
    : CResizableDialog(CSubtitleDlDlg::IDD, pParentWnd)
    , m_ps(nullptr, 0, 0)
    , m_url(url)
    , m_fReplaceSubs(false)
    , m_pMainFrame(*(CMainFrame*)(pParentWnd))
{
}

CSubtitleDlDlg::~CSubtitleDlDlg()
{
}

void CSubtitleDlDlg::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_list);
    DDX_Control(pDX, IDC_PROGRESS1, m_progress);
    DDX_Control(pDX, IDC_STATUSBAR, m_status);
}

void CSubtitleDlDlg::SetStatusText(const CString& status, BOOL bPropagate/* = TRUE*/)
{
    m_status.SetText(status, 0, 0);
    if (bPropagate) {
        m_pMainFrame.SendStatusMessage(status, 5000);
    }
}

void CSubtitleDlDlg::SetListViewSortColumn()
{
    CHeaderCtrl* header = m_list.GetHeaderCtrl();
    int columnCount = header->GetItemCount();
    for (int i = 0; i < columnCount; ++i) {
        HDITEM hi = { 0 };
        hi.mask = HDI_FORMAT;
        header->GetItem(i, &hi);

        hi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
        //Set sort image to this column
        if (i == m_ps.m_nSortColumn)  {
            hi.fmt |= m_ps.m_fSortOrder > 0 ? HDF_SORTUP : m_ps.m_fSortOrder < 0 ? HDF_SORTDOWN : NULL;
        }
        header->SetItem(i, &hi);
    }
}

int CALLBACK CSubtitleDlDlg::SortCompare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
    PPARAMSORT ps = (PPARAMSORT)(lParamSort);
    CListCtrl* list = (CListCtrl*)CListCtrl::FromHandle(ps->m_hWnd);

    if (ps->m_fSortOrder == 0) {
        DWORD left = (*(SubtitlesInfo*)(list->GetItemData((int)lParam1))).Score();
        DWORD right = (*(SubtitlesInfo*)(list->GetItemData((int)lParam2))).Score();
        return left == right ? 0 : left < right ? 1 : -1;
    } else if (ps->m_nSortColumn == COL_DOWNLOADS) {
        int left = (*(SubtitlesInfo*)(list->GetItemData((int)lParam1))).downloadCount;
        int right = (*(SubtitlesInfo*)(list->GetItemData((int)lParam2))).downloadCount;
        if (left == -1 && right != -1) {
            return 1;
        } else if (left != -1 && right == -1) {
            return -1;
        } else {
            return left == right ? 0 : (ps->m_fSortOrder == 1)
                   ? (left > right ? 1 : -1)
                   : (left < right ? 1 : -1);
        }
#ifdef _DEBUG
    } else if (ps->m_nSortColumn == COL_SCORE) {
        SHORT left = (SHORT)LOWORD((*(SubtitlesInfo*)(list->GetItemData((int)lParam1))).Score());
        SHORT right = (SHORT)LOWORD((*(SubtitlesInfo*)(list->GetItemData((int)lParam2))).Score());
        return left == right ? 0 : (ps->m_fSortOrder == 1)
               ? (left > right ? 1 : -1)
               : (left < right ? 1 : -1);
#endif
    } else {
        CString left(list->GetItemText((int)lParam1, ps->m_nSortColumn));
        CString right(list->GetItemText((int)lParam2, ps->m_nSortColumn));
        if (left == "-" && right != "-") {
            return 1;
        } else if (left != "-" && right == "-") {
            return -1;
        } else {
            return (ps->m_fSortOrder == 1) ? StrCmpLogicalW(left, right) : StrCmpLogicalW(right, left);
        }
    }
}

BOOL CSubtitleDlDlg::OnInitDialog()
{
    __super::OnInitDialog();

    m_progress.SetParent(&m_status);
    m_progress.UpdateWindow();

    m_list.SetExtendedStyle(m_list.GetExtendedStyle()
                            | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT
                            | LVS_EX_CHECKBOXES   | LVS_EX_LABELTIP);

    m_ps.m_hWnd = m_list.GetSafeHwnd();
    m_ps.m_nSortColumn = AfxGetApp()->GetProfileInt(IDS_R_DLG_SUBTITLEDL, IDS_RS_DLG_SUBTITLEDL_SORTCOLUMN, 0);
    m_ps.m_fSortOrder = AfxGetApp()->GetProfileInt(IDS_R_DLG_SUBTITLEDL, IDS_RS_DLG_SUBTITLEDL_SORTORDER, 0);

    int n = 0, curPos = 0;
    CArray<int> columnWidth;

    CString strColumnWidth(AfxGetApp()->GetProfileString(IDS_R_DLG_SUBTITLEDL, IDS_RS_DLG_SUBTITLEDL_COLWIDTH));
    CString token(strColumnWidth.Tokenize(_T(","), curPos));
    while (!token.IsEmpty()) {
        if (_stscanf_s(token, L"%d", &n) == 1) {
            columnWidth.Add(n);
            token = strColumnWidth.Tokenize(_T(","), curPos);
        } else {
            throw 1;
        }
    }

    if (columnWidth.GetCount() != COL_TOTAL_COLUMNS) {
        // default sizes
        columnWidth.RemoveAll();
        columnWidth.Add(100);
        columnWidth.Add(300);
        columnWidth.Add(80);
        columnWidth.Add(40);
        columnWidth.Add(50);
        columnWidth.Add(40);
        columnWidth.Add(250);
#ifdef _DEBUG
        columnWidth.Add(40);
#endif
    }

    m_list.InsertColumn(COL_PROVIDER, _T("Provider")/*ResStr(IDS_SUBDL_DLG_PROVIDER_COL)*/, LVCFMT_LEFT, columnWidth[COL_PROVIDER]);
    m_list.InsertColumn(COL_FILENAME, ResStr(IDS_SUBDL_DLG_FILENAME_COL), LVCFMT_LEFT, columnWidth[COL_FILENAME]);
    m_list.InsertColumn(COL_LANGUAGE, ResStr(IDS_SUBDL_DLG_LANGUAGE_COL), LVCFMT_CENTER, columnWidth[COL_LANGUAGE]);
    m_list.InsertColumn(COL_DISC, ResStr(IDS_SUBDL_DLG_DISC_COL), LVCFMT_CENTER, columnWidth[COL_DISC]);
    m_list.InsertColumn(COL_HEARINGIMPAIRED, _T("Hearing Impaired"), LVCFMT_CENTER, columnWidth[COL_HEARINGIMPAIRED]);
    m_list.InsertColumn(COL_DOWNLOADS, _T("Downloads"), LVCFMT_RIGHT, columnWidth[COL_DOWNLOADS]);
    m_list.InsertColumn(COL_TITLES, ResStr(IDS_SUBDL_DLG_TITLES_COL), LVCFMT_LEFT, columnWidth[COL_TITLES]);
#ifdef _DEBUG
    m_list.InsertColumn(COL_SCORE, _T("Score"), LVCFMT_RIGHT, columnWidth[COL_SCORE]);
#endif
    SetListViewSortColumn();

    AddAnchor(IDC_LIST1, TOP_LEFT, BOTTOM_RIGHT);
    AddAnchor(IDC_CHECK1, BOTTOM_LEFT);
    AddAnchor(IDC_BUTTON1, BOTTOM_RIGHT);
    AddAnchor(IDC_BUTTON2, BOTTOM_RIGHT);
    AddAnchor(IDC_BUTTON3, BOTTOM_RIGHT);
    AddAnchor(IDOK, BOTTOM_RIGHT);
    AddAnchor(IDC_STATUSBAR, BOTTOM_LEFT, BOTTOM_RIGHT);

    const CSize s(500, 250);
    SetMinTrackSize(s);
    EnableSaveRestore(IDS_R_DLG_SUBTITLEDL, TRUE);

    return TRUE;
}

BOOL CSubtitleDlDlg::PreTranslateMessage(MSG* pMsg)
{
    // Inhibit default handling for the Enter key when the list has the focus and an item is selected.
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN
            && pMsg->hwnd == m_list.GetSafeHwnd() && m_list.GetSelectedCount() > 0) {
        return FALSE;
    }

    return __super::PreTranslateMessage(pMsg);
}

void CSubtitleDlDlg::OnOK()
{
    SetStatusText(ResStr(IDS_SUBDL_DLG_DOWNLOADING));

    m_fReplaceSubs = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;

    if (m_fReplaceSubs) {
        CAutoLock cAutoLock(&m_pMainFrame.m_csSubLock);
        m_pMainFrame.m_pSubStreams.RemoveAll();
    }

    BOOL bActivate = TRUE;
    for (int i = 0; i < m_list.GetItemCount(); ++i) {
        if (m_list.GetCheck(i) == TRUE) {

            SubtitlesInfo& subtitlesInfo = *(SubtitlesInfo*)(m_list.GetItemData(i));
            LVITEMINDEX lvii = { i , -1 };
            m_list.SetItemIndexState(&lvii, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
            HRESULT hr = S_OK;
            subtitlesInfo.Download(bActivate);
            bActivate = FALSE;
        }
    }

    __super::OnOK();
}

void CSubtitleDlDlg::OnRefresh()
{
    m_list.DeleteAllItems();
    m_pMainFrame.m_pSubtitlesProviders->Search(FALSE);
}

void CSubtitleDlDlg::OnAbort()
{
    m_pMainFrame.m_pSubtitlesProviders->Abort(SubtitlesThreadType(STT_SEARCH | STT_DOWNLOAD));
}

void CSubtitleDlDlg::OnOptions()
{
    m_pMainFrame.ShowOptions(CPPageSubMisc::IDD);
}

void CSubtitleDlDlg::OnUpdateOk(CCmdUI* pCmdUI)
{
    bool fEnable = false;
    for (int i = 0; !fEnable && i < m_list.GetItemCount(); ++i) {
        fEnable = (m_list.GetCheck(i) == TRUE);
    }

    pCmdUI->Enable(fEnable);
}

void CSubtitleDlDlg::OnUpdateRefresh(CCmdUI* pCmdUI)
{
    bool fEnable = true;
    pCmdUI->Enable(fEnable);
}

void CSubtitleDlDlg::OnColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMHEADER phdr = (LPNMHEADER)(pNMHDR);
    *pResult = 0;

    if (phdr->iItem == m_ps.m_nSortColumn) {
        if (m_ps.m_fSortOrder == 1) {
            m_ps.m_fSortOrder = (m_ps.m_nSortColumn == COL_DOWNLOADS) ? 0 : -1;
        } else if (m_ps.m_fSortOrder == -1) {
            m_ps.m_fSortOrder = (m_ps.m_nSortColumn == COL_DOWNLOADS) ? 1 : 0;
        } else if (m_ps.m_fSortOrder == 0) {
            m_ps.m_fSortOrder = (m_ps.m_nSortColumn == COL_DOWNLOADS) ? -1 : 1;
        }
    } else {
        m_ps.m_nSortColumn = phdr->iItem;
        m_ps.m_fSortOrder = (m_ps.m_nSortColumn == COL_DOWNLOADS) ? -1 : 1;
    }
    SetListViewSortColumn();

    SetRedraw(FALSE);
    m_list.SortItemsEx(SortCompare, (DWORD_PTR)&m_ps);

    SetRedraw(TRUE);
    m_list.Invalidate();
    m_list.UpdateWindow();
}

void CSubtitleDlDlg::OnSize(UINT nType, int cx, int cy)
{
    __super::OnSize(nType, cx, cy);

    ArrangeLayout();

    if (m_status && m_progress) {
        // Reposition the progress control correctly!
        CRect statusRect, buttonRect;
        m_status.GetClientRect(&statusRect);
        GetDlgItem(IDOK)->GetWindowRect(&buttonRect);
        ScreenToClient(&buttonRect);
        int parts[2] = { buttonRect.left - 2, -1 };
        m_status.SetParts(2, parts);
        m_status.GetRect(1, &statusRect);
        statusRect.DeflateRect(1, 1, 1, 1);
        m_progress.SetWindowPos(&wndTop, statusRect.left, statusRect.top, statusRect.Width(), statusRect.Height(),  SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

void CSubtitleDlDlg::OnDestroy()
{
    RemoveAllAnchors();

    const CHeaderCtrl& pHC = *m_list.GetHeaderCtrl();
    CString strColumnWidth;

    for (int i = 0; i < pHC.GetItemCount(); ++i) {
        int w = m_list.GetColumnWidth(i);
        strColumnWidth.AppendFormat(L"%d,", w);
    }
    AfxGetApp()->WriteProfileString(IDS_R_DLG_SUBTITLEDL, IDS_RS_DLG_SUBTITLEDL_COLWIDTH, strColumnWidth);
    AfxGetApp()->WriteProfileInt(IDS_R_DLG_SUBTITLEDL, IDS_RS_DLG_SUBTITLEDL_SORTCOLUMN, m_ps.m_nSortColumn);
    AfxGetApp()->WriteProfileInt(IDS_R_DLG_SUBTITLEDL, IDS_RS_DLG_SUBTITLEDL_SORTORDER, m_ps.m_fSortOrder);

    __super::OnDestroy();
}

BOOL CSubtitleDlDlg::OnEraseBkgnd(CDC* pDC)
{
    EraseBackground(pDC);

    return TRUE;
}

void CSubtitleDlDlg::DownloadSelectedSubtitles()
{
    POSITION pos = m_list.GetFirstSelectedItemPosition();
    while (pos) {
        int nItem = m_list.GetNextSelectedItem(pos);
        if (nItem >= 0 && nItem < m_list.GetItemCount()) {
            m_list.SetCheck(nItem, TRUE);
        }
    }
    OnOK();
}

// ON_UPDATE_COMMAND_UI does not work for modeless dialogs
BEGIN_MESSAGE_MAP(CSubtitleDlDlg, CResizableDialog)
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_COMMAND(IDC_BUTTON1, OnRefresh)
    ON_COMMAND(IDC_BUTTON2, OnAbort)
    ON_COMMAND(IDC_BUTTON3, OnOptions)
    ON_UPDATE_COMMAND_UI(IDOK, OnUpdateOk)
    ON_UPDATE_COMMAND_UI(IDC_BUTTON1, OnUpdateRefresh)
    ON_NOTIFY(HDN_ITEMCLICK, 0, OnColumnClick)
    ON_WM_DESTROY()
    ON_NOTIFY(NM_DBLCLK, IDC_LIST1, OnDoubleClickSubtitle)
    ON_NOTIFY(LVN_KEYDOWN, IDC_LIST1, OnKeyPressedSubtitle)
    ON_NOTIFY(NM_RCLICK, IDC_LIST1, OnRightClick)
    ON_NOTIFY(LVN_ITEMCHANGING, IDC_LIST1, OnItemChanging)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, OnItemChanged)
    ON_WM_SHOWWINDOW()

    ON_MESSAGE(UWM_SEARCH, OnSearch)
    ON_MESSAGE(UWM_SEARCHING, OnSearching)
    ON_MESSAGE(UWM_DOWNLOADING, OnDownloading)
    ON_MESSAGE(UWM_DOWNLOADED, OnDownloaded)
    ON_MESSAGE(UWM_COMPLETED, OnCompleted)
    ON_MESSAGE(UWM_FINISHED, OnFinished)
    ON_MESSAGE(UWM_FAILED, OnFailed)
    ON_MESSAGE(UWM_CLEAR, OnClear)
END_MESSAGE_MAP()

void CSubtitleDlDlg::OnDoubleClickSubtitle(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pItemActivate = (LPNMITEMACTIVATE)(pNMHDR);

    if (pItemActivate->iItem >= 0 &&  m_list.GetCheck(pItemActivate->iItem) != -1) {
        DownloadSelectedSubtitles();
    }
}

void CSubtitleDlDlg::OnKeyPressedSubtitle(NMHDR* pNMHDR, LRESULT* pResult)
{
    LV_KEYDOWN* pLVKeyDow = (LV_KEYDOWN*)pNMHDR;

    if (pLVKeyDow->wVKey == VK_RETURN) {
        DownloadSelectedSubtitles();
        *pResult = TRUE;
    }
}

void CSubtitleDlDlg::OnRightClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)pNMHDR;

    if (lpnmlv->iItem >= 0 && lpnmlv->iSubItem >= 0) {
        SubtitlesInfo& subtitlesInfo = *(SubtitlesInfo*)(m_list.GetItemData(lpnmlv->iItem));

        enum {
            DOWNLOAD = 0x1000,
            OPEN_URL,
        };

        CMenu m;
        m.CreatePopupMenu();
        m.AppendMenu(MF_STRING | (m_list.GetCheck(lpnmlv->iItem) != -1 ? MF_ENABLED : MF_DISABLED), DOWNLOAD, L"Download && Open" /*ResStr(IDS_ENABLE_ALL_FILTERS)*/);
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING | (!subtitlesInfo.url.empty() ? MF_ENABLED : MF_DISABLED), OPEN_URL, L"Open Url" /*ResStr(IDS_ENABLE_ALL_FILTERS)*/);

        CPoint pt = lpnmlv->ptAction;
        ::MapWindowPoints(lpnmlv->hdr.hwndFrom, HWND_DESKTOP, &pt, 1);

        switch (m.TrackPopupMenu(TPM_LEFTBUTTON | TPM_RETURNCMD, pt.x, pt.y, this)) {
            case DOWNLOAD:
                m_list.SetCheck(lpnmlv->iItem, TRUE);
                OnOK();
                break;
            case OPEN_URL:
                subtitlesInfo.OpenUrl();
                break;
            default:
                break;
        }
    }
}

void CSubtitleDlDlg::OnItemChanging(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)(pNMHDR);

    if (pNMLV->uOldState == 0 && pNMLV->uNewState == 0x1000 && pNMLV->lParam) {
        *pResult = TRUE;
    }
}

void CSubtitleDlDlg::OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    UpdateDialogControls(this, FALSE);
}


void CSubtitleDlDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    __super::OnShowWindow(bShow, nStatus);

    const auto& s = AfxGetAppSettings();

    if (bShow == TRUE && !m_list.GetItemCount() && !m_pMainFrame.m_fAudioOnly && s.fEnableSubtitles  && !s.bAutoDownloadSubtitles) {
        OnRefresh();
    }
}


afx_msg LRESULT CSubtitleDlDlg::OnSearch(WPARAM wParam, LPARAM /*lParam*/)
{
    INT _nCount = (INT)wParam;

    SetStatusText(_T("Searching for subtitles online, please wait..."));
    GetDlgItem(IDC_BUTTON1)->EnableWindow(FALSE);
    GetDlgItem(IDC_BUTTON1)->ShowWindow(FALSE);
    GetDlgItem(IDC_BUTTON2)->ShowWindow(TRUE);

    m_progress.SetRange32(0, _nCount);
    m_progress.SetStep(1);
    m_progress.SetPos(0);

    return S_OK;
}

afx_msg LRESULT CSubtitleDlDlg::OnSearching(WPARAM /*wParam*/, LPARAM lParam)
{
    SubtitlesInfo& _fileInfo = *(SubtitlesInfo*)lParam;
    CString title = _T("Download subtitles - ") + CString(_fileInfo.fileName.c_str());
    SetWindowText(title);
    return S_OK;
}

afx_msg LRESULT CSubtitleDlDlg::OnDownloading(WPARAM /*wParam*/, LPARAM lParam)
{
    SubtitlesInfo& _fileInfo = *(SubtitlesInfo*)lParam;

    CString statusMessage;
    statusMessage.Format(_T("Downloading [%s] \"%s\""), CString(_fileInfo.Provider().Name().c_str()), CString(_fileInfo.fileName.c_str()));
    SetStatusText(statusMessage);

    return S_OK;
}

afx_msg LRESULT CSubtitleDlDlg::OnDownloaded(WPARAM /*wParam*/, LPARAM lParam)
{
    SubtitlesInfo& _fileInfo = *(SubtitlesInfo*)lParam;

    if (!GetDlgItem(IDC_BUTTON1)->IsWindowEnabled()) {
        SetStatusText(_T("Searching for subtitles online, please wait..."), FALSE);
    }

    for (int i = 0; i < m_list.GetItemCount(); ++i) {
        SubtitlesInfo& iter = *(SubtitlesInfo*)m_list.GetItemData(i);
        if (iter.UID() == _fileInfo.UID()) {
            LVITEMINDEX lvii = { i, -1 };
            m_list.SetItemIndexState(&lvii, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
        }
    }

    return S_OK;
}

afx_msg LRESULT CSubtitleDlDlg::OnCompleted(WPARAM wParam, LPARAM lParam)
{
    SRESULT _result = (SRESULT)wParam;
    SubtitlesList& _subtitlesList = *(SubtitlesList*)lParam;

    m_progress.StepIt();

    if (_result == SR_ABORTED) {
        SetStatusText("Online subtitles search aborting...");
    } else if (!_subtitlesList.empty()) {
        m_list.SetRedraw(FALSE);
        SubtitlesList::const_iterator _begin = std::next(m_Subtitles.begin(), !m_Subtitles.empty() ? m_Subtitles.size() - 1 : 0);
        std::copy(_subtitlesList.begin(), _subtitlesList.end(), std::back_inserter(m_Subtitles));
        _begin = (_begin == m_Subtitles.end()) ? _begin = m_Subtitles.begin() : ++_begin;
        SubtitlesList::const_iterator _end = std::next(_begin, _subtitlesList.size());

        SubtitlesList::const_iterator iter = _begin;
        for (; iter != _end; ++iter) {
            int iItem = m_list.InsertItem(0, UTF8To16(iter->Provider().Name().c_str()));
            m_list.SetItemText(iItem, COL_FILENAME, UTF8To16(iter->fileName.c_str()));
            m_list.SetItemText(iItem, COL_LANGUAGE, UTF8To16(iter->languageName.c_str()));
            CString disc;
            disc.Format(_T("%d/%d"), iter->discNumber, iter->discCount);
            m_list.SetItemText(iItem, COL_DISC, disc);
            m_list.SetItemText(iItem, COL_HEARINGIMPAIRED, iter->hearingImpaired == -1 ? _T("-") : iter->hearingImpaired > 0 ? _T("Y") : _T("N"));
            CString downloads(_T("-"));
            if (iter->downloadCount != -1) {
                downloads.Format(_T("%d"), iter->downloadCount);
                downloads = FormatNumber(downloads);
            }
            m_list.SetItemText(iItem, COL_DOWNLOADS, downloads);
            m_list.SetItemText(iItem, COL_TITLES, UTF8To16(iter->Title().c_str()));
#ifdef _DEBUG
            CString score;
            score.Format(_T("%d"), (SHORT)LOWORD(iter->Score()));
            m_list.SetItemText(iItem, COL_SCORE, score);
#endif
            m_list.SetItemData(iItem, (DWORD_PTR) & ((SubtitlesInfo&)*iter));
        }

        // sort
        m_list.SortItemsEx(SortCompare, (DWORD_PTR)&m_ps);

        m_list.SetRedraw(TRUE);
        m_list.Invalidate();
        m_list.UpdateWindow();
    }
    UpdateWindow();

    return S_OK;
}

afx_msg LRESULT CSubtitleDlDlg::OnFinished(WPARAM wParam, LPARAM lParam)
{
    BOOL _bAborted = (BOOL)wParam;
    BOOL _bShowDialog = (BOOL)lParam;

    if (_bAborted == FALSE) {
        if (!m_Subtitles.empty()) {
            if (_bShowDialog == TRUE && !IsWindowVisible()) {
                ShowWindow(SW_SHOW);
            }

            CString message;
            message.Format(L"Online subtitles search completed, %d subtitles found.", (int)m_Subtitles.size());
            SetStatusText(message);
        } else {
            SetStatusText(L"Online subtitles search completed, no subtitles found.");
        }
    } else {
        SetStatusText("Online subtitles search aborted.");
    }

    int nLower = 0, nUpper = 0;
    m_progress.GetRange(nLower, nUpper);
    m_progress.SetPos(nUpper);

    GetDlgItem(IDC_BUTTON2)->ShowWindow(FALSE);
    GetDlgItem(IDC_BUTTON1)->ShowWindow(TRUE);
    GetDlgItem(IDC_BUTTON1)->EnableWindow(TRUE);
    UpdateWindow();

    return S_OK;
}

afx_msg LRESULT CSubtitleDlDlg::OnFailed(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SetStatusText("Online subtitles search failed."/*ResStr(IDS_SUBDL_DLG_CONNECT_ERROR)*/);

    return S_OK;
}


afx_msg LRESULT CSubtitleDlDlg::OnClear(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    CString title = _T("Download subtitles");
    SetWindowText(title);

    m_progress.SetPos(0);
    SetStatusText("");
    m_list.DeleteAllItems();
    m_Subtitles.clear();

    GetDlgItem(IDC_BUTTON2)->ShowWindow(FALSE);
    GetDlgItem(IDC_BUTTON1)->ShowWindow(TRUE);
    GetDlgItem(IDC_BUTTON1)->EnableWindow(TRUE);
    UpdateWindow();

    return S_OK;
}

void CSubtitleDlDlg::DoSearch(INT _nCount)
{
    SendMessage(UWM_SEARCH, (WPARAM)_nCount, (LPARAM)nullptr);
}
void CSubtitleDlDlg::DoSearching(SubtitlesInfo& _fileInfo)
{
    SendMessage(UWM_SEARCHING, (WPARAM)nullptr, (LPARAM)&_fileInfo);
}
void CSubtitleDlDlg::DoDownloading(SubtitlesInfo& _fileInfo)
{
    SendMessage(UWM_DOWNLOADING, (WPARAM)nullptr, (LPARAM)&_fileInfo);
}
void CSubtitleDlDlg::DoDownloaded(SubtitlesInfo& _fileInfo)
{
    SendMessage(UWM_DOWNLOADED, (WPARAM)nullptr, (LPARAM)&_fileInfo);
}
void CSubtitleDlDlg::DoCompleted(SRESULT _result, SubtitlesList& _subtitlesList)
{
    SendMessage(UWM_COMPLETED, (WPARAM)_result, (LPARAM)&_subtitlesList);
}
void CSubtitleDlDlg::DoFinished(BOOL _bAborted, BOOL _bShowDialog)
{
    SendMessage(UWM_FINISHED, (WPARAM)_bAborted, (LPARAM)_bShowDialog);
}
void CSubtitleDlDlg::DoFailed()
{
    SendMessage(UWM_FAILED, (WPARAM)nullptr, (LPARAM)nullptr);
}
void CSubtitleDlDlg::DoClear()
{
    SendMessage(UWM_CLEAR, (WPARAM)nullptr, (LPARAM)nullptr);
}
