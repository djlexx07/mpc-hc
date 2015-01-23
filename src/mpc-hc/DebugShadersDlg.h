/*
 * (C) 2013-2014 see Authors.txt
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

#include "ResizableLib/ResizableDialog.h"

#include "EventDispatcher.h"
#include "PixelShaderCompiler.h"
#include "Shaders.h"
#include "TimerWrappers.h"

class CModelessDialog : public CResizableDialog
{
public:
    CModelessDialog(UINT nIDTemplate);
    BOOL DestroyWindow();

private:
    virtual void OnCancel();
    virtual void OnOK();
};

class CDebugShadersDlg : public CModelessDialog, public FileChangeNotifier
{
public:
    CDebugShadersDlg();
    BOOL DestroyWindow();

private:
    enum { WM_APP_RECOMPILE_SHADER = WM_APP + 100 };

    enum class TimerOneTimeSubscriber {
        SELECTED_SHADER_CHANGE_COOLDOWN,
    };
    OneTimeTimerPool<TimerOneTimeSubscriber> m_timerOneTime;

    enum { IDD = IDD_DEBUGSHADERS_DLG };
    enum { ps_2_0 = 0, ps_2_b, ps_2_a, ps_3_0 };
    enum {
        TIMER_ONETIME_START = 1,
        TIMER_ONETIME_END = TIMER_ONETIME_START + 16,
    };

protected:
    int m_iVersion;
    CComboBox m_Shaders;
    CEdit m_DebugInfo;
    CPixelShaderCompiler m_Compiler;

    EventClient m_eventc;
    void EventCallback(MpcEvent ev);

    virtual FileSet GetWatchedList() override;
    virtual void WatchedFilesChanged(const FileSet& changes) override;

    void ShaderFileChangedCooldownCallback();
    void OnListRefresh();

    virtual void DoDataExchange(CDataExchange* pDX) override;
    void OnTimer(UINT_PTR nIDEvent);

    void OnRecompileShader();
    void OnSelChange();
    void OnVersionClicked();

    DECLARE_MESSAGE_MAP()
};
