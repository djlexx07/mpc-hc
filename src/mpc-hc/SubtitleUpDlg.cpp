/*
 * (C) 2014 see Authors.txt
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
#include "AuthDlg.h"
#include "SubtitleUpDlg.h"
#include "SubtitlesProvider.h"

// User Defined Window Messages
enum {
    UWM_UPLOAD = WM_USER + 100,
    UWM_UPLOADING,
    UWM_COMPLETED,
    UWM_FINISHED,
    UWM_FAILED,
    UWM_CLEAR,
};

CSubtitleUpDlg::CSubtitleUpDlg(CWnd* pParentWnd)
    : CResizableDialog(CSubtitleUpDlg::IDD, pParentWnd)
    , m_pMainFrame(*(CMainFrame*)(pParentWnd))
{
}

CSubtitleUpDlg::~CSubtitleUpDlg()
{
}

void CSubtitleUpDlg::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_list);
    DDX_Control(pDX, IDC_PROGRESS1, m_progress);
    DDX_Control(pDX, IDC_STATUSBAR, m_status);
}

void CSubtitleUpDlg::SetStatusText(const CString& status, BOOL bPropagate/* = TRUE*/)
{
    m_status.SetText(status, 0, 0);
    if (bPropagate) {
        m_pMainFrame.SendStatusMessage(status, 5000);
    }
}

BOOL CSubtitleUpDlg::OnInitDialog()
{
    __super::OnInitDialog();

    m_progress.SetParent(&m_status);
    m_progress.UpdateWindow();

    int n = 0, curPos = 0;
    CArray<int> columnWidth;

    CString strColumnWidth(AfxGetApp()->GetProfileString(IDS_R_DLG_SUBTITLEUP, IDS_RS_DLG_SUBTITLEUP_COLWIDTH));
    CString token(strColumnWidth.Tokenize(_T(","), curPos));
    while (!token.IsEmpty()) {
        if (_stscanf_s(token, L"%d", &n) == 1) {
            columnWidth.Add(n);
            token = strColumnWidth.Tokenize(_T(","), curPos);
        } else {
            throw 1;
        }
    }

    m_list.SetExtendedStyle(m_list.GetExtendedStyle()
                            | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT
                            | LVS_EX_CHECKBOXES   | LVS_EX_LABELTIP);

    if (columnWidth.GetCount() != COL_TOTAL_COLUMNS) {
        // default sizes
        columnWidth.RemoveAll();
        columnWidth.Add(120);
        columnWidth.Add(75);
        columnWidth.Add(250);
    }

    m_list.InsertColumn(COL_PROVIDER, _T("Provider")/*ResStr(IDS_SUBDL_DLG_PROVIDER_COL)*/, LVCFMT_LEFT, columnWidth[COL_PROVIDER]);
    m_list.InsertColumn(COL_USERNAME, _T("Username")/*ResStr(IDS_SUBDL_DLG_USERNAME_COL)*/, LVCFMT_LEFT, columnWidth[COL_USERNAME]);
    m_list.InsertColumn(COL_STATUS, _T("Status")/*ResStr(IDS_SUBDL_DLG_LANGUAGE_COL)*/, LVCFMT_LEFT, columnWidth[COL_STATUS]);

    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();

    int i = 0;
    for (const auto& iter : m_pMainFrame.m_pSubtitlesProviders->Providers()) {
        if (iter->Flags(SPF_UPLOAD)) {
            int iItem = m_list.InsertItem((int)i++, CString(iter->Name().c_str()));
            m_list.SetItemText(iItem, COL_USERNAME, UTF8To16(iter->UserName().c_str()));
            m_list.SetItemText(iItem, COL_STATUS, _T("Ready..."));
            m_list.SetCheck(iItem, iter->Enabled(SPF_UPLOAD));
            m_list.SetItemData(iItem, (DWORD_PTR)(iter));
            //} else {
            //    LVITEMINDEX lvii = { iItem, -1 };
            //    m_list.SetItemIndexState(&lvii, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
            //    m_list.SetItemText(iItem, COL_STATUS, _T("Not implemented."));
        }
    }

    m_list.SetRedraw(TRUE);
    m_list.Invalidate();
    m_list.UpdateWindow();

    UpdateData(FALSE);

    AddAnchor(IDC_LIST1, TOP_LEFT, BOTTOM_RIGHT);
    AddAnchor(IDC_BUTTON1, BOTTOM_RIGHT);
    AddAnchor(IDC_BUTTON2, BOTTOM_RIGHT);
    AddAnchor(IDOK, BOTTOM_RIGHT);
    AddAnchor(IDC_STATUSBAR, BOTTOM_LEFT, BOTTOM_RIGHT);

    const CSize s(500, 250);
    SetMinTrackSize(s);
    EnableSaveRestore(IDS_R_DLG_SUBTITLEUP, TRUE);

    return TRUE;
}

BOOL CSubtitleUpDlg::PreTranslateMessage(MSG* pMsg)
{
    // Inhibit default handling for the Enter key when the list has the focus and an item is selected.
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN
            && pMsg->hwnd == m_list.GetSafeHwnd() && m_list.GetSelectedCount() > 0) {
        return FALSE;
    }

    return __super::PreTranslateMessage(pMsg);
}

void CSubtitleUpDlg::OnOK()
{
    m_pMainFrame.m_pSubtitlesProviders->Upload();
}

void CSubtitleUpDlg::OnAbort()
{
    m_pMainFrame.m_pSubtitlesProviders->Abort(SubtitlesThreadType(STT_UPLOAD));
}

void CSubtitleUpDlg::OnOptions()
{
    m_pMainFrame.ShowOptions(CPPageSubMisc::IDD);
}

void CSubtitleUpDlg::OnUpdateOk(CCmdUI* pCmdUI)
{
    bool fEnable = false;
    for (int i = 0; !fEnable && i < m_list.GetItemCount(); ++i) {
        fEnable = (m_list.GetCheck(i) == TRUE);
    }

    pCmdUI->Enable(fEnable);
}

void CSubtitleUpDlg::OnUpdateRefresh(CCmdUI* pCmdUI)
{
    bool fEnable = true;
    pCmdUI->Enable(fEnable);
}

void CSubtitleUpDlg::OnSize(UINT nType, int cx, int cy)
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

void CSubtitleUpDlg::OnDestroy()
{
    RemoveAllAnchors();

    const CHeaderCtrl& pHC = *m_list.GetHeaderCtrl();
    CString strColumnWidth;

    for (int i = 0; i < pHC.GetItemCount(); ++i) {
        int w = m_list.GetColumnWidth(i);
        strColumnWidth.AppendFormat(L"%d,", w);
    }
    AfxGetApp()->WriteProfileString(IDS_R_DLG_SUBTITLEUP, IDS_RS_DLG_SUBTITLEUP_COLWIDTH, strColumnWidth);

    __super::OnDestroy();
}

BOOL CSubtitleUpDlg::OnEraseBkgnd(CDC* pDC)
{
    EraseBackground(pDC);

    return TRUE;
}

void CSubtitleUpDlg::DownloadSelectedSubtitles()
{
    POSITION pos = m_list.GetFirstSelectedItemPosition();
    while (pos) {
        int nItem = m_list.GetNextSelectedItem(pos);
        if (nItem >= 0 && nItem < m_list.GetItemCount()) {
            ListView_SetCheckState(m_list.GetSafeHwnd(), nItem, TRUE);
        }
    }
    //OnOK();
}

// ON_UPDATE_COMMAND_UI dows not work for modless dialogs
BEGIN_MESSAGE_MAP(CSubtitleUpDlg, CResizableDialog)
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_COMMAND(IDC_BUTTON1, OnAbort)
    ON_COMMAND(IDC_BUTTON2, OnOptions)
    ON_UPDATE_COMMAND_UI(IDOK, OnUpdateOk)
    ON_WM_DESTROY()
    ON_NOTIFY(NM_DBLCLK, IDC_LIST1, OnDoubleClickSubtitle)
    ON_NOTIFY(LVN_KEYDOWN, IDC_LIST1, OnKeyPressedSubtitle)
    ON_NOTIFY(NM_RCLICK, IDC_LIST1, OnRightClick)
    ON_NOTIFY(LVN_ITEMCHANGING, IDC_LIST1, OnItemChanging)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, OnItemChanged)
    ON_WM_SHOWWINDOW()

    ON_MESSAGE(UWM_UPLOAD, OnUpload)
    ON_MESSAGE(UWM_UPLOADING, OnUploading)
    ON_MESSAGE(UWM_COMPLETED, OnCompleted)
    ON_MESSAGE(UWM_FINISHED, OnFinished)
    ON_MESSAGE(UWM_FAILED, OnFailed)
    ON_MESSAGE(UWM_CLEAR, OnClear)
END_MESSAGE_MAP()

void CSubtitleUpDlg::OnDoubleClickSubtitle(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMITEMACTIVATE pItemActivate = (LPNMITEMACTIVATE)(pNMHDR);

    if (pItemActivate->iItem >= 0 &&  m_list.GetCheck(pItemActivate->iItem) != -1) {
        //DownloadSelectedSubtitles();
    }
}

void CSubtitleUpDlg::OnKeyPressedSubtitle(NMHDR* pNMHDR, LRESULT* pResult)
{
    LV_KEYDOWN* pLVKeyDow = (LV_KEYDOWN*)pNMHDR;

    if (pLVKeyDow->wVKey == VK_RETURN) {
        //DownloadSelectedSubtitles();
        *pResult = TRUE;
    }
}

void CSubtitleUpDlg::OnRightClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)pNMHDR;

    if (lpnmlv->iItem >= 0 && lpnmlv->iSubItem >= 0) {
        auto& s = AfxGetAppSettings();
        SubtitlesProvider& provider = *(SubtitlesProvider*)(m_list.GetItemData(lpnmlv->iItem));

        enum {
            SET_CREDENTIALS = 0x1000,
            RESET_CREDENTIALS,
            MOVE_UP,
            MOVE_DOWN,
            OPEN_URL,
        };

        CMenu m;
        m.CreatePopupMenu();
        m.AppendMenu(MF_STRING | (provider.Flags(SPF_LOGIN) ? MF_ENABLED : MF_DISABLED), SET_CREDENTIALS, L"Setup"/*ResStr(IDS_DISABLE_ALL_FILTERS)*/);
        m.AppendMenu(MF_STRING | (provider.Flags(SPF_LOGIN) && !provider.UserName().empty() ? MF_ENABLED : MF_DISABLED), RESET_CREDENTIALS, L"Reset"/*ResStr(IDS_DISABLE_ALL_FILTERS)*/);
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING | (lpnmlv->iItem > 0 ? MF_ENABLED : MF_DISABLED), MOVE_UP, L"Move Up"/*ResStr(IDS_DISABLE_ALL_FILTERS)*/);
        m.AppendMenu(MF_STRING | (lpnmlv->iItem < m_list.GetItemCount() - 1  ? MF_ENABLED : MF_DISABLED), MOVE_DOWN, L"Move Down"/*ResStr(IDS_DISABLE_ALL_FILTERS)*/);
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING | MF_ENABLED, OPEN_URL, L"Open Url" /*ResStr(IDS_ENABLE_ALL_FILTERS)*/);

        CPoint pt = lpnmlv->ptAction;
        ::MapWindowPoints(lpnmlv->hdr.hwndFrom, HWND_DESKTOP, &pt, 1);

        switch (m.TrackPopupMenu(TPM_LEFTBUTTON | TPM_RETURNCMD, pt.x, pt.y, this)) {
            case OPEN_URL:
                provider.OpenUrl();
                break;
            case SET_CREDENTIALS: {
                CString szUser(UTF8To16(provider.UserName().c_str()));
                CString szPass(UTF8To16(provider.Password().c_str()));
                CString szDomain(provider.Name().c_str());
                if (ERROR_SUCCESS == PromptForCredentials(GetSafeHwnd(),
                                                          L"Enter website credentitals", L"Enter your credentials to connect to: " + CString(provider.Url().c_str()),
                                                          szDomain, szUser, szPass, /*&bSave*/nullptr)) {
                    provider.UserName((const char*)UTF16To8(szUser));
                    provider.Password((const char*)UTF16To8(szPass));
                    m_list.SetItemText(lpnmlv->iItem, 1, szUser);
                    s.strSubtitlesProviders = CString(m_pMainFrame.m_pSubtitlesProviders->WriteSettings().c_str());
                    s.SaveSettings();
                }
                break;
            }
            case RESET_CREDENTIALS:
                provider.UserName("");
                provider.Password("");
                m_list.SetItemText(lpnmlv->iItem, 1, _T(""));
                s.strSubtitlesProviders = CString(m_pMainFrame.m_pSubtitlesProviders->WriteSettings().c_str());
                s.SaveSettings();
                break;
            case MOVE_UP: {
                m_pMainFrame.m_pSubtitlesProviders->MoveUp(lpnmlv->iItem);
                ListView_SortItemsEx(m_list.GetSafeHwnd(), SortCompare, m_list.GetSafeHwnd());
                s.strSubtitlesProviders = CString(m_pMainFrame.m_pSubtitlesProviders->WriteSettings().c_str());
                s.SaveSettings();
                break;
            }
            case MOVE_DOWN: {
                m_pMainFrame.m_pSubtitlesProviders->MoveDown(lpnmlv->iItem);
                ListView_SortItemsEx(m_list.GetSafeHwnd(), SortCompare, m_list.GetSafeHwnd());
                s.strSubtitlesProviders = CString(m_pMainFrame.m_pSubtitlesProviders->WriteSettings().c_str());
                s.SaveSettings();
                break;
            }
            default:
                break;
        }
    }
}

int CALLBACK CSubtitleUpDlg::SortCompare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
    CListCtrl& list = *(CListCtrl*)CListCtrl::FromHandle((HWND)(lParamSort));
    size_t left = ((SubtitlesProvider*)list.GetItemData((int)lParam1))->Index();
    size_t right = ((SubtitlesProvider*)list.GetItemData((int)lParam2))->Index();
    return left == right ? 0 : (int)(left - right);
}

void CSubtitleUpDlg::OnItemChanging(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)(pNMHDR);

    if (pNMLV->uOldState == 0 && pNMLV->uNewState == 0x1000 && pNMLV->lParam) {
        *pResult = TRUE;
    }
}

void CSubtitleUpDlg::OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)(pNMHDR);

    if (((pNMLV->uOldState | pNMLV->uNewState) == 0x3000) && pNMLV->lParam) {
        SubtitlesProvider& _provider = *(SubtitlesProvider*)pNMLV->lParam;
        _provider.Enabled(SPF_UPLOAD, pNMLV->uNewState == 0x2000 ? TRUE : FALSE);
        auto& s = AfxGetAppSettings();
        s.strSubtitlesProviders = CString(m_pMainFrame.m_pSubtitlesProviders->WriteSettings().c_str());
        s.SaveSettings();
    }

    UpdateDialogControls(this, FALSE);
}


void CSubtitleUpDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    __super::OnShowWindow(bShow, nStatus);

    const auto& s = AfxGetAppSettings();

    if (bShow == TRUE && !m_list.GetItemCount() && m_pMainFrame.m_pSubStreams.IsEmpty() && !m_pMainFrame.m_fAudioOnly && s.fEnableSubtitles  && !s.bAutoDownloadSubtitles) {
        //OnRefresh();
    }
}


afx_msg LRESULT CSubtitleUpDlg::OnUpload(WPARAM wParam, LPARAM /*lParam*/)
{
    INT _nCount = (INT)wParam;

    SetStatusText(L"Uploading subtitles, please wait...");
    GetDlgItem(IDC_BUTTON1)->EnableWindow(TRUE);

    m_progress.SetRange32(0, _nCount);
    m_progress.SetStep(1);
    m_progress.SetPos(0);

    return S_OK;
}

afx_msg LRESULT CSubtitleUpDlg::OnUploading(WPARAM /*wParam*/, LPARAM lParam)
{
    SubtitlesProvider& _provider = *(SubtitlesProvider*)lParam;

    for (int i = 0; i < m_list.GetItemCount(); ++i) {
        SubtitlesProvider& iter = *(SubtitlesProvider*)m_list.GetItemData(i);
        if (&iter == &_provider) {
            m_list.SetItemText(i, COL_STATUS, _T("Uploading..."));
        }
    }
    UpdateWindow();

    return S_OK;
}

afx_msg LRESULT CSubtitleUpDlg::OnCompleted(WPARAM wParam, LPARAM lParam)
{
    SRESULT _result = (SRESULT)wParam;
    SubtitlesProvider& _provider = *(SubtitlesProvider*)lParam;

    m_progress.StepIt();

    for (int i = 0; i < m_list.GetItemCount(); ++i) {
        SubtitlesProvider& iter = *(SubtitlesProvider*)m_list.GetItemData(i);
        if (&iter == &_provider) {
            switch (_result) {
                case SR_SUCCEEDED:
                    m_list.SetItemText(i, COL_STATUS, _T("Subtitles uploaded succeessfuly."));
                    break;
                case SR_FAILED:
                    m_list.SetItemText(i, COL_STATUS, _T("Subtitles upload failed."));
                    break;
                case SR_ABORTED:
                    m_list.SetItemText(i, COL_STATUS, _T("Subtitles upload aborted."));
                    break;
                case SR_EXISTS:
                    m_list.SetItemText(i, COL_STATUS, _T("Subtitles already exist."));
                    break;
            }
        }
    }
    UpdateWindow();

    return S_OK;
}

afx_msg LRESULT CSubtitleUpDlg::OnFinished(WPARAM wParam, LPARAM /*lParam*/)
{
    BOOL _bAborted = (BOOL)wParam;

    if (_bAborted == FALSE) {
        SetStatusText(_T("Upload finished."));
    } else {
        SetStatusText(_T("Upload aborted."));
    }

    GetDlgItem(IDC_BUTTON1)->EnableWindow(FALSE);
    UpdateWindow();

    return S_OK;
}

afx_msg LRESULT CSubtitleUpDlg::OnFailed(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SetStatusText(_T("Upload failed."));

    return S_OK;
}

afx_msg LRESULT CSubtitleUpDlg::OnClear(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    m_progress.SetPos(0);
    SetStatusText("");

    for (int i = 0; i < m_list.GetItemCount(); ++i) {
        SubtitlesProvider& iter = *(SubtitlesProvider*)m_list.GetItemData(i);
        if (iter.Flags(SPF_UPLOAD)) {
            m_list.SetItemText(i, COL_STATUS, _T("Ready..."));
        }
    }

    GetDlgItem(IDC_BUTTON2)->ShowWindow(FALSE);
    GetDlgItem(IDC_BUTTON1)->ShowWindow(TRUE);
    GetDlgItem(IDC_BUTTON1)->EnableWindow(TRUE);
    UpdateWindow();

    return S_OK;
}

void CSubtitleUpDlg::DoUpload(INT _nCount)
{
    SendMessage(UWM_UPLOAD, (WPARAM)_nCount, (LPARAM)nullptr);
}
void CSubtitleUpDlg::DoUploading(SubtitlesProvider& _provider)
{
    SendMessage(UWM_UPLOADING, (WPARAM)nullptr, (LPARAM)&_provider);
}
void CSubtitleUpDlg::DoCompleted(SRESULT _result, SubtitlesProvider& _provider)
{
    SendMessage(UWM_COMPLETED, (WPARAM)_result, (LPARAM)&_provider);
}
void CSubtitleUpDlg::DoFinished(BOOL _bAborted)
{
    SendMessage(UWM_FINISHED, (WPARAM)_bAborted, (LPARAM)nullptr);
}
void CSubtitleUpDlg::DoFailed()
{
    SendMessage(UWM_FAILED, (WPARAM)nullptr, (LPARAM)nullptr);
}
void CSubtitleUpDlg::DoClear()
{
    SendMessage(UWM_CLEAR, (WPARAM)nullptr, (LPARAM)nullptr);
}
