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

#include "stdafx.h"
#include "DSUtil.h"
#include "CoverArt.h"
#include "DSMPropertyBag.h"

CString CoverArt::FindExternal(const CString& path, const CString& author)
{
    if (!path.IsEmpty()) {
        CAtlList<CString> files;
        FindFiles(path + _T("\\*front*.png"), files);
        FindFiles(path + _T("\\*front*.jp*g"), files);
        FindFiles(path + _T("\\*front*.bmp"), files);
        FindFiles(path + _T("\\*cover*.png"), files);
        FindFiles(path + _T("\\*cover*.jp*g"), files);
        FindFiles(path + _T("\\*cover*.bmp"), files);
        FindFiles(path + _T("\\*folder*.png"), files);
        FindFiles(path + _T("\\*folder*.jp*g"), files);
        FindFiles(path + _T("\\*folder*.bmp"), files);
        if (!author.IsEmpty()) {
            FindFiles(path + _T("\\*") + author + _T("*.png"), files);
            FindFiles(path + _T("\\*") + author + _T("*.jp*g"), files);
            FindFiles(path + _T("\\*") + author + _T("*.bmp"), files);
        }

        if (!files.IsEmpty()) {
            return files.GetHead();
        }
    }
    return _T("");
}

bool CoverArt::FindEmbedded(CComPtr<IFilterGraph> pFilterGraph, std::vector<BYTE>& internalCover)
{
    internalCover.clear();

    bool bGoodMatch = false;
    BeginEnumFilters(pFilterGraph, pEF, pBF) {
        if (CComQIPtr<IDSMResourceBag> pRB = pBF) {
            for (DWORD j = 0; j < pRB->ResGetCount(); j++) {
                CComBSTR name, desc, mime;
                BYTE* pData = nullptr;
                DWORD len = 0;

                if (SUCCEEDED(pRB->ResGet(j, &name, &desc, &mime, &pData, &len, nullptr)) && len > 0) {
                    if (name == _T("EmbeddedCover.jpg")) { // Embedded cover as exported by our custom internal LAV Splitter
                        internalCover.assign(pData, pData + len);
                        break; // Always preferred
                    } else if (!bGoodMatch && CString(mime).MakeLower().Find(_T("image")) != -1) {
                        // Check if we have a good match
                        CString nameLower = CString(name).MakeLower();
                        bGoodMatch = nameLower.Find(_T("cover")) || nameLower.Find(_T("front"));
                        // Override the previous match only if we think this one is better
                        if (bGoodMatch || internalCover.empty()) {
                            internalCover.assign(pData, pData + len);
                        }
                        // Keep looking for a better match
                    }
                    CoTaskMemFree(pData);
                }
            }
        }
    }
    EndEnumFilters;

    return !internalCover.empty();
}
