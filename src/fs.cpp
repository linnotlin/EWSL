#include "fs.h"

#include <objbase.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>

namespace wslterm {

#ifndef FOS_PICKFOLDERS
#define FOS_PICKFOLDERS 0x00000020
#endif
#ifndef FOS_FORCEFILESYSTEM
#define FOS_FORCEFILESYSTEM 0x00000040
#endif
#ifndef FOS_PATHMUSTEXIST
#define FOS_PATHMUSTEXIST 0x00000800
#endif
#ifndef SIGDN_FILESYSPATH
#define SIGDN_FILESYSPATH ((SIGDN)0x80058000)
#endif

static const GUID kClsidFileOpenDialog =
    { 0xDC1C5A9C, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 } };
static const GUID kIidFileOpenDialog =
    { 0x42F85136, 0xDB7E, 0x439C, { 0x85, 0xF1, 0xE4, 0x07, 0x5D, 0x13, 0x5F, 0xC8 } };
static inline bool isSep(wchar_t c) {
    return c == L'\\' || c == L'/';
}

std::wstring pathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;

    if (b.size() >= 2 && b[1] == L':') return b;
    if (isSep(b[0])) return b;

    std::wstring r = a;
    if (!isSep(r[r.size() - 1])) r.push_back(L'\\');
    r += b;
    return r;
}

std::wstring pathNormalize(const std::wstring& p) {
    std::wstring s;
    s.reserve(p.size());
    for (size_t i = 0; i < p.size(); ++i) {
        s.push_back(p[i] == L'/' ? L'\\' : p[i]);
    }
    while (s.size() > 1 && isSep(s[s.size() - 1])) {
        if (s.size() == 3 && s[1] == L':') break;
        s.erase(s.size() - 1);
    }
    return s;
}

std::wstring pathParent(const std::wstring& raw) {
    std::wstring s = pathNormalize(raw);
    if (s.empty()) return s;

    size_t i = s.find_last_of(L'\\');
    if (i == std::wstring::npos) return std::wstring();

    if (i == 2 && s.size() > 2 && s[1] == L':') return s.substr(0, 3);
    if (i == 0) return std::wstring();
    return s.substr(0, i);
}

std::wstring pathFileName(const std::wstring& raw) {
    std::wstring s = pathNormalize(raw);
    size_t i = s.find_last_of(L'\\');
    if (i == std::wstring::npos) return s;
    return s.substr(i + 1);
}

std::wstring pathExt(const std::wstring& raw) {
    std::wstring name = pathFileName(raw);
    size_t i = name.find_last_of(L'.');
    if (i == std::wstring::npos || i + 1 >= name.size()) return std::wstring();

    std::wstring e = name.substr(i + 1);
    for (size_t k = 0; k < e.size(); ++k) {
        wchar_t c = e[k];
        if (c >= L'A' && c <= L'Z') e[k] = (wchar_t)(c - L'A' + L'a');
    }
    return e;
}

bool pathIsDir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool pathExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<DirEntry> listDirectory(const std::wstring& raw) {
    std::vector<DirEntry> out;
    if (raw.empty()) return out;

    std::wstring dir = pathNormalize(raw);
    if (dir.empty()) return out;

    std::wstring pattern = dir;
    if (!isSep(pattern[pattern.size() - 1])) pattern.push_back(L'\\');
    pattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;

    do {
        const wchar_t* n = fd.cFileName;
        if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;

        DirEntry e;
        e.name      = n;
        e.path      = dir + L"\\" + n;
        e.isDir     = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.isSymlink = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (e.isSymlink && e.isDir) {
            if (GetFileAttributesW(e.path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        }

        out.push_back(e);
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return lstrcmpiW(a.name.c_str(), b.name.c_str()) < 0;
    });

    return out;
}

std::vector<std::wstring> listDrives() {
    std::vector<std::wstring> out;

    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[4];
        root[0] = (wchar_t)(L'A' + i);
        root[1] = L':';
        root[2] = L'\\';
        root[3] = 0;

        UINT t = GetDriveTypeW(root);
        if (t == DRIVE_UNKNOWN || t == DRIVE_NO_ROOT_DIR) continue;
        out.push_back(root);
    }
    return out;
}

std::wstring homeDirectory() {
    WCHAR buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH)) return std::wstring(buf);
    if (GetEnvironmentVariableW(L"HOMEDRIVE", buf, MAX_PATH)) {
        std::wstring d = buf;
        if (GetEnvironmentVariableW(L"HOMEPATH", buf, MAX_PATH)) return d + buf;
    }
    return std::wstring();
}

static bool decodeUtf8Strict(const char* p, int n, std::wstring& out) {
    if (n <= 0) {
        out.clear();
        return true;
    }

    int w = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, NULL, 0);
    if (w <= 0) return false;

    out.assign((size_t)w, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, &out[0], w);
    return true;
}

bool readTextFile(const std::wstring& path, std::wstring& out, bool& wasUtf16) {
    out.clear();
    wasUtf16 = false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > (LONGLONG)(64 * 1024 * 1024)) {
        CloseHandle(h);
        return false;
    }

    std::string raw((size_t)sz.QuadPart, '\0');
    size_t total = 0;
    while (total < raw.size()) {
        DWORD got = 0;
        if (!ReadFile(h, &raw[total], (DWORD)(raw.size() - total), &got, NULL) || got == 0) break;
        total += (size_t)got;
    }
    raw.resize(total);
    CloseHandle(h);

    const unsigned char* b = (const unsigned char*)raw.data();

    if (raw.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        wasUtf16 = true;
        size_t units = (raw.size() - 2) / 2;
        out.assign(units, L'\0');
        if (units) memcpy(&out[0], raw.data() + 2, units * 2);
        return true;
    }

    if (raw.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        wasUtf16 = true;
        size_t units = (raw.size() - 2) / 2;
        out.assign(units, L'\0');
        for (size_t i = 0; i < units; ++i) {
            out[i] = (wchar_t)(((unsigned)(unsigned char)raw[2 + i * 2] << 8) |
                               (unsigned char)raw[3 + i * 2]);
        }
        return true;
    }

    if (raw.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
        return decodeUtf8Strict(raw.data() + 3, (int)(raw.size() - 3), out);
    }

    if (decodeUtf8Strict(raw.data(), (int)raw.size(), out)) return true;

    int w = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), NULL, 0);
    if (w <= 0) {
        out.clear();
        return true;
    }
    out.assign((size_t)w, L'\0');
    MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), &out[0], w);
    return true;
}

bool writeTextFile(const std::wstring& path, const std::wstring& text, bool asUtf16) {
    std::string bytes;

    if (asUtf16) {
        bytes.resize(2 + text.size() * 2);
        bytes[0] = (char)0xFF;
        bytes[1] = (char)0xFE;
        if (!text.empty()) memcpy(&bytes[2], text.data(), text.size() * 2);
    } else {
        if (!text.empty()) {
            int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
            if (n <= 0) return false;
            bytes.resize((size_t)n);
            WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &bytes[0], n, NULL, NULL);
        }
    }

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    size_t total = 0;
    bool ok = true;
    while (total < bytes.size()) {
        DWORD w = 0;
        if (!WriteFile(h, bytes.data() + total, (DWORD)(bytes.size() - total), &w, NULL) || w == 0) {
            ok = false;
            break;
        }
        total += (size_t)w;
    }

    CloseHandle(h);
    return ok;
}

bool pickFolder(HWND owner, std::wstring& out) {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool comInited = SUCCEEDED(hrCom);

    void* pv = NULL;
    HRESULT hr = CoCreateInstance(kClsidFileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  kIidFileOpenDialog, &pv);

    bool got = false;
    if (SUCCEEDED(hr) && pv) {
        IFileOpenDialog* dlg = (IFileOpenDialog*)pv;

        DWORD opts = 0;
        if (SUCCEEDED(dlg->GetOptions(&opts))) {
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }

        if (SUCCEEDED(dlg->Show(owner))) {
            void* pvItem = NULL;
            if (SUCCEEDED(dlg->GetResult((IShellItem**)&pvItem)) && pvItem) {
                IShellItem* item = (IShellItem*)pvItem;
                PWSTR psz = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    out = psz;
                    got = true;
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        dlg->Release();
    }

    if (comInited) CoUninitialize();
    return got;
}

}
