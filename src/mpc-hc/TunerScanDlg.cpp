/*
 * (C) 2009-2017 see Authors.txt
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

// TunerScanDlg.cpp : implementation file
//

#include "stdafx.h"
#include "mplayerc.h"
#include "TunerScanDlg.h"
#include "TVToolsDlg.h"
#include "MainFrm.h"


enum TSC_COLUMN {
    TSCC_NUMBER,
    TSCC_NAME,
    TSCC_FREQUENCY,
    TSCC_ENCRYPTED,
    TSCC_VIDEO_FORMAT,
    TSCC_VIDEO_FPS,
    TSCC_VIDEO_RES,
    TSCC_VIDEO_AR,
    TSCC_CHANNEL
};

// CTunerScanDlg dialog

IMPLEMENT_DYNAMIC(CTunerScanDlg, CDialog)

CTunerScanDlg::CTunerScanDlg(CWnd* pParent /*=nullptr*/)
    : CDialog(CTunerScanDlg::IDD, pParent)
    , m_bInProgress(false)
    , m_bStopRequested(false)
    , m_bRemoveChannels(false)
    , pTSD(DEBUG_NEW TunerScanData)
    , m_pTVToolsThread(nullptr)
{
    const CAppSettings& s = AfxGetAppSettings();
    m_pParent = pParent;

    m_ulFrequencyStart = s.iBDAScanFreqStart;
    m_ulFrequencyEnd = s.iBDAScanFreqEnd;
    m_ulBandwidth = s.iBDABandwidth * 1000;
    m_bUseOffset = s.fBDAUseOffset;
    m_lOffset = s.iBDAOffset;
    m_bIgnoreEncryptedChannels = s.fBDAIgnoreEncryptedChannels;

}

CTunerScanDlg::~CTunerScanDlg()
{
}

BOOL CTunerScanDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_pTVToolsThread = dynamic_cast<CTVToolsDlg*> (m_pParent)->m_pTVToolsThread;
    if (!m_pTVToolsThread) {
        ASSERT(FALSE);
    }

    m_OffsetEditBox.EnableWindow(m_bUseOffset);

    m_ChannelList.InsertColumn(TSCC_NUMBER, ResStr(IDS_DVB_CHANNEL_NUMBER), LVCFMT_LEFT, 35);
    m_ChannelList.InsertColumn(TSCC_NAME, ResStr(IDS_DVB_CHANNEL_NAME), LVCFMT_LEFT, 190);
    m_ChannelList.InsertColumn(TSCC_FREQUENCY, ResStr(IDS_DVB_CHANNEL_FREQUENCY), LVCFMT_LEFT, 65);
    m_ChannelList.InsertColumn(TSCC_ENCRYPTED, ResStr(IDS_DVB_CHANNEL_ENCRYPTION), LVCFMT_CENTER, 55);
    m_ChannelList.InsertColumn(TSCC_VIDEO_FORMAT, ResStr(IDS_DVB_CHANNEL_FORMAT), LVCFMT_CENTER, 55);
    m_ChannelList.InsertColumn(TSCC_VIDEO_FPS, ResStr(IDS_DVB_CHANNEL_FPS), LVCFMT_CENTER, 50);
    m_ChannelList.InsertColumn(TSCC_VIDEO_RES, ResStr(IDS_DVB_CHANNEL_RESOLUTION), LVCFMT_CENTER, 70);
    m_ChannelList.InsertColumn(TSCC_VIDEO_AR, ResStr(IDS_DVB_CHANNEL_ASPECT_RATIO), LVCFMT_CENTER, 50);
    m_ChannelList.InsertColumn(TSCC_CHANNEL, _T("Channel"), LVCFMT_LEFT, 0);

    m_Progress.SetRange(0, 100);
    m_Strength.SetRange(0, 100);
    m_Quality.SetRange(0, 100);
    m_btnSave.EnableWindow(FALSE);
    GetDlgItem(IDC_CHECK_REMOVE_CHANNELS)->EnableWindow(FALSE);

    return TRUE;
}

void CTunerScanDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_FREQ_START, m_ulFrequencyStart);
    DDX_Text(pDX, IDC_FREQ_END, m_ulFrequencyEnd);
    DDX_Text(pDX, IDC_BANDWIDTH, m_ulBandwidth);
    DDX_Text(pDX, IDC_OFFSET, m_lOffset);
    DDX_Check(pDX, IDC_CHECK_OFFSET, m_bUseOffset);
    DDX_Check(pDX, IDC_CHECK_IGNORE_ENCRYPTED, m_bIgnoreEncryptedChannels);
    DDX_Control(pDX, IDC_PROGRESS, m_Progress);
    DDX_Control(pDX, IDC_STRENGTH, m_Strength);
    DDX_Control(pDX, IDC_QUALITY, m_Quality);
    DDX_Control(pDX, IDC_CHANNEL_LIST, m_ChannelList);
    DDX_Control(pDX, ID_START, m_btnStart);
    DDX_Control(pDX, ID_SAVE, m_btnSave);
    DDX_Check(pDX, IDC_CHECK_REMOVE_CHANNELS, m_bRemoveChannels);
    DDX_Control(pDX, IDCANCEL, m_btnCancel);
    DDX_Control(pDX, IDC_OFFSET, m_OffsetEditBox);
}

BEGIN_MESSAGE_MAP(CTunerScanDlg, CDialog)
    ON_MESSAGE(WM_TUNER_SCAN_PROGRESS, OnScanProgress)
    ON_MESSAGE(WM_TUNER_SCAN_END, OnScanEnd)
    ON_MESSAGE(WM_TUNER_STATS, OnStats)
    ON_MESSAGE(WM_TUNER_NEW_CHANNEL, OnNewChannel)
    ON_BN_CLICKED(ID_SAVE, &CTunerScanDlg::OnBnClickedSave)
    ON_BN_CLICKED(ID_START, &CTunerScanDlg::OnBnClickedStart)
    ON_BN_CLICKED(IDCANCEL, &CTunerScanDlg::OnBnClickedCancel)
    ON_BN_CLICKED(IDC_CHECK_REMOVE_CHANNELS, OnUpdateData)
    ON_BN_CLICKED(IDC_CHECK_OFFSET, &CTunerScanDlg::OnBnClickedCheckOffset)
END_MESSAGE_MAP()

// CTunerScanDlg message handlers

void CTunerScanDlg::OnUpdateData()
{
    UpdateData(true);
}

void CTunerScanDlg::OnBnClickedSave()
{
    auto& DVBChannels = AfxGetAppSettings().m_DVBChannels;
    const size_t maxChannelsNum = ID_NAVIGATE_JUMPTO_SUBITEM_END - ID_NAVIGATE_JUMPTO_SUBITEM_START + 1;
    CAppSettings& s = AfxGetAppSettings();
    int iChannel = 0;

    if (m_bRemoveChannels) {
        // Remove only DVB Channels
        auto new_end = std::remove_if(std::begin(DVBChannels), std::end(DVBChannels), [](const CDVBChannel & c) { return c.IsDVB(); });
        DVBChannels.erase(new_end, DVBChannels.end());
        s.uNextChannelCount = 0;
    }

    for (int i = 0; i < m_ChannelList.GetItemCount(); i++) {
        try {
            CDVBChannel channel(m_ChannelList.GetItemText(i, TSCC_CHANNEL));
            bool bItemUpdated = false;
            auto it = DVBChannels.begin();
            while (it != DVBChannels.end() && !bItemUpdated) {
                if (channel.IsDVB()) {
                    if (it->GetONID() == channel.GetONID() && it->GetTSID() == channel.GetTSID()
                            && it->GetSID() == channel.GetSID()) {
                        // Update existing channel
                        channel.SetPrefNumber(it->GetPrefNumber());
                        *it = channel;
                        iChannel = channel.GetPrefNumber();
                        bItemUpdated = true;
                    }
                }
                if (!bItemUpdated) {
                    *it++;
                }
            }
            if (!bItemUpdated) {
                // Add new channel to the end
                const size_t size = DVBChannels.size();
                if (size < maxChannelsNum) {
                    UINT nNextChannelID = s.uNextChannelCount;
                    while (s.FindChannelByPref(nNextChannelID)) {
                        nNextChannelID++;
                    }
                    channel.SetPrefNumber(nNextChannelID);
                    s.uNextChannelCount = nNextChannelID + 1;
                    DVBChannels.push_back(channel);
                    iChannel = channel.GetPrefNumber();
                } else {
                    // Just to be safe. We have 600 channels limit, but we never know what user might load there.
                    CString msg;
                    msg.Format(_T("Unable to add new channel \"%s\" to the list. Channels list is full. Please notify developers about the problem."), channel.GetName());
                    AfxMessageBox(msg, MB_OK | MB_ICONERROR);
                }
            }
        } catch (CException* e) {
            // The tokenisation can fail if the input string was invalid
            TRACE(_T("Failed to parse a DVB channel from string \"%s\""), m_ChannelList.GetItemText(i, TSCC_CHANNEL).GetString());
            ASSERT(FALSE);
            e->Delete();
        }
    }

    // Update the preferred numbers
    int nPrefNumber = 0;
    int nDVBLastChannel = s.nDVBLastChannel;
    for (auto& channel : s.m_DVBChannels) {
        // Make sure the current channel number will be updated
        if (channel.GetPrefNumber() == s.nDVBLastChannel) {
            nDVBLastChannel = nPrefNumber;
        }
        channel.SetPrefNumber(nPrefNumber++);
    }
    s.nDVBLastChannel = nDVBLastChannel;

    GetParent()->SendMessage(WM_CLOSE);
    GetParent()->SendMessage(WM_DESTROY);
}

void CTunerScanDlg::OnBnClickedStart()
{
    if (!m_bInProgress) {
        UpdateData(true);
        if (!pTSD) {
            pTSD.m_p = DEBUG_NEW TunerScanData;
        }
        pTSD->Hwnd           = m_hWnd;
        pTSD->FrequencyStart = m_ulFrequencyStart;
        pTSD->FrequencyStop  = m_ulFrequencyEnd;
        pTSD->Bandwidth      = m_ulBandwidth;
        pTSD->Offset         = m_bUseOffset ? m_lOffset : 0;
        SaveScanSettings();

        m_ChannelList.DeleteAllItems();
        if (m_pTVToolsThread) {
            m_pTVToolsThread->PostThreadMessage(CTVToolsThread::TM_TUNER_SCAN, 0, (LPARAM)pTSD.Detach());
        } else {
            TRACE(_T("m_pTVToolsThread thread not found."));
            ASSERT(FALSE);
        }

        SetProgress(true);
    } else {
        m_bStopRequested = true;
        m_btnStart.EnableWindow(false);
    }
}

void CTunerScanDlg::OnBnClickedCancel()
{
    m_btnCancel.EnableWindow(false);
    m_btnStart.EnableWindow(false);
    m_bStopRequested = true;

    GetParent()->SendMessage(WM_CLOSE);
    GetParent()->SendMessage(WM_DESTROY);
}

void CTunerScanDlg::OnBnClickedCheckOffset()
{
    UpdateData(true);
    m_OffsetEditBox.EnableWindow(m_bUseOffset);
}

LRESULT CTunerScanDlg::OnScanProgress(WPARAM wParam, LPARAM lParam)
{
    m_Progress.SetPos(static_cast<int>(wParam));
    return TRUE;
}

LRESULT CTunerScanDlg::OnScanEnd(WPARAM wParam, LPARAM lParam)
{
    SetProgress(false);
    m_bStopRequested = false;
    m_btnStart.EnableWindow(true);
    return TRUE;
}

LRESULT CTunerScanDlg::OnStats(WPARAM wParam, LPARAM lParam)
{
    m_Strength.SetPos((int)wParam);
    m_Quality.SetPos((int)lParam);
    return TRUE;
}

LRESULT CTunerScanDlg::OnNewChannel(WPARAM wParam, LPARAM lParam)
{
    try {
        CDVBChannel channel((LPCTSTR)lParam);
        if (!m_bIgnoreEncryptedChannels || !channel.IsEncrypted()) {
            CString strTemp;
            int nItem, nChannelNumber;

            if (channel.GetOriginNumber() != 0) { // LCN is available
                nChannelNumber = channel.GetOriginNumber();
                // Insert new channel so that channels are sorted by their logical number
                for (nItem = 0; nItem < m_ChannelList.GetItemCount(); nItem++) {
                    if ((int)m_ChannelList.GetItemData(nItem) > nChannelNumber || (int)m_ChannelList.GetItemData(nItem) == 0) {
                        break;
                    }
                }
            } else {
                nChannelNumber = 0;
                nItem = m_ChannelList.GetItemCount();
            }

            strTemp.Format(_T("%d"), nChannelNumber);
            nItem = m_ChannelList.InsertItem(nItem, strTemp);
            m_ChannelList.EnsureVisible(m_ChannelList.GetItemCount() - 1, false); // Scroll down to the bottom

            m_ChannelList.SetItemData(nItem, channel.GetOriginNumber());

            m_ChannelList.SetItemText(nItem, TSCC_NAME, channel.GetName());

            strTemp.Format(_T("%lu"), channel.GetFrequency());
            m_ChannelList.SetItemText(nItem, TSCC_FREQUENCY, strTemp);

            m_ChannelList.SetItemText(nItem, TSCC_ENCRYPTED, ResStr(channel.IsEncrypted() ? IDS_YES : IDS_NO));
            if (channel.GetVideoType() == DVB_H264) {
                strTemp = _T("H.264");
            } else if (channel.GetVideoType() == DVB_HEVC) {
                strTemp = _T("HEVC");
            } else if (channel.GetVideoPID()) {
                strTemp = _T("MPEG-2");
            } else {
                strTemp = _T("-");
            }
            m_ChannelList.SetItemText(nItem, TSCC_VIDEO_FORMAT, strTemp);
            strTemp = channel.GetVideoFpsDesc();
            m_ChannelList.SetItemText(nItem, TSCC_VIDEO_FPS, strTemp);
            if (channel.GetVideoWidth() || channel.GetVideoHeight()) {
                strTemp.Format(_T("%lux%lu"), channel.GetVideoWidth(), channel.GetVideoHeight());
            } else {
                strTemp = _T("-");
            }
            m_ChannelList.SetItemText(nItem, TSCC_VIDEO_RES, strTemp);
            strTemp.Format(_T("%lu/%lu"), channel.GetVideoARy(), channel.GetVideoARx());
            m_ChannelList.SetItemText(nItem, TSCC_VIDEO_AR, strTemp);
            m_ChannelList.SetItemText(nItem, TSCC_CHANNEL, (LPCTSTR) lParam);
        }
    } catch (CException* e) {
        // The tokenisation can fail if the input string was invalid
        TRACE(_T("Failed to parse a DVB channel from string \"%s\""), (LPCTSTR)lParam);
        ASSERT(FALSE);
        e->Delete();
        return FALSE;
    }

    return TRUE;
}

void CTunerScanDlg::SetProgress(bool bState)
{
    if (bState) {
        m_btnStart.SetWindowTextW(ResStr(IDS_DVB_CHANNEL_STOP_SCAN));
    } else {
        m_btnStart.SetWindowTextW(ResStr(IDS_DVB_CHANNEL_START_SCAN));
        m_Progress.SetPos(0);
    }

    m_btnSave.EnableWindow(!bState);
    GetDlgItem(IDC_CHECK_REMOVE_CHANNELS)->EnableWindow(!bState);
    auto pParentWnd = dynamic_cast<CTVToolsDlg*>(GetParent());
    if (pParentWnd->m_TabCtrl) {
        pParentWnd->m_TabCtrl.EnableWindow(!bState);
    }

    m_bInProgress = bState;
}

void CTunerScanDlg::SaveScanSettings()
{
    CAppSettings& s = AfxGetAppSettings();

    s.iBDAScanFreqStart = m_ulFrequencyStart;
    s.iBDAScanFreqEnd = m_ulFrequencyEnd;
    div_t bdw = div(m_ulBandwidth, 1000);
    s.iBDABandwidth = bdw.quot;
    s.fBDAUseOffset = !!m_bUseOffset;
    s.iBDAOffset = m_lOffset;
    s.fBDAIgnoreEncryptedChannels = !!m_bIgnoreEncryptedChannels;
}
