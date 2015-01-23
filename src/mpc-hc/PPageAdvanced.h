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

#include "AppSettings.h"
#include "PPageBase.h"
#include "resource.h"
#include <afxwin.h>
#include <afxcmn.h>
#include <utility>
#include <memory>
#include <map>
#include <deque>

class SettingsBase
{
    CString name;
    CString toolTipText;

public:
    SettingsBase(CString name, CString toolTipText)
        : name(name)
        , toolTipText(toolTipText) {
    }

    CString GetToolTipText() const { return toolTipText; }
    CString GetName() const { return name; }
    virtual bool IsDefault() const PURE;
    virtual void ResetDefault() PURE;
    virtual void Apply() PURE;
};

class SettingsBool : public SettingsBase
{
    const bool defaultValue;
    bool currentValue;
    bool& settingReference;

public:
    SettingsBool(CString name, bool defaultValue, bool& settingReference, CString toolTipText)
        : SettingsBase(name, toolTipText)
        , defaultValue(defaultValue)
        , currentValue(settingReference)
        , settingReference(settingReference) {
    }

    bool IsDefault() const { return currentValue == defaultValue; }
    void ResetDefault() { SetValue(defaultValue); }
    void SetValue(bool value) { currentValue = value; }
    bool GetValue() const { return currentValue; }
    void Apply() { settingReference = currentValue; }
    void Toggle() { currentValue = !currentValue; }
};

class SettingsInt : public SettingsBase
{
    const int defaultValue;
    int currentValue;
    int& settingReference;
    std::pair<int, int> range;

public:
    SettingsInt(CString name, int defaultValue, int& settingReference, std::pair<int, int> range, CString toolTipText)
        : SettingsBase(name, toolTipText)
        , defaultValue(defaultValue)
        , currentValue(settingReference)
        , settingReference(settingReference)
        , range(range) {
    }

    bool IsDefault() const { return currentValue == defaultValue; }
    void ResetDefault() { SetValue(defaultValue); }
    void SetValue(int value) { currentValue = value; }
    int GetValue() const { return currentValue; }
    void Apply() { settingReference = currentValue; }
    std::pair<int, int> GetRange() const { return range; }
};

class SettingsCombo : public SettingsInt
{
    std::deque<CString> list;

public:
    SettingsCombo(CString name, int defaultValue, int& settingReference, std::deque<CString> list, CString toolTipText)
        : SettingsInt(name, defaultValue, settingReference, std::make_pair(0, (int)list.size() - 1), toolTipText)
        , list(list) {
    }

    std::deque<CString> GetList() const { return list; }
};

class SettingsCString : public SettingsBase
{
    const CString defaultValue;
    CString currentValue;
    CString& settingReference;

public:
    SettingsCString(CString name, CString defaultValue, CString& settingReference, CString toolTipText)
        : SettingsBase(name, toolTipText)
        , defaultValue(defaultValue)
        , currentValue(settingReference)
        , settingReference(settingReference) {
    }

    bool IsDefault() const { return currentValue == defaultValue; }
    void ResetDefault() { SetValue(defaultValue); }
    void SetValue(const CString& value) { currentValue = value; }
    CString GetValue() const { return currentValue; }
    void Apply() { settingReference = currentValue; }
};

class CPPageAdvanced : public CPPageBase
{
public:
    CPPageAdvanced();
    virtual ~CPPageAdvanced() = default;

private:
    enum { IDD = IDD_PPAGEADVANCED };

    enum ADVANCED_SETTINGS {
        HIDE_WINDOWED,
        BLOCK_VSFILTER,
        RECENT_FILES_NB,
        FILE_POS_LONGER,
        FILE_POS_AUDIO,
        COVER_SIZE_LIMIT
    };

    enum {
        COL_NAME,
        COL_VALUE
    };

    CFont m_fontBold;
    CComboBox m_comboBox;

    std::map<ADVANCED_SETTINGS, std::shared_ptr<SettingsBase>> m_hiddenOptions;

    int m_lastSelectedItem = -1;

    CString m_strTrue;
    CString m_strFalse;

    void InitSettings();
    bool IsDefault(ADVANCED_SETTINGS) const;
    inline const int GetListSelectionMark() const {
        const int iItem = m_list.GetSelectionMark();
        if (iItem != m_lastSelectedItem) {
            return -1;
        }
        return iItem;
    };

protected:
    CListCtrl m_list;

    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;
    virtual BOOL OnApply() override;

    afx_msg void OnBnClickedSetButton();
    afx_msg void OnBnClickedDefaultButton();
    afx_msg void OnUpdateSetButton(CCmdUI* pCmdUI);
    afx_msg void OnUpdateDefaultButton(CCmdUI* pCmdUI);
    afx_msg void OnNMDblclk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLvnGetInfoTipList(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLvnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnBnClickedRadio1();
    afx_msg void OnBnClickedRadio2();
    afx_msg void OnCbnSelchangeCombobox();
    afx_msg void OnEnChangeEdit();

    DECLARE_MESSAGE_MAP()
};

