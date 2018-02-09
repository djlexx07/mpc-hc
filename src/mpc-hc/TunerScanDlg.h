/*
 * (C) 2009-2014 see Authors.txt
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

#include <afxcmn.h>
#include <afxwin.h>
#include <atomic>

class TunerScanData
{
public:
    ULONG FrequencyStart;
    ULONG FrequencyStop;
    ULONG Bandwidth;
    LONG  Offset;
    HWND  Hwnd;
};

// CTunerScanDlg dialog

class CTunerScanDlg : public CDialog
{
    DECLARE_DYNAMIC(CTunerScanDlg)

public:
    CTunerScanDlg(CWnd* pParent = nullptr);   // standard constructor
    virtual ~CTunerScanDlg();
    virtual BOOL OnInitDialog();

    // Dialog Data
    enum { IDD = IDD_TUNER_SCAN };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    void         SetProgress(bool bState);
    void SaveScanSettings();

    DECLARE_MESSAGE_MAP()
public:
    ULONG m_ulFrequencyStart;
    ULONG m_ulFrequencyEnd;
    ULONG m_ulBandwidth;
    LONG m_lOffset;
    CEdit m_OffsetEditBox;
    BOOL m_bUseOffset;
    BOOL m_bIgnoreEncryptedChannels;
    CProgressCtrl m_Progress;
    CProgressCtrl m_Strength;
    CProgressCtrl m_Quality;
    CListCtrl m_ChannelList;
    std::atomic<bool> m_bInProgress;
    std::atomic<bool> m_bStopRequested;
    CButton m_btnStart;
    CButton m_btnSave;
    CButton m_btnCancel;
    BOOL m_bRemoveChannels;
    CAutoPtr<TunerScanData> pTSD;

    afx_msg LRESULT OnScanProgress(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnScanEnd(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnStats(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnNewChannel(WPARAM wParam, LPARAM lParam);
    afx_msg void OnUpdateData();
    afx_msg void OnBnClickedCheckOffset();
    afx_msg void OnBnClickedSave();
    afx_msg void OnBnClickedStart();
    afx_msg void OnBnClickedCancel();

private:
    CWinThread* m_pTVToolsThread;
    CWnd* m_pParent;
};
