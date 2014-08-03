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
#include "mplayerc.h"
#include "SaveThumbnailsDialog.h"
#include "SysVersion.h"


// CSaveThumbnailsDialog

IMPLEMENT_DYNAMIC(CSaveThumbnailsDialog, CSaveImageDialog)
CSaveThumbnailsDialog::CSaveThumbnailsDialog(
    int nJpegQuality, int rows, int cols, int width,
    LPCTSTR lpszDefExt, LPCTSTR lpszFileName,
    LPCTSTR lpszFilter, CWnd* pParentWnd) :
    CSaveImageDialog(nJpegQuality, lpszDefExt, lpszFileName, lpszFilter, pParentWnd),
    m_rows(rows),
    m_cols(cols),
    m_width(width)
{
    if (SysVersion::IsVistaOrLater()) {
        // customization has to be done before OnInitDialog
        IFileDialogCustomize* pfdc = GetIFileDialogCustomize();
        CStringW str;

        pfdc->StartVisualGroup(IDS_THUMB_IMAGE_WIDTH, ResStr(IDS_THUMB_IMAGE_WIDTH));
        pfdc->AddText(IDS_THUMB_PIXELS, ResStr(IDS_THUMB_PIXELS));
        str.Format(L"%d", std::max(256, std::min(2560, m_width)));
        pfdc->AddEditBox(IDC_EDIT4, str);
        pfdc->EndVisualGroup();

        pfdc->StartVisualGroup(IDS_THUMB_THUMBNAILS, ResStr(IDS_THUMB_THUMBNAILS));
        pfdc->AddText(IDS_THUMB_ROWNUMBER, ResStr(IDS_THUMB_ROWNUMBER));
        str.Format(L"%d", std::max(1, std::min(20, m_rows)));
        pfdc->AddEditBox(IDC_EDIT2, str);

        pfdc->AddText(IDS_THUMB_COLNUMBER, ResStr(IDS_THUMB_COLNUMBER));
        str.Format(L"%d", std::max(1, std::min(10, m_cols)));
        pfdc->AddEditBox(IDC_EDIT3, str);
        pfdc->EndVisualGroup();

        pfdc->Release();
    } else {
        SetTemplate(0, IDD_SAVETHUMBSDIALOGTEMPL);
    }
}

CSaveThumbnailsDialog::~CSaveThumbnailsDialog()
{
}

void CSaveThumbnailsDialog::DoDataExchange(CDataExchange* pDX)
{
    if (!SysVersion::IsVistaOrLater()) {
        DDX_Control(pDX, IDC_SPIN2, m_rowsctrl);
        DDX_Control(pDX, IDC_SPIN3, m_colsctrl);
        DDX_Control(pDX, IDC_SPIN4, m_widthctrl);
    }
    __super::DoDataExchange(pDX);
}

BOOL CSaveThumbnailsDialog::OnInitDialog()
{
    __super::OnInitDialog();

    if (!SysVersion::IsVistaOrLater()) {
        m_rowsctrl.SetRange32(1, 20);
        m_colsctrl.SetRange32(1, 10);
        m_widthctrl.SetRange32(256, 2560);
        m_rowsctrl.SetPos32(m_rows);
        m_colsctrl.SetPos32(m_cols);
        m_widthctrl.SetPos32(m_width);
    }

    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}

BEGIN_MESSAGE_MAP(CSaveThumbnailsDialog, CFileDialog)
END_MESSAGE_MAP()

// CSaveThumbnailsDialog message handlers

BOOL CSaveThumbnailsDialog::OnFileNameOK()
{
    if (SysVersion::IsVistaOrLater()) {
        IFileDialogCustomize* pfdc = GetIFileDialogCustomize();
        WCHAR* result;

        pfdc->GetEditBoxText(IDC_EDIT2, &result);
        m_rows = _wtoi(result);
        CoTaskMemFree(result);
        pfdc->GetEditBoxText(IDC_EDIT3, &result);
        m_cols = _wtoi(result);
        CoTaskMemFree(result);
        pfdc->GetEditBoxText(IDC_EDIT4, &result);
        m_width = _wtoi(result);
        CoTaskMemFree(result);

        pfdc->Release();
    } else {
        m_rows = m_rowsctrl.GetPos32();
        m_cols = m_colsctrl.GetPos32();
        m_width = m_widthctrl.GetPos32();
    }

    return __super::OnFileNameOK();
}
