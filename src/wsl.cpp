#include "wsl.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstring>

namespace wslterm {

static bool fileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool realExe(const std::wstring& p) {
    if (!fileExistsW(p)) return false;

    WIN32_FILE_ATTRIBUTE_DATA fd;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fd)) return false;

    return fd.nFileSizeHigh == 0 && fd.nFileSizeLow >= 8192;
}

std::wstring findWslExe() {
    std::vector<std::wstring> real;
    std::vector<std::wstring> alias;
    WCHAR buf[MAX_PATH];

    if (GetEnvironmentVariableW(L"ProgramW6432", buf, MAX_PATH)) {
        real.push_back(std::wstring(buf) + L"\\WSL\\wsl.exe");
    }
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH)) {
        real.push_back(std::wstring(buf) + L"\\WSL\\wsl.exe");
    }

    WCHAR sysDir[MAX_PATH];
    sysDir[0] = 0;
    GetSystemDirectoryW(sysDir, MAX_PATH);
    real.push_back(std::wstring(sysDir) + L"\\wsl.exe");

    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) {
        alias.push_back(std::wstring(buf) + L"\\Microsoft\\WindowsApps\\wsl.exe");
    }

    for (size_t i = 0; i < real.size(); ++i) {
        if (realExe(real[i])) return real[i];
    }
    for (size_t i = 0; i < alias.size(); ++i) {
        if (fileExistsW(alias[i])) return alias[i];
    }
    return std::wstring();
}

bool wslAvailable() {
    return !findWslExe().empty();
}

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring decodeOutput(const std::string& raw) {
    if (raw.empty()) return std::wstring();

    size_t off = 0;
    bool utf16 = false;

    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        utf16 = true;
        off = 2;
    } else {
        size_t pairs = 0;
        size_t zeros = 0;
        for (size_t i = 1; i < raw.size(); i += 2) {
            ++pairs;
            if (raw[i] == 0) ++zeros;
        }
        if (pairs >= 4 && zeros * 10 >= pairs * 8) utf16 = true;
    }

    if (!utf16) return utf8ToWide(raw);

    size_t count = (raw.size() - off) / 2;
    std::wstring w((size_t)count, L'\0');
    memcpy(&w[0], raw.data() + off, count * sizeof(wchar_t));
    return w;
}

struct ReadCtx {
    HANDLE      h;
    std::string data;
};

static DWORD WINAPI readAll(LPVOID p) {
    ReadCtx* c = (ReadCtx*)p;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(c->h, buf, (DWORD)sizeof(buf), &n, NULL) && n > 0) {
        c->data.append(buf, (size_t)n);
    }
    return 0;
}

std::wstring runCapture(const std::wstring& cmdline, unsigned timeoutMs) {
    if (cmdline.empty()) return std::wstring();

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE outRead = NULL;
    HANDLE outWrite = NULL;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) return std::wstring();
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.hStdInput = NULL;

    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(0);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(NULL, buf.data(), NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(outWrite);

    if (!ok) {
        CloseHandle(outRead);
        return std::wstring();
    }

    ReadCtx* ctx = new ReadCtx();
    ctx->h = outRead;

    HANDLE th = CreateThread(NULL, 0, readAll, ctx, 0, NULL);

    DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (w == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);

    if (th) {
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }

    std::wstring out = decodeOutput(ctx->data);

    delete ctx;
    CloseHandle(outRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return out;
}

static std::wstring trimW(const std::wstring& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e) {
        wchar_t c = s[b];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xFEFF) ++b;
        else break;
    }
    while (e > b) {
        wchar_t c = s[e - 1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xFEFF) --e;
        else break;
    }
    return s.substr(b, e - b);
}

static std::vector<std::wstring> splitLines(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == 0) continue;
        if (c == L'\r') continue;
        if (c == L'\n') {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<std::wstring> listInstalledDistros() {
    std::vector<std::wstring> out;

    std::wstring exe = findWslExe();
    if (exe.empty()) return out;

    std::wstring res = runCapture(L"\"" + exe + L"\" -l -q", 12000);
    std::vector<std::wstring> lines = splitLines(res);

    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring t = trimW(lines[i]);
        if (t.empty()) continue;
        out.push_back(t);
    }
    return out;
}

std::wstring defaultDistro() {
    std::wstring exe = findWslExe();
    if (exe.empty()) return std::wstring();

    std::wstring res = runCapture(L"\"" + exe + L"\" -l -v", 12000);
    std::vector<std::wstring> lines = splitLines(res);

    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring t = trimW(lines[i]);
        if (t.empty() || t[0] != L'*') continue;

        size_t p = 1;
        while (p < t.size() && (t[p] == L' ' || t[p] == L'\t')) ++p;
        size_t q = p;
        while (q < t.size() && t[q] != L' ' && t[q] != L'\t') ++q;
        if (q > p) return t.substr(p, q - p);
    }

    std::vector<std::wstring> inst = listInstalledDistros();
    if (!inst.empty()) return inst[0];
    return std::wstring();
}

static bool dirExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring regStr(HKEY key, const wchar_t* name) {
    WCHAR buf[1024];
    DWORD cb = sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)buf, &cb) != ERROR_SUCCESS)
        return std::wstring();
    if (type != REG_SZ && type != REG_EXPAND_SZ) return std::wstring();
    buf[(cb / sizeof(WCHAR)) < 1023 ? (cb / sizeof(WCHAR)) : 1023] = 0;
    std::wstring s(buf);
    if (type == REG_EXPAND_SZ) {
        WCHAR ex[2048];
        DWORD n = ExpandEnvironmentStringsW(s.c_str(), ex, 2048);
        if (n > 0 && n <= 2048) s = ex;
    }
    return s;
}

int distroRootfsState(const std::wstring& name) {
    if (name.empty()) return ROOTFS_MISSING;

    HKEY lxss = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss",
                      0, KEY_READ, &lxss) != ERROR_SUCCESS) {
        return ROOTFS_MISSING;
    }

    int state = ROOTFS_MISSING;
    for (DWORD idx = 0; ; ++idx) {
        WCHAR sub[512];
        DWORD subLen = 511;
        if (RegEnumKeyExW(lxss, idx, sub, &subLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        HKEY k = NULL;
        if (RegOpenKeyExW(lxss, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;

        std::wstring dn = regStr(k, L"DistributionName");
        std::wstring bp = regStr(k, L"BasePath");
        RegCloseKey(k);

        if (dn.empty() || lstrcmpiW(dn.c_str(), name.c_str()) != 0) continue;

        state = ROOTFS_BROKEN;

        if (bp.empty()) break;
        if (!dirExistsW(bp)) break;
        if (fileExistsW(bp + L"\\ext4.vhdx") || fileExistsW(bp + L"\\rootfs")) {
            state = ROOTFS_OK;
            break;
        }

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((bp + L"\\*.vhdx").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
            state = ROOTFS_OK;
        }
        break;
    }

    RegCloseKey(lxss);
    return state;
}

static bool looksLikeDistroId(const std::wstring& t) {
    if (t.size() < 2 || t.size() > 64) return false;
    bool hasAlpha = false;
    for (size_t i = 0; i < t.size(); ++i) {
        wchar_t c = t[i];
        bool alpha = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
        bool digit = (c >= L'0' && c <= L'9');
        if (!alpha && !digit && c != L'-' && c != L'_' && c != L'.') return false;
        if (alpha) hasAlpha = true;
    }
    return hasAlpha;
}

struct BuiltinDistro {
    const wchar_t* id;
    const wchar_t* label;
};

static const BuiltinDistro kBuiltin[] = {
    { L"Ubuntu",                      L"Ubuntu" },
    { L"Ubuntu-26.04",                L"Ubuntu 26.04 LTS" },
    { L"Ubuntu-24.04",                L"Ubuntu 24.04 LTS" },
    { L"Ubuntu-22.04",                L"Ubuntu 22.04 LTS" },
    { L"Ubuntu-20.04",                L"Ubuntu 20.04 LTS" },
    { L"Debian",                      L"Debian GNU/Linux" },
    { L"kali-linux",                  L"Kali Linux Rolling" },
    { L"archlinux",                   L"Arch Linux" },
    { L"FedoraLinux-44",              L"Fedora Linux 44" },
    { L"FedoraLinux-43",              L"Fedora Linux 43" },
    { L"openSUSE-Tumbleweed",         L"openSUSE Tumbleweed" },
    { L"openSUSE-Leap-16.0",          L"openSUSE Leap 16.0" },
    { L"SUSE-Linux-Enterprise-16.0",  L"SUSE Linux Enterprise 16.0" },
    { L"SUSE-Linux-Enterprise-15-SP7", L"SUSE Linux Enterprise 15 SP7" },
    { L"SUSE-Linux-Enterprise-15-SP6", L"SUSE Linux Enterprise 15 SP6" },
    { L"AlmaLinux-10",                L"AlmaLinux OS 10" },
    { L"AlmaLinux-9",                 L"AlmaLinux OS 9" },
    { L"AlmaLinux-8",                 L"AlmaLinux OS 8" },
    { L"AlmaLinux-Kitten-10",         L"AlmaLinux OS Kitten 10" },
    { L"OracleLinux_9_5",             L"Oracle Linux 9.5" },
    { L"OracleLinux_8_10",            L"Oracle Linux 8.10" },
    { L"OracleLinux_7_9",             L"Oracle Linux 7.9" },
    { L"eLxr",                        L"eLxr 12 GNU/Linux" }
};

static bool sameId(const std::wstring& a, const std::wstring& b) {
    return lstrcmpiW(a.c_str(), b.c_str()) == 0;
}

static std::wstring findLiveLabel(const std::vector<DistroEntry>& live,
                                  const std::wstring& id) {
    for (size_t i = 0; i < live.size(); ++i) {
        if (sameId(live[i].id, id)) return live[i].label;
    }
    return std::wstring();
}

static bool knownBuiltin(const std::wstring& id) {
    for (size_t i = 0; i < sizeof(kBuiltin) / sizeof(kBuiltin[0]); ++i) {
        if (sameId(kBuiltin[i].id, id)) return true;
    }
    return false;
}

std::vector<DistroEntry> listOnlineDistros() {
    std::vector<DistroEntry> out;
    std::vector<DistroEntry> live;

    std::wstring exe = findWslExe();
    if (!exe.empty()) {
        std::wstring res = runCapture(L"\"" + exe + L"\" --list --online", 25000);
        std::vector<std::wstring> lines = splitLines(res);

        for (size_t i = 0; i < lines.size(); ++i) {
            std::wstring t = trimW(lines[i]);
            if (t.empty()) continue;

            size_t p = 0;
            while (p < t.size() && t[p] != L' ' && t[p] != L'\t') ++p;
            std::wstring id = t.substr(0, p);

            if (!looksLikeDistroId(id)) continue;
            if (lstrcmpiW(id.c_str(), L"NAME") == 0) continue;

            size_t q = p;
            while (q < t.size() && (t[q] == L' ' || t[q] == L'\t')) ++q;
            std::wstring label = trimW(t.substr(q));

            bool dup = false;
            for (size_t j = 0; j < live.size(); ++j) {
                if (sameId(live[j].id, id)) { dup = true; break; }
            }
            if (dup) continue;

            DistroEntry e;
            e.id = id;
            e.label = label.empty() ? id : label;
            e.installed = false;
            live.push_back(e);
        }
    }

    DistroEntry e;
    for (size_t i = 0; i < sizeof(kBuiltin) / sizeof(kBuiltin[0]); ++i) {
        e.id = kBuiltin[i].id;
        e.installed = false;
        std::wstring l = findLiveLabel(live, e.id);
        e.label = l.empty() ? std::wstring(kBuiltin[i].label) : l;
        out.push_back(e);
    }

    for (size_t i = 0; i < live.size(); ++i) {
        if (knownBuiltin(live[i].id)) continue;
        out.push_back(live[i]);
    }

    return out;
}

bool wslSetDefault(const std::wstring& name) {
    if (name.empty()) return false;
    std::wstring exe = findWslExe();
    if (exe.empty()) return false;

    std::wstring res = runCapture(L"\"" + exe + L"\" --set-default " + name, 15000);
    std::vector<std::wstring> lines = splitLines(res);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring t = trimW(lines[i]);
        if (t.empty()) continue;
        if (t.find(L"Unable") != std::wstring::npos ||
            t.find(L"error") != std::wstring::npos ||
            t.find(L"ERROR") != std::wstring::npos) return false;
    }
    return true;
}

bool wslUnregister(const std::wstring& name) {
    if (name.empty()) return false;
    std::wstring exe = findWslExe();
    if (exe.empty()) return false;

    std::wstring res = runCapture(L"\"" + exe + L"\" --unregister " + name, 30000);
    std::vector<std::wstring> lines = splitLines(res);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring t = trimW(lines[i]);
        if (t.empty()) continue;
        if (t.find(L"Unable") != std::wstring::npos ||
            t.find(L"error") != std::wstring::npos ||
            t.find(L"ERROR") != std::wstring::npos) return false;
    }
    return true;
}

bool wslInstall(const std::wstring& name) {
    if (name.empty()) return false;
    std::wstring exe = findWslExe();
    if (exe.empty()) return false;

    std::wstring res = runCapture(L"\"" + exe + L"\" --install --web-download -d "
                                  + name, 120000);
    std::vector<std::wstring> lines = splitLines(res);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring t = trimW(lines[i]);
        if (t.empty()) continue;
        if (t.find(L"Unable to install") != std::wstring::npos ||
            t.find(L"WSL_E_INSTALL") != std::wstring::npos) return false;
    }
    return true;
}

std::vector<DistroStatus> listDistroStatus() {
    std::vector<DistroStatus> out;

    std::wstring exe = findWslExe();
    if (exe.empty()) return out;

    std::wstring res = runCapture(L"\"" + exe + L"\" -l -v", 15000);
    std::vector<std::wstring> lines = splitLines(res);

    for (size_t i = 0; i < lines.size(); ++i) {
        std::wstring t = trimW(lines[i]);
        if (t.empty()) continue;

        bool isDef = (t[0] == L'*');
        size_t p = isDef ? 1 : 0;
        while (p < t.size() && (t[p] == L' ' || t[p] == L'\t')) ++p;

        size_t q = p;
        while (q < t.size() && t[q] != L' ' && t[q] != L'\t') ++q;
        if (q <= p) continue;

        DistroStatus s;
        s.name    = t.substr(p, q - p);
        s.isDefault = isDef;

        size_t r = q;
        while (r < t.size() && (t[r] == L' ' || t[r] == L'\t')) ++r;
        size_t e = r;
        while (e < t.size() && t[e] != L' ' && t[e] != L'\t') ++e;
        if (e > r) {
            std::wstring st = trimW(t.substr(r, e - r));
            s.running = (st == L"Running");
        }

        if (e < t.size()) {
            size_t v = e;
            while (v < t.size() && (t[v] == L' ' || t[v] == L'\t')) ++v;
            if (v < t.size()) {
                int n = 0;
                bool dig = true;
                for (size_t k = v; k < t.size(); ++k) {
                    if (t[k] < L'0' || t[k] > L'9') { dig = false; break; }
                }
                if (dig) n = _wtoi(t.substr(v).c_str());
                s.version = n > 0 ? n : 2;
            }
        }

        out.push_back(s);
    }

    return out;
}

}
