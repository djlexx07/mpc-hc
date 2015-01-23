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
#include "PPageFormats.h"
#include "FileAssoc.h"
#include "PathUtils.h"
#include "SysVersion.h"
#include <psapi.h>
#include <string>
#include <atlimage.h>


// CPPageFormats dialog


IMPLEMENT_DYNAMIC(CPPageFormats, CPPageBase)
CPPageFormats::CPPageFormats()
    : CPPageBase(CPPageFormats::IDD, CPPageFormats::IDD)
    , m_list(0)
    , m_bInsufficientPrivileges(false)
    , m_bFileExtChanged(false)
    , m_iRtspHandler(0)
    , m_fRtspFileExtFirst(FALSE)
    , m_bHaveRegisteredCategory(false)
{
}

CPPageFormats::~CPPageFormats()
{
}

void CPPageFormats::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_list);
    DDX_Text(pDX, IDC_EDIT1, m_exts);
    DDX_Control(pDX, IDC_STATIC1, m_autoplay);
    DDX_Control(pDX, IDC_CHECK1, m_apvideo);
    DDX_Control(pDX, IDC_CHECK2, m_apmusic);
    DDX_Control(pDX, IDC_CHECK3, m_apaudiocd);
    DDX_Control(pDX, IDC_CHECK4, m_apdvd);
    DDX_Radio(pDX, IDC_RADIO1, m_iRtspHandler);
    DDX_Check(pDX, IDC_CHECK5, m_fRtspFileExtFirst);
    DDX_Control(pDX, IDC_CHECK6, m_fContextDir);
    DDX_Control(pDX, IDC_CHECK7, m_fContextFiles);
    DDX_Control(pDX, IDC_CHECK8, m_fAssociatedWithIcons);
}

int CPPageFormats::IsCheckedMediaCategory(int iItem)
{
    LVITEM lvi;
    lvi.iItem = iItem;
    lvi.iSubItem = 0;
    lvi.mask = LVIF_IMAGE;
    m_list.GetItem(&lvi);
    return lvi.iImage;
}

void CPPageFormats::SetCheckedMediaCategory(int iItem, int iChecked)
{
    LVITEM lvi;
    lvi.iItem = iItem;
    lvi.iSubItem = 0;
    lvi.mask = LVIF_IMAGE;
    lvi.iImage = iChecked;
    m_list.SetItem(&lvi);
}

void CPPageFormats::UpdateMediaCategoryState(int iItem)
{
    if (iItem < 0) {
        return;
    }

    auto& s = AfxGetAppSettings();

    CFileAssoc::reg_state_t state = s.fileAssoc.IsRegistered(m_mf[m_list.GetItemData(iItem)]);

    SetCheckedMediaCategory(iItem, (state == CFileAssoc::SOME_REGISTERED) ? 2 : (state == CFileAssoc::ALL_REGISTERED));
}

bool CPPageFormats::IsNeededIconsLib()
{
    for (int i = 0, cnt = m_list.GetItemCount(); i < cnt; i++) {
        if (IsCheckedMediaCategory(i) == 1) {
            return true;
        }
    }

    return false;
}

BEGIN_MESSAGE_MAP(CPPageFormats, CPPageBase)
    ON_NOTIFY(NM_CLICK, IDC_LIST1, OnMediaCategoryClicked)
    ON_NOTIFY(LVN_KEYDOWN, IDC_LIST1, OnMediaCategoryKeyDown)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, OnMediaCategorySelected)
    ON_NOTIFY(LVN_BEGINLABELEDIT, IDC_LIST1, OnBeginEditMediaCategoryEngine)
    ON_NOTIFY(LVN_DOLABELEDIT, IDC_LIST1, OnEditMediaCategoryEngine)
    ON_NOTIFY(LVN_ENDLABELEDIT, IDC_LIST1, OnEndEditMediaCategoryEngine)
    ON_BN_CLICKED(IDC_BUTTON2, OnBnClickedResetExtensionsList)
    ON_BN_CLICKED(IDC_BUTTON_EXT_SET, OnBnClickedSetExtensionsList)
    ON_BN_CLICKED(IDC_BUTTON1, OnBnRunAsAdmin)
    ON_BN_CLICKED(IDC_BUTTON7, OnBnWin8SetDefProg)
    ON_BN_CLICKED(IDC_ASSOCIATE_ALL_FORMATS, OnAssociateAllFormats)
    ON_BN_CLICKED(IDC_ASSOCIATE_AUDIO_FORMATS, OnAssociateAudioFormatsOnly)
    ON_BN_CLICKED(IDC_ASSOCIATE_VIDEO_FORMATS, OnAssociateVideoFormatsOnly)
    ON_BN_CLICKED(IDC_CLEAR_ALL_ASSOCIATIONS, OnClearAllAssociations)
    ON_BN_CLICKED(IDC_CHECK7, OnFilesAssocModified)
    ON_BN_CLICKED(IDC_CHECK8, OnFilesAssocModified)
    ON_UPDATE_COMMAND_UI(IDC_BUTTON2, OnUpdateButtonDefault)
    ON_UPDATE_COMMAND_UI(IDC_BUTTON_EXT_SET, OnUpdateButtonSet)
    ON_UPDATE_COMMAND_UI(IDC_BUTTON7, OnUpdateBnWin8SetDefProg)
END_MESSAGE_MAP()

// CPPageFormats message handlers

void CPPageFormats::LoadSettings()
{
    m_bFileExtChanged = false;
    m_bHaveRegisteredCategory = false;

    int fSetContextFiles = FALSE;

    const auto& s = AfxGetAppSettings();
    m_mf = s.m_Formats;
    m_list.DeleteAllItems();

    for (int i = 0, cnt = (int)m_mf.GetCount(); i < cnt; i++) {
        if (!m_mf[i].IsAssociable()) {
            continue;
        }

        CString label;
        label.Format(_T("%s (%s)"), m_mf[i].GetDescription(), m_mf[i].GetExts());

        int iItem = m_list.InsertItem(i, label);
        m_list.SetItemData(iItem, i);
        engine_t e = m_mf[i].GetEngineType();
        m_list.SetItemText(iItem, COL_ENGINE,
                           e == DirectShow ? _T("DirectShow") :
                           e == RealMedia ? _T("RealMedia") :
                           e == QuickTime ? _T("QuickTime") :
                           e == ShockWave ? _T("ShockWave") : _T("-"));

        CFileAssoc::reg_state_t state = s.fileAssoc.IsRegistered(m_mf[i]);
        if (!m_bHaveRegisteredCategory && state != CFileAssoc::NOT_REGISTERED) {
            m_bHaveRegisteredCategory = true;
        }
        SetCheckedMediaCategory(iItem, (state == CFileAssoc::SOME_REGISTERED) ? 2 : (state == CFileAssoc::ALL_REGISTERED));

        if (!fSetContextFiles && s.fileAssoc.AreRegisteredFileContextMenuEntries(m_mf[i]) != CFileAssoc::NOT_REGISTERED) {
            fSetContextFiles = TRUE;
        }
    }

    m_list.SetColumnWidth(COL_ENGINE, LVSCW_AUTOSIZE_USEHEADER);

    m_list.SetSelectionMark(0);
    m_list.SetItemState(0, LVIS_SELECTED, LVIS_SELECTED);
    m_exts = m_mf[m_list.GetItemData(0)].GetExtsWithPeriod();

    bool fRtspFileExtFirst;
    engine_t e = m_mf.GetRtspHandler(fRtspFileExtFirst);
    m_iRtspHandler = (e == RealMedia ? 0 : e == QuickTime ? 1 : 2);
    m_fRtspFileExtFirst = fRtspFileExtFirst;

    m_fContextFiles.SetCheck(fSetContextFiles);

    m_apvideo.SetCheck(s.fileAssoc.IsAutoPlayRegistered(CFileAssoc::AP_VIDEO));
    m_apmusic.SetCheck(s.fileAssoc.IsAutoPlayRegistered(CFileAssoc::AP_MUSIC));
    m_apaudiocd.SetCheck(s.fileAssoc.IsAutoPlayRegistered(CFileAssoc::AP_AUDIOCD));
    m_apdvd.SetCheck(s.fileAssoc.IsAutoPlayRegistered(CFileAssoc::AP_DVDMOVIE));

    m_fContextDir.SetCheck(s.fileAssoc.AreRegisteredFolderContextMenuEntries());
    m_fAssociatedWithIcons.SetCheck(s.fAssociatedWithIcons);

    UpdateData(FALSE);
}

BOOL CPPageFormats::OnInitDialog()
{
    __super::OnInitDialog();

    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT);

    m_list.InsertColumn(COL_CATEGORY, _T("Category"), LVCFMT_LEFT, 290);
    m_list.InsertColumn(COL_ENGINE, _T("Engine"), LVCFMT_RIGHT, 50);

    // We don't use m_onoff.Create(IDB_CHECKBOX, 12, 3, 0xffffff) since
    // we want to load the bitmap directly from the main executable.
    CImage onoff;
    onoff.LoadFromResource(AfxGetInstanceHandle(), IDB_CHECKBOX);
    m_onoff.Create(12, 12, ILC_COLOR4 | ILC_MASK, 0, 3);
    m_onoff.Add(CBitmap::FromHandle(onoff), 0xffffff);
    m_list.SetImageList(&m_onoff, LVSIL_SMALL);

    LoadSettings();
    CreateToolTip();

    SetButtonIcon(IDC_ASSOCIATE_ALL_FORMATS,   IDB_CHECK_ALL);
    SetButtonIcon(IDC_ASSOCIATE_AUDIO_FORMATS, IDB_CHECK_AUDIO);
    SetButtonIcon(IDC_ASSOCIATE_VIDEO_FORMATS, IDB_CHECK_VIDEO);
    SetButtonIcon(IDC_CLEAR_ALL_ASSOCIATIONS,  IDB_UNCHECK_ALL);

    if (SysVersion::IsVistaOrLater() && !IsUserAnAdmin()) {
        GetDlgItem(IDC_EDIT1)->EnableWindow(FALSE);
        GetDlgItem(IDC_ASSOCIATE_ALL_FORMATS)->EnableWindow(FALSE);
        GetDlgItem(IDC_ASSOCIATE_AUDIO_FORMATS)->EnableWindow(FALSE);
        GetDlgItem(IDC_ASSOCIATE_VIDEO_FORMATS)->EnableWindow(FALSE);
        GetDlgItem(IDC_CLEAR_ALL_ASSOCIATIONS)->EnableWindow(FALSE);

        GetDlgItem(IDC_CHECK1)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK2)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK3)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK4)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK5)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK6)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK7)->EnableWindow(FALSE);
        GetDlgItem(IDC_CHECK8)->ShowWindow(SW_HIDE);

        GetDlgItem(IDC_RADIO1)->EnableWindow(FALSE);
        GetDlgItem(IDC_RADIO2)->EnableWindow(FALSE);
        GetDlgItem(IDC_RADIO3)->EnableWindow(FALSE);

        GetDlgItem(IDC_BUTTON1)->SendMessage(BCM_SETSHIELD, 0, TRUE);
        GetDlgItem(IDC_BUTTON1)->ShowWindow(SW_SHOW);

        m_bInsufficientPrivileges = true;
    } else {
        GetDlgItem(IDC_BUTTON1)->ShowWindow(SW_HIDE);
    }

    if (SysVersion::Is8OrLater()) {
        GetDlgItem(IDC_BUTTON7)->ShowWindow(SW_SHOW);

        auto offsetControlBottomRight = [this](int nID, int dx, int dy) {
            CRect r;
            GetDlgItem(nID)->GetWindowRect(r);
            ScreenToClient(r);
            r.BottomRight().Offset(dx, dy);
            GetDlgItem(nID)->MoveWindow(r);
        };

        auto moveControl = [this](int nID, int dx, int dy) {
            CRect r;
            GetDlgItem(nID)->GetWindowRect(r);
            ScreenToClient(r);
            r.OffsetRect(dx, dy);
            GetDlgItem(nID)->MoveWindow(r);
        };

        const int dy = DPI().ScaleY(-5);

        offsetControlBottomRight(IDC_STATIC2, 0, dy);
        offsetControlBottomRight(IDC_LIST1, 0, dy);

        moveControl(IDC_EDIT1, 0, dy);
        moveControl(IDC_BUTTON2, 0, dy);
        moveControl(IDC_BUTTON_EXT_SET, 0, dy);

        CRect r;
        GetDlgItem(IDC_STATIC3)->GetWindowRect(r);
        ScreenToClient(r);
        r.TopLeft().Offset(0, dy);
        GetDlgItem(IDC_STATIC3)->MoveWindow(r);

        moveControl(IDC_CHECK8, 0, dy);
        moveControl(IDC_BUTTON1, 0, dy);
    } else {
        GetDlgItem(IDC_BUTTON7)->ShowWindow(SW_HIDE);
    }

    UpdateData(FALSE);

    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPageFormats::OnApply()
{
    auto& s = AfxGetAppSettings();

    m_bHaveRegisteredCategory = false;

    if (!m_bInsufficientPrivileges) {
        UpdateData();

        int iSelectedItem = m_list.GetSelectionMark();
        if (iSelectedItem >= 0) {
            DWORD_PTR i = m_list.GetItemData(iSelectedItem);

            m_mf[i].SetExts(m_exts);
            m_exts = m_mf[i].GetExtsWithPeriod();
            UpdateData(FALSE);
        }

        s.fileAssoc.RegisterApp();

        int fSetContextFiles = m_fContextFiles.GetCheck();
        int fSetAssociatedWithIcon = m_fAssociatedWithIcons.GetCheck();

        if (m_bFileExtChanged) {
            auto iconLib = s.fileAssoc.GetIconLib();

            if (fSetAssociatedWithIcon && IsNeededIconsLib() && !iconLib) {
                AfxMessageBox(IDS_MISSING_ICONS_LIB, MB_ICONEXCLAMATION | MB_OK, 0);
            }

            for (int i = 0, cnt = m_list.GetItemCount(); i < cnt; i++) {
                int iChecked = IsCheckedMediaCategory(i);
                if (!m_bHaveRegisteredCategory && iChecked) {
                    m_bHaveRegisteredCategory = true;
                }
                if (iChecked == 2) {
                    continue;
                }

                s.fileAssoc.Register(m_mf[m_list.GetItemData(i)], !!iChecked, !!fSetContextFiles, !!fSetAssociatedWithIcon);
            }

            m_bFileExtChanged = false;
        }

        s.fileAssoc.RegisterFolderContextMenuEntries(!!m_fContextDir.GetCheck());

        UpdateMediaCategoryState(m_list.GetSelectionMark());

        s.fileAssoc.RegisterAutoPlay(CFileAssoc::AP_VIDEO, !!m_apvideo.GetCheck());
        s.fileAssoc.RegisterAutoPlay(CFileAssoc::AP_MUSIC, !!m_apmusic.GetCheck());
        s.fileAssoc.RegisterAutoPlay(CFileAssoc::AP_AUDIOCD, !!m_apaudiocd.GetCheck());
        s.fileAssoc.RegisterAutoPlay(CFileAssoc::AP_DVDMOVIE, !!m_apdvd.GetCheck());

        m_mf.SetRtspHandler(m_iRtspHandler == 0 ? RealMedia : m_iRtspHandler == 1 ? QuickTime : DirectShow, !!m_fRtspFileExtFirst);

        s.m_Formats = m_mf;
        s.fAssociatedWithIcons = !!m_fAssociatedWithIcons.GetCheck();

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    return __super::OnApply();
}

void CPPageFormats::OnMediaCategoryClicked(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)pNMHDR;

    // Check or uncheck the media category
    if (lpnmlv->iItem >= 0 && lpnmlv->iSubItem == COL_CATEGORY) {
        CRect r;
        m_list.GetItemRect(lpnmlv->iItem, r, LVIR_ICON);
        if (r.PtInRect(lpnmlv->ptAction)) {
            if (m_bInsufficientPrivileges) {
                MessageBox(ResStr(IDS_CANNOT_CHANGE_FORMAT));
            } else {
                SetCheckedMediaCategory(lpnmlv->iItem, (IsCheckedMediaCategory(lpnmlv->iItem) != 1));
                m_bFileExtChanged = true;
                SetModified();
            }
        }
    }

    *pResult = 0;
}

void CPPageFormats::OnMediaCategoryKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLVKEYDOWN lpkd = (LPNMLVKEYDOWN)pNMHDR;

    if (lpkd->wVKey == VK_SPACE) {
        if (m_bInsufficientPrivileges) {
            MessageBox(ResStr(IDS_CANNOT_CHANGE_FORMAT));
        } else {
            int iItem = m_list.GetSelectionMark();
            SetCheckedMediaCategory(iItem, (IsCheckedMediaCategory(iItem) != 1));
            m_bFileExtChanged = true;
            SetModified();
        }
    }

    *pResult = 0;
}

void CPPageFormats::OnMediaCategorySelected(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);

    // Update the extensions' list
    if (pNMLV->iItem >= 0 && pNMLV->iSubItem == COL_CATEGORY
            && (pNMLV->uChanged & LVIF_STATE) && (pNMLV->uNewState & LVIS_SELECTED)) {
        m_exts = m_mf[m_list.GetItemData(pNMLV->iItem)].GetExtsWithPeriod();
        UpdateData(FALSE);
    }

    *pResult = 0;
}

void CPPageFormats::OnBeginEditMediaCategoryEngine(NMHDR* pNMHDR, LRESULT* pResult)
{
    LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
    LV_ITEM* pItem = &pDispInfo->item;

    *pResult = FALSE;

    if (pItem->iItem >= 0 && pItem->iSubItem == COL_ENGINE) {
        if (m_bInsufficientPrivileges) {
            MessageBox(ResStr(IDS_CANNOT_CHANGE_FORMAT));
            // This isn't technically true, because we have access,
            // but we enforce user to use elevated window for consistency
        } else {
            *pResult = TRUE;
        }
    }
}

void CPPageFormats::OnEditMediaCategoryEngine(NMHDR* pNMHDR, LRESULT* pResult)
{
    LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
    LV_ITEM* pItem = &pDispInfo->item;

    *pResult = FALSE;

    if (pItem->iItem >= 0 && pItem->iSubItem == COL_ENGINE) {
        const CMediaFormatCategory& mfc = m_mf[m_list.GetItemData(pItem->iItem)];

        CAtlList<CString> sl;
        sl.AddTail(_T("DirectShow"));
        sl.AddTail(_T("RealMedia"));
        sl.AddTail(_T("QuickTime"));
        sl.AddTail(_T("ShockWave"));

        int nSel = (int)mfc.GetEngineType();

        m_list.ShowInPlaceComboBox(pItem->iItem, pItem->iSubItem, sl, nSel);

        *pResult = TRUE;
    }
}

void CPPageFormats::OnEndEditMediaCategoryEngine(NMHDR* pNMHDR, LRESULT* pResult)
{
    LV_DISPINFO* pDispInfo = (LV_DISPINFO*)pNMHDR;
    LV_ITEM* pItem = &pDispInfo->item;

    *pResult = FALSE;

    if (m_list.m_fInPlaceDirty && pItem->iItem >= 0 && pItem->iSubItem == COL_ENGINE && pItem->lParam >= 0) {
        CMediaFormatCategory& mfc = m_mf[m_list.GetItemData(pItem->iItem)];

        mfc.SetEngineType((engine_t)pItem->lParam);
        m_list.SetItemText(pItem->iItem, pItem->iSubItem, pItem->pszText);
        *pResult = TRUE;

        SetModified();
    }
}

void CPPageFormats::SetSelectionAllFormats(bool bSelect)
{
    for (int i = 0, cnt = m_list.GetItemCount(); i < cnt; i++) {
        SetCheckedMediaCategory(i, bSelect);
    }

    const int nCheck = bSelect ? BST_CHECKED : BST_UNCHECKED;
    m_apvideo.SetCheck(nCheck);
    m_apmusic.SetCheck(nCheck);
    m_apaudiocd.SetCheck(nCheck);
    m_apdvd.SetCheck(nCheck);

    m_bFileExtChanged = true;
    SetModified();
}

void CPPageFormats::OnAssociateAllFormats()
{
    SetSelectionAllFormats(true);
}

void CPPageFormats::OnAssociateVideoFormatsOnly()
{
    for (int i = 0, cnt = m_list.GetItemCount(); i < cnt; i++) {
        if (!m_mf[m_list.GetItemData(i)].GetLabel().CompareNoCase(_T("pls"))) {
            SetCheckedMediaCategory(i, 0);
        } else {
            SetCheckedMediaCategory(i, !m_mf[m_list.GetItemData(i)].IsAudioOnly());
        }
    }

    m_apvideo.SetCheck(BST_CHECKED);
    m_apmusic.SetCheck(BST_UNCHECKED);
    m_apaudiocd.SetCheck(BST_UNCHECKED);
    m_apdvd.SetCheck(BST_CHECKED);

    m_bFileExtChanged = true;
    SetModified();
}

void CPPageFormats::OnAssociateAudioFormatsOnly()
{
    for (int i = 0, cnt = m_list.GetItemCount(); i < cnt; i++) {
        SetCheckedMediaCategory(i, m_mf[m_list.GetItemData(i)].IsAudioOnly());
    }

    m_apvideo.SetCheck(BST_UNCHECKED);
    m_apmusic.SetCheck(BST_CHECKED);
    m_apaudiocd.SetCheck(BST_CHECKED);
    m_apdvd.SetCheck(BST_UNCHECKED);

    m_bFileExtChanged = true;
    SetModified();
}

void CPPageFormats::OnClearAllAssociations()
{
    SetSelectionAllFormats(false);
}

void CPPageFormats::OnBnRunAsAdmin()
{
    CString strCmd;
    strCmd.Format(_T("/adminoption %d"), IDD);

    AfxGetMyApp()->RunAsAdministrator(PathUtils::GetProgramPath(true), strCmd, true);

    auto& s = AfxGetAppSettings();
    s.m_Formats.UpdateData(false);
    s.fAssociatedWithIcons = !!AfxGetApp()->GetProfileInt(IDS_R_SETTINGS, IDS_RS_ASSOCIATED_WITH_ICON, TRUE);

    LoadSettings();
}

void CPPageFormats::OnBnClickedResetExtensionsList()
{
    int iItem = m_list.GetSelectionMark();

    if (iItem >= 0) {
        DWORD_PTR i = m_list.GetItemData(iItem);
        CMediaFormatCategory& mfc = m_mf[i];

        mfc.RestoreDefaultExts();
        m_exts = mfc.GetExtsWithPeriod();

        CString label;
        label.Format(_T("%s (%s)"), mfc.GetDescription(), mfc.GetExts());
        m_list.SetItemText(iItem, COL_CATEGORY, label);

        UpdateMediaCategoryState(iItem);
        UpdateData(FALSE);

        SetModified();
    }
}

void CPPageFormats::OnBnClickedSetExtensionsList()
{
    UpdateData();

    int iItem = m_list.GetSelectionMark();

    if (iItem >= 0) {
        DWORD_PTR i = m_list.GetItemData(iItem);
        CMediaFormatCategory& mfc = m_mf[i];

        mfc.SetExts(m_exts);
        m_exts = mfc.GetExtsWithPeriod();

        CString label;
        label.Format(_T("%s (%s)"), mfc.GetDescription(), mfc.GetExts());
        m_list.SetItemText(iItem, COL_CATEGORY, label);

        UpdateMediaCategoryState(iItem);
        UpdateData(FALSE);

        SetModified();
    }
}

void CPPageFormats::OnFilesAssocModified()
{
    m_bFileExtChanged = true;
    SetModified();
}

void CPPageFormats::OnBnWin8SetDefProg()
{
    // Windows 8 prevents the applications from programmatically changing the default handler
    // for a file type or protocol so we have to make use of Windows UI for that.
    AfxGetAppSettings().fileAssoc.ShowWindowsAssocDialog();
}

void CPPageFormats::OnUpdateButtonDefault(CCmdUI* pCmdUI)
{
    int iItem = m_list.GetSelectionMark();
    if (iItem < 0) {
        pCmdUI->Enable(FALSE);
    } else {
        DWORD_PTR i = m_list.GetItemData(iItem);

        CString orgexts, newexts;
        GetDlgItem(IDC_EDIT1)->GetWindowText(newexts);
        newexts.Trim();
        orgexts = m_mf[i].GetBackupExtsWithPeriod();

        pCmdUI->Enable(!!newexts.CompareNoCase(orgexts));
    }
}

void CPPageFormats::OnUpdateButtonSet(CCmdUI* pCmdUI)
{
    int iItem = m_list.GetSelectionMark();
    if (iItem < 0) {
        pCmdUI->Enable(FALSE);
    } else {
        DWORD_PTR i = m_list.GetItemData(iItem);

        CString orgexts, newexts;
        GetDlgItem(IDC_EDIT1)->GetWindowText(newexts);
        newexts.Trim();
        orgexts = m_mf[i].GetExtsWithPeriod();

        pCmdUI->Enable(!!newexts.CompareNoCase(orgexts));
    }
}

void CPPageFormats::OnUpdateBnWin8SetDefProg(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(m_bHaveRegisteredCategory);
}
