/*
* (C) 2018 Nicholas Parkanyi
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
#include "stdafx.h"

struct  CUtf16JSON;

class CYoutubeDLInstance
{
public:
    CYoutubeDLInstance();
    ~CYoutubeDLInstance();

    bool Run(CString url);
    bool GetHttpStreams(CAtlList<CString>& videos, CAtlList<CString>& audio, CAtlList<CString>& names);

private:
    CUtf16JSON* pJSON;
    bool bIsPlaylist;
    HANDLE hStdout_r, hStdout_w;
    HANDLE hStderr_r, hStderr_w;
    char* buf_out;
    char* buf_err;
    DWORD idx_out;
    DWORD idx_err;
    DWORD capacity_out, capacity_err;

    bool loadJSON();
    static DWORD WINAPI BuffOutThread(void* ydl_inst);
    static DWORD WINAPI BuffErrThread(void* ydl_inst);
};
