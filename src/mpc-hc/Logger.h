/*
 * (C) 2015-2017 see Authors.txt
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

#include "PathUtils.h"
#include "mplayerc.h"

enum class LogTargets {
    BDA,
    SUBTITLES
};

namespace
{
    template<LogTargets TARGET>
    constexpr LPCTSTR GetFileName();

    template<>
    constexpr LPCTSTR GetFileName<LogTargets::BDA>()
    {
        return _T("bda.log");
    }

    template<>
    constexpr LPCTSTR GetFileName<LogTargets::SUBTITLES>()
    {
        return _T("subtitles.log");
    }

    void WriteToFile(FILE* f, LPCSTR function, LPCSTR file, int line, _In_z_ _Printf_format_string_ LPCTSTR fmt, va_list& args)
    {
        SYSTEMTIME local_time;
        GetLocalTime(&local_time);

        _ftprintf_s(f, _T("%.2hu:%.2hu:%.2hu.%.3hu - %S: "), local_time.wHour, local_time.wMinute,
                    local_time.wSecond, local_time.wMilliseconds, function);
        _vftprintf_s(f, fmt, args);
        _ftprintf_s(f, _T(" (%S:%d)\n"), file, line);
    }
}

template<LogTargets TARGET>
struct Logger final {
    static void Log(LPCSTR function, LPCSTR file, int line, LPCTSTR fmt...) {
        static Logger logger;

        if (!logger.m_file) {
            return;
        }

        va_list args;
        va_start(args, fmt);
        WriteToFile(logger.m_file, function, file, line, fmt, args);
        va_end(args);
    }

private:
    Logger() {
        const auto& s = AfxGetAppSettings();
        // Check if logging is enabled only during initialization to avoid incomplete logs
        ASSERT(s.IsInitialized());
        CString savePath;
        if (s.bEnableLogging && AfxGetMyApp()->GetAppSavePath(savePath)) {
            if (!PathUtils::Exists(savePath)) {
                ::CreateDirectory(savePath, nullptr);
            }
            m_file = _tfsopen(PathUtils::CombinePaths(savePath, GetFileName<TARGET>()), _T("at"), SH_DENYWR);
        } else {
            m_file = nullptr;
        }
        ASSERT(!s.bEnableLogging || m_file);
    }

    ~Logger() {
        if (m_file) {
            fclose(m_file);
        }
    }

    FILE* m_file;
};


#define MPCHC_LOG(TARGET, fmt, ...) Logger<LogTargets::TARGET>::Log(__FUNCTION__, __FILE__, __LINE__, fmt, __VA_ARGS__)
#define BDA_LOG(...) MPCHC_LOG(BDA, __VA_ARGS__)
#define SUBTITLES_LOG(...) MPCHC_LOG(SUBTITLES, __VA_ARGS__)
