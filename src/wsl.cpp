#include "wsl.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstring>
#include <cwchar>

#include "lang.h"

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

// 组件状态不能靠 GetWindowsOptionalFeature 读，那需要管理员 + 慢。正经做法是
// 问 WSL 自己：`wsl.exe --status` 在组件被禁用时返回非 0，而且不会像 --help
// 那样把帮助文本打到 stdout，所以不会污染调用方的解析。
//
// 但 --status 在「Store 版 WSL 未安装、只有 inbox 版」时同样返回非 0，两者要
// 区分开：Store 版有 C:\Program Files\WSL\wsl.exe，inbox 版没有。
static bool storeWslPresent() {
    WCHAR buf[MAX_PATH];

    if (GetEnvironmentVariableW(L"ProgramW6432", buf, MAX_PATH)) {
        std::wstring p = std::wstring(buf) + L"\\WSL\\wsl.exe";
        if (realExe(p)) return true;
    }
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH)) {
        std::wstring p = std::wstring(buf) + L"\\WSL\\wsl.exe";
        if (realExe(p)) return true;
    }
    return false;
}

bool wslComponentEnabled() {
    std::wstring exe = findWslExe();
    if (exe.empty()) return false;

    DWORD code = (DWORD)-1;
    runCaptureExit(L"\"" + exe + L"\" --status", 10000, &code);
    return code == 0;
}

bool wslVirtualMachineEnabled() {
    // vmcompute 服务是 VirtualMachinePlatform 带进来的。读服务状态不需要管理员。
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;

    bool on = false;
    SC_HANDLE s = OpenServiceW(scm, L"vmcompute", SERVICE_QUERY_STATUS);
    if (s) {
        SERVICE_STATUS st;
        memset(&st, 0, sizeof(st));
        if (QueryServiceStatus(s, &st)) on = (st.dwCurrentState != SERVICE_STOPPED);
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm);
    return on;
}

static WslState g_state = WSL_UNKNOWN;

WslState wslState() {
    if (g_state != WSL_UNKNOWN) return g_state;

    if (findWslExe().empty()) {
        g_state = WSL_NO_EXE;
        return g_state;
    }

    if (wslComponentEnabled()) {
        g_state = WSL_READY;
        return g_state;
    }

    // 组件没启用。再分一次：Store 版缺失说明这台机器压根没装过 WSL 包。
    if (!storeWslPresent()) {
        g_state = WSL_NO_COMPONENT;
    } else if (!wslVirtualMachineEnabled()) {
        g_state = WSL_NO_VMP;
    } else {
        g_state = WSL_NO_COMPONENT;
    }
    return g_state;
}

void resetWslState() {
    g_state = WSL_UNKNOWN;
}

std::wstring wslStateLabel() {
    switch (wslState()) {
    case WSL_READY:       return L"WSL_READY";
    case WSL_NO_EXE:      return L"WSL_NO_EXE";
    case WSL_NO_VMP:      return L"WSL_NO_VMP";
    case WSL_NO_COMPONENT:return L"WSL_NO_COMPONENT";
    default:              return L"WSL_UNKNOWN";
    }
}

std::wstring wslEnableCommand() {
    // inbox 版 wsl.exe 自带 --install；Store 版也有。两者都能把缺失的组件装上，
    // 缺 VirtualMachinePlatform 时一并带上。
    std::wstring exe = findWslExe();
    if (exe.empty()) return L"wsl.exe --install";
    return L"\"" + exe + L"\" --install";
}

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// 子进程输出的编码判定。
//
// inbox wsl.exe 在组件被禁用 / 出错时把消息按 UTF-16LE 写进 stdout，而
// 「中文的 UTF-16 字节里一个 0x00 都没有」（「安装」= 89 5B C5 88），所以
// 不能只数零字节，必须先看 UTF-8 结构是否成立。
//
// 奇数位零字节的比例只能当补充证据：实测 wsl.exe --help 的比例是 0.809，
// 离0.80 的阈值只差 0.009，输出里中文一多就掉到阈值以下被误判成 UTF-8。
// 所以阈值放到 0.75，并且要求 UTF-8 结构非法时才认UTF-16——两条判据
// 互相独立，不会因为某一段输出偏中文就整体翻车。
static int scanUtf8Strict(const unsigned char* b, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = b[i];
        int need;
        unsigned char lo, hi;

        if (c < 0x80) { ++i; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { need = 1; lo = 0x80; hi = 0xBF; }
        else if (c == 0xE0)               { need = 2; lo = 0xA0; hi = 0xBF; }
        else if (c >= 0xE1 && c <= 0xEC)  { need = 2; lo = 0x80; hi = 0xBF; }
        else if (c == 0xED)               { need = 2; lo = 0x80; hi = 0x9F; }
        else if (c >= 0xEE && c <= 0xEF)  { need = 2; lo = 0x80; hi = 0xBF; }
        else if (c == 0xF0)               { need = 3; lo = 0x90; hi = 0xBF; }
        else if (c >= 0xF1 && c <= 0xF3)  { need = 3; lo = 0x80; hi = 0xBF; }
        else if (c == 0xF4)               { need = 3; lo = 0x80; hi = 0x8F; }
        else return 1;                // 0x80..0xC1 / 0xF5..0xFF 非法

        if (i + (size_t)need >= n) break;         // 尾部截断，判不了也不算错
        for (int k = 1; k <= need; ++k) {
            unsigned char t = b[i + (size_t)k];
            if (t < lo || t > hi) return 1;
        }
        i += (size_t)need + 1;
    }
    return 0;
}

static std::wstring decodeOutput(const std::string& raw) {
    if (raw.empty()) return std::wstring();

    const unsigned char* b = (const unsigned char*)raw.data();
    size_t n = raw.size();
    size_t off = 0;
    bool utf16 = false;

    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        utf16 = true;
        off = 2;
    } else if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        // BE BOM罕见，转成 LE 字节序再按 UTF-16 读。
        std::wstring w;
        for (size_t i = 2; i + 1 < n; i += 2) {
            w.push_back((wchar_t)(((uint32_t)b[i] << 8) | b[i + 1]));
        }
        return w;
    } else {
        size_t pairs = 0, zeros = 0;
        for (size_t i = 1; i < n; i += 2) {
            ++pairs;
            if (b[i] == 0x00) ++zeros;
        }

        // UTF-8 结构非法 -> 一定是 UTF-16（纯 CJK 的 UTF-16 逐字节看全是
        // 合法 ASCII，只有结构判定能把它们摘出来）。
        if (scanUtf8Strict(b, n) != 0) {
            utf16 = true;
        } else if (pairs >= 4 && zeros * 4 >= pairs * 3) {
            utf16 = true;
        }
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

std::wstring runCaptureExit(const std::wstring& cmdline, unsigned timeoutMs, DWORD* exitCode) {
    if (exitCode) *exitCode = (DWORD)-1;
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
    if (w == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        if (exitCode) *exitCode = (DWORD)-1;
    }

    if (th) {
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }

    std::wstring out = decodeOutput(ctx->data);

    if (exitCode && w != WAIT_TIMEOUT) {
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code)) *exitCode = code;
    }

    delete ctx;
    CloseHandle(outRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return out;
}

std::wstring runCapture(const std::wstring& cmdline, unsigned timeoutMs) {
    return runCaptureExit(cmdline, timeoutMs, NULL);
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


// ---------------------------------------------------------------- WSLg
//
// msrdc.exe /wslg 是 WSLg（Linux 图形界面）的显示通道，父进程是 wslhost。
// 每当 WSL 启动它就被拉起来，所以它一出问题就是无限弹窗。
//
// 这里只做两件事：判断它坏了没有，以及往 .wslconfig 写 guiApplications=false。
// 不去动 C:\Program Files\WSL 下的任何文件 —— rdclientax.dll 虽然加载失败，
// 但它是必需文件，删了会让 WSLg 彻底起不来（连带正常功能一起坏）。

static std::wstring wslDir() {
    WCHAR buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramFiles", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return std::wstring();
    return std::wstring(buf) + L"\\WSL";
}

static std::wstring wslConfigPath() {
    WCHAR buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return std::wstring();
    return std::wstring(buf) + L"\\.wslconfig";
}

// rdclientax.dll 装不上，主因通常是系统文件版本与 Build 号不匹配：
// 它要 KERNEL32!GetTempPath2W，老一些的累积更新里没这个导出，于是 127。
// 文件大小不能作为依据（实测踩过：13MB 完好无损，一样导不进来）。
static bool rdclientaxLoadable() {
    std::wstring dir = wslDir();
    if (dir.empty()) return false;
    std::wstring dll = dir + L"\\rdclientax.dll";
    if (!fileExistsW(dll)) return false;

    // 不用 SetDllDirectoryW：它改的是整个进程的搜索路径，而 wsl.cpp 别处还要
    // 跑子进程，状态留着不安全。绝对路径 + LOAD_WITH_ALTERED_SEARCH_PATH
    // 只影响这一次加载。
    HMODULE h = LoadLibraryExW(dll.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) return false;
    FreeLibrary(h);
    return true;
}

static bool readAllW(const std::wstring& path, std::string& outData) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li;
    if (!GetFileSizeEx(f, &li) || li.QuadPart > 64 * 1024) {
        CloseHandle(f);
        return false;
    }
    DWORD want = (DWORD)li.QuadPart;
    std::string buf;
    buf.resize(want);
    DWORD got = 0;
    BOOL ok = ReadFile(f, want ? &buf[0] : NULL, want, &got, NULL);
    CloseHandle(f);
    if (!ok) return false;
    buf.resize(got);
    outData.swap(buf);
    return true;
}

static bool writeAllW(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    BOOL ok = WriteFile(f, data.empty() ? NULL : data.data(),
                        (DWORD)data.size(), &put, NULL);
    CloseHandle(f);
    return ok && put == data.size();
}

static std::string wslConfigText() {
    std::wstring p = wslConfigPath();
    if (p.empty()) return std::string();
    std::string utf8;
    if (!readAllW(p, utf8)) return std::string();
    return utf8;
}

static void lowerAscii(std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
    }
}

static bool guiAppsDisabledInConfig() {
    std::string t = wslConfigText();
    if (t.empty()) return false;
    lowerAscii(t);
    return t.find("guiapplications") != std::string::npos;
}

WslgState wslgState() {
    std::wstring dir = wslDir();
    std::wstring msrdc = dir.empty() ? std::wstring() : dir + L"\\msrdc.exe";
    if (msrdc.empty() || !fileExistsW(msrdc)) return WSLG_NO_DLL;

    if (guiAppsDisabledInConfig()) return WSLG_OFF;
    return rdclientaxLoadable() ? WSLG_ON : WSLG_BROKEN;
}

std::wstring wslgStateLabel() {
    switch (wslgState()) {
    case WSLG_OFF:return LS(L"已关闭");
    case WSLG_ON: return LS(L"正常");
    case WSLG_NO_DLL: return LS(L"未安装");
    default:              return LS(L"加载失败，正在弹窗");
    }
}

bool disableWslg() {
    if (guiAppsDisabledInConfig()) return true;

    std::wstring path = wslConfigPath();
    if (path.empty()) return false;

    std::string t = wslConfigText();

    // CRLF：ini 解析器也认 LF，但统一 CRLF 省得跟 Notepad 里手工编辑的人打架。
    const char* RULE = "\r\n";
    const std::string KEY = "guiApplications=false";

    size_t sec = t.find("[wsl2]");
    std::string out;
    if (sec == std::string::npos) {
        // 没有 [wsl2] 段就新建一个追加在末尾。
        out = t;
        if (out.empty()) {
            out = "[wsl2]";
            out += RULE;
        } else {
            if (out[out.size() - 1] != '\n') out += RULE;
            out += RULE;
            out += "[wsl2]";
            out += RULE;
        }
        out += KEY;
        out += RULE;
    } else {
        // 插进 [wsl2] 段的开头。插段头而不是段尾，是为了避免插到下一个
        // [section] 之后 —— ini 里的键归属它上面最近的段名。
        //
        // 落点要把段名后面的换行一起吃掉。光取 sec+7 会停在 '\r' 或 '\n' 之前，
        // 于是插进去就成了 "[wsl2]\n\r\nguiApplications=falsememory=4GB"——
        // 键和下一条粘成一行，WSL 解析不出这个键，配置等于没写。
        size_t at = sec + 7;
        while (at < t.size() && (t[at] == '\r' || t[at] == '\n')) ++at;
        out = t.substr(0, at);
        out += KEY;
        out += RULE;
        out += t.substr(at);
    }

    return writeAllW(path, out);
}

bool enableWslg() {
    std::wstring path = wslConfigPath();
    if (path.empty()) return false;

    std::string t = wslConfigText();
    if (t.empty()) return true;

    std::string out;
    out.reserve(t.size());

    size_t i = 0;
    while (i <= t.size()) {
        size_t eol = t.find('\n', i);
        std::string line = (eol == std::string::npos) ? t.substr(i)
                                                      : t.substr(i, eol - i + 1);
        std::string low = line;
        lowerAscii(low);
        // 命中「键名在等号左边」的行才删。条件写成low.find(key) < low.find('=')
        // 的话，遇到没有 '=' 的行（npos 参与比较）会误判，所以显式分开判。
        size_t kp = low.find("guiapplications");
        size_t eq = low.find('=');
        bool isKeyLine = (kp != std::string::npos) &&
                         (eq != std::string::npos) && (kp < eq);
        if (!isKeyLine) out += line;

        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return writeAllW(path, out);
}

std::wstring wslShutdownCommand() {
    std::wstring exe = findWslExe();
    if (exe.empty()) return L"wsl.exe --shutdown";
    return L"\"" + exe + L"\" --shutdown";
}

}
