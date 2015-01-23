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

#pragma once

#include "resource.h"
#include "ResizableLib/ResizableDialog.h"
#include <list>

class CMainFrame;
struct SubtitlesInfo;
class SubtitlesProvider;
class SubtitlesProviders;
typedef std::list<SubtitlesInfo> SubtitlesList;
enum SRESULT;

class CSubtitleUpDlg : public CResizableDialog
{
private:
    enum {
        COL_PROVIDER,
        COL_USERNAME,
        COL_STATUS,
        COL_TOTAL_COLUMNS,
    };

    CListCtrl m_list;
    CProgressCtrl m_progress;
    CStatusBarCtrl m_status;
    CMainFrame& m_pMainFrame;

    void DownloadSelectedSubtitles();
    void SetStatusText(const CString& status, BOOL bPropagate = TRUE);

public:
    CSubtitleUpDlg(CWnd* pParentWnd);
    virtual ~CSubtitleUpDlg();
    enum { IDD = IDD_SUBTITLEUP_DLG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual BOOL PreTranslateMessage(MSG* pMsg);
    virtual void OnOK();

    static int CALLBACK SortCompare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

    DECLARE_MESSAGE_MAP()

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnUpdateOk(CCmdUI* pCmdUI);
    afx_msg void OnUpdateRefresh(CCmdUI* pCmdUI);
    afx_msg void OnAbort();
    afx_msg void OnRefresh();
    afx_msg void OnOptions();
    afx_msg void OnDestroy();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnDoubleClickSubtitle(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnKeyPressedSubtitle(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnRightClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnItemChanging(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);

    afx_msg LRESULT OnUpload(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnUploading(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCompleted(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnFinished(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnFailed(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnClear(WPARAM wParam, LPARAM lParam);

public:
    void DoUpload(INT _nCount);
    void DoUploading(SubtitlesProvider& _provider);
    void DoCompleted(SRESULT _result, SubtitlesProvider& _provider);
    void DoFinished(BOOL _bAborted);
    void DoFailed();
    void DoClear();
};
