/*
* (C) 2016 see Authors.txt
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
#include "PixelShaderCache.h"
#include "RenderersSettings.h"

CPixelShaderCache::CPixelShaderCache(IDirect3DDevice9* pD3DDev)
    : m_pD3DDev(pD3DDev)
{
    if (IsEnabled()) {
        LoadCache();
    } else {
        DestroyCache();
    }
}

HRESULT CPixelShaderCache::CreatePixelShader(
    LPCSTR pProfile,
    LPCSTR pSourceData,
    SIZE_T SourceDataSize,
    IDirect3DPixelShader9** ppPixelShader)
{
    if (!IsEnabled()) {
        return E_FAIL;
    }

    uint64_t hash = Hash(pProfile, pSourceData, SourceDataSize);
    auto pixelShader = m_PixelShaders.PLookup(hash);

    if (pixelShader) {
        HRESULT res = m_pD3DDev->CreatePixelShader(pixelShader->value.m_p, ppPixelShader);

        if (FAILED(res)) {
            DeleteCache(hash);
        } else {
            CString file_path;
            if (GetCacheFilePath(file_path, hash)) {
                TouchFile(file_path);
            }
        }
        return res;
    }

    return E_FAIL;
}

void CPixelShaderCache::SavePixelShader(
    LPCSTR pProfile,
    LPCSTR pSourceData,
    SIZE_T SourceDataSize,
    void* pCompiledData,
    SIZE_T CompiledDataSize)
{
    if (!IsEnabled()) {
        return;
    }

    uint64_t hash = Hash(pProfile, pSourceData, SourceDataSize);

    CAutoVectorPtr<DWORD> buffer;
    if (buffer.Allocate(CompiledDataSize)) {
        memcpy(buffer.m_p, pCompiledData, CompiledDataSize);
        m_PixelShaders.SetAt(hash, buffer);
    }

    CString cacheFilePath;
    if (GetCacheFilePath(cacheFilePath, hash)) {
        CFile file;
        if (file.Open(cacheFilePath, CFile::modeWrite | CFile::typeBinary)) {
            file.SetLength(0);
        } else if (!file.Open(cacheFilePath, CFile::modeWrite | CFile::typeBinary | CFile::modeCreate)) {
            return;
        }

        file.Write(&m_Version, sizeof(m_Version));
        file.Write(&CompiledDataSize, sizeof(CompiledDataSize));
        file.Write(pCompiledData, CompiledDataSize);

        bool res = file.GetLength() == (sizeof(m_Version) + sizeof(CompiledDataSize) + CompiledDataSize);

        file.Close();

        if (!res) {
            CFile::Remove(cacheFilePath);
        }
    }
}

void CPixelShaderCache::LoadCache()
{
    CString cacheFolder;
    if (GetCacheFolder(cacheFolder)) {
        CFileFind finder;
        BOOL working = finder.FindFile(cacheFolder + _T("\\*"));

        while (working) {
            working = finder.FindNextFile();
            if (!finder.IsDirectory() && !finder.IsDots()) {
                if (!LoadCache(finder.GetFileName(), finder.GetFilePath())) {
                    CFile::Remove(finder.GetFilePath());
                }
            }
        }
    }
}

void CPixelShaderCache::DestroyCache()
{
    CString cacheFolder;
    if (GetCacheFolder(cacheFolder)) {
        CFileFind finder;
        BOOL working = finder.FindFile(cacheFolder + _T("\\*"));

        while (working) {
            working = finder.FindNextFile();
            if (!finder.IsDirectory() && !finder.IsDots()) {
                CFile::Remove(finder.GetFilePath());
            }
        }
    }
}

bool CPixelShaderCache::IsEnabled() const
{
    return GetRenderersSettings().m_AdvRendSets.bCacheShaders;
}

void CPixelShaderCache::TouchFile(const CString& FileName) const
{
    CFileStatus status;
    if (CFile::GetStatus(FileName, status)) {
        status.m_mtime = CTime::GetCurrentTime();
        CFile::SetStatus(FileName, status);
    }
}

bool CPixelShaderCache::IsFileOutdated(const CString& FileName) const
{
    CFileStatus status;
    if (CFile::GetStatus(FileName, status)) {
        CTimeSpan timespan(m_CachedDaysLimit, 0, 0, 0);
        return CTime::GetCurrentTime() - status.m_mtime > timespan;
    }

    return true;
}

bool CPixelShaderCache::LoadCache(const CString& FileName, const CString& FilePath)
{
    if (IsFileOutdated(FilePath)) {
        return false;
    }

    CPath path(FileName);
    path.RemoveExtension();
    uint64_t hash = _tcstoui64(path, NULL, 10);
    CFile file;
    if (file.Open(FilePath, CFile::modeRead | CFile::typeBinary)) {
        unsigned int version;
        bool ret = false;

        if ((file.Read(&version, sizeof(version)) == sizeof(version)) && (version == m_Version)) {
            SIZE_T size;
            if (file.Read(&size, sizeof(size)) == sizeof(size)) {
                CAutoVectorPtr<DWORD> buffer;
                if (buffer.Allocate(size)) {
                    ret = (file.Read(buffer.m_p, size) == size);

                    m_PixelShaders.SetAt(hash, buffer);
                }
            }
        }

        file.Close();

        return ret;
    }
    return false;
}

void CPixelShaderCache::DeleteCache(uint64_t Hash)
{
    m_PixelShaders.RemoveKey(Hash);

    CString cacheFilePath;
    if (GetCacheFilePath(cacheFilePath, Hash)) {
        CFile::Remove(cacheFilePath);
    }
}

bool CPixelShaderCache::GetCacheFilePath(CString& CacheFilePath, uint64_t Hash) const
{
    CString cacheFolder;
    if (GetCacheFolder(cacheFolder)) {
        CacheFilePath.Format(_T("\\%020llu"), Hash);
        CacheFilePath = cacheFolder + CacheFilePath + m_Extension;
        return true;
    }
    return false;
}

bool CPixelShaderCache::GetCacheFolder(CString& CacheFolder) const
{
    CacheFolder = GetRenderersSettings().m_AdvRendSets.sShaderCachePath;

    return CacheFolder.GetLength() > 0 && (PathFileExists(CacheFolder) || CreateDirectory(CacheFolder, nullptr));
}

uint64_t CPixelShaderCache::Hash(LPCSTR pProfile, LPCSTR pSourceData, SIZE_T SourceDataSize) const
{
    return Hash(pProfile) + Hash(pSourceData) + (uint64_t)SourceDataSize;
}

uint64_t CPixelShaderCache::Hash(LPCSTR pStr) const
{
    uint64_t hash = 3074457345618258791ull;
    LPCSTR p = pStr;

    while (p && *p != 0) {
        hash = (hash + *p) * 3074457345618258799ull;
        ++p;
    }
    return hash;
}
