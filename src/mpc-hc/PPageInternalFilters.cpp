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
#include "FGFilterLAV.h"
#include "PPageInternalFilters.h"
#include "../filters/Filters.h"
#include "InternalFiltersConfig.h"

IMPLEMENT_DYNAMIC(CPPageInternalFiltersListBox, CCheckListBox)
CPPageInternalFiltersListBox::CPPageInternalFiltersListBox(int n, const CArray<filter_t>& filters)
    : CCheckListBox()
    , m_filters(filters)
    , m_n(n)
{
    for (int i = 0; i < FILTER_TYPE_NB; i++) {
        m_nbFiltersPerType[i] = m_nbChecked[i] = 0;
    }
}

void CPPageInternalFiltersListBox::PreSubclassWindow()
{
    __super::PreSubclassWindow();
    EnableToolTips(TRUE);
}

INT_PTR CPPageInternalFiltersListBox::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
{
    BOOL b = FALSE;
    int row = ItemFromPoint(point, b);
    if (row < 0) {
        return -1;
    }

    CRect r;
    GetItemRect(row, r);
    pTI->rect = r;
    pTI->hwnd = m_hWnd;
    pTI->uId = (UINT)row;
    pTI->lpszText = LPSTR_TEXTCALLBACK;
    pTI->uFlags |= TTF_ALWAYSTIP;

    return pTI->uId;
}

BEGIN_MESSAGE_MAP(CPPageInternalFiltersListBox, CCheckListBox)
    ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnToolTipNotify)
    ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

BOOL CPPageInternalFiltersListBox::OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
    TOOLTIPTEXT* pTTT = (TOOLTIPTEXT*)pNMHDR;

    filter_t* f = (filter_t*)GetItemDataPtr(static_cast<int>(pNMHDR->idFrom));
    if (f->nHintID == 0) {
        return FALSE;
    }

    ::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, 1000);

    static CString strTipText; // static string
    strTipText.LoadString(f->nHintID);
    pTTT->lpszText = strTipText.GetBuffer();

    *pResult = 0;

    return TRUE;    // message was handled
}

int CPPageInternalFiltersListBox::AddFilter(filter_t* filter, bool checked)
{
    int index = AddString(filter->label);
    // SetItemDataPtr must be called before SetCheck
    SetItemDataPtr(index, filter);
    SetCheck(index, checked);

    return index;
}

void CPPageInternalFiltersListBox::UpdateCheckState()
{
    for (int i = 0; i < FILTER_TYPE_NB; i++) {
        m_nbFiltersPerType[i] = m_nbChecked[i] = 0;
    }

    for (int i = 0; i < GetCount(); i++) {
        filter_t* filter = (filter_t*) GetItemDataPtr(i);

        m_nbFiltersPerType[filter->type]++;

        if (GetCheck(i)) {
            m_nbChecked[filter->type]++;
        }
    }
}

void CPPageInternalFiltersListBox::OnContextMenu(CWnd* pWnd, CPoint point)
{
    BOOL bOutside;

    if (point.x == -1 && point.y == -1) {
        // The context menu was triggered using the keyboard,
        // get the coordinates of the currently selected item.
        int iSel = GetCurSel();
        CRect r;
        if (GetItemRect(iSel, &r) != LB_ERR) {
            point.SetPoint(r.left, r.bottom);
        } else {
            point.SetPoint(0, 0);
        }
        bOutside = iSel == LB_ERR;
    } else {
        ScreenToClient(&point);
        ItemFromPoint(point, bOutside);
    }

    // Check that we really inside the list
    if (bOutside) {
        // Trigger the default behavior
        ClientToScreen(&point);
        __super::OnContextMenu(pWnd, point);
        return;
    }

    CMenu m;
    m.CreatePopupMenu();

    enum {
        ENABLE_ALL = 1,
        DISABLE_ALL,
        ENABLE_VIDEO,
        DISABLE_VIDEO,
        ENABLE_AUDIO,
        DISABLE_AUDIO
    };

    int totalFilters = 0, totalChecked = 0;
    for (int i = 0; i < FILTER_TYPE_NB; i++) {
        totalFilters += m_nbFiltersPerType[i];
        totalChecked += m_nbChecked[i];
    }

    UINT state = (totalChecked != totalFilters) ? MF_ENABLED : MF_GRAYED;
    m.AppendMenu(MF_STRING | state, ENABLE_ALL, ResStr(IDS_ENABLE_ALL_FILTERS));
    state = (totalChecked != 0) ? MF_ENABLED : MF_GRAYED;
    m.AppendMenu(MF_STRING | state, DISABLE_ALL, ResStr(IDS_DISABLE_ALL_FILTERS));

    if (m_n == 1) {
        m.AppendMenu(MF_SEPARATOR);
        state = (m_nbChecked[AUDIO_DECODER] != m_nbFiltersPerType[AUDIO_DECODER]) ? MF_ENABLED : MF_GRAYED;
        m.AppendMenu(MF_STRING | state, ENABLE_AUDIO, ResStr(IDS_ENABLE_AUDIO_FILTERS));
        state = (m_nbChecked[AUDIO_DECODER] != 0) ? MF_ENABLED : MF_GRAYED;
        m.AppendMenu(MF_STRING | state, DISABLE_AUDIO, ResStr(IDS_DISABLE_AUDIO_FILTERS));

        m.AppendMenu(MF_SEPARATOR);
        state = (m_nbChecked[VIDEO_DECODER] != m_nbFiltersPerType[VIDEO_DECODER]) ? MF_ENABLED : MF_GRAYED;
        m.AppendMenu(MF_STRING | state, ENABLE_VIDEO, ResStr(IDS_ENABLE_VIDEO_FILTERS));
        state = (m_nbChecked[VIDEO_DECODER] != 0) ? MF_ENABLED : MF_GRAYED;
        m.AppendMenu(MF_STRING | state, DISABLE_VIDEO, ResStr(IDS_DISABLE_VIDEO_FILTERS));
    }

    ClientToScreen(&point);
    UINT id = m.TrackPopupMenu(TPM_LEFTBUTTON | TPM_RETURNCMD, point.x, point.y, this);

    if (id == 0) {
        return;
    }

    int index = 0;
    for (int i = 0; i < m_filters.GetCount(); i++) {
        switch (m_filters[i].type) {
            case SOURCE_FILTER:
                if (m_n == 1) {
                    continue;
                }
                break;
            case AUDIO_DECODER:
            case VIDEO_DECODER:
                if (m_n == 0) {
                    continue;
                }
                break;
            default:
                continue;
        }

        switch (id) {
            case ENABLE_ALL:
                SetCheck(index, TRUE);
                break;
            case DISABLE_ALL:
                SetCheck(index, FALSE);
                break;
            case ENABLE_AUDIO:
                if (m_filters[i].type == AUDIO_DECODER) {
                    SetCheck(index, TRUE);
                }
                break;
            case DISABLE_AUDIO:
                if (m_filters[i].type == AUDIO_DECODER) {
                    SetCheck(index, FALSE);
                }
                break;
            case ENABLE_VIDEO:
                if (m_filters[i].type == VIDEO_DECODER) {
                    SetCheck(index, TRUE);
                }
                break;
            case DISABLE_VIDEO:
                if (m_filters[i].type == VIDEO_DECODER) {
                    SetCheck(index, FALSE);
                }
                break;
        }
        index++;
    }

    GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), CLBN_CHKCHANGE), (LPARAM)m_hWnd);
}

// CPPageInternalFilters dialog

IMPLEMENT_DYNAMIC(CPPageInternalFilters, CPPageBase)
CPPageInternalFilters::CPPageInternalFilters()
    : CPPageBase(CPPageInternalFilters::IDD, CPPageInternalFilters::IDD)
    , m_listSrc(0, m_filters)
    , m_listTra(1, m_filters)
{
}

CPPageInternalFilters::~CPPageInternalFilters()
{
}

void CPPageInternalFilters::DoDataExchange(CDataExchange* pDX)
{
    __super::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_listSrc);
    DDX_Control(pDX, IDC_LIST2, m_listTra);
}

BEGIN_MESSAGE_MAP(CPPageInternalFilters, CPPageBase)
    ON_LBN_SELCHANGE(IDC_LIST1, OnSelChange)
    ON_LBN_SELCHANGE(IDC_LIST2, OnSelChange)
    ON_CLBN_CHKCHANGE(IDC_LIST1, OnCheckBoxChange)
    ON_CLBN_CHKCHANGE(IDC_LIST2, OnCheckBoxChange)
    ON_BN_CLICKED(IDC_SPLITTER_CONF, OnBnClickedSplitterConf)
    ON_BN_CLICKED(IDC_VIDEO_DEC_CONF, OnBnClickedVideoDecConf)
    ON_BN_CLICKED(IDC_AUDIO_DEC_CONF, OnBnClickedAudioDecConf)
END_MESSAGE_MAP()

// CPPageInternalFilters message handlers

BOOL CPPageInternalFilters::OnInitDialog()
{
    __super::OnInitDialog();

    const CAppSettings& s = AfxGetAppSettings();

    InitFiltersList();

    for (int i = 0; i < m_filters.GetCount(); i++) {
        CPPageInternalFiltersListBox* l;
        bool checked;

        switch (m_filters[i].type) {
            case SOURCE_FILTER:
                l = &m_listSrc;
                checked = s.SrcFilters[m_filters[i].flag];
                break;
            case AUDIO_DECODER:
            case VIDEO_DECODER:
                l = &m_listTra;
                checked = s.TraFilters[m_filters[i].flag];
                break;
            default:
                l = nullptr;
                checked = false;
        }

        if (l) {
            l->AddFilter(&m_filters[i], checked);
        }
    }

    m_listSrc.UpdateCheckState();
    m_listTra.UpdateCheckState();

    UpdateData(FALSE);

    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPageInternalFilters::InitFiltersList()
{
    bool bLAVSplitterIsAvailable = CFGFilterLAV::CheckVersion(CFGFilterLAV::GetFilterPath(CFGFilterLAV::SPLITTER));
    bool bLAVVideoIsAvailable = CFGFilterLAV::CheckVersion(CFGFilterLAV::GetFilterPath(CFGFilterLAV::VIDEO_DECODER));
    bool bLAVAudioIsAvailable = CFGFilterLAV::CheckVersion(CFGFilterLAV::GetFilterPath(CFGFilterLAV::AUDIO_DECODER));

    GetDlgItem(IDC_SPLITTER_CONF)->EnableWindow(bLAVSplitterIsAvailable);
    GetDlgItem(IDC_VIDEO_DEC_CONF)->EnableWindow(bLAVVideoIsAvailable);
    GetDlgItem(IDC_AUDIO_DEC_CONF)->EnableWindow(bLAVAudioIsAvailable);

    m_filters.RemoveAll();

#if INTERNAL_SOURCEFILTER_AVI
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("AVI"), SOURCE_FILTER, SRC_AVI, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_AVS
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("Avisynth"), SOURCE_FILTER, SRC_AVS, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_CDDA
    m_filters.Add(filter_t(_T("CDDA (Audio CD)"), SOURCE_FILTER, SRC_CDDA, 0));
#endif
#if INTERNAL_SOURCEFILTER_CDXA
    m_filters.Add(filter_t(_T("CDXA (VCD/SVCD/XCD)"), SOURCE_FILTER, SRC_CDXA, 0));
#endif
#if INTERNAL_SOURCEFILTER_DSM
    m_filters.Add(filter_t(_T("DirectShow Media"), SOURCE_FILTER, SRC_DSM, 0));
#endif
#if INTERNAL_SOURCEFILTER_DTSAC3
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("DTS/AC3"), SOURCE_FILTER, SRC_DTSAC3, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_VTS
    m_filters.Add(filter_t(_T("DVD Video Title Set"), SOURCE_FILTER, SRC_VTS, IDS_SRC_VTS));
#endif
#if INTERNAL_SOURCEFILTER_FLIC
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("FLI/FLC"), SOURCE_FILTER, SRC_FLIC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_FLAC
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("FLAC"), SOURCE_FILTER, SRC_FLAC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_FLV
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("FLV"), SOURCE_FILTER, SRC_FLV, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_GIF
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("GIF"), SOURCE_FILTER, SRC_GIF, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_MATROSKA
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("Matroska"), SOURCE_FILTER, SRC_MATROSKA, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_MP4
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("MP4/MOV"), SOURCE_FILTER, SRC_MP4, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_MPEGAUDIO
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("MPEG Audio"), SOURCE_FILTER, SRC_MPA, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_MPEG
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("MPEG PS/TS/PVA"), SOURCE_FILTER, SRC_MPEG, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_ASF
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("WMV/ASF/DVR-MS"), SOURCE_FILTER, SRC_ASF, IDS_INTERNAL_LAVF_WMV));
    }
#endif
#if INTERNAL_SOURCEFILTER_OGG
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("Ogg"), SOURCE_FILTER, SRC_OGG, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_REALMEDIA
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("RealMedia"), SOURCE_FILTER, SRC_REALMEDIA, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_HTTP
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("HTTP"), SOURCE_FILTER, SRC_HTTP, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_RTSP
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("RTSP"), SOURCE_FILTER, SRC_RTSP, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_UDP
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("UDP"), SOURCE_FILTER, SRC_UDP, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_RTP
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("RTP"), SOURCE_FILTER, SRC_RTP, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_MMS
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("MMS"), SOURCE_FILTER, SRC_MMS, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_RTMP
    if (bLAVSplitterIsAvailable) {
        m_filters.Add(filter_t(_T("RTMP"), SOURCE_FILTER, SRC_RTMP, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_SOURCEFILTER_RFS
    m_filters.Add(filter_t(_T("RAR"), SOURCE_FILTER, SRC_RFS, IDS_SRC_RFS));
#endif

#if INTERNAL_DECODER_AAC
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("AAC"), AUDIO_DECODER, TRA_AAC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_AC3
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("AC3/E-AC3/TrueHD/MLP"), AUDIO_DECODER, TRA_AC3, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_DTS
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("DTS"), AUDIO_DECODER, TRA_DTS, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_LPCM
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("LPCM"), AUDIO_DECODER, TRA_LPCM, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_MPEGAUDIO
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("MPEG Audio"), AUDIO_DECODER, TRA_MPA, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_VORBIS
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("Vorbis"), AUDIO_DECODER, TRA_VORBIS, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_FLAC
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("FLAC"), AUDIO_DECODER, TRA_FLAC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_NELLYMOSER
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("Nellymoser"), AUDIO_DECODER, TRA_NELLY, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_ALAC
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("ALAC"), AUDIO_DECODER, TRA_ALAC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_ALS
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("ALS"), AUDIO_DECODER, TRA_ALS, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_AMR
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("AMR"), AUDIO_DECODER, TRA_AMR, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_REALAUDIO
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("RealAudio"), AUDIO_DECODER, TRA_RA, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_PS2AUDIO
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("PS2 Audio (PCM/ADPCM)"), AUDIO_DECODER, TRA_PS2AUD, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_PCM
    if (bLAVAudioIsAvailable) {
        m_filters.Add(filter_t(_T("Other PCM/ADPCM"), AUDIO_DECODER, TRA_PCM, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_MPEG1
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("MPEG-1 Video"), VIDEO_DECODER, TRA_MPEG1, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_MPEG2
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("MPEG-2 Video"), VIDEO_DECODER, TRA_MPEG2, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_REALVIDEO
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("RealVideo"), VIDEO_DECODER, TRA_RV, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_H264
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("H264/AVC"), VIDEO_DECODER, TRA_H264, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_HEVC
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("HEVC"), VIDEO_DECODER, TRA_HEVC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_VC1
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("VC1"), VIDEO_DECODER, TRA_VC1, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_XVID
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("Xvid/MPEG-4"), VIDEO_DECODER, TRA_XVID, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_DIVX
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("DivX"), VIDEO_DECODER, TRA_DIVX, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_MSMPEG4
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("MS MPEG-4"), VIDEO_DECODER, TRA_MSMPEG4, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_FLV
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("FLV1/4"), VIDEO_DECODER, TRA_FLV4, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_VP356
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("VP3/5/6"), VIDEO_DECODER, TRA_VP356, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_VP8
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("VP8"), VIDEO_DECODER, TRA_VP8, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_VP9
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("VP9"), VIDEO_DECODER, TRA_VP9, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_WMV
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("WMV1/2/3"), VIDEO_DECODER, TRA_WMV, IDS_INTERNAL_LAVF_WMV));
    }
#endif
#if INTERNAL_DECODER_SVQ
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("SVQ1/3"), VIDEO_DECODER, TRA_SVQ3, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_H263
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("H263"), VIDEO_DECODER, TRA_H263, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_AMVV
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("AMV video"), VIDEO_DECODER, TRA_AMVV, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_THEORA
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("Theora"), VIDEO_DECODER, TRA_THEORA, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_MJPEG
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("MJPEG"), VIDEO_DECODER, TRA_MJPEG, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_INDEO
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("Indeo 3/4/5"), VIDEO_DECODER, TRA_INDEO, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_SCREEN
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("Screen Capture (TSCC, VMnc)"), VIDEO_DECODER, TRA_SCREEN, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_FLIC
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("FLIC"), VIDEO_DECODER, TRA_FLIC, IDS_INTERNAL_LAVF));
    }
#endif
#if INTERNAL_DECODER_V210_V410
    if (bLAVVideoIsAvailable) {
        m_filters.Add(filter_t(_T("v210/v410"), VIDEO_DECODER, TRA_V210_V410, IDS_INTERNAL_LAVF));
    }
#endif
}

BOOL CPPageInternalFilters::OnApply()
{
    UpdateData();

    CAppSettings& s = AfxGetAppSettings();

    CPPageInternalFiltersListBox* list = &m_listSrc;
    for (int l = 0; l < 2; l++) {
        for (int i = 0; i < list->GetCount(); i++) {
            filter_t* f = (filter_t*) list->GetItemDataPtr(i);

            switch (f->type) {
                case SOURCE_FILTER:
                    s.SrcFilters[f->flag] = !!list->GetCheck(i);
                    break;
                case AUDIO_DECODER:
                case VIDEO_DECODER:
                    s.TraFilters[f->flag] = !!list->GetCheck(i);
                    break;
            }
        }
        list = &m_listTra;
    }

    return __super::OnApply();
}

void CPPageInternalFilters::OnSelChange()
{
    // We only catch the message so that the page is not marked as modified.
}

void CPPageInternalFilters::OnCheckBoxChange()
{
    m_listSrc.UpdateCheckState();
    m_listTra.UpdateCheckState();

    SetModified();
}

void CPPageInternalFilters::OnBnClickedSplitterConf()
{
    CFGFilterLAVSplitter::ShowPropertyPages(this);
}

void CPPageInternalFilters::OnBnClickedVideoDecConf()
{
    CFGFilterLAVVideo::ShowPropertyPages(this);
}

void CPPageInternalFilters::OnBnClickedAudioDecConf()
{
    CFGFilterLAVAudio::ShowPropertyPages(this);
}
