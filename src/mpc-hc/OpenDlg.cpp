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
#include <atlpath.h>
#include "mplayerc.h"
#include "PathUtils.h"
#include "OpenDlg.h"
#include "OpenFileDlg.h"


// COpenDlg dialog

//IMPLEMENT_DYNAMIC(COpenDlg, CResizableDialog)
COpenDlg::COpenDlg(CWnd* pParent /*=nullptr*/)
    : CResizableDialog(COpenDlg::IDD, pParent)
    , m_bMultipleFiles(false)
    , m_bAppendToPlaylist(FALSE)
{
}

COpenDlg::~COpenDlg()
{
}

void COpenDlg::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    DDX_Control(pDX, IDR_MAINFRAME, m_icon);
    DDX_Control(pDX, IDC_COMBO1, m_cbMRU);
    DDX_CBString(pDX, IDC_COMBO1, m_path);
    DDX_Control(pDX, IDC_COMBO2, m_cbMRUDub);
    DDX_CBString(pDX, IDC_COMBO2, m_pathDub);
    DDX_Control(pDX, IDC_STATIC1, m_labelDub);
    DDX_Check(pDX, IDC_CHECK1, m_bAppendToPlaylist);
}


BEGIN_MESSAGE_MAP(COpenDlg, CResizableDialog)
    ON_BN_CLICKED(IDC_BUTTON1, OnBrowseFile)
    ON_BN_CLICKED(IDC_BUTTON2, OnBrowseDubFile)
    ON_BN_CLICKED(IDOK, OnOk)
    ON_UPDATE_COMMAND_UI(IDC_STATIC1, OnUpdateDub)
    ON_UPDATE_COMMAND_UI(IDC_COMBO2, OnUpdateDub)
    ON_UPDATE_COMMAND_UI(IDC_BUTTON2, OnUpdateDub)
    ON_UPDATE_COMMAND_UI(IDOK, OnUpdateOk)
END_MESSAGE_MAP()


// COpenDlg message handlers

BOOL COpenDlg::OnInitDialog()
{
    __super::OnInitDialog();

    m_icon.SetIcon((HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));

    CAppSettings& s = AfxGetAppSettings();

    CRecentFileList& MRU = s.MRU;
    MRU.ReadList();
    m_cbMRU.ResetContent();
    for (int i = 0; i < MRU.GetSize(); i++) {
        if (!MRU[i].IsEmpty()) {
            m_cbMRU.AddString(MRU[i]);
        }
    }
    CorrectComboListWidth(m_cbMRU);

    CRecentFileList& MRUDub = s.MRUDub;
    MRUDub.ReadList();
    m_cbMRUDub.ResetContent();
    for (int i = 0; i < MRUDub.GetSize(); i++) {
        if (!MRUDub[i].IsEmpty()) {
            m_cbMRUDub.AddString(MRUDub[i]);
        }
    }
    CorrectComboListWidth(m_cbMRUDub);

    if (m_cbMRU.GetCount() > 0) {
        m_cbMRU.SetCurSel(0);
    }

    m_fns.RemoveAll();
    m_path.Empty();
    m_pathDub.Empty();
    m_bMultipleFiles = false;
    m_bAppendToPlaylist = FALSE;

    AddAnchor(m_cbMRU, TOP_LEFT, TOP_RIGHT);
    AddAnchor(m_cbMRUDub, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_BUTTON1, TOP_RIGHT);
    AddAnchor(IDC_BUTTON2, TOP_RIGHT);
    AddAnchor(IDOK, TOP_RIGHT);
    AddAnchor(IDCANCEL, TOP_RIGHT);
    AddAnchor(IDC_STATIC1, TOP_LEFT, TOP_RIGHT);

    CRect r;
    GetWindowRect(r);
    CSize size = r.Size();
    SetMinTrackSize(size);
    size.cx = 1000;
    SetMaxTrackSize(size);

    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}

static CString GetFileName(CString str)
{
    CPath p = str;
    p.StripPath();
    return (LPCTSTR)p;
}

void COpenDlg::OnBrowseFile()
{
    UpdateData();

    const CAppSettings& s = AfxGetAppSettings();

    CString filter;
    CAtlArray<CString> mask;
    s.m_Formats.GetFilter(filter, mask);

    DWORD dwFlags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_ENABLEINCLUDENOTIFY | OFN_NOCHANGEDIR;
    if (!s.fKeepHistory) {
        dwFlags |= OFN_DONTADDTORECENT;
    }

    COpenFileDlg fd(mask, true, nullptr, m_path, dwFlags, filter, this);
    if (fd.DoModal() != IDOK) {
        return;
    }

    m_fns.RemoveAll();

    POSITION pos = fd.GetStartPosition();
    while (pos) {
        /*
                CString str = fd.GetNextPathName(pos);
                POSITION insertpos = m_fns.GetTailPosition();
                while (insertpos && GetFileName(str).CompareNoCase(GetFileName(m_fns.GetAt(insertpos))) <= 0)
                    m_fns.GetPrev(insertpos);
                if (!insertpos) m_fns.AddHead(str);
                else m_fns.InsertAfter(insertpos, str);
        */
        m_fns.AddTail(fd.GetNextPathName(pos));
    }

    if (m_fns.GetCount() > 1
            || m_fns.GetCount() == 1
            && (m_fns.GetHead()[m_fns.GetHead().GetLength() - 1] == '\\'
                || m_fns.GetHead()[m_fns.GetHead().GetLength() - 1] == '*')) {
        m_bMultipleFiles = true;
        EndDialog(IDOK);
        return;
    }

    m_cbMRU.SetWindowText(fd.GetPathName());
}

void COpenDlg::OnBrowseDubFile()
{
    UpdateData();

    const CAppSettings& s = AfxGetAppSettings();

    CString filter;
    CAtlArray<CString> mask;
    s.m_Formats.GetAudioFilter(filter, mask);

    DWORD dwFlags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_HIDEREADONLY | OFN_ENABLEINCLUDENOTIFY | OFN_NOCHANGEDIR;
    if (!s.fKeepHistory) {
        dwFlags |= OFN_DONTADDTORECENT;
    }

    COpenFileDlg fd(mask, false, nullptr, m_pathDub, dwFlags, filter, this);

    if (fd.DoModal() != IDOK) {
        return;
    }

    m_cbMRUDub.SetWindowText(fd.GetPathName());
}

void COpenDlg::OnOk()
{
    UpdateData();

    m_fns.RemoveAll();
    m_fns.AddTail(PathUtils::Unquote(m_path));
    if (m_cbMRUDub.IsWindowEnabled()) {
        m_fns.AddTail(PathUtils::Unquote(m_pathDub));
    }

    m_bMultipleFiles = false;

    OnOK();
}

void COpenDlg::OnUpdateDub(CCmdUI* pCmdUI)
{
    UpdateData();
    pCmdUI->Enable(AfxGetAppSettings().m_Formats.GetEngine(m_path) == DirectShow);
}

void COpenDlg::OnUpdateOk(CCmdUI* pCmdUI)
{
    UpdateData();
    pCmdUI->Enable(!m_path.IsEmpty() || !m_pathDub.IsEmpty());
}
