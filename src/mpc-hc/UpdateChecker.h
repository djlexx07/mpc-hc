/*
 * (C) 2012-2013 see Authors.txt
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

#include <afxwin.h>

struct Version {
    UINT major;
    UINT minor;
    UINT patch;
    UINT revision;

    CString ToString() const {
        CString versionStr;

        versionStr.Format(_T("%u.%u.%u"), major, minor, patch);
        if (revision) {
            versionStr.AppendFormat(_T(".%u"), revision);
        }

        return versionStr;
    }
};

enum Update_Status {
    UPDATER_ERROR = -1,
    UPDATER_LATEST_STABLE,
    UPDATER_NEWER_VERSION,
    UPDATER_UPDATE_AVAILABLE,
    UPDATER_UPDATE_AVAILABLE_IGNORED
};

enum AutoUpdate_Status {
    AUTOUPDATE_UNKNOWN = -1,
    AUTOUPDATE_DISABLE,
    AUTOUPDATE_ENABLE
};

class UpdateChecker
{
public:
    static const Version MPC_HC_VERSION;
    static const LPCTSTR MPC_HC_UPDATE_URL;

    UpdateChecker(CString versionFileURL);
    ~UpdateChecker();

    Update_Status IsUpdateAvailable(const Version& currentVersion);
    Update_Status IsUpdateAvailable();
    const Version& GetLatestVersion() const { return latestVersion; };
    void IgnoreLatestVersion();

    static bool IsAutoUpdateEnabled();
    static bool IsTimeToAutoUpdate();
    static void CheckForUpdate(bool autoCheck = false);
    static bool IsRunningInstalled();

private:
    static bool bIsCheckingForUpdate;
    static CCritSec csIsCheckingForUpdate;

    CString versionFileURL;
    Version latestVersion;

    static bool ParseVersion(const CString& versionStr, Version& version);
    static int CompareVersion(const Version& v1, const Version& v2);

    friend static UINT RunCheckForUpdateThread(LPVOID pParam);
};
