#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cstring>
#include <string>
#include <vector>

#include "catalog.h"
#include "download.h"
#include "editor.h"
#include "fs.h"
#include "lang.h"
#include "render.h"
#include "terminal.h"
#include "ui.h"
#include "wsl.h"

using namespace wslterm;

typedef void* HPCON_T;
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON_T*);
typedef HRESULT (WINAPI *PFN_ResizePseudoConsole)(HPCON_T, COORD);
typedef void    (WINAPI *PFN_ClosePseudoConsole)(HPCON_T);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define WM_APP_PTY_DATA (WM_APP + 1)
#define WM_APP_PTY_EXIT (WM_APP + 2)
#define WM_APP_PROBE    (WM_APP + 3)
#define WM_APP_ONLINE   (WM_APP + 4)
#define WM_APP_DL_TICK  (WM_APP + 5)
#define WM_APP_DL_DONE  (WM_APP + 6)
#define WM_APP_VERIFY   (WM_APP + 7)
#define WM_APP_DISTRO   (WM_APP + 8)
#define WM_APP_WSLFIX   (WM_APP + 9)

#define TIMER_REPAINT 1
#define TIMER_CARET   2
#define TIMER_PTYWAIT 3
#define TIMER_ANIM    4

#define TRANSPORT_CONPTY 0
#define TRANSPORT_PIPE   1

static const wchar_t* kClassName = L"EWSLWndClass";
static const wchar_t* kAppVersion = L"1.0.0";

// RT_ICON resource id emitted by tools/make-res.py from icon.png
#define IDI_APP 1
static const int kInitialFontHeight = 16;
static const int kResizeBorder = 6;

struct ProbeResult {
    bool wslOk;
    std::vector<std::wstring> installed;
    std::vector<std::wstring> dead;
    std::wstring def;
    std::wstring version;
    int  state;

    // WSLg（Linux 图形界面）状态。msrdc 装不上 rdclientax.dll 时会无限弹
    // 「无法加载远程桌面服务 ActiveX 控件」，跟能不能用终端毫无关系。
    // wslgFixed 表示这一轮已经替用户把 guiApplications=false 写进 .wslconfig。
    int  wslg;
    bool wslgFixed;

    ProbeResult() : wslOk(false), state(WSL_UNKNOWN), wslg(WSLG_NO_DLL), wslgFixed(false) {}
};

struct App {
    HWND      hwnd;
    Terminal* term;
    Renderer* rend;
    Ui*       ui;
    Editor*   ed;

    HPCON_T hpc;
    HANDLE  hPipeWrite;
    HANDLE  hPipeRead;
    HANDLE  hProcess;
    HANDLE  hThread;
    HANDLE  hReader;
    HANDLE  hWriter;
    HANDLE  hWriteEvent;
    std::vector<char> writeQueue;
    std::vector<char> readQueue;
    bool    readPosted;
    bool    writeExit;
    CRITICAL_SECTION cs;

    PFN_ResizePseudoConsole fnResize;
    PFN_ClosePseudoConsole  fnClose;

    int   scrollOffset;
    bool  focused;
    DWORD startTick;
    bool  exited;
    DWORD ptyBytes;

    int         transport;
    bool        jobMode;
    std::wstring lastArgs;
    std::wstring lastExe;

    Selection sel;
    bool      dragging;

    int       page;
    UiModel   model;
    std::wstring pendingInstall;
    std::wstring pendingStart;
    std::wstring pendingLabel;

    std::wstring folder;
    std::vector<std::wstring> expandedDirs;
    std::vector<std::wstring> brokenDistros;
    std::vector<std::wstring> sessionBad;

    int          instStage;
    std::wstring instDistro;
    std::wstring instLabel;
    std::wstring instUrl;
    std::wstring instFile;
    std::wstring instDir;
    std::wstring instErr;
    int          instAttempt;
    bool         instIsFix;
    bool         instUserCancel;
    volatile long instCancel;
    HANDLE       instThread;
    DWORD        instTick;
    int          instLastPct;
    HANDLE       distroThread;

    bool edDragging;
    bool caretOn;
    bool repaintPending;
    bool swallowChar;
    bool unregisterBusy;
    bool prepUnregistering;
    bool suppressLaunch;
    bool autoFallbackTried;
    bool autoTarget;
    bool quietExit;
    bool verifying;
    bool verifyWasAuto;
    bool ptyBridge;
    int  conptyOk;
    int  escState;
    int  ptyText;
    DWORD animTick;
    bool wslFixBusy;
    std::wstring wslFixOut;
    volatile LONG wslFixApproved;    // -1 失败 / 0 未知 / 1 已提权成功
    volatile LONG wslFixElapsedMs;   // 后台线程累计的等待毫秒

    App()
        : hwnd(NULL), term(NULL), rend(NULL), ui(NULL), ed(NULL), hpc(NULL),
          hPipeWrite(NULL), hPipeRead(NULL), hProcess(NULL),
          hThread(NULL), hReader(NULL),
          hWriter(NULL), hWriteEvent(CreateEventW(NULL, FALSE, FALSE, NULL)),
          readPosted(false),
          writeExit(false),
          fnResize(NULL), fnClose(NULL),
          scrollOffset(0), focused(false), startTick(0), exited(false),
          ptyBytes(0),
          transport(TRANSPORT_CONPTY), jobMode(false),
          dragging(false), page(PAGE_TERMINAL),
          instStage(INST_IDLE), instAttempt(0), instIsFix(false),
          instUserCancel(false), instCancel(0),
          instThread(NULL), instTick(0), instLastPct(-1), distroThread(NULL),
          edDragging(false), caretOn(true), repaintPending(false),
          swallowChar(false),
          unregisterBusy(false), prepUnregistering(false),
          suppressLaunch(false), autoFallbackTried(false),
          autoTarget(false), quietExit(false), verifying(false),
          verifyWasAuto(false), ptyBridge(false), conptyOk(-1), escState(0),
          ptyText(0), animTick(0), wslFixBusy(false),
          wslFixApproved(0), wslFixElapsedMs(0) {}
};

static App* g_app = NULL;

static std::wstring distroArg(const std::wstring& id) {
    if (id.find(L' ') == std::wstring::npos && id.find(L'"') == std::wstring::npos)
        return id;
    return L"\"" + id + L"\"";
}

static void countText(App* app, const char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];

        if (app->escState == 0) {
            if (c == 0x1b) { app->escState = 1; continue; }
            if (c == '\r' || c == '\n' || c == '\t') { ++app->ptyText; continue; }
            if (c >= 0x20 && c <= 0x7e) { ++app->ptyText; continue; }
            if (c >= 0x80) { ++app->ptyText; continue; }
        } else if (app->escState == 1) {
            if (c == '[' || c == ']' || c == '(' || c == ')') app->escState = 2;
            else app->escState = 0;
        } else {
            if (c >= 0x40 && c <= 0x7e) app->escState = 0;
            else if (c == 0x07) app->escState = 0;
        }
    }
}

static bool installRunning(const App* app);
static void showToast(App* app, const std::wstring& text);
static void refreshOnline(App* app);
static void gotoPage(App* app, int page);
static void refreshUi(App* app);
static void saveSettings(App* app);
static std::wstring g_startDistro;

struct BackBuffer {
    HDC     dc;
    HBITMAP bmp;
    HBITMAP old;
    int     w;
    int     h;

    BackBuffer() : dc(NULL), bmp(NULL), old(NULL), w(0), h(0) {}
};

static BackBuffer g_bb;
static BackBuffer g_layer;

static void layerRelease() {
    if (g_layer.dc) {
        if (g_layer.old) SelectObject(g_layer.dc, g_layer.old);
        if (g_layer.bmp) DeleteObject(g_layer.bmp);
        DeleteDC(g_layer.dc);
    }
    g_layer.dc = NULL;
    g_layer.bmp = NULL;
    g_layer.old = NULL;
    g_layer.w = 0;
    g_layer.h = 0;
}

static bool layerEnsure(HDC ref, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (g_layer.dc && g_layer.w == w && g_layer.h == h) return true;

    layerRelease();
    g_layer.dc = CreateCompatibleDC(ref);
    if (!g_layer.dc) return false;

    g_layer.bmp = CreateCompatibleBitmap(ref, w, h);
    if (!g_layer.bmp) {
        DeleteDC(g_layer.dc);
        g_layer.dc = NULL;
        return false;
    }
    g_layer.old = (HBITMAP)SelectObject(g_layer.dc, g_layer.bmp);
    g_layer.w = w;
    g_layer.h = h;
    return true;
}

static void bbRelease() {
    if (g_bb.dc) {
        if (g_bb.old) SelectObject(g_bb.dc, g_bb.old);
        if (g_bb.bmp) DeleteObject(g_bb.bmp);
        DeleteDC(g_bb.dc);
    }
    g_bb.dc = NULL;
    g_bb.bmp = NULL;
    g_bb.old = NULL;
    g_bb.w = 0;
    g_bb.h = 0;
}

static bool bbEnsure(HDC ref, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (g_bb.dc && g_bb.w == w && g_bb.h == h) return true;

    bbRelease();
    g_bb.dc = CreateCompatibleDC(ref);
    if (!g_bb.dc) return false;

    g_bb.bmp = CreateCompatibleBitmap(ref, w, h);
    if (!g_bb.bmp) {
        DeleteDC(g_bb.dc);
        g_bb.dc = NULL;
        return false;
    }
    g_bb.old = (HBITMAP)SelectObject(g_bb.dc, g_bb.bmp);
    g_bb.w = w;
    g_bb.h = h;
    return true;
}

static std::wstring trimW(const std::wstring& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n' || s[b] == L'"')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n' || s[e - 1] == L'"')) --e;
    return s.substr(b, e - b);
}

static inline bool isKeyDown(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

static void scheduleRepaint(App* app) {
    if (!app || app->repaintPending) return;
    app->repaintPending = true;
    SetTimer(app->hwnd, TIMER_REPAINT, 8, NULL);
}

static void writePty(App* app, const char* data, int len) {
    if (!app->hPipeWrite || len <= 0) return;
    EnterCriticalSection(&app->cs);
    size_t old = app->writeQueue.size();
    app->writeQueue.resize(old + (size_t)len);
    memcpy(app->writeQueue.data() + old, data, (size_t)len);
    LeaveCriticalSection(&app->cs);
    SetEvent(app->hWriteEvent);
}

static DWORD WINAPI WriterThread(LPVOID param) {
    App* app = (App*)param;
    for (;;) {
        WaitForSingleObject(app->hWriteEvent, INFINITE);
        EnterCriticalSection(&app->cs);
        bool exit = app->writeExit;
        std::vector<char> buf;
        if (!app->writeQueue.empty()) buf.swap(app->writeQueue);
        HANDLE wh = app->hPipeWrite;
        LeaveCriticalSection(&app->cs);
        if (exit && buf.empty()) break;
        if (!buf.empty() && wh) {
            DWORD written = 0;
            WriteFile(wh, buf.data(), (DWORD)buf.size(), &written, NULL);
        }
        if (exit) break;
    }
    return 0;
}

static void writePtyStr(App* app, const char* s) {
    writePty(app, s, (int)strlen(s));
}

static void termPrint(App* app, const std::wstring& text) {
    if (!app->term || text.empty()) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return;
    std::vector<char> buf((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), buf.data(), n, NULL, NULL);
    app->term->feed(buf.data(), buf.size());
}

// The reader used to PostMessage one WM_APP_PTY_DATA per ReadFile and HeapAlloc
// a copy each time. A chatty command (dnf / pacman) could therefore queue
// thousands of messages ahead of the user's keystrokes, and the terminal looked
// frozen. Now the reader only appends into a CS-protected buffer and posts a
// single wakeup when one is not already pending; the UI thread drains the whole
// buffer at once, so typing stays responsive no matter how loud the process is.
static std::vector<char> takePendingPty(App* app) {
    std::vector<char> chunk;
    if (!app) return chunk;
    EnterCriticalSection(&app->cs);
    chunk.swap(app->readQueue);
    app->readPosted = false;
    LeaveCriticalSection(&app->cs);
    return chunk;
}

static DWORD WINAPI ReaderThread(LPVOID param) {
    App* app = (App*)param;
    char buf[16384];

    for (;;) {
        DWORD n = 0;
        if (!ReadFile(app->hPipeRead, buf, (DWORD)sizeof(buf), &n, NULL) || n == 0) break;

        bool wake = false;
        EnterCriticalSection(&app->cs);
        app->readQueue.insert(app->readQueue.end(), buf, buf + n);
        if (!app->readPosted) {
            app->readPosted = true;
            wake = true;
        }
        LeaveCriticalSection(&app->cs);

        if (wake && !PostMessageW(app->hwnd, WM_APP_PTY_DATA, 0, 0)) break;
    }

    PostMessageW(app->hwnd, WM_APP_PTY_EXIT, 0, 0);
    return 0;
}

static void stopPty(App* app) {
    if (app->hProcess) {
        TerminateProcess(app->hProcess, 0);
        CloseHandle(app->hProcess);
        app->hProcess = NULL;
    }
    if (app->hThread) {
        CloseHandle(app->hThread);
        app->hThread = NULL;
    }
    if (app->hpc && app->fnClose) {
        app->fnClose(app->hpc);
        app->hpc = NULL;
    }
    if (app->hPipeWrite) {
        EnterCriticalSection(&app->cs);
        app->writeExit = true;
        LeaveCriticalSection(&app->cs);
        SetEvent(app->hWriteEvent);
        CloseHandle(app->hPipeWrite);
        app->hPipeWrite = NULL;
    }
    if (app->hWriter) {
        WaitForSingleObject(app->hWriter, 3000);
        CloseHandle(app->hWriter);
        app->hWriter = NULL;
    }
    if (app->hReader) {
        WaitForSingleObject(app->hReader, 2000);
        CloseHandle(app->hReader);
        app->hReader = NULL;
    }
    EnterCriticalSection(&app->cs);
    app->readQueue.clear();
    app->readPosted = false;
    LeaveCriticalSection(&app->cs);
    if (app->hPipeRead) {
        CloseHandle(app->hPipeRead);
        app->hPipeRead = NULL;
    }
}

static std::wstring g_wslPath;
static DWORD       g_wslPathTick = 0;

static const std::wstring& cachedWslPath() {
    DWORD now = GetTickCount();
    if (g_wslPath.empty() || (DWORD)(now - g_wslPathTick) > 300000) {
        g_wslPath = findWslExe();
        g_wslPathTick = now;
    }
    return g_wslPath;
}

static void dropWslPathCache() {
    g_wslPath.clear();
    g_wslPathTick = GetTickCount();
}

static bool startSession(App* app, const std::wstring& extraArgs, int transport) {
    std::wstring exe = findWslExe();
    if (exe.empty()) return false;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE inRead = NULL;
    HANDLE inWrite = NULL;
    HANDLE outRead = NULL;
    HANDLE outWrite = NULL;

    if (!CreatePipe(&inRead, &inWrite, &sa, 0)) return false;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
        CloseHandle(inRead);
        CloseHandle(inWrite);
        return false;
    }

    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + exe + L"\"";
    if (!extraArgs.empty()) {
        cmd += L" ";
        cmd += extraArgs;
    }

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    bool ok = false;
    HPCON_T hpc = NULL;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (transport == TRANSPORT_CONPTY) {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        PFN_CreatePseudoConsole pCreate =
            reinterpret_cast<PFN_CreatePseudoConsole>(
                reinterpret_cast<void*>(GetProcAddress(k32, "CreatePseudoConsole")));
        app->fnResize =
            reinterpret_cast<PFN_ResizePseudoConsole>(
                reinterpret_cast<void*>(GetProcAddress(k32, "ResizePseudoConsole")));
        app->fnClose =
            reinterpret_cast<PFN_ClosePseudoConsole>(
                reinterpret_cast<void*>(GetProcAddress(k32, "ClosePseudoConsole")));

        if (!pCreate || !app->fnResize || !app->fnClose) {
            CloseHandle(inRead);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(outWrite);
            return false;
        }

        COORD sz;
        sz.X = (SHORT)app->term->cols();
        sz.Y = (SHORT)app->term->rows();

        if (FAILED(pCreate(sz, inRead, outWrite, 0, &hpc)) || !hpc) {
            CloseHandle(inRead);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(outWrite);
            return false;
        }

        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
        LPPROC_THREAD_ATTRIBUTE_LIST attrs =
            (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);

        if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
            UpdateProcThreadAttribute(attrs, 0, (DWORD_PTR)PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                      (PVOID)hpc, sizeof(hpc), NULL, NULL)) {
            STARTUPINFOEXW si;
            memset(&si, 0, sizeof(si));
            si.StartupInfo.cb = sizeof(si);
            si.lpAttributeList = attrs;

            ok = CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE,
                                EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                NULL, NULL, &si.StartupInfo, &pi) != 0;
        }

        if (attrs) {
            DeleteProcThreadAttributeList(attrs);
            HeapFree(GetProcessHeap(), 0, attrs);
        }

        if (!ok) {
            app->fnClose(hpc);
            hpc = NULL;
        }
    } else {
        STARTUPINFOW si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput  = inRead;
        si.hStdOutput = outWrite;
        si.hStdError  = outWrite;

        ok = CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                            NULL, NULL, &si, &pi) != 0;
    }

    if (!ok) {
        if (hpc && app->fnClose) app->fnClose(hpc);
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }

    CloseHandle(inRead);
    CloseHandle(outWrite);

    app->hpc        = hpc;
    app->transport  = transport;
    if (app->term) app->term->setLfImpliesCr(transport == TRANSPORT_PIPE);
    app->hPipeWrite = inWrite;

    {
        EnterCriticalSection(&app->cs);
        app->writeQueue.clear();
        app->writeExit = false;
        LeaveCriticalSection(&app->cs);
        if (!app->hWriter)
            app->hWriter = CreateThread(NULL, 0, WriterThread, app, 0, NULL);
        else
            SetEvent(app->hWriteEvent);
    }
    app->hPipeRead  = outRead;
    app->hProcess   = pi.hProcess;
    app->hThread    = pi.hThread;
    app->startTick  = GetTickCount();
    app->exited     = false;
    app->ptyBytes   = 0;
    app->escState   = 0;
    app->ptyText    = 0;
    app->lastArgs   = extraArgs;
    app->lastExe    = exe;

    if (app->hwnd) SetTimer(app->hwnd, TIMER_PTYWAIT, 6000, NULL);

    app->hReader = CreateThread(NULL, 0, ReaderThread, app, 0, NULL);
    return true;
}

static bool startPty(App* app, const std::wstring& extraArgs) {
    return startSession(app, extraArgs, TRANSPORT_CONPTY);
}

static bool startJob(App* app, const std::wstring& extraArgs) {
    return startSession(app, extraArgs, TRANSPORT_PIPE);
}

static void jobLog(App* app, const std::wstring& line) {
    std::wstring t = trimW(line);
    if (t.empty()) return;
    app->model.instLog.push_back(t);
    if (app->model.instLog.size() > 400) {
        app->model.instLog.erase(app->model.instLog.begin());
    }
}

static void jobLogStream(App* app, const std::string& raw) {
    std::wstring w;
    int n = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), NULL, 0);
    if (n > 0) {
        std::vector<wchar_t> buf((size_t)n);
        MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), buf.data(), n);
        w.assign(buf.data(), (size_t)n);
    }

    static std::wstring carry;
    carry += w;

    for (;;) {
        size_t at = carry.find_first_of(L"\r\n");
        if (at == std::wstring::npos) break;
        std::wstring line = carry.substr(0, at);
        carry.erase(0, at + 1);
        if (!line.empty()) jobLog(app, line);
    }
    if (carry.size() > 4096) carry.clear();
}

struct WslFixArg {
    App* app;
    WslFixArg() : app(NULL) {}
};

//启用组件要管理员，wsl --install 自己不带提权。非提权跑一遍只会拿到
// 「此操作需要提升的权限」，所以直接用 runas verb 拉起，由 UAC 弹窗授权。
// 提权被拒时 ShellExecuteEx 返回 ERROR_CANCELLED，这里如实回报，不假装成功。
static DWORD WINAPI WslFixThread(LPVOID param) {
    WslFixArg* a = (WslFixArg*)param;
    App* app = a->app;

    std::wstring exe = findWslExe();
    std::wstring verb = L"runas";
    std::wstring file = exe.empty() ? std::wstring(L"wsl.exe") : exe;
    std::wstring args = L"--install --no-distribution";

    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb = verb.c_str();
    sei.lpFile = file.c_str();
    sei.lpParameters = args.c_str();
    sei.lpDirectory = NULL;
    sei.nShow = SW_HIDE;

    std::wstring out;
    BOOL ok = ShellExecuteExW(&sei);

    WCHAR num[64];

    if (!ok) {
        DWORD err = GetLastError();
        wsprintfW(num, L"%lu", err);
        if (err == ERROR_CANCELLED)
            out = LS(L"已取消授权，没有做任何更改");
        else
            out = LS(L"无法提权启动 wsl --install（错误码 ") + std::wstring(num) + LS(L"）");
        app->wslFixApproved = -1;          // 失败，别停在「等待授权」
    } else {
        app->wslFixApproved = 1;
        if (sei.hProcess) {
            // 分段等待，好让 UI 线程有机会推进阶段与超时提示。
            // wsl --install 启用系统组件可能要好几分钟，超时给个明确说法，
            // 而不是让进度条一直停在 95% 让人猜。
            const DWORD kWait = 15 * 60 * 1000;
            DWORD waited = 0;
            bool timedOut = false;
            for (;;) {
                DWORD r = WaitForSingleObject(sei.hProcess, 1000);
                if (r != WAIT_TIMEOUT) break;
                waited += 1000;
                if (waited >= kWait) { timedOut = true; break; }
                app->wslFixElapsedMs = waited;
            }
            app->wslFixElapsedMs = waited;

            DWORD code = 0;
            if (timedOut) {
                out = LS(L"命令仍在后台运行，请稍候或重启后重新检测");
            } else if (GetExitCodeProcess(sei.hProcess, &code) && code != 0) {
                wsprintfW(num, L"%lu", code);
                out = LS(L"wsl --install 返回 ") + std::wstring(num);
            }
            CloseHandle(sei.hProcess);
        }
        if (out.empty()) out = LS(L"命令已执行完成");
    }

    app->wslFixOut = out;
    app->wslFixElapsedMs = 0;
    PostMessageW(app->hwnd, WM_APP_WSLFIX, 0, 0);
    return 0;
}

static DWORD WINAPI ProbeThread(LPVOID param) {
    App* app = (App*)param;

    ProbeResult* r = new ProbeResult();
    r->state = wslState();
    // inbox wsl.exe 在组件被禁用时照样存在，findWslExe() 非空不代表 WSL 能跑。
    // 只有探到 READY 才去列发行版，否则会把 inbox 版的帮助文本当成发行版列表。
    r->wslOk = (r->state == WSL_READY);
    if (r->wslOk) {
        r->installed = listInstalledDistros();

        for (size_t i = 0; i < r->installed.size(); ++i) {
            if (distroRootfsState(r->installed[i]) == ROOTFS_BROKEN) {
                r->dead.push_back(r->installed[i]);
            }
        }

        if (!r->installed.empty()) {
            r->def = defaultDistro();

            for (size_t i = 0; i < r->dead.size(); ++i) {
                if (!r->def.empty() &&
                    lstrcmpiW(r->dead[i].c_str(), r->def.c_str()) == 0) {
                    r->def.clear();
                    break;
                }
            }

            if (r->def.empty()) {
                for (size_t i = 0; i < r->installed.size(); ++i) {
                    bool dead = false;
                    for (size_t j = 0; j < r->dead.size(); ++j) {
                        if (lstrcmpiW(r->dead[j].c_str(), r->installed[i].c_str()) == 0) {
                            dead = true;
                            break;
                        }
                    }
                    if (!dead) { r->def = r->installed[i]; break; }
                }
            }
        }
    }

    std::wstring exe = findWslExe();
    if (!exe.empty()) {
        std::wstring v = runCapture(L"\"" + exe + L"\" --version", 8000);
        size_t pos = 0;
        while (pos < v.size()) {
            size_t eol = v.find_first_of(L"\r\n", pos);
            if (eol == std::wstring::npos) eol = v.size();
            std::wstring t = trimW(v.substr(pos, eol - pos));
            pos = eol + 1;
            if (t.size() <= 90 && t.find(L'.') != std::wstring::npos &&
                t.find(L'\\') == std::wstring::npos && t.find(L'/') == std::wstring::npos) {
                r->version = t;
                break;
            }
        }
    }

    // WSLg 坏了就顺手关掉。这个检测只花一次 LoadLibrary 的时间，
    // 而收益是用户不再被无限弹窗骚扰，所以放在启动探测里无条件做。
    //
    // 前提是 WSL 本身能用：组件都没启用的话 msrdc 根本不会被拉起来，
    // 没什么可关的，交给「启用 WSL 组件」那条路去处理。
    if (r->wslOk) {
        r->wslg = wslgState();
        if (r->wslg == WSLG_BROKEN && disableWslg()) {
            // 写 .wslconfig 不需要管理员权限，但 wsl --shutdown 会关掉所有
            // 正在跑的发行版。EWSL 自己正要开的那个终端也会一起没，
            // 所以这里只标记，让 UI 告诉用户「下次生效」而不是直接关。
            r->wslgFixed = true;
            r->wslg = WSLG_OFF;
        }
    }

    PostMessageW(app->hwnd, WM_APP_PROBE, 0, (LPARAM)r);
    return 0;
}

struct VerifyResult {
    std::wstring id;
    bool alive;
    VerifyResult() : alive(false) {}
};

static DWORD WINAPI VerifyThread(LPVOID param) {
    App* app = (App*)param;

    VerifyResult* r = new VerifyResult();
    r->id = app->model.activeDistro;

    std::wstring exe = findWslExe();
    if (!exe.empty() && !r->id.empty()) {
        std::wstring out = runCapture(
            L"\"" + exe + L"\" -d " + distroArg(r->id) + L" -e /bin/echo WSL_OK", 25000);
        r->alive = (out.find(L"WSL_OK") != std::wstring::npos);
    }

    PostMessageW(app->hwnd, WM_APP_VERIFY, 0, (LPARAM)r);
    return 0;
}

static DWORD WINAPI OnlineThread(LPVOID param) {
    App* app = (App*)param;

    std::vector<DistroEntry>* r = new std::vector<DistroEntry>(listOnlineDistros());
    PostMessageW(app->hwnd, WM_APP_ONLINE, 0, (LPARAM)r);
    return 0;
}

struct DistroRefresh {
    std::vector<DistroStatus> statuses;
    std::vector<DistroEntry>  online;

    DistroRefresh() {}
};

static DWORD WINAPI DistroThread(LPVOID param) {
    App* app = (App*)param;

    DistroRefresh* r = new DistroRefresh();
    r->statuses = listDistroStatus();
    r->online   = listOnlineDistros();

    PostMessageW(app->hwnd, WM_APP_DISTRO, 0, (LPARAM)r);
    return 0;
}

static void refreshDistros(App* app) {
    app->model.distroBusy = true;
    app->model.distroLoading = true;
    refreshUi(app);

    refreshOnline(app);

    if (app->distroThread) {
        CloseHandle(app->distroThread);
        app->distroThread = NULL;
    }
    app->distroThread = CreateThread(NULL, 0, DistroThread, app, 0, NULL);
}

// 目录名沿用历史名 WslEmbed：发行版目录的路径已经写进 Lxss 注册表的 BasePath，
// 改名会让已导入的发行版全部失效，所以例外地保留旧名，只有界面/产物叫 EWSL。
static std::wstring settingsPath() {
    WCHAR buf[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH)) return std::wstring();
    std::wstring dir = std::wstring(buf) + L"\\WslEmbed";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\settings.ini";
}

static void loadSettings(App* app) {
    std::wstring p = settingsPath();
    if (p.empty()) return;

    int f = (int)GetPrivateProfileIntW(L"ui", L"fontSize", 14, p.c_str());
    int tf = (int)GetPrivateProfileIntW(L"terminal", L"fontSize", -1, p.c_str());
    int th = (int)GetPrivateProfileIntW(L"ui", L"theme", 0, p.c_str());
    int tm = (int)GetPrivateProfileIntW(L"ui", L"terminalMode", 0, p.c_str());
    int cok = (int)GetPrivateProfileIntW(L"ui", L"conptyOk", -1, p.c_str());
    int lang = (int)GetPrivateProfileIntW(L"ui", L"language", LANG_AUTO, p.c_str());

    if (tf < 0) tf = f;

    if (f < 10) f = 10;
    if (f > 30) f = 30;
    if (tf < 8) tf = 8;
    if (tf > 40) tf = 40;
    if (th != 0 && th != 1) th = 0;
    if (tm < 0 || tm > 2) tm = 0;
    if (cok < -1 || cok > 1) cok = -1;
    if (lang < LANG_AUTO || lang > LANG_EN) lang = LANG_AUTO;

    app->model.fontSize = f;
    app->model.termFontSize = tf;
    app->model.theme = th;
    app->model.terminalMode = tm;
    app->conptyOk = cok;
    app->model.lang = lang;

    // Language has to be settled before anything can build a label, so this is
    // the one thing loadSettings does that is not just filling in the model.
    langInit(lang);

    WCHAR broken[2048];
    broken[0] = 0;
    GetPrivateProfileStringW(L"distros", L"broken", L"", broken, 2048, p.c_str());
    std::wstring bs(broken);
    size_t start = 0;
    while (start <= bs.size()) {
        size_t sep = bs.find(L';', start);
        if (sep == std::wstring::npos) sep = bs.size();
        std::wstring one = bs.substr(start, sep - start);
        if (!one.empty()) app->brokenDistros.push_back(one);
        if (sep == bs.size()) break;
        start = sep + 1;
    }
}

static void saveSettings(App* app) {
    std::wstring p = settingsPath();
    if (p.empty()) return;

    wchar_t buf[32];
    wsprintfW(buf, L"%d", app->model.fontSize);
    WritePrivateProfileStringW(L"ui", L"fontSize", buf, p.c_str());
    wsprintfW(buf, L"%d", app->model.termFontSize);
    WritePrivateProfileStringW(L"terminal", L"fontSize", buf, p.c_str());
    // Tab 宽度那一档已经删掉了，顺手把老配置里的键也清掉。
    WritePrivateProfileStringW(L"ui", L"tabWidth", NULL, p.c_str());
    wsprintfW(buf, L"%d", app->model.theme);
    WritePrivateProfileStringW(L"ui", L"theme", buf, p.c_str());
    wsprintfW(buf, L"%d", app->model.terminalMode);
    WritePrivateProfileStringW(L"ui", L"terminalMode", buf, p.c_str());

    wsprintfW(buf, L"%d", app->conptyOk);
    WritePrivateProfileStringW(L"ui", L"conptyOk", buf, p.c_str());

    wsprintfW(buf, L"%d", app->model.lang);
    WritePrivateProfileStringW(L"ui", L"language", buf, p.c_str());

    std::wstring joined;
    for (size_t i = 0; i < app->brokenDistros.size(); ++i) {
        if (i) joined += L";";
        joined += app->brokenDistros[i];
    }
    WritePrivateProfileStringW(L"distros", L"broken", joined.c_str(), p.c_str());
}

// 终端底色跟着界面主题走：浅色界面 = 白底深字，深色界面 = 原来那套暗色。
// 画面里每个格子都存了颜色快照，所以这里要顺手把缓冲区重映射一遍。
static void applyTerminalTheme(App* app) {
    if (!app) return;
    int want = (app->model.theme == 1) ? 0 : 1;
    int have = terminalTheme();
    if (want == have) return;

    ThemePalette from = themePalette(have);
    ThemePalette to   = themePalette(want);
    setTerminalTheme(want);
    if (app->term) app->term->retheme(from, to);
}

static bool isExpandedDir(App* app, const std::wstring& p) {
    for (size_t i = 0; i < app->expandedDirs.size(); ++i) {
        if (lstrcmpiW(app->expandedDirs[i].c_str(), p.c_str()) == 0) return true;
    }
    return false;
}

static void toggleExpandDir(App* app, const std::wstring& p) {
    for (size_t i = 0; i < app->expandedDirs.size(); ++i) {
        if (lstrcmpiW(app->expandedDirs[i].c_str(), p.c_str()) == 0) {
            app->expandedDirs.erase(app->expandedDirs.begin() + (int)i);
            return;
        }
    }
    app->expandedDirs.push_back(p);
}

static void addDirToTree(App* app, const std::wstring& dir, int depth) {
    if (depth > 10) return;

    std::vector<DirEntry> es = listDirectory(dir);
    int count = 0;

    for (size_t i = 0; i < es.size(); ++i) {
        if (++count > 500) break;

        TreeRow r;
        r.name     = es[i].name;
        r.path     = es[i].path;
        r.depth    = depth;
        r.isDir    = es[i].isDir;
        r.expanded = es[i].isDir && isExpandedDir(app, es[i].path);
        r.isOpenFile = (app->ed->isOpen() &&
                        lstrcmpiW(app->ed->path().c_str(), es[i].path.c_str()) == 0);

        app->model.tree.push_back(r);

        if (r.isDir && r.expanded) addDirToTree(app, es[i].path, depth + 1);
    }
}

static void rebuildTree(App* app) {
    app->model.tree.clear();
    if (app->folder.empty()) return;
    addDirToTree(app, app->folder, 0);
}

static void buildModel(App* app) {
    UiModel& m = app->model;

    m.page = app->page;
    m.hasFolder = !app->folder.empty();
    m.folderPath = app->folder;

    m.editorOpen = app->ed->isOpen();
    m.editorDirty = app->ed->dirty();
    m.editorName = app->ed->isOpen() ? app->ed->fileName() : std::wstring();
    m.editorLang = app->ed->isOpen() ? langName(app->ed->lang()) : std::wstring();
    m.wslPath = cachedWslPath();
    m.terminalMode = app->model.terminalMode;
    m.activeMode = app->model.hasTerminal ? app->transport : -1;
    m.instLocked = installRunning(app);
    m.unregisterBusy = app->unregisterBusy;
    m.brokenDistros = app->brokenDistros;

    if (app->page == PAGE_TERMINAL) {
        m.title = m.activeDistro.empty() ? LS(L"终端") : (LS(L"终端 · ") + m.activeDistro);
        m.hint = m.hasTerminal ? std::wstring() : L"WSL";
    } else if (app->page == PAGE_PROJECT) {
        m.title = m.editorOpen ? (m.editorName + LS(L" — 项目")) : LS(L"项目");
        m.hint = app->folder;
    } else if (app->page == PAGE_DISTRO) {
        m.title = LS(L"发行版");
        m.hint = m.wslPath.empty() ? std::wstring(LS(L"未找到 wsl.exe")) : std::wstring();
    } else if (app->page == PAGE_INSTALL) {
        m.title = LS(L"安装 ") + (m.instName.empty() ? std::wstring(LS(L"发行版")) : m.instName);
        m.hint = m.instStage == INST_IDLE ? std::wstring() : LS(L"安装");
    } else {
        m.title = LS(L"设置");
        m.hint.clear();
    }

}

static void refreshUi(App* app) {
    buildModel(app);
    if (app->hwnd) InvalidateRect(app->hwnd, NULL, FALSE);
}

static void notifyPtySize(App* app);

static void refitTerminal(App* app) {
    RECT rc;
    GetClientRect(app->hwnd, &rc);
    int l, t, r, b;
    app->ui->contentRect(rc.right, rc.bottom, l, t, r, b, app->model.page);
    int cols = (r - l) / app->rend->cellW();
    int rows = (b - t) / app->rend->cellH();
    // 同syncTermSize：列数上限于实际可绘制宽度，不能靠抬到 20 来"补齐"
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    if (app->term) app->term->resize(cols, rows);
    if (app->hpc && app->fnResize) {
        COORD sz;
        sz.X = (SHORT)cols;
        sz.Y = (SHORT)rows;
        app->fnResize(app->hpc, sz);
    }
    // 改字号/ 开编辑器都会走到这里，列数变了 guest 必须知道，
    // 否则它按旧列数折行、我们按新列数画，又会错位。
    notifyPtySize(app);

    {
        static DWORD lastSave = 0;
        DWORD now = GetTickCount();
        if ((DWORD)(now - lastSave) >= 400) {
            lastSave = now;
            saveSettings(app);
        }
    }
    refreshUi(app);
}

static void applyUiFont(App* app, int px) {
    if (px < 10) px = 10;
    if (px > 30) px = 30;
    if (px == app->model.fontSize) return;

    app->model.fontSize = px;
    app->ui->setUiFontSize(px);
    saveSettings(app);
    refreshUi(app);
}

static void applyTermFont(App* app, int px) {
    if (px < 8) px = 8;
    if (px > 40) px = 40;
    if (px == app->model.termFontSize) return;

    app->model.termFontSize = px;
    app->rend->create(px);
    app->ui->prepare(px);
    refitTerminal(app);
}

static int chooseTransport(const App* app) {
    // Auto (0) and "force compatibility" (2) both use the pipe bridge. WSL only
    // gives the guest a tty when wsl.exe owns a Windows console, and ConPTY has
    // proven unreliable here, so the pipe bridge - which allocates a real Linux
    // pty itself - is the default. ConPTY is still reachable explicitly (1).
    if (app->model.terminalMode == 1) return TRANSPORT_CONPTY;
    return TRANSPORT_PIPE;
}

// ---------------------------------------------------------------------------
// Guest-side pty bridge.
//
// When wsl.exe has pipes for stdin/stdout the guest process gets a plain pipe,
// not a terminal. A shell started that way has no line discipline at all:
//
//   * CR is not a line terminator, so answering a prompt such as dnf's
//     "Is this ok [y/N]:" is impossible - the program waits forever, which is
//     exactly the "terminal is stuck" symptom;
//   * isatty() is false, so progress bars, colours, pagers and editors degrade;
//   * there is no foreground process group, so Ctrl+C cannot deliver SIGINT;
//   * the pty size is unknown, so programs wrap at the wrong column.
//
// util-linux's script(1) fixes all of that, but Fedora's WSL image ships
// without it. python3 is present in every mainstream image and its pty module
// wraps forkpty(), so allocate the terminal with python and relay the bytes
// ourselves. The relay also understands one private request,
//
//     ESC ] 9999 ; <cols> x <rows> BEL
//
// sent on stdin, which it consumes instead of forwarding, so the pty follows
// the window as it is resized. On startup the bridge emits
//
//     ESC ] 9998 ; wemb-pty BEL
//
// on its output; the terminal emulator swallows OSC sequences it does not know,
// so the marker is invisible and doubles as the handshake that tells us the
// bridge is live (only then is it safe to send resize requests).

static std::string numStr(int v) {
    char buf[24];
    int n = 0;
    if (v < 0) { buf[n++] = '-'; v = -v; }
    char tmp[16];
    int m = 0;
    if (v == 0) tmp[m++] = '0';
    while (v > 0) { tmp[m++] = (char)('0' + (v % 10)); v /= 10; }
    while (m > 0) buf[n++] = tmp[--m];
    buf[n] = 0;
    return std::string(buf, (size_t)n);
}

static std::string b64Encode(const std::string& in) {
    static const char* kTab =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() / 3 + 1) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) |
                     ((unsigned char)in[i + 1] << 8) |
                     (unsigned char)in[i + 2];
        out.push_back(kTab[(v >> 18) & 63]);
        out.push_back(kTab[(v >> 12) & 63]);
        out.push_back(kTab[(v >> 6) & 63]);
        out.push_back(kTab[v & 63]);
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned)((unsigned char)in[i] << 16);
        out.push_back(kTab[(v >> 18) & 63]);
        out.push_back(kTab[(v >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned)((unsigned char)in[i] << 16)) |
                     ((unsigned)((unsigned char)in[i + 1]) << 8);
        out.push_back(kTab[(v >> 18) & 63]);
        out.push_back(kTab[(v >> 12) & 63]);
        out.push_back(kTab[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

static std::wstring asciiToWide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

static std::string ptyBridgeSource(int cols, int rows) {
    std::string p;
    p += "import os,pty,select,sys,struct,fcntl,termios\n";
    p += "W=" + numStr(cols) + "\n";
    p += "H=" + numStr(rows) + "\n";
    p += "M=b'\\x1b]9999;'\n";
    p += "def rz(f):\n";
    // ioctl 失败不要静默吞掉：尺寸设不上，guest 就一直按默认 80 列折行，
    // 我们按真实列数画，两边错位之后提示符会插在行中间。
    // 往 stderr 写一行，宿主侧能从管道日志里看到。
    p += " try: fcntl.ioctl(f,termios.TIOCSWINSZ,struct.pack('HHHH',H,W,0,0))\n";
    p += " except Exception as e: os.write(2,('winsz %dx%d: %s\\n'%(H,W,e)).encode())\n";
    p += "pid,fd=pty.fork()\n";
    p += "if pid==0:\n";
    p += " os.environ['TERM']='xterm-256color'\n";
    p += " os.environ['COLORTERM']='truecolor'\n";
    p += " try: os.execv('/bin/bash',['/bin/bash','-l','-i'])\n";
    p += " except Exception:\n";
    p += "  try: os.execv('/bin/sh',['/bin/sh','-l','-i'])\n";
    p += "  except Exception: pass\n";
    p += " os._exit(127)\n";
    p += "rz(fd)\n";
    p += "os.write(sys.stdout.fileno(),b'\\x1b]9998;wemb-pty\\x07')\n";
    p += "IF=sys.stdin.fileno()\n";
    p += "OF=sys.stdout.fileno()\n";
    p += "buf=b''\n";
    p += "while True:\n";
    p += " try: r,_,_=select.select([IF,fd],[],[])\n";
    p += " except Exception: break\n";
    p += " if fd in r:\n";
    p += "  try: d=os.read(fd,65536)\n";
    p += "  except OSError: break\n";
    p += "  if not d: break\n";
    p += "  try: os.write(OF,d)\n";
    p += "  except OSError: break\n";
    p += " if IF in r:\n";
    p += "  try: d=os.read(IF,65536)\n";
    p += "  except OSError: break\n";
    p += "  if not d: break\n";
    p += "  buf+=d\n";
    p += "  out=b''\n";
    p += "  while True:\n";
    p += "   i=buf.find(M)\n";
    p += "   if i<0:\n";
    p += "    out+=buf\n";
    p += "    buf=b''\n";
    p += "    break\n";
    p += "   out+=buf[:i]\n";
    p += "   j=buf.find(b'\\x07',i)\n";
    p += "   if j<0:\n";
    p += "    buf=buf[i:]\n";
    p += "    break\n";
    p += "   q=buf[i+len(M):j]\n";
    p += "   buf=buf[j+1:]\n";
    p += "   try:\n";
    p += "    a=q.split(b'x')\n";
    p += "    W=int(a[0])\n";
    p += "    H=int(a[1])\n";
    p += "    rz(fd)\n";
    p += "   except Exception: pass\n";
    p += "  if out:\n";
    p += "   try: os.write(fd,out)\n";
    p += "   except OSError: break\n";
    p += "try: os.close(fd)\n";
    p += "except Exception: pass\n";
    return p;
}

static std::string g_ptyBridgeB64;

static std::wstring interactiveArgs(App* app, const std::wstring& distro) {
    int cols = 100;
    int rows = 30;
    if (app && app->term) {
        cols = app->term->cols();
        rows = app->term->rows();
        // 不要在这里「抬到下限」。bridge 拿这个值去开 guest 的 pty，
        // 抬过之后 guest 以为的列数就比我们能画的多，它在多出来的列上折行、
        // 我们在真实边界上折，两边错位，提示符会插在行中间。
        // 宁可给个偏小的值——syncTermSize 随后会再通知一次真正的尺寸。
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;
    }
    g_ptyBridgeB64 = b64Encode(ptyBridgeSource(cols, rows));

    std::wstring a;
    if (!distro.empty()) a = L"-d " + distroArg(distro);

    // 两条路径。
    //
    // 有 python：走 bridge 脚本，它自己管 pty、能解析 \x1b]9999;CxR\x07
    // 动态改尺寸，交互最干净。
    //
    // 没 python（本机 archlinux 就是这样，`pacman -Q python` 查无此包）：
    // 退到 script。**script 有个必须处理的坑**——它的 pty 尺寸继承自自己的 stdin，
    // 而 stdin 是我们从 Windows 喂进去的管道，实测尺寸是 **0 0**：
    //     printf '' | script -qfc 'stty size' /dev/null   ->   0 0
    // `stty sane` 只重置控制标志、不碰尺寸，所以 bash 拿到的是 0 列，
    // readline 的每一列计算都错——症状就是提示符位置乱、长行折行错位、
    // 新旧内容交错（这几轮反复出现的现象，根因就在这里）。
    // 所以必须显式 stty rows/cols 把真实尺寸灌进去。
    //
    // 已知残留问题（script 分支的固有限制，装 python 可彻底消除）：
    //   - script 会把 stdin 原样回显一遍（管道输入时尤其明显）
    //   - 不认 \x1b]9999;CxR\x07，窗口缩放后 guest 尺寸不更新
    // 这两条都比「列数为 0」轻得多。
    WCHAR rowsW[32], colsW[32];
    _snwprintf(rowsW, 32, L"%d", rows);
    _snwprintf(colsW, 32, L"%d", cols);
    std::wstring sttyCmd = L"stty rows ";
    sttyCmd += rowsW;
    sttyCmd += L" cols ";
    sttyCmd += colsW;
    sttyCmd += L" 2>/dev/null; stty sane 2>/dev/null; exec /bin/bash -l -i";

    // LINES/COLUMNS 是 bash/readline 的环境兜底：万一 stty 那步失败
    // （某些 script 实现会吞掉 stty），readline 至少还能从这里拿到列数。
    std::wstring sh =
        L"export LINES=";
    sh += rowsW;
    sh += L" COLUMNS=";
    sh += colsW;
    sh += L"; ";
    sh += L"if P=$(command -v python3 || command -v python); then exec $P -c "
          L"'import sys,base64;exec(base64.b64decode(sys.argv[1]).decode())' '";
    sh += asciiToWide(g_ptyBridgeB64);
    sh += L"'; fi; "
          L"if command -v script >/dev/null 2>&1; then exec env "
          L"TERM=xterm-256color script -qfc '";
    sh += sttyCmd;
    sh += L"' /dev/null; fi; "
          L"exec env TERM=xterm-256color /bin/bash -l -i";

    a += L" -e sh -c \"";
    a += sh;
    a += L"\"";
    return a;
}

static void notifyPtySize(App* app) {
    if (!app || !app->term) return;
    if (app->transport != TRANSPORT_PIPE) return;
    if (!app->hPipeWrite) return;
    // ptyBridge 这个门槛必须留着：bridge 脚本还没跑到读 stdin 那一段时，
    // OSC 会被当成普通输入喂给 guest，readline 直接把 ^[[9999;193x56^G
    // 显示在命令行上（试过去掉，泄漏得很明显）。
    // 尺寸同步改由「bridge 回执之后补发一次」保证，见 detectPtyBridge。
    if (!app->ptyBridge) return;

    std::string msg = "\x1b]9999;";
    msg += numStr(app->term->cols());
    msg += "x";
    msg += numStr(app->term->rows());
    msg += "\x07";
    writePty(app, msg.data(), (int)msg.size());
}

static bool containsAscii(const char* hay, size_t n, const char* needle) {
    size_t m = strlen(needle);
    if (m == 0 || n < m) return false;
    for (size_t i = 0; i + m <= n; ++i) {
        if (memcmp(hay + i, needle, m) == 0) return true;
    }
    return false;
}

// The bridge announces itself with an OSC sequence the terminal already
// ignores. A read can end anywhere, so keep a small tail around until the
// marker is complete - only then may resize requests be sent on stdin.
static std::string g_probeBuf;
static bool g_probeHit = false;

static void resetPtyProbe() {
    g_probeBuf.clear();
    g_probeHit = false;
}

static void detectPtyBridge(App* app, const char* data, size_t len) {
    if (g_probeHit || !app || !data || len == 0) return;

    static const char kMark[] = "\x1b]9998;wemb-pty\x07";
    const size_t kMarkLen = sizeof(kMark) - 1;

    g_probeBuf.append(data, len);
    if (g_probeBuf.size() > 4096) {
        g_probeBuf.erase(0, g_probeBuf.size() - 4096);
    }
    if (!containsAscii(g_probeBuf.data(), g_probeBuf.size(), kMark)) {
        if (g_probeBuf.size() > 64) {
            g_probeBuf.erase(0, g_probeBuf.size() - 64);
        }
        return;
    }

    g_probeHit = true;
    g_probeBuf.clear();
    app->ptyBridge = true;
    notifyPtySize(app);
}

static bool isDistroBroken(const App* app, const std::wstring& id) {
    for (size_t i = 0; i < app->brokenDistros.size(); ++i) {
        if (lstrcmpiW(app->brokenDistros[i].c_str(), id.c_str()) == 0) return true;
    }
    for (size_t i = 0; i < app->sessionBad.size(); ++i) {
        if (lstrcmpiW(app->sessionBad[i].c_str(), id.c_str()) == 0) return true;
    }
    return false;
}

static void clearBrokenDistro(App* app, const std::wstring& id) {
    for (size_t i = 0; i < app->brokenDistros.size(); ) {
        if (lstrcmpiW(app->brokenDistros[i].c_str(), id.c_str()) == 0)
            app->brokenDistros.erase(app->brokenDistros.begin() + (int)i);
        else ++i;
    }
    for (size_t i = 0; i < app->sessionBad.size(); ) {
        if (lstrcmpiW(app->sessionBad[i].c_str(), id.c_str()) == 0)
            app->sessionBad.erase(app->sessionBad.begin() + (int)i);
        else ++i;
    }
}

static void syncOnlineInstalled(App* app) {
    for (size_t i = 0; i < app->model.online.size(); ++i) {
        bool inst = false;
        for (size_t j = 0; j < app->model.installed.size(); ++j) {
            if (lstrcmpiW(app->model.online[i].id.c_str(),
                          app->model.installed[j].c_str()) == 0) { inst = true; break; }
        }
        app->model.online[i].installed = inst;
    }
}

static void markDistroBroken(App* app, const std::wstring& id) {
    if (id.empty()) return;
    if (!isDistroBroken(app, id)) app->brokenDistros.push_back(id);

    for (size_t i = 0; i < app->model.installed.size(); ) {
        if (lstrcmpiW(app->model.installed[i].c_str(), id.c_str()) == 0)
            app->model.installed.erase(app->model.installed.begin() + (int)i);
        else ++i;
    }
    syncOnlineInstalled(app);
    saveSettings(app);
}

static void markDistroBadSession(App* app, const std::wstring& id) {
    if (id.empty()) return;

    bool known = false;
    for (size_t i = 0; i < app->sessionBad.size(); ++i) {
        if (lstrcmpiW(app->sessionBad[i].c_str(), id.c_str()) == 0) { known = true; break; }
    }
    if (!known) app->sessionBad.push_back(id);

    for (size_t i = 0; i < app->model.installed.size(); ) {
        if (lstrcmpiW(app->model.installed[i].c_str(), id.c_str()) == 0)
            app->model.installed.erase(app->model.installed.begin() + (int)i);
        else ++i;
    }
    syncOnlineInstalled(app);
}

#define WM_APP_UNREGISTER_DONE   (WM_APP + 40)
#define WM_APP_UNREG_PREP_DONE   (WM_APP + 41)

struct UnregArg {
    App*        app;
    std::wstring id;
    bool         ok;

    UnregArg() : app(NULL), ok(false) {}
};

static DWORD WINAPI UnregisterThread(LPVOID param) {
    UnregArg* a = (UnregArg*)param;
    if (a && a->app) {
        a->ok = wslUnregister(a->id);
        PostMessageW(a->app->hwnd, WM_APP_UNREGISTER_DONE, 0, (LPARAM)a);
    } else {
        delete a;
    }
    return 0;
}

static void startUnregisterThread(App* app, const std::wstring& id) {
    UnregArg* a = new UnregArg();
    a->app = app;
    a->id  = id;

    HANDLE h = CreateThread(NULL, 0, UnregisterThread, a, 0, NULL);
    if (!h) {
        delete a;
        app->unregisterBusy = false;
        refreshUi(app);
        return;
    }
    CloseHandle(h);
}

static bool unregisterDistro(const std::wstring& id, std::wstring* out);

struct PrepUnregArg {
    App*        app;
    std::wstring name;
    bool         ok;
    std::wstring out;

    PrepUnregArg() : app(NULL), ok(false) {}
};

static DWORD WINAPI PrepUnregisterThread(LPVOID param) {
    PrepUnregArg* a = (PrepUnregArg*)param;
    if (a && a->app) {
        a->ok  = unregisterDistro(a->name, &a->out);
        PostMessageW(a->app->hwnd, WM_APP_UNREG_PREP_DONE, 0, (LPARAM)a);
    } else {
        delete a;
    }
    return 0;
}

static bool unregisterDistro(const std::wstring& id, std::wstring* out) {
    std::wstring exe = findWslExe();
    if (exe.empty()) { if (out) *out = LS(L"未找到 wsl.exe"); return false; }

    runCapture(L"\"" + exe + L"\" --terminate " + distroArg(id), 20000);

    for (int attempt = 0; attempt < 3; ++attempt) {
        std::wstring res = runCapture(L"\"" + exe + L"\" --unregister " + distroArg(id), 60000);
        if (out) *out = trimW(res);

        if (res.find(L"WSL_E_") == std::wstring::npos &&
            (res.find(L"0x") == std::wstring::npos ||
             res.find(L"unregister") != std::wstring::npos)) {
            return true;
        }
        Sleep(700);
    }
    return false;
}

static bool launchTerminal(App* app, const std::wstring& distro) {
    if (app->term) app->term->hardReset();
    app->sel.clear();
    app->scrollOffset = 0;
    app->autoFallbackTried = false;

    if (!distro.empty()) {
        bool known = false;
        for (size_t i = 0; i < app->model.installed.size(); ++i) {
            if (lstrcmpiW(app->model.installed[i].c_str(), distro.c_str()) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            app->page = PAGE_TERMINAL;
            app->model.hasTerminal = false;
            app->model.activeDistro = distro;

            termPrint(app, LS(L"\r\n[EWSL] 发行版 ") + distro + LS(L" 没有可用的注册信息。\r\n\r\n"));
            if (app->model.installed.empty()) {
                termPrint(app, LS(L"  WSL 里当前没有任何发行版。\r\n"));
            } else {
                termPrint(app, LS(L"  WSL 当前可用的发行版：\r\n"));
                for (size_t i = 0; i < app->model.installed.size(); ++i) {
                    termPrint(app, L"    · " + app->model.installed[i] + L"\r\n");
                }
            }
            termPrint(app, LS(L"\r\n  直接执行 wsl -d ") + distro + LS(L" 会返回 Wsl/Service/WSL_E_DISTRO_NOT_FOUND，\r\n  说明它只在注册表里留了名字，实际启动不了（常见于上次导入没跑完）。\r\n\r\n  修复：点上方发行版菜单 → 选 ") + distro + LS(L" → 「重新安装」，\r\n        安装页会先清掉同名旧注册项，再重新下载导入。\r\n  手动清理：wsl --unregister ") + distro + L"\r\n");

            showToast(app, LS(L"发行版 ") + distro + LS(L" 的注册信息不可用，可从菜单重新安装"));
            app->model.distroMenuOpen = true;
            syncOnlineInstalled(app);
            refreshUi(app);
            return false;
        }
    }

    int tp = chooseTransport(app);

    std::wstring args;
    if (tp == TRANSPORT_PIPE) {
        args = interactiveArgs(app, distro);
        app->ptyBridge = false;
        resetPtyProbe();
    } else if (!distro.empty()) {
        args = L"-d " + distroArg(distro);
    }

    if (!startSession(app, args, tp)) {
        app->page = PAGE_TERMINAL;
        app->model.hasTerminal = true;
        app->model.installing = false;
        app->model.activeDistro = distro;

        std::wstring exe = cachedWslPath();
        termPrint(app, LS(L"\r\n无法启动 wsl.exe\r\n\r\n"));
        termPrint(app, LS(L"  检测到的位置: ") + (exe.empty() ? std::wstring(LS(L"未找到")) : exe) + L"\r\n");
        termPrint(app, LS(L"  目标发行版: ") + (distro.empty() ? std::wstring(LS(L"(默认)")) : distro) + L"\r\n\r\n");
        termPrint(app, LS(L"排查建议\r\n"));
        termPrint(app, LS(L"  1. 在 PowerShell 中执行 wsl --status 确认 WSL 已启用\r\n"));
        termPrint(app, LS(L"  2. 执行 wsl -l -v 确认发行版名称与本程序识别的一致\r\n"));
        termPrint(app, LS(L"  3. 若未安装发行版，展开左侧「终端」菜单选择安装\r\n"));
        return false;
    }

    app->model.hasTerminal = true;
    app->model.installing = false;
    app->model.activeDistro = distro;

    if (app->hwnd) SetTimer(app->hwnd, TIMER_PTYWAIT, 3000, NULL);

    std::wstring exe = findWslExe();
    if (exe.empty()) {
        termPrint(app, LS(L"[EWSL] 未找到 wsl.exe\r\n"));
    }
    return true;
}

static bool parseNoLaunchSupport(const std::wstring& v) {
    if (v.size() < 8) return false;

    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < v.size(); ++i) {
        wchar_t c = v[i];
        if (c == L'\r' || c == L'\n') {
            if (!cur.empty()) lines.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back((c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c);
    }
    if (!cur.empty()) lines.push_back(cur);

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring& ln = lines[i];
        if (ln.size() > 80) continue;
        if (ln.find(L"wsl.exe") != std::wstring::npos) continue;
        if (ln.find(L"\u7528\u6cd5") != std::wstring::npos) continue;
        if (ln.find(L"usage") != std::wstring::npos) continue;

        size_t at = ln.find(L"version");
        if (at == std::wstring::npos) at = ln.find(L"\u7248\u672c");
        if (at == std::wstring::npos) continue;

        size_t p = at;
        while (p < ln.size() && !(ln[p] >= L'0' && ln[p] <= L'9')) ++p;
        if (p >= ln.size()) continue;

        size_t dot = ln.find(L'.', p);
        if (dot == std::wstring::npos) continue;

        int major = 0;
        size_t q = p;
        while (q < ln.size() && ln[q] >= L'0' && ln[q] <= L'9') {
            major = major * 10 + (int)(ln[q] - L'0');
            ++q;
        }

        return (major >= 2);
    }

    return false;
}

static LONG g_noLaunch = -1;

static DWORD WINAPI NoLaunchProbe(LPVOID) {
    std::wstring exe = cachedWslPath();
    bool ok = false;
    if (!exe.empty()) {
        std::wstring v = runCapture(L"\"" + exe + L"\" --version", 8000);
        ok = parseNoLaunchSupport(v);
    }
    InterlockedExchange(&g_noLaunch, ok ? 1 : 0);
    return 0;
}

static bool wslSupportsNoLaunch() {
    LONG v = InterlockedCompareExchange(&g_noLaunch, -2, -1);
    if (v == -1) {
        HANDLE h = CreateThread(NULL, 0, NoLaunchProbe, NULL, 0, NULL);
        if (h) CloseHandle(h);
        return false;
    }
    if (v == -2) return false;
    return v != 0;
}

static void instSetStage(App* app, int stage, const std::wstring& text,
                         const std::wstring& detail) {
    app->instStage = stage;
    app->model.instStage = stage;
    app->model.instStageText = text;
    app->model.instDetail = detail;
    app->model.instCancelable =
        (stage == INST_RESOLVE || stage == INST_DOWNLOAD || stage == INST_IMPORT ||
         stage == INST_VERIFY);

    if (stage == INST_RESOLVE || stage == INST_DOWNLOAD || stage == INST_IMPORT ||
        stage == INST_VERIFY) {
        if (app->hwnd) SetTimer(app->hwnd, TIMER_ANIM, 16, NULL);
        app->animTick = GetTickCount();
    }
}

static std::wstring labelFor(const std::wstring& id) {
    const DistroImage* img = findImage(id);
    if (img) return img->name;
    return id;
}

static bool installRunning(const App* app) {
    if (app->instThread) return true;
    if (app->jobMode && app->pendingInstall.size()) return true;
    switch (app->instStage) {
    case INST_RESOLVE:
    case INST_DOWNLOAD:
    case INST_IMPORT:
    case INST_VERIFY:
    case INST_UNREGISTER:
        return true;
    default:
        return false;
    }
}

static void drainInstallMessages(App* app) {
    if (!app->hwnd) return;

    MSG m;
    while (PeekMessageW(&m, app->hwnd, WM_APP_DL_DONE, WM_APP_DL_DONE, PM_REMOVE)) {
        std::wstring* p = (std::wstring*)m.lParam;
        delete p;
    }
    while (PeekMessageW(&m, app->hwnd, WM_APP_PTY_EXIT, WM_APP_PTY_EXIT, PM_REMOVE)) {
    }
    while (PeekMessageW(&m, app->hwnd, WM_APP_PROBE, WM_APP_PROBE, PM_REMOVE)) {
        ProbeResult* pr = (ProbeResult*)m.lParam;
        delete pr;
    }
}

static void abortInstall(App* app) {
    if (app->instThread) {
        InterlockedExchange(&app->instCancel, 1);
        DWORD w = WaitForSingleObject(app->instThread, 5000);
        if (w == WAIT_TIMEOUT) {
            TerminateThread(app->instThread, 0);
            WaitForSingleObject(app->instThread, 1000);
        }
        CloseHandle(app->instThread);
        app->instThread = NULL;
    }

    if (app->jobMode && !app->pendingInstall.empty()) {
        app->jobMode = false;
        app->pendingInstall.clear();
        app->pendingLabel.clear();
        stopPty(app);
    }

    InterlockedExchange(&app->instCancel, 0);
    app->prepUnregistering = false;
    drainInstallMessages(app);
}

static void clearPartialFile(const std::wstring& path) {
    if (path.empty()) return;
    for (int i = 0; i < 8; ++i) {
        if (DeleteFileW(path.c_str())) return;
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return;
        Sleep(90);
    }
}

static void showToast(App* app, const std::wstring& text) {
    app->model.toast = text;
    app->model.toastTick = GetTickCount();
    if (app->hwnd) SetTimer(app->hwnd, TIMER_ANIM, 16, NULL);
}

static bool instTick(void* user, const DownloadProgress& p) {
    App* app = (App*)user;

    app->model.instGot = p.got;
    app->model.instTotal = p.total;
    app->model.instPercent = p.percent;

    if (app->hwnd) PostMessageW(app->hwnd, WM_APP_DL_TICK, 0, 0);
    return InterlockedCompareExchange(&app->instCancel, 0, 0) == 0;
}

static DWORD WINAPI DownloadThread(LPVOID param) {
    App* app = (App*)param;

    std::wstring err;
    bool ok = httpDownload(app->instUrl, app->instFile, instTick, app, err,
                           &app->instCancel);

    std::wstring* out = new std::wstring(err);
    if (app->hwnd) {
        PostMessageW(app->hwnd, WM_APP_DL_DONE, ok ? 1 : 0, (LPARAM)out);
    } else {
        delete out;
    }
    return 0;
}

static void startDownload(App* app) {
    abortInstall(app);
    clearPartialFile(app->instFile);

    instSetStage(app, INST_DOWNLOAD, LS(L"正在下载镜像…"), L"");
    app->model.instPercent = -1;
    app->model.instGot = 0;
    app->model.instTotal = 0;
    app->ui->resetBarAnim();
    InterlockedExchange(&app->instCancel, 0);
    app->instThread = CreateThread(NULL, 0, DownloadThread, app, 0, NULL);
}

static void beginStoreInstall(App* app, const std::wstring& id,
                              const std::wstring& label,
                              const std::wstring& reason) {
    app->model.instLog.clear();
    jobLog(app, LS(L"使用 WSL 官方安装通道：") + id);

    if (!reason.empty()) jobLog(app, L"!" + reason);

    std::wstring args = L"--install -d " + distroArg(id);
    bool noLaunch = wslSupportsNoLaunch();
    if (noLaunch) args += L" --no-launch";
    jobLog(app, L"> wsl " + args);

    app->instDir.clear();
    app->model.instUrl.clear();

    if (!startJob(app, args)) {
        app->model.instErr = LS(L"无法启动 wsl.exe。请确认已启用 WSL（wsl --install --no-distribution）。");
        instSetStage(app, INST_FAILED, LS(L"安装失败"), app->model.instErr);
        app->model.instCancelable = false;
        return;
    }

    app->jobMode = true;
    app->pendingInstall = id;
    app->pendingLabel = label;
    app->pendingStart = id;
    app->model.installing = true;
    instSetStage(app, INST_DOWNLOAD, LS(L"正在通过 WSL 下载 ") + label + L"…",
                 noLaunch ? LS(L"下载完成后自动注册，不会自动进入") : LS(L"当前 WSL 版本较旧，安装后可能自动进入"));
}

static void runImport(App* app, int version) {
    wchar_t vbuf[16];
    wsprintfW(vbuf, L"%d", version);

    std::wstring args = L"--import " + distroArg(app->instDistro) + L" \"" + app->instDir +
                        L"\" \"" + app->instFile + L"\" --version " + vbuf;

    app->model.instLog.clear();
    jobLog(app, L"> wsl " + args);

    if (!startJob(app, args)) {
        app->model.instErr = LS(L"无法启动 wsl.exe 执行导入。");
        instSetStage(app, INST_FAILED, LS(L"导入失败"), app->model.instErr);
        return;
    }

    app->jobMode = true;
    app->instAttempt = version;
    instSetStage(app, INST_IMPORT, LS(L"正在导入到 WSL…（首次导入需要几十秒）"),
                 LS(L"镜像 ") + app->instFile);
}

static void beginInstall(App* app, const std::wstring& id) {
    abortInstall(app);

    if (app->term) app->term->hardReset();
    app->sel.clear();
    app->scrollOffset = 0;
    app->pendingInstall.clear();
    app->pendingStart.clear();
    app->model.instLog.clear();
    app->model.instErr.clear();
    app->model.instHint.clear();
    app->model.instName = labelFor(id);
    app->instUserCancel = false;
    app->ui->resetBarAnim();

    if (id.empty()) {
        app->model.instErr = LS(L"未选择发行版");
        instSetStage(app, INST_FAILED, LS(L"未选择发行版"), L"");
        gotoPage(app, PAGE_INSTALL);
        refreshUi(app);
        return;
    }

    {
        int from = app->page;
        app->page = PAGE_INSTALL;
        app->model.distroMenuOpen = false;
        app->ui->beginPageTransition(from, PAGE_INSTALL);
        if (app->hwnd) SetTimer(app->hwnd, TIMER_ANIM, 16, NULL);
    }

    app->instDistro = id;
    app->instLabel = app->model.instName;
    app->model.instPercent = -1;
    app->model.instGot = 0;
    app->model.instTotal = 0;
    app->model.instUrl.clear();
    app->model.instHint.clear();
    instSetStage(app, INST_RESOLVE, LS(L"正在解析镜像地址…"), L"");
    refreshUi(app);

    const DistroImage* img = findImage(id);
    std::wstring url;
    if (img) {
        HostArch arch = hostArch();
        url = (arch == ARCH_ARM64) ? img->arm64 : img->amd64;
        if (url.empty() && arch == ARCH_ARM64) url = img->amd64;
    }

    {
        wchar_t override_[1024];
        if (GetEnvironmentVariableW(L"WSLEMBED_IMAGE_URL", override_, 1024)) {
            url = trimW(override_);
        }
    }

    if (url.empty()) {
        beginStoreInstall(app, id, app->instLabel,
                          img ? LS(L"内置目录里没有适配本机架构的直链镜像，改用 wsl --install")
                              : LS(L"该发行版没有独立镜像包，改用 wsl --install"));
        refreshUi(app);
        return;
    }

    std::wstring dir = defaultInstallDir(id);
    if (dir.empty()) {
        app->model.instErr = LS(L"无法创建安装目录（%LOCALAPPDATA% 不可写）");
        instSetStage(app, INST_FAILED, LS(L"准备失败"), app->model.instErr);
        refreshUi(app);
        return;
    }

    std::wstring name = sanitizeDistroName(id);
    app->instDistro = name;
    app->instDir = dir;
    app->instFile = dir + L"\\image.wsl";
    app->instUrl = url;
    app->model.instUrl = url;

    jobLog(app, LS(L"发行版：") + app->instLabel + LS(L"（") + id + LS(L"）"));
    jobLog(app, LS(L"下载到：") + app->instFile);
    jobLog(app, LS(L"安装到：") + dir);
    jobLog(app, L"");
    jobLog(app, L"> GET " + url);

    {
        bool mayExist = isDistroBroken(app, name);
        for (size_t i = 0; i < app->model.installed.size(); ++i) {
            if (lstrcmpiW(app->model.installed[i].c_str(), name.c_str()) == 0) {
                mayExist = true;
                break;
            }
        }

        if (mayExist) {
            jobLog(app, L"");
            jobLog(app, LS(L"!检测到同名旧注册项 ") + name + LS(L"，正在后台清理…"));

            PrepUnregArg* a = new PrepUnregArg();
            a->app  = app;
            a->name = name;

            app->prepUnregistering = true;
            HANDLE h = CreateThread(NULL, 0, PrepUnregisterThread, a, 0, NULL);
            if (h) {
                CloseHandle(h);
            } else {
                delete a;
                jobLog(app, LS(L"!注销线程启动失败，仍继续导入（可能因重名而失败）"));
            }

            return;
        }
    }

    startDownload(app);
    refreshUi(app);
}

static void refreshOnline(App* app) {
    if (app->model.loadingOnline || !app->model.online.empty()) return;
    app->model.loadingOnline = true;
    CreateThread(NULL, 0, OnlineThread, app, 0, NULL);
}

static void copySelection(App* app) {
    if (!app->sel.active || !app->term) return;

    const int cols = app->term->cols();
    std::wstring out;

    for (int r = app->sel.r0; r <= app->sel.r1; ++r) {
        const std::vector<Cell>& row = app->term->visibleLine(r, app->scrollOffset);
        if ((int)row.size() < cols) continue;

        int cs = (r == app->sel.r0) ? app->sel.c0 : 0;
        int ce = (r == app->sel.r1) ? app->sel.c1 : cols - 1;

        std::wstring line;
        for (int c = cs; c <= ce; ++c) {
            const Cell& cell = row[(size_t)c];
            if (cell.ch[0] == CELL_TAIL || cell.ch[0] == CELL_EMPTY) {
                line.push_back(L' ');
                continue;
            }
            line.push_back(cell.ch[0]);
            if (cell.ch[1]) line.push_back(cell.ch[1]);
        }
        while (!line.empty() && line[line.size() - 1] == L' ') line.erase(line.size() - 1);

        out += line;
        if (r < app->sel.r1) out += L"\r\n";
    }

    if (out.empty()) return;
    if (!OpenClipboard(app->hwnd)) return;

    EmptyClipboard();
    SIZE_T bytes = (out.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* dst = GlobalLock(hMem);
        if (dst) {
            memcpy(dst, out.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

static std::wstring readClipboardText(HWND hwnd) {
    std::wstring in;
    if (!OpenClipboard(hwnd)) return in;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            in = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return in;
}

static void pasteToPty(App* app) {
    std::wstring in = readClipboardText(app->hwnd);
    if (in.empty()) return;

    std::wstring norm;
    norm.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c = in[i];
        if (c == L'\r') {
            if (i + 1 < in.size() && in[i + 1] == L'\n') continue;
            norm.push_back(L'\r');
        } else if (c == L'\n') {
            norm.push_back(L'\r');
        } else {
            norm.push_back(c);
        }
    }
    if (norm.empty()) return;

    int len = WideCharToMultiByte(CP_UTF8, 0, norm.c_str(), (int)norm.size(), NULL, 0, NULL, NULL);
    if (len <= 0) return;

    std::vector<char> buf((size_t)len);
    WideCharToMultiByte(CP_UTF8, 0, norm.c_str(), (int)norm.size(), buf.data(), len, NULL, NULL);

    bool bracket = app->term && app->term->bracketedPaste();
    if (bracket) writePtyStr(app, "\x1b[200~");
    writePty(app, buf.data(), len);
    if (bracket) writePtyStr(app, "\x1b[201~");
}

static void applyDwm(HWND hwnd) {
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

static void syncTermSize(App* app, int clientW, int clientH, bool notify) {
    if (!app->rend || !app->term || !app->ui) return;
    if (app->hwnd && IsIconic(app->hwnd)) return;

    int l, t, r, b;
    app->ui->contentRect(clientW, clientH, l, t, r, b, app->model.page);

    int cw = app->rend->cellW();
    int ch = app->rend->cellH();
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;

    int cols = (r - l) / cw;
    int rows = (b - t) / ch;

    // 窗口被拖得比最小宽度还窄时，(r-l)/cw 会掉到 20 以下。以前这里是把 cols
    // 抬回 20，于是终端以为有 20 列、实际只有 8 列放得下，右边那几列直接画到
    // 窗口外被裁掉——长行看起来「不折行、末尾缺一截」。现在反其道而行：
    // 宁可少列，让内容折行，也不要超出可绘制范围。
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > 4000) cols = 4000;
    if (rows > 2000) rows = 2000;

    if (cols == app->term->cols() && rows == app->term->rows()) return;

    app->term->resize(cols, rows);

    if (notify && app->hpc && app->fnResize) {
        COORD sz;
        sz.X = (SHORT)cols;
        sz.Y = (SHORT)rows;
        app->fnResize(app->hpc, sz);
    }
    if (notify) notifyPtySize(app);

    if (app->scrollOffset > app->term->scrollbackSize()) {
        app->scrollOffset = app->term->scrollbackSize();
    }
}

static void openFileInEditor(App* app, const std::wstring& path) {
    if (!app->ed->open(path)) {
        MessageBoxW(app->hwnd, LS(L"无法打开该文件。"), L"EWSL", MB_OK | MB_ICONWARNING);
        return;
    }
    app->ed->setCursor(0, 0, false);
    app->ed->setScroll(0, 0);
    rebuildTree(app);
    refreshUi(app);
}

static void newFileInProject(App* app) {
    if (app->folder.empty()) return;

    std::wstring base = L"untitled";
    std::wstring path;
    for (int i = 0; i < 100; ++i) {
        std::wstring name = base + ((i == 0) ? L"" : std::to_wstring(i)) + L".txt";
        path = pathJoin(app->folder, name);
        if (!pathExists(path)) break;
    }

    app->ed->newFile();
    if (app->ed->saveAs(path)) {
        rebuildTree(app);
        refreshUi(app);
    }
}

static void editorClick(App* app, int mx, int my, bool extend) {
    if (!app->ed->isOpen()) return;

    RECT rc;
    GetClientRect(app->hwnd, &rc);

    EdLayout L = app->ui->edLayout(rc.right, rc.bottom);

    int relY = my - (L.t + L.toolH);
    int relX = mx - (L.l + L.gutterW);

    int lineH = (L.lineH > 0) ? L.lineH : 16;
    int charW = (L.charW > 0) ? L.charW : 8;

    int lineIdx = app->ed->scrollY() + (relY >= 0 ? relY / lineH : 0);
    int vcol = app->ed->scrollX() + (relX >= 0 ? relX / charW : 0);

    int col = app->ed->colFromVisual(lineIdx, vcol);
    app->ed->setCursor(lineIdx, col, extend);
    app->ed->ensureCursorVisible(L.viewLines, L.viewCols);

    app->caretOn = true;
    scheduleRepaint(app);
}

static void gotoPage(App* app, int page) {
    if (app->page == page) return;

    int from = app->page;
    app->page = page;
    app->model.distroMenuOpen = false;
    app->ui->beginPageTransition(from, page);
    if (app->hwnd) SetTimer(app->hwnd, TIMER_ANIM, 16, NULL);

    if (page == PAGE_TERMINAL) {
        refreshOnline(app);
    }
    if (page == PAGE_PROJECT) {
        rebuildTree(app);
    }

    if (page == PAGE_PROJECT && app->ed->isOpen()) {
        SetTimer(app->hwnd, TIMER_CARET, 530, NULL);
    } else {
        KillTimer(app->hwnd, TIMER_CARET);
    }

    refreshUi(app);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;

    switch (msg) {

    case WM_CREATE:
        if (!app) return -1;
        app->hwnd = hwnd;
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            syncTermSize(app, rc.right, rc.bottom, false);
        }
        CreateThread(NULL, 0, ProbeThread, app, 0, NULL);
        SetFocus(hwnd);
        return 0;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        if (!app || !app->ui) break;

        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);

        RECT rc;
        GetWindowRect(hwnd, &rc);

        bool left   = (pt.x < rc.left + kResizeBorder);
        bool right  = (pt.x >= rc.right - kResizeBorder);
        bool top    = (pt.y < rc.top + kResizeBorder);
        bool bottom = (pt.y >= rc.bottom - kResizeBorder);

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        POINT cp = pt;
        ScreenToClient(hwnd, &cp);

        RECT crc;
        GetClientRect(hwnd, &crc);
        UiClick hit = app->ui->hitTest(crc.right, crc.bottom, cp.x, cp.y, app->model);
        if (hit.action == UI_DRAG) return HTCAPTION;

        return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        if (app && app->rend) {
            mmi->ptMinTrackSize.x = app->rend->cellW() * 40;
            mmi->ptMinTrackSize.y = app->rend->cellH() * 14;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        if (IsIconic(hwnd)) {
            ValidateRect(hwnd, NULL);
            return 0;
        }

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        if (app && app->ui && bbEnsure(hdc, w, h)) {
            HDC mem = g_bb.dc;

            RECT all = { 0, 0, w, h };
            HBRUSH bg = CreateSolidBrush(app->model.theme == 1
                                             ? RGB(0x14, 0x14, 0x17)
                                             : RGB(0xF2, 0xF2, 0xF7));

            app->ui->prepare(app->model.termFontSize);

            bool sliding = app->ui->transitionActive() && layerEnsure(hdc, w, h);


            if (sliding) {
                HDC lay = g_layer.dc;
                FillRect(lay, &all, bg);

                int ox = app->ui->pageSlideX();

                if (app->page == PAGE_TERMINAL && app->model.hasTerminal &&
                    app->term && app->rend) {
                    int l, t, r, b;
                    app->ui->contentRect(w, h, l, t, r, b, app->model.page);
                    int cw = r - l;
                    int chh = b - t;
                    if (cw > 0 && chh > 0) {
                        if (app->scrollOffset > app->term->scrollbackSize()) {
                            app->scrollOffset = app->term->scrollbackSize();
                        }
                        if (app->scrollOffset < 0) app->scrollOffset = 0;

                        int sv = SaveDC(lay);
                        SetViewportOrgEx(lay, l, t, NULL);
                        IntersectClipRect(lay, 0, 0, cw, chh);
                        app->rend->paint(lay, cw, chh, *app->term, app->scrollOffset,
                                         app->focused, app->sel);
                        RestoreDC(lay, sv);
                    }
                }

                app->ui->paintContent(lay, w, h, app->model, app->ed, app->caretOn);

                FillRect(mem, &all, bg);

                int l, t, r, b;
                app->ui->contentRect(w, h, l, t, r, b, app->model.page);
                if (r > l && b > t) {
                    // Offset at blit time so every pixel of the page - glyphs
                    // painted with GDI, cards painted with GDI+ - travels
                    // together. The sidebar and title bar are repainted on top
                    // right afterwards, so spill in that direction is covered.
                    BitBlt(mem, l + ox, t, r - l, b - t, lay, l, t, SRCCOPY);
                }

                app->ui->paintChrome(mem, w, h, app->model);

                double fade = app->ui->pageFade();
                if (fade < 0.999) {
                    int alpha = (int)((1.0 - fade) * 92.0 + 0.5);
                    app->ui->paintVeil(mem, l, t, r, b, alpha, app->model.theme);
                }

                app->ui->paintOverlay(mem, w, h, app->model);
            } else {
                FillRect(mem, &all, bg);

                if (app->page == PAGE_TERMINAL && app->model.hasTerminal &&
                    app->term && app->rend) {
                    int l, t, r, b;
                    app->ui->contentRect(w, h, l, t, r, b, app->model.page);
                    int cw = r - l;
                    int chh = b - t;

                    if (cw > 0 && chh > 0) {
                        if (app->scrollOffset > app->term->scrollbackSize()) {
                            app->scrollOffset = app->term->scrollbackSize();
                        }
                        if (app->scrollOffset < 0) app->scrollOffset = 0;

                        int sv = SaveDC(mem);
                        SetViewportOrgEx(mem, l, t, NULL);
                        IntersectClipRect(mem, 0, 0, cw, chh);
                        app->rend->paint(mem, cw, chh, *app->term, app->scrollOffset,
                                         app->focused, app->sel);
                        RestoreDC(mem, sv);
                    }
                }

                app->ui->paint(mem, w, h, app->model, app->ed, app->caretOn);
            }

            DeleteObject(bg);
            BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER: {
        if (!app) return 0;

        if (wParam == TIMER_REPAINT) {
            KillTimer(hwnd, TIMER_REPAINT);
            app->repaintPending = false;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (wParam == TIMER_CARET) {
            app->caretOn = !app->caretOn;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (wParam == TIMER_ANIM) {
            DWORD now = GetTickCount();
            DWORD dt = now - app->animTick;
            if (dt == 0) dt = 16;
            if (dt > 80) dt = 80;
            app->animTick = now;

            // 启用组件的进度：秒表一直走，扫光才动得起来。
            // 后台线程给的毫秒数比 UI 帧数精确得多，用它算秒数，
            // 这样即使定时器被别处SetTimer 冲掉也不会走慢。
            if (app->model.fixStage == 1 || app->model.fixStage == 2) {
                DWORD bg = (DWORD)InterlockedCompareExchange(&app->wslFixElapsedMs, 0, 0);
                DWORD base = app->model.fixStartTick;
                DWORD byTick = (now >= base) ? (now - base) : 0;
                DWORD el = (bg > byTick) ? bg : byTick;
                app->model.fixElapsed = (int)(el / 1000);

                // 提权成功就切到「执行中」。UAC 弹窗还开着的时候 approved 还是 0，
                // 这段时间显示「等待授权」并让用户知道要去点那个窗口。
                LONG ap = InterlockedCompareExchange(&app->wslFixApproved, 0, 0);
                if (app->model.fixStage == 1 && ap == 1) {
                    app->model.fixStage = 2;
                    app->model.fixTick = now;
                    app->model.fixMsg = LS(L"正在启用系统组件，可能需要几分钟");
                }
            }

            app->ui->tickAnim((int)dt, app->model);

            bool toastAlive = false;
            if (!app->model.toast.empty()) {
                unsigned long held = (now >= app->model.toastTick)
                                         ? (now - app->model.toastTick) : 0;
                if (held >= 2600) app->model.toast.clear();
                else toastAlive = true;
            }

            bool spinning = (app->page == PAGE_INSTALL &&
                             (app->instStage == INST_RESOLVE ||
                              app->instStage == INST_DOWNLOAD ||
                              app->instStage == INST_IMPORT ||
                              app->instStage == INST_VERIFY));
            bool shimmer = (app->instStage == INST_DOWNLOAD);

            // 启用流程没结束就得继续重绘：扫光、百分比、秒表都在动。
            bool fixing = (app->model.fixStage == 1 || app->model.fixStage == 2);

            if (!app->ui->animActive() && !toastAlive && !spinning && !shimmer && !fixing) {
                KillTimer(hwnd, TIMER_ANIM);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (wParam == TIMER_PTYWAIT) {
            if (app->ptyText >= 2) {
                if (app->transport == TRANSPORT_CONPTY && app->conptyOk != 1) {
                    app->conptyOk = 1;
                    saveSettings(app);
                }
                return 0;
            }
            if (!app->model.hasTerminal || !app->term) return 0;

            bool isShell = (!app->jobMode && !app->model.installing &&
                            app->instStage != INST_IMPORT);

            if (isShell && app->transport == TRANSPORT_CONPTY && !app->autoFallbackTried) {
                app->autoFallbackTried = true;
                KillTimer(hwnd, TIMER_PTYWAIT);

                app->conptyOk = 0;
                saveSettings(app);

                std::wstring distro = app->model.activeDistro;
                std::wstring a2 = interactiveArgs(app, distro);
                app->ptyBridge = false;
                resetPtyProbe();

                app->quietExit = true;
                stopPty(app);
                if (app->term) app->term->hardReset();

                if (startSession(app, a2, TRANSPORT_PIPE)) {
                    SetTimer(hwnd, TIMER_PTYWAIT, 3000, NULL);
                    termPrint(app, LS(L"[EWSL] ConPTY 通道没有输出，已自动改用兼容通道并启动交互式 shell。\r\n\r\n"));
                    refreshUi(app);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                termPrint(app, LS(L"[EWSL] 兼容通道也没能启动。\r\n"));
            }

            KillTimer(hwnd, TIMER_PTYWAIT);
            refreshUi(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_APP_PTY_DATA: {
        std::vector<char> chunk = takePendingPty(app);
        const char* data = chunk.empty() ? NULL : chunk.data();
        size_t len = chunk.size();

        if (app && app->term && data) app->term->feed(data, len);
        if (app) {
            if (data) detectPtyBridge(app, data, len);
            app->ptyBytes += (DWORD)len;
            if (data) countText(app, data, len);
            if (app->ptyText >= 2) KillTimer(hwnd, TIMER_PTYWAIT);
            if (app->jobMode && data) {
                jobLogStream(app, std::string(data, len));
            }

            if (!app->jobMode && data &&
                !app->model.activeDistro.empty() &&
                !isDistroBroken(app, app->model.activeDistro) &&
                containsAscii(data, len, "WSL_E_DISTRO_NOT_FOUND")) {
                std::wstring id = app->model.activeDistro;
                bool wasAuto = app->autoTarget;
                app->autoTarget = false;

                termPrint(app, LS(L"\r\n[EWSL] wsl 报告找不到发行版 ") + id + LS(L"。\r\n  正在后台确认它到底还能不能用…\r\n"));

                if (!app->verifying) {
                    app->verifying = true;
                    app->verifyWasAuto = wasAuto;
                    CreateThread(NULL, 0, VerifyThread, app, 0, NULL);
                }
            }
        }
        {
            static DWORD lastFrame = 0;
            DWORD now = GetTickCount();
            if ((DWORD)(now - lastFrame) >= 24) {
                lastFrame = now;
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                scheduleRepaint(app);
            }
        }
        return 0;
    }

    case WM_APP_VERIFY: {
        VerifyResult* v = (VerifyResult*)lParam;
        if (!app || !v) return 0;

        std::wstring id = v->id;
        bool alive = v->alive;
        bool wasAuto = app->verifyWasAuto;
        delete v;

        app->verifying = false;
        app->verifyWasAuto = false;

        if (id.empty()) return 0;

        if (alive) {
            clearBrokenDistro(app, id);
            saveSettings(app);

            bool listed = false;
            for (size_t i = 0; i < app->model.installed.size(); ++i) {
                if (lstrcmpiW(app->model.installed[i].c_str(), id.c_str()) == 0) {
                    listed = true;
                    break;
                }
            }
            if (!listed) {
                app->model.installed.push_back(id);
                syncOnlineInstalled(app);
            }

            showToast(app, id + LS(L" 实际可用，已重新启动"));

            app->quietExit = true;
            if (app->hpc || app->term) stopPty(app);
            app->autoTarget = false;
            launchTerminal(app, id);
        } else {
            markDistroBroken(app, id);

            std::wstring next;
            for (size_t i = 0; i < app->model.installed.size(); ++i) {
                if (lstrcmpiW(app->model.installed[i].c_str(), id.c_str()) != 0) {
                    next = app->model.installed[i];
                    break;
                }
            }

            if (wasAuto && !next.empty()) {
                showToast(app, id + LS(L" 不可用，已切换到 ") + next);

                app->quietExit = true;
                if (app->hpc || app->term) stopPty(app);
                app->autoTarget = false;
                launchTerminal(app, next);
            } else {
                termPrint(app, LS(L"\r\n[EWSL] 确认 ") + id + LS(L" 已经启动不了。\r\n  已标记为「注册信息失效」：菜单里它回到「可安装」区，\r\n  右侧标签是红色的「重新安装」——点它会先执行\r\n    wsl --unregister ") + id + LS(L"\r\n  再重新下载导入。也可以手动执行这条命令。\r\n"));

                if (wasAuto) {
                    termPrint(app, LS(L"  当前没有其他可用的发行版，请从上方菜单下载安装。\r\n\r\n"));
                    app->model.hasTerminal = false;
                }
                showToast(app, id + LS(L" 的注册信息不可用，点菜单里的「重新安装」"));
            }
        }

        refreshUi(app);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_APP_DL_TICK: {
        if (!app) return 0;
        DownloadProgress p;
        p.got = app->model.instGot;
        p.total = app->model.instTotal;
        p.percent = app->model.instPercent;

        std::wstring text = LS(L"正在下载镜像 ") + formatBytes(p.got);
        if (p.total > 0) {
            text += L" / " + formatBytes(p.total);
            text += LS(L"（") + std::to_wstring(p.percent < 0 ? 0 : p.percent) + LS(L"%）");
        }
        app->model.instStageText = text;
        app->model.instDetail = app->model.instUrl;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_APP_DL_DONE: {
        if (!app) return 0;
        std::wstring* err = (std::wstring*)lParam;
        bool ok = (wParam != 0);
        std::wstring emsg = err ? *err : std::wstring();
        delete err;

        if (app->instThread) {
            CloseHandle(app->instThread);
            app->instThread = NULL;
        }

        if (ok) {
            jobLog(app, LS(L"+镜像下载完成：") + formatBytes(app->model.instGot));
            jobLog(app, L"");
            runImport(app, 2);
        } else {
            bool canceled = (InterlockedCompareExchange(&app->instCancel, 0, 0) != 0);
            app->model.instErr = emsg.empty() ? LS(L"下载失败") : emsg;
            jobLog(app, LS(L"!下载失败：") + app->model.instErr);
            instSetStage(app, canceled ? INST_CANCELED : INST_FAILED,
                         canceled ? LS(L"已取消") : LS(L"下载失败"), app->model.instErr);
            if (canceled) app->model.instPercent = -1;
            app->model.instHint = LS(L"可以点「重试」重新下载，或改用 WSL 官方通道安装。");
        }
        refreshUi(app);
        return 0;
    }

    case WM_APP_PTY_EXIT: {
        if (!app) return 0;
        KillTimer(hwnd, TIMER_PTYWAIT);
        app->exited = true;
        app->ptyBridge = false;
        resetPtyProbe();

        DWORD code = 0;
        bool haveCode = false;

        if (app->jobMode) {
            app->jobMode = false;
            if (app->hProcess) haveCode = GetExitCodeProcess(app->hProcess, &code) != 0;
            stopPty(app);

            if (app->instUserCancel) {
                app->instUserCancel = false;
                app->pendingInstall.clear();
                app->pendingStart.clear();
                app->model.installing = false;
                jobLog(app, LS(L"已取消"));
                app->model.instErr = LS(L"安装已取消");
                app->model.instHint = LS(L"可以点「重试」重新开始。");
                instSetStage(app, INST_CANCELED, LS(L"已取消"), L"");
                app->model.instPercent = -1;
                refreshUi(app);
                return 0;
            }

            bool stepOk = (!haveCode || code == 0);
            wchar_t buf[220];
            if (haveCode) wsprintfW(buf, LS(L"（退出码 %lu）"), (unsigned long)code);
            else          buf[0] = 0;

            if (app->instIsFix) {
                app->instIsFix = false;
                if (stepOk) {
                    jobLog(app, LS(L"+WSL 内核已更新"));
                    instSetStage(app, INST_DONE, LS(L"WSL 已更新"),
                                 LS(L"点右下角返回，然后重新检测环境"));
                    app->model.instPercent = 100;
                } else {
                    jobLog(app, LS(L"!wsl --update 失败") + std::wstring(buf));
                    app->model.instErr = LS(L"wsl --update 失败");
                    app->model.instHint = LS(L"可以试试用管理员身份运行，或先执行 wsl --shutdown。");
                    instSetStage(app, INST_FAILED, LS(L"更新失败"), app->model.instErr);
                }
                refreshUi(app);
                return 0;
            }

            if (app->instStage == INST_IMPORT) {
                if (stepOk) {
                    jobLog(app, LS(L"+导入完成，正在校验…"));
                    app->pendingStart = app->instDistro;
                    app->model.installing = false;
                    instSetStage(app, INST_VERIFY, LS(L"正在校验安装结果…"), L"");
                    CreateThread(NULL, 0, ProbeThread, app, 0, NULL);
                    refreshUi(app);
                    return 0;
                }

                if (app->instAttempt != 1) {
                    jobLog(app, LS(L"!WSL2 导入失败，改用 WSL1 重试…"));
                    runImport(app, 1);
                    refreshUi(app);
                    return 0;
                }

                jobLog(app, LS(L"!导入失败") + std::wstring(buf));
                app->model.instErr = LS(L"wsl --import 失败，请检查是否已启用 WSL2（wsl --status）");
                app->model.instHint = LS(L"点「重试」重新导入，或用 WSL 官方通道安装。");
                app->pendingStart.clear();
                instSetStage(app, INST_FAILED, LS(L"导入失败"), app->model.instErr);
                refreshUi(app);
                return 0;
            }

            {
                std::wstring d = app->pendingInstall;
                std::wstring label = app->pendingLabel;
                app->pendingInstall.clear();
                app->pendingLabel.clear();
                app->model.installing = false;

                if (!stepOk) {
                    jobLog(app, LS(L"!wsl --install 失败") + std::wstring(buf));
                    app->model.instErr = LS(L"wsl --install 未成功");
                    app->model.instHint = LS(L"常见原因：网络不可达、未启用「虚拟机平台」、发行版 id 不被当前 WSL 版本支持。");
                    app->pendingStart.clear();
                    instSetStage(app, INST_FAILED, LS(L"安装失败"), app->model.instErr);
                } else {
                    jobLog(app, LS(L"+wsl --install 已完成，正在校验…"));
                    app->pendingStart = d;
                    instSetStage(app, INST_VERIFY,
                                 LS(L"正在刷新发行版列表…"), label);
                    CreateThread(NULL, 0, ProbeThread, app, 0, NULL);
                }
            }

            refreshUi(app);
            return 0;
        }

        if (app->hProcess) {
            haveCode = GetExitCodeProcess(app->hProcess, &code) != 0;
        }

        if (app->quietExit) {
            app->quietExit = false;
            refreshUi(app);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (app->term) {
            wchar_t buf[220];
            if (haveCode && code != 0) {
                wsprintfW(buf, LS(L"\r\n[进程已退出，退出码 0x%08X]\r\n"), (unsigned)code);
                termPrint(app, buf);
            } else if (haveCode) {
                termPrint(app, LS(L"\r\n[进程正常退出]\r\n"));
            } else {
                termPrint(app, LS(L"\r\n[进程已退出]\r\n"));
            }

            if (app->ptyBytes == 0) {
                termPrint(app, LS(L"本次未收到任何输出。请在 PowerShell 执行 wsl -l -v 确认是否已安装发行版。\r\n"));
            }
        }

        refreshUi(app);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_APP_UNREGISTER_DONE: {
        UnregArg* a = (UnregArg*)lParam;
        if (a) {
            if (app) {
                app->unregisterBusy = false;
                if (a->ok) {
                    showToast(app, LS(L"已注销 ") + a->id);
                    refreshDistros(app);
                } else {
                    showToast(app, LS(L"注销失败，该发行版可能正在使用中"));
                    refreshUi(app);
                }
            }
            delete a;
        }
        return 0;
    }

    case WM_APP_UNREG_PREP_DONE: {
        PrepUnregArg* a = (PrepUnregArg*)lParam;
        if (!a) return 0;

        if (app && app->prepUnregistering) {
            app->prepUnregistering = false;
            if (a->ok) {
                jobLog(app, LS(L"+已注销旧注册项 ") + a->name);
                clearBrokenDistro(app, a->name);
                for (size_t i = 0; i < app->model.installed.size(); ) {
                    if (lstrcmpiW(app->model.installed[i].c_str(), a->name.c_str()) == 0)
                        app->model.installed.erase(app->model.installed.begin() + (int)i);
                    else ++i;
                }
                syncOnlineInstalled(app);
                saveSettings(app);
            } else {
                jobLog(app, LS(L"!旧注册项注销失败，仍继续导入（可能因重名而失败）"));
                if (!a->out.empty()) jobLog(app, L"  " + a->out);
            }
            startDownload(app);
            refreshUi(app);
        }

        delete a;
        return 0;
    }

    case WM_APP_WSLFIX: {
        if (!app) return 0;
        app->wslFixBusy = false;

        // 组件状态缓存在 wsl.cpp 里，不清掉的话重新探测还是拿到旧结论。
        resetWslState();
        dropWslPathCache();

        std::wstring msg = app->wslFixOut;
        app->wslFixOut.clear();

        bool approved = (InterlockedCompareExchange(&app->wslFixApproved, 0, 0) == 1);

        if (!approved) {
            app->model.fixStage = 4;
            app->model.fixMsg = msg.empty() ? LS(L"操作未完成") : msg;
            refreshUi(app);
            return 0;
        }

        // 探一次，看组件到底生效没有。wsl --install 启用系统组件后通常要重启，
        // 这时候探测依然会报未启用——这不是失败，得如实说成「需要重启」。
        WslState st = wslState();

        if (st == WSL_READY) {
            app->model.fixStage = 0;
            app->model.fixMsg.clear();
            showToast(app, LS(L"WSL 组件已启用"));
            CreateThread(NULL, 0, ProbeThread, app, 0, NULL);
            return 0;
        }

        // 没生效：十有八九是等着重启。wsl --install 自己也会在输出里提示重启，
        // 而它重启了本进程就没机会把这句话说出来，所以由界面来说。
        app->model.fixStage = 3;
        app->model.fixMsg = LS(L"组件已安装，重启后即可使用");
        if (!msg.empty() && msg != LS(L"命令已执行完成")) {
            app->model.fixMsg = msg + LS(L"（可能需要重启）");
        }
        refreshUi(app);
        return 0;
    }

    case WM_APP_PROBE: {
        ProbeResult* r = (ProbeResult*)lParam;
        if (!app || !r) return 0;

        app->model.checking = false;
        app->model.wslMissing = !r->wslOk;
        app->model.wslState = r->state;
        app->model.installed = r->installed;
        app->model.wslVersion = r->version;
        app->model.wslg = r->wslg;
        app->model.wslgFixed = r->wslgFixed;

        // 探到可用就说明组件真的生效了，把「等重启」那条提示收掉。
        if (r->wslOk && app->model.fixStage == 3) {
            app->model.fixStage = 0;
            app->model.fixMsg.clear();
        }

        if (!r->wslOk) {
            dropWslPathCache();
            refreshUi(app);
            delete r;
            return 0;
        }

        // WSLg 坏掉时探测线程已经替用户写好了 .wslconfig。得说一声，
        // 否则「重启 WSL 后 Linux 图形程序会打不开」这种后果就是无预告的。
        if (r->wslgFixed) {
            showToast(app, LS(L"已自动关闭 WSL 图形界面（rdclientax.dll 加载失败会导致反复弹窗），重启 WSL 后生效"));
        }

        if (!r->dead.empty()) {
            bool changed = false;
            std::wstring names;
            for (size_t i = 0; i < r->dead.size(); ++i) {
                if (!names.empty()) names += LS(L"、");
                names += r->dead[i];
                if (!isDistroBroken(app, r->dead[i])) {
                    app->brokenDistros.push_back(r->dead[i]);
                    changed = true;
                }
            }
            if (changed) saveSettings(app);

            if (!app->suppressLaunch) {
                showToast(app, names + LS(L" 的根文件系统已丢失，可在菜单里「重新安装」"));
            }
        }

        {
            if (r->wslOk) {
                bool changed = false;
                for (size_t i = 0; i < app->brokenDistros.size(); ) {
                    bool listed = false;
                    for (size_t j = 0; j < r->installed.size(); ++j) {
                        if (lstrcmpiW(r->installed[j].c_str(),
                                      app->brokenDistros[i].c_str()) == 0) {
                            listed = true;
                            break;
                        }
                    }

                    bool healed = listed &&
                        distroRootfsState(app->brokenDistros[i]) == ROOTFS_OK;

                    if (listed && !healed) {
                        ++i;
                    } else {
                        app->brokenDistros.erase(app->brokenDistros.begin() + (int)i);
                        changed = true;
                    }
                }
                if (changed) saveSettings(app);
            }

            for (size_t i = 0; i < app->model.installed.size(); ) {
                if (isDistroBroken(app, app->model.installed[i]))
                    app->model.installed.erase(app->model.installed.begin() + (int)i);
                else ++i;
            }
        }

        std::wstring target;
        app->autoTarget = false;

        if (app->instStage == INST_VERIFY) {
            bool found = false;
            for (size_t i = 0; i < r->installed.size(); ++i) {
                if (lstrcmpiW(r->installed[i].c_str(), app->instDistro.c_str()) == 0) {
                    found = true;
                    break;
                }
            }

            app->pendingStart.clear();
            jobLog(app, found ? LS(L"+注册成功：") + app->instDistro
                              : LS(L"!注册后未在 wsl -l -q 中看到 ") + app->instDistro);

            if (found) {
                app->pendingStart = app->instDistro;
                if (isDistroBroken(app, app->instDistro)) {
                    clearBrokenDistro(app, app->instDistro);
                    saveSettings(app);
                }
                instSetStage(app, INST_DONE, LS(L"安装完成"),
                             app->instLabel + LS(L" 已就绪，点右下角进入终端"));
                app->model.instErr.clear();
                app->model.instHint.clear();
                app->model.instPercent = 100;

                // 装完发行版正是 WSLg 会被拉起来的那一刻，顺手查一下。
                // 新装的发行版第一次启动就会弹满屏 RDP 报错，比安装前更烦人。
                if (wslgState() == WSLG_BROKEN && disableWslg()) {
                    app->model.wslg = WSLG_OFF;
                    app->model.wslgFixed = true;
                    app->model.instHint = LS(L"检测到 WSL 图形界面组件加载失败，会反复弹远程桌面提示，已自动关闭。重启 WSL 后生效。");
                }
            } else {
                app->model.instErr = LS(L"安装已完成但 WSL 未注册该发行版");
                app->model.instHint = LS(L"可能是 WSL 需要重启：在 PowerShell 执行 wsl --shutdown 后重试。");
                instSetStage(app, INST_FAILED, LS(L"注册校验失败"), app->model.instErr);
            }

            delete r;

            if (app->model.hasTerminal && app->hpc) stopPty(app);

            RECT crc;
            GetClientRect(hwnd, &crc);
            syncTermSize(app, crc.right, crc.bottom, true);
            refreshUi(app);
            return 0;
        }

        if (!app->pendingStart.empty()) {
            target = app->pendingStart;
            app->pendingStart.clear();

            bool found = false;
            for (size_t i = 0; i < r->installed.size(); ++i) {
                if (lstrcmpiW(r->installed[i].c_str(), target.c_str()) == 0) { found = true; break; }
            }
            if (!found) {
                termPrint(app, LS(L"\r\n[EWSL] 安装流程结束了，但发行版列表里没有 ") + target
                               + LS(L"。\r\n  请展开上方下拉菜单确认，或重新安装。\r\n"));
                target.clear();
                app->suppressLaunch = true;
            }
        } else if (!app->suppressLaunch) {
            target = g_startDistro;
            app->autoTarget = false;

            if (target.empty() && !r->def.empty() && !isDistroBroken(app, r->def)) {
                target = r->def;
                app->autoTarget = true;
            }
            if (target.empty() && !app->model.installed.empty()) {
                target = app->model.installed[0];
                app->autoTarget = true;
            }
        }

        app->suppressLaunch = false;

        delete r;

        if (app->model.hasTerminal && app->hpc) {
            stopPty(app);
        }

        app->page = PAGE_TERMINAL;

        if (target.empty()) {
            if (!app->model.hasTerminal) {
                app->model.installing = false;
                app->model.tree.clear();
                refreshOnline(app);
            }
        } else {
            launchTerminal(app, target);
        }

        RECT rc;
        GetClientRect(hwnd, &rc);
        syncTermSize(app, rc.right, rc.bottom, true);
        refreshUi(app);
        return 0;
    }

    case WM_APP_ONLINE: {
        std::vector<DistroEntry>* r = (std::vector<DistroEntry>*)lParam;
        if (!app || !r) return 0;

        app->model.loadingOnline = false;
        app->model.online = *r;
        delete r;

        for (size_t i = 0; i < app->model.online.size(); ++i) {
            app->model.online[i].installed = false;
            for (size_t j = 0; j < app->model.installed.size(); ++j) {
                if (lstrcmpiW(app->model.online[i].id.c_str(),
                              app->model.installed[j].c_str()) == 0) {
                    app->model.online[i].installed = true;
                    break;
                }
            }
        }

        refreshUi(app);
        return 0;
    }

    case WM_APP_DISTRO: {
        DistroRefresh* r = (DistroRefresh*)lParam;
        if (!app || !r) return 0;

        app->model.statuses = r->statuses;
        delete r;

        app->model.distroBusy = false;
        app->model.distroLoading = false;

        refreshUi(app);
        return 0;
    }

    case WM_SIZE: {
        if (!app || !app->term || !app->rend) return 0;
        int w = (int)LOWORD(lParam);
        int h = (int)HIWORD(lParam);
        if (w <= 0 || h <= 0) return 0;

        syncTermSize(app, w, h, true);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_SETFOCUS:
        if (app) app->focused = true;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KILLFOCUS:
        if (app) app->focused = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEACTIVATE:
        if (!app) break;
        SetFocus(hwnd);
        return MA_ACTIVATE;

    case WM_KEYDOWN: {
        if (!app) break;

        const int  vk    = (int)wParam;
        const bool ctrl  = isKeyDown(VK_CONTROL);
        const bool shift = isKeyDown(VK_SHIFT);

        // Any swallow request only ever applies to the WM_CHAR that the previous
        // keystroke queued, so clear it up front on every new key down.
        app->swallowChar = false;

        if (vk == VK_ESCAPE && app->model.distroMenuOpen) {
            app->model.distroMenuOpen = false;
            // Escape also queues a WM_CHAR carrying 0x1B; closing the menu should
            // not simultaneously ship an ESC to whatever is running in the shell.
            app->swallowChar = true;
            refreshUi(app);
            return 0;
        }

        if (app->page == PAGE_PROJECT && app->ed->isOpen()) {
            Editor* e = app->ed;

            if (ctrl && vk == 'S') {
                if (!e->path().empty()) {
                    if (e->save()) {
                        refreshUi(app);
                    } else {
                        MessageBoxW(hwnd, LS(L"保存失败。"), L"EWSL", MB_OK | MB_ICONWARNING);
                    }
                }
                app->swallowChar = true;
                return 0;
            }
            if (ctrl && vk == 'A') {
                e->selectAll();
                app->swallowChar = true;
                scheduleRepaint(app);
                return 0;
            }

            if (ctrl && vk == 'C') {
                std::wstring s = e->selectedText();
                if (!s.empty() && OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    SIZE_T bytes = (s.size() + 1) * sizeof(wchar_t);
                    HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, bytes);
                    if (hm) {
                        void* dst = GlobalLock(hm);
                        if (dst) {
                            memcpy(dst, s.c_str(), bytes);
                            GlobalUnlock(hm);
                            SetClipboardData(CF_UNICODETEXT, hm);
                        } else {
                            GlobalFree(hm);
                        }
                    }
                    CloseClipboard();
                }
                app->swallowChar = true;
                return 0;
            }

            if (ctrl && vk == 'V') {
                std::wstring s = readClipboardText(hwnd);
                if (!s.empty()) e->insertString(s);
                app->swallowChar = true;
                scheduleRepaint(app);
                return 0;
            }

            switch (vk) {
            case VK_LEFT:   e->moveLeft(shift); break;
            case VK_RIGHT:  e->moveRight(shift); break;
            case VK_UP:     e->moveUp(1, shift); break;
            case VK_DOWN:   e->moveDown(1, shift); break;
            case VK_HOME:   e->moveHome(shift); break;
            case VK_END:    e->moveEnd(shift); break;
            case VK_PRIOR:  e->moveUp(20, shift); break;
            case VK_NEXT:   e->moveDown(20, shift); break;
            case VK_BACK:
                // Backspace also queues a WM_CHAR with a raw BS byte; without
                // this the editor would eat two characters per press.
                app->swallowChar = true;
                e->backspace();
                break;
            case VK_DELETE: e->deleteForward(); break;
            default: return 0;
            }

            RECT rc;
            GetClientRect(hwnd, &rc);
            EdLayout L = app->ui->edLayout(rc.right, rc.bottom);
            e->ensureCursorVisible(L.viewLines, L.viewCols);
            app->caretOn = true;
            scheduleRepaint(app);
            return 0;
        }

        if (!app->model.hasTerminal) break;

        // Terminal copy/paste. Ctrl+Shift+C/V is the classic pair; Ctrl+V also
        // pastes, Ctrl+Insert / Shift+Insert work too, and a plain Ctrl+C copies
        // when there is a selection (only with no selection does it fall through
        // to the shell as ^C / SIGINT). swallowChar eats the control character
        // that TranslateMessage queued for the same keystroke.
        if (ctrl && vk == 'C') {
            if (shift || app->sel.active) {
                copySelection(app);
                app->swallowChar = true;
                return 0;
            }
        } else if (ctrl && vk == 'V') {
            pasteToPty(app);
            app->swallowChar = true;
            return 0;
        } else if (ctrl && vk == VK_INSERT) {
            copySelection(app);
            app->swallowChar = true;
            return 0;
        } else if (shift && vk == VK_INSERT) {
            pasteToPty(app);
            app->swallowChar = true;
            return 0;
        }

        const char* seq = NULL;
        switch (vk) {
        case VK_UP:     seq = "\x1b[A";  break;
        case VK_DOWN:   seq = "\x1b[B";  break;
        case VK_RIGHT:  seq = "\x1b[C";  break;
        case VK_LEFT:   seq = "\x1b[D";  break;
        case VK_HOME:   seq = "\x1b[H";  break;
        case VK_END:    seq = "\x1b[F";  break;
        case VK_INSERT: seq = "\x1b[2~"; break;
        case VK_DELETE: seq = "\x1b[3~"; break;
        case VK_PRIOR:  seq = "\x1b[5~"; break;
        case VK_NEXT:   seq = "\x1b[6~"; break;
        case VK_BACK:   seq = "\x7f";    break;
        case VK_ESCAPE: seq = "\x1b";    break;
        case VK_F1:     seq = "\x1bOP";  break;
        case VK_F2:     seq = "\x1bOQ";  break;
        case VK_F3:     seq = "\x1bOR";  break;
        case VK_F4:     seq = "\x1bOS";  break;
        case VK_F5:     seq = "\x1b[15~"; break;
        case VK_F6:     seq = "\x1b[17~"; break;
        case VK_F7:     seq = "\x1b[18~"; break;
        case VK_F8:     seq = "\x1b[19~"; break;
        case VK_F9:     seq = "\x1b[20~"; break;
        case VK_F10:    seq = "\x1b[21~"; break;
        case VK_F11:    seq = "\x1b[23~"; break;
        case VK_F12:    seq = "\x1b[24~"; break;
        case VK_TAB:
            if (shift) seq = "\x1b[Z";
            break;
        default:
            break;
        }

        // Some of these keys (Backspace, Escape, Shift+Tab) also make
        // TranslateMessage queue a WM_CHAR carrying the same information - a raw
        // BS / ESC / TAB byte. Sending both is what made one Backspace erase two
        // characters. swallowChar eats that duplicate; plain Tab is deliberately
        // left out because it has no sequence here and must reach the shell as
        // the WM_CHAR byte.
        if (seq) {
            app->swallowChar = true;
            writePtyStr(app, seq);
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        if (!app) break;
        if (app->swallowChar) { app->swallowChar = false; return 0; }
        wchar_t wc = (wchar_t)wParam;
        if (wc == 0) break;

        if (app->page == PAGE_PROJECT && app->ed->isOpen()) {
            // Control characters reaching here are the leftovers of a Ctrl+key
            // chord or a stray BS / ESC, never text. Inserting them used to make
            // one Backspace look like it deleted two characters here as well.
            if (wc < 0x20 && wc != L'\r' && wc != L'\n' && wc != L'\t') return 0;

            app->ed->insertChar(wc);

            RECT rc;
            GetClientRect(hwnd, &rc);
            EdLayout L = app->ui->edLayout(rc.right, rc.bottom);
            app->ed->ensureCursorVisible(L.viewLines, L.viewCols);

            app->caretOn = true;
            scheduleRepaint(app);
            return 0;
        }

        if (!app->model.hasTerminal) break;

        // Enter. With a real terminal in front of the guest (ConPTY, or the
        // guest-side pty bridge) the line discipline maps CR to NL for us.
        //
        // 早先这里在 PIPE 模式下还挑 `!app->ptyBridge ? "\n" : "\r"`，理由是
        // 「裸管道没有行规程、只认 NL」。可 PIPE 模式一定注入了 bridge 脚本，
        // 那边 pty.fork() 出来的 guest 前面是有真 pty 的，CR 会被正确转成 NL；
        // 而 ptyBridge 要等 \x1b]9998;wemb-pty 回执才置 true，启动初期一直是
        // false，于是刚开始那几秒按回车发的是裸 \n —— readline 收到的东西
        // 和预期不一样，提示符就画到了行中间（症状：zabc[root@...]# ）。
        // 「还没确认 bridge 活着」不能当「没有 bridge」用。
        if (wc == L'\r' || wc == L'\n') {
            writePty(app, "\r", 1);
            return 0;
        }

        char utf8[8];
        int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, (int)sizeof(utf8), NULL, NULL);
        if (n > 0) writePty(app, utf8, n);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!app || !app->ui) break;

        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);

        RECT rc;
        GetClientRect(hwnd, &rc);

        UiClick hit = app->ui->hitTest(rc.right, rc.bottom, mx, my, app->model);

        if (hit.action != UI_NONE && hit.action != UI_DRAG) {
            app->ui->notePress(app->ui->hoverBtnId());
            SetTimer(hwnd, TIMER_ANIM, 16, NULL);
            app->animTick = GetTickCount();
        }

        switch (hit.action) {
        case UI_CLOSE:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case UI_MINIMIZE:
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        case UI_MAXIMIZE:
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;

        case UI_NAV_TERMINAL:
            if (installRunning(app) && app->page != PAGE_INSTALL) {
                gotoPage(app, PAGE_INSTALL);
                showToast(app, LS(L"安装正在进行，暂时不能离开安装页"));
                return 0;
            }
            gotoPage(app, PAGE_TERMINAL);
            return 0;
        case UI_NAV_PROJECT:
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，完成或取消后才能切换"));
                return 0;
            }
            gotoPage(app, PAGE_PROJECT);
            return 0;
        case UI_NAV_DISTRO:
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，完成或取消后才能切换"));
                return 0;
            }
            gotoPage(app, PAGE_DISTRO);
            if (app->model.installed.empty() || app->model.statuses.empty()) {
                refreshDistros(app);
            }
            return 0;
        case UI_NAV_SETTINGS:
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，完成或取消后才能切换"));
                return 0;
            }
            gotoPage(app, PAGE_SETTINGS);
            return 0;

        case UI_DISTRO_MENU:
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，请先等待或取消"));
                return 0;
            }
            app->model.distroMenuOpen = !app->model.distroMenuOpen;
            if (app->model.distroMenuOpen) {
                app->ui->resetMenuScroll();
                refreshOnline(app);
                if (app->hwnd) SetTimer(app->hwnd, TIMER_ANIM, 16, NULL);
            }
            refreshUi(app);
            return 0;

        case UI_SELECT_DISTRO:
            app->model.distroMenuOpen = false;
            app->model.activeDistro = hit.arg;
            stopPty(app);
            gotoPage(app, PAGE_TERMINAL);
            launchTerminal(app, hit.arg);
            refreshUi(app);
            return 0;

        case UI_INSTALL_DISTRO:
            app->model.distroMenuOpen = false;
            if (installRunning(app)) {
                if (app->page != PAGE_INSTALL) gotoPage(app, PAGE_INSTALL);
                showToast(app, LS(L"已经有一个安装在进行中"));
                return 0;
            }
            stopPty(app);
            beginInstall(app, hit.arg);
            return 0;

        case UI_INSTALL_CANCEL:
            if (app->jobMode) {
                app->instUserCancel = true;
                jobLog(app, LS(L"!用户取消"));
                app->model.instStageText = LS(L"正在取消…");
                stopPty(app);
            } else {
                InterlockedExchange(&app->instCancel, 1);
                app->model.instStageText = LS(L"正在取消…");
                jobLog(app, LS(L"!用户取消"));
            }
            refreshUi(app);
            return 0;

        case UI_INSTALL_RETRY:
            beginInstall(app, app->instDistro.empty() ? app->model.instName
                                                      : app->instDistro);
            return 0;

        case UI_INSTALL_CLOSE:
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，请先点「取消」"));
                return 0;
            }
            if (app->instStage == INST_DONE && !app->pendingStart.empty()) {
                std::wstring d = app->pendingStart;
                app->pendingStart.clear();
                app->instStage = INST_IDLE;
                app->model.instStage = INST_IDLE;
                gotoPage(app, PAGE_TERMINAL);
                launchTerminal(app, d);
            } else {
                app->instStage = INST_IDLE;
                app->model.instStage = INST_IDLE;
                gotoPage(app, PAGE_TERMINAL);
            }
            refreshUi(app);
            return 0;

        case UI_EMPTY_INSTALL:
            gotoPage(app, PAGE_TERMINAL);
            app->model.distroMenuOpen = true;
            refreshOnline(app);
            refreshUi(app);
            return 0;

        case UI_DISTRO_REFRESH:
            refreshDistros(app);
            return 0;

        case UI_DISTRO_OPEN:
            if (hit.arg.empty()) return 0;
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，请先等待或取消"));
                return 0;
            }
            if (app->model.activeDistro != hit.arg) {
                app->model.activeDistro = hit.arg;
                stopPty(app);
            }
            gotoPage(app, PAGE_TERMINAL);
            launchTerminal(app, hit.arg);
            refreshUi(app);
            return 0;

        case UI_DISTRO_DEFAULT:
            if (hit.arg.empty()) return 0;
            showToast(app, LS(L"正在设为默认…"));
            if (wslSetDefault(hit.arg)) {
                showToast(app, LS(L"已将 ") + hit.arg + LS(L" 设为默认发行版"));
                refreshDistros(app);
            } else {
                showToast(app, LS(L"设置默认失败，可能已在此应用启动中"));
                refreshUi(app);
            }
            return 0;

        case UI_DISTRO_UNREGISTER:
            if (hit.arg.empty() || app->unregisterBusy) return 0;
            app->unregisterBusy = true;
            showToast(app, LS(L"正在注销 ") + hit.arg + L"…");
            markDistroBadSession(app, hit.arg);
            refreshUi(app);
            startUnregisterThread(app, hit.arg);
            return 0;

        case UI_DISTRO_INSTALL:
            if (hit.arg.empty()) return 0;
            if (installRunning(app)) {
                if (app->page != PAGE_INSTALL) gotoPage(app, PAGE_INSTALL);
                showToast(app, LS(L"已经有一个安装在进行中"));
                return 0;
            }
            stopPty(app);
            beginInstall(app, hit.arg);
            return 0;

        case UI_TREE_UP: {
            std::wstring parent = pathParent(app->folder);
            if (!parent.empty() && pathIsDir(parent)) {
                app->folder = parent;
                app->expandedDirs.clear();
                app->expandedDirs.push_back(app->folder);
                rebuildTree(app);
                refreshUi(app);
            }
            return 0;
        }

        case UI_OPEN_FOLDER: {
            std::wstring dir;
            if (pickFolder(hwnd, dir)) {
                app->folder = pathNormalize(dir);
                app->expandedDirs.clear();
                app->expandedDirs.push_back(app->folder);
                app->page = PAGE_PROJECT;
                rebuildTree(app);
                refreshUi(app);
            }
            return 0;
        }

        case UI_ENABLE_WSL: {
            if (app->wslFixBusy) {
                showToast(app, LS(L"正在处理，请稍候"));
                return 0;
            }
            if (app->model.wslState == WSL_NO_EXE) {
                showToast(app, LS(L"未找到 wsl.exe，无法自动启用"));
                return 0;
            }

            app->wslFixBusy = true;
            InterlockedExchange(&app->wslFixApproved, 0);
            InterlockedExchange(&app->wslFixElapsedMs, 0);

            app->model.fixStage = 1;              // 等待 UAC 授权
            app->model.fixStartTick = GetTickCount();
            app->model.fixTick = app->model.fixStartTick;
            app->model.fixElapsed = 0;
            app->model.fixMsg = LS(L"请在弹出的 UAC 窗口点「是」");

            WslFixArg* a = new WslFixArg();
            a->app = app;
            CreateThread(NULL, 0, WslFixThread, a, 0, NULL);

            // 进度条有扫光和秒表，动画定时器得一直转
            SetTimer(app->hwnd, TIMER_ANIM, 16, NULL);
            refreshUi(app);
            return 0;
        }

        case UI_FIX_REBOOT: {
            // 组件装完必须重启才生效。直接把原因摆出来，别让用户以为装失败了。
            // LS() 返回的是 const wchar_t*，相加前要先包成 std::wstring。
            std::wstring q = std::wstring(LS(L"WSL 组件已安装，需要重启 Windows 才能生效。\n\n"))
                           + LS(L"现在重启会关闭所有打开的程序，确定吗？");
            int ans = MessageBoxW(app->hwnd, q.c_str(), LS(L"需要重启"),
                                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (ans == IDYES) {
                // 没有 SeShutdownPrivilege 时会失败，至少把提示给出来
                if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0)) {
                    showToast(app, LS(L"系统拒绝了重启请求，请手动重启"));
                }
            }
            return 0;
        }

        case UI_TREE_ENTRY: {
            int idx = hit.index;
            if (idx < 0 || idx >= (int)app->model.tree.size()) return 0;
            const TreeRow& row = app->model.tree[(size_t)idx];

            if (row.isDir) {
                toggleExpandDir(app, row.path);
                rebuildTree(app);
                refreshUi(app);
            } else {
                openFileInEditor(app, row.path);
            }
            return 0;
        }

        case UI_EDITOR_NEW:
            newFileInProject(app);
            return 0;

        case UI_EDITOR_SAVE:
            if (app->ed->isOpen() && !app->ed->path().empty()) {
                if (app->ed->save()) refreshUi(app);
            }
            return 0;

        case UI_EDITOR_CLOSE:
            app->ed->close();
            KillTimer(hwnd, TIMER_CARET);
            rebuildTree(app);
            refreshUi(app);
            return 0;

        case UI_EDITOR_CLICK:
            editorClick(app, mx, my, isKeyDown(VK_SHIFT));
            app->edDragging = true;
            SetCapture(hwnd);
            return 0;

        case UI_SET_FONT_DEC:
            applyUiFont(app, app->model.fontSize - 1);
            return 0;
        case UI_SET_FONT_INC:
            applyUiFont(app, app->model.fontSize + 1);
            return 0;
        case UI_SET_TERMFONT_DEC:
            applyTermFont(app, app->model.termFontSize - 1);
            return 0;
        case UI_SET_TERMFONT_INC:
            applyTermFont(app, app->model.termFontSize + 1);
            return 0;
        case UI_SET_THEME_LIGHT:
            app->model.theme = 0;
            applyTerminalTheme(app);
            saveSettings(app);
            refreshUi(app);
            return 0;
        case UI_SET_THEME_DARK:
            app->model.theme = 1;
            applyTerminalTheme(app);
            saveSettings(app);
            refreshUi(app);
            return 0;

        case UI_SET_TERM_AUTO:
        case UI_SET_TERM_PTY:
        case UI_SET_TERM_PIPE: {
            int mode = (hit.action == UI_SET_TERM_AUTO) ? 0
                     : (hit.action == UI_SET_TERM_PTY ? 1 : 2);
            if (mode == app->model.terminalMode) return 0;
            app->model.terminalMode = mode;
            app->conptyOk = -1;
            saveSettings(app);

            if (app->model.hasTerminal || app->hPipeRead) {
                std::wstring distro = app->model.activeDistro;
                stopPty(app);
                app->model.hasTerminal = false;
                if (distro.empty() && !app->model.installed.empty()) {
                    distro = app->model.installed[0];
                }
                if (!distro.empty()) {
                    gotoPage(app, PAGE_TERMINAL);
                    launchTerminal(app, distro);
                }
            }
            refreshUi(app);
            return 0;
        }

        case UI_SET_LANG_AUTO:
        case UI_SET_LANG_ZH:
        case UI_SET_LANG_EN: {
            int mode = (hit.action == UI_SET_LANG_AUTO) ? LANG_AUTO
                     : (hit.action == UI_SET_LANG_ZH ? LANG_ZH : LANG_EN);
            if (mode == app->model.lang) return 0;
            app->model.lang = mode;
            langInit(mode);
            saveSettings(app);
            refreshUi(app);
            return 0;
        }

        case UI_RECHECK:
            app->model.checking = true;
            app->model.wslVersion.clear();
            if (!app->brokenDistros.empty()) {
                app->brokenDistros.clear();
                saveSettings(app);
            }
            app->suppressLaunch = true;
            CreateThread(NULL, 0, ProbeThread, app, 0, NULL);
            refreshUi(app);
            return 0;

        case UI_FIX_WSL: {
            if (installRunning(app)) {
                showToast(app, LS(L"安装进行中，请先等待或取消"));
                return 0;
            }
            app->model.instLog.clear();
            app->model.instName = LS(L"修复 WSL");
            app->instDistro.clear();
            app->instDir.clear();
            {
                int from = app->page;
                app->page = PAGE_INSTALL;
                app->model.distroMenuOpen = false;
                app->ui->beginPageTransition(from, PAGE_INSTALL);
            }
            app->instIsFix = true;
            jobLog(app, L"> wsl --update");
            if (startJob(app, L"--update")) {
                app->jobMode = true;
                instSetStage(app, INST_IMPORT, LS(L"正在更新 WSL 内核…"), L"wsl --update");
            } else {
                app->model.instErr = LS(L"无法启动 wsl.exe");
                instSetStage(app, INST_FAILED, LS(L"修复失败"), app->model.instErr);
            }
            refreshUi(app);
            return 0;
        }

        default:
            break;
        }

        if (app->page == PAGE_TERMINAL && app->model.hasTerminal && app->term && app->rend) {
            int l, t, r, b;
            app->ui->contentRect(rc.right, rc.bottom, l, t, r, b, app->model.page);
            if (mx < l || my < t) return 0;

            SetCapture(hwnd);
            app->dragging = true;

            int c = (mx - l) / app->rend->cellW();
            int rw = (my - t) / app->rend->cellH();
            if (c < 0) c = 0;
            if (rw < 0) rw = 0;
            if (c > app->term->cols() - 1) c = app->term->cols() - 1;
            if (rw > app->term->rows() - 1) rw = app->term->rows() - 1;

            app->sel.r0 = app->sel.r1 = rw;
            app->sel.c0 = app->sel.c1 = c;
            app->sel.active = true;
            app->sel.normalize();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!app || !app->ui) break;

        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);

        RECT rc;
        GetClientRect(hwnd, &rc);

        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        if (app->edDragging && app->ed->isOpen()) {
            editorClick(app, mx, my, true);
            return 0;
        }

        if (app->dragging && app->term && app->rend) {
            int l, t, r, b;
            app->ui->contentRect(rc.right, rc.bottom, l, t, r, b, app->model.page);

            int c = (mx - l) / app->rend->cellW();
            int rw = (my - t) / app->rend->cellH();
            if (c < 0) c = 0;
            if (rw < 0) rw = 0;
            if (c > app->term->cols() - 1) c = app->term->cols() - 1;
            if (rw > app->term->rows() - 1) rw = app->term->rows() - 1;

            app->sel.r1 = rw;
            app->sel.c1 = c;
            app->sel.active = true;
            app->sel.normalize();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (app->ui->updateHover(rc.right, rc.bottom, mx, my, app->model)) {
            SetTimer(hwnd, TIMER_ANIM, 16, NULL);
            app->animTick = GetTickCount();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (app && app->ui) {
            app->ui->clearHover();
            SetTimer(hwnd, TIMER_ANIM, 16, NULL);
            app->animTick = GetTickCount();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (app) {
            app->dragging = false;
            app->edDragging = false;
            ReleaseCapture();
            if (app->sel.r0 == app->sel.r1 && app->sel.c0 == app->sel.c1) app->sel.clear();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL: {
        if (!app || !app->ui) break;

        int delta = GET_WHEEL_DELTA_WPARAM(wParam);

        if (app->model.distroMenuOpen) {
            RECT mrc;
            GetClientRect(hwnd, &mrc);
            int step = (delta > 0) ? -3 : 3;
            if (app->ui->menuScrollBy(step, mrc.right, mrc.bottom, app->model)) {
                refreshUi(app);
            }
            return 0;
        }

        if (isKeyDown(VK_CONTROL)) {
            applyTermFont(app, app->model.termFontSize + (delta > 0 ? 1 : -1));
            return 0;
        }

        POINT pt;
        pt.x = (int)(short)LOWORD(lParam);
        pt.y = (int)(short)HIWORD(lParam);
        ScreenToClient(hwnd, &pt);

        RECT rc;
        GetClientRect(hwnd, &rc);

        int l, t, r, b;
        app->ui->contentRect(rc.right, rc.bottom, l, t, r, b, app->model.page);

        if (app->page == PAGE_DISTRO) {
            int step = (delta > 0) ? -3 : 3;
            if (app->ui->distroScrollBy(step, rc.right, rc.bottom, app->model)) {
                refreshUi(app);
            }
            return 0;
        }

        if (app->page == PAGE_PROJECT && app->model.hasFolder) {
            if (pt.x < l + app->ui->treeW()) {
                app->ui->treeScrollBy(delta > 0 ? -3 : 3, rc.bottom, app->model);
                refreshUi(app);
                return 0;
            }

            if (app->ed->isOpen()) {
                int step = 3;
                int y = app->ed->scrollY() + (delta > 0 ? -step : step);
                if (y < 0) y = 0;
                int maxY = app->ed->lineCount() - 1;
                if (y > maxY) y = maxY;
                app->ed->setScroll(y, app->ed->scrollX());
                scheduleRepaint(app);
                return 0;
            }
            return 0;
        }

        if (app->page != PAGE_TERMINAL || !app->term) return 0;

        if (delta > 0) app->scrollOffset += 3;
        else           app->scrollOffset -= 3;

        int maxOff = app->term->scrollbackSize();
        if (app->scrollOffset < 0) app->scrollOffset = 0;
        if (app->scrollOffset > maxOff) app->scrollOffset = maxOff;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_RBUTTONUP:
        if (app && app->page == PAGE_TERMINAL) {
            app->sel.clear();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_DESTROY:
        if (app) {
            abortInstall(app);
            stopPty(app);
            saveSettings(app);
            KillTimer(hwnd, TIMER_REPAINT);
            KillTimer(hwnd, TIMER_CARET);
            KillTimer(hwnd, TIMER_ANIM);
            KillTimer(hwnd, TIMER_PTYWAIT);
            layerRelease();
            DeleteCriticalSection(&app->cs);
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static std::wstring skipProgramName(const wchar_t* cmd) {
    if (!cmd) return std::wstring();
    const wchar_t* p = cmd;
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'"') {
        ++p;
        while (*p && *p != L'"') ++p;
        if (*p == L'"') ++p;
    } else {
        while (*p && *p != L' ' && *p != L'\t') ++p;
    }
    return std::wstring(p);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDPIAware();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    SetEnvironmentVariableW(L"WSL_UTF8", L"1");

    std::wstring args = trimW(skipProgramName(GetCommandLineW()));

    if (args == L"-h" || args == L"--help" || args == L"/?") {
        std::wstring help = L"EWSL (EasyWSL) ";
        help += kAppVersion;
        help += LS(L" - 内嵌 WSL 终端 / 代码编辑器\r\nMIT License\r\n\r\n  EWSL.exe              启动默认发行版\r\n  EWSL.exe -d <name>    指定发行版\r\n\r\n左侧边栏\r\n  终端    切换或安装 Linux 发行版\r\n  项目    打开文件夹、浏览目录、编辑代码\r\n  设置    字体大小、界面主题、终端模式\r\n\r\n快捷键\r\n  Ctrl + S            保存文件\r\n  Ctrl + 滚轮         缩放字体\r\n  Ctrl + Shift + C/V  终端复制 / 粘贴\r\n");
        MessageBoxW(NULL, help.c_str(), L"EWSL", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (!args.empty()) {
        if (args.size() > 9 && args.compare(0, 9, L"--distro=") == 0) {
            g_startDistro = trimW(args.substr(9));
        } else if (args.size() >= 2 && (args[0] == L'-' || args[0] == L'/') &&
                   (args[1] == L'd' || args[1] == L'D')) {
            g_startDistro = trimW(args.substr(2));
        } else {
            g_startDistro = args;
        }
    }

    App app;
    g_app = &app;

    InitializeCriticalSection(&app.cs);

    int dpi = 96;
    {
        HDC screen = GetDC(NULL);
        if (screen) {
            dpi = GetDeviceCaps(screen, LOGPIXELSY);
            ReleaseDC(NULL, screen);
        }
    }

    app.ui = new Ui();
    if (!app.ui->init(dpi)) {
        MessageBoxW(NULL, LS(L"初始化界面失败。"), L"EWSL", MB_OK | MB_ICONERROR);
        delete app.ui;
        DeleteCriticalSection(&app.cs);
        return 1;
    }

    app.rend = new Renderer();
    if (!app.rend->create(kInitialFontHeight)) {
        MessageBoxW(NULL, LS(L"初始化字体失败。"), L"EWSL", MB_OK | MB_ICONERROR);
        delete app.rend;
        delete app.ui;
        DeleteCriticalSection(&app.cs);
        return 1;
    }

    app.ed = new Editor();

    loadSettings(&app);
    applyTerminalTheme(&app);
    app.ui->prepare(app.model.termFontSize);
    app.ui->setUiFontSize(app.model.fontSize);
    app.rend->create(app.model.termFontSize);

    int cellW = app.rend->cellW();
    int cellH = app.rend->cellH();
    if (cellW < 4) cellW = 8;
    if (cellH < 8) cellH = 16;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (screenW < 640) screenW = 1280;
    if (screenH < 480) screenH = 720;

    int sideW = app.ui->sideW();
    if (sideW < 120) sideW = 200;

    int wantW = screenW * 2 / 3;
    int wantH = screenH * 2 / 3;
    if (wantW < 900) wantW = 900;
    if (wantH < 560) wantH = 560;

    int cols = (wantW - sideW) / cellW;
    int rows = (wantH - app.ui->titleH()) / cellH;
    if (cols < 20) cols = 20;
    if (rows < 4) rows = 4;

    app.term = new Terminal(cols, rows);

    int cxIcon = GetSystemMetrics(SM_CXICON);
    int cxSmIcon = GetSystemMetrics(SM_CXSMICON);
    // Deliberately no LR_SHARED: it caches by resource id, so the second call
    // would hand back the 32px bitmap for the 16px request as well and the
    // caption icon would look wrong. Each size is created on its own.
    HICON hAppIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP),
                                       IMAGE_ICON, cxIcon, cxIcon,
                                       LR_DEFAULTCOLOR);
    if (!hAppIcon) hAppIcon = LoadIconW(NULL, IDI_APPLICATION);
    HICON hAppIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP),
                                         IMAGE_ICON, cxSmIcon, cxSmIcon,
                                         LR_DEFAULTCOLOR);
    if (!hAppIconSm) hAppIconSm = hAppIcon;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = kClassName;
    wc.lpszMenuName = NULL;
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIconSm;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, LS(L"注册窗口类失败。"), L"EWSL", MB_OK | MB_ICONERROR);
        delete app.term;
        delete app.rend;
        delete app.ed;
        delete app.ui;
        DeleteCriticalSection(&app.cs);
        return 1;
    }

    // 客户区先给个下限，保证侧栏 + 至少一屏内容放得下。
    int clientW = cols * cellW + sideW;
    int clientH = rows * cellH + app.ui->titleH();
    if (clientW < 880) clientW = 880;
    if (clientH < 560) clientH = 560;
    int winW = clientW;
    int winH = clientH;

    // Centre inside the work area, not the whole screen. SM_CYSCREEN still counts
    // the strip the taskbar sits on, so centring on it can tuck the custom title
    // bar under the taskbar - or push the bottom edge off the display entirely on
    // a screen that is only a little taller than the window.
    RECT wa;
    memset(&wa, 0, sizeof(wa));
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0) ||
        wa.right <= wa.left || wa.bottom <= wa.top) {
        wa.left = 0;
        wa.top = 0;
        wa.right = screenW;
        wa.bottom = screenH;
    }
    int availW = wa.right - wa.left;
    int availH = wa.bottom - wa.top;

    // Never open larger than the work area either: a window taller than the
    // desktop puts its own title bar buttons out of reach. WM_SIZE right after
    // creation re-derives cols/rows, so shrinking here costs nothing.
    if (winW > availW) winW = availW;
    if (winH > availH) winH = availH;

    int x = wa.left + (availW - winW) / 2;
    int y = wa.top + (availH - winH) / 2;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;

    // 客户区尺寸 -> 外框尺寸。少了这一步，CreateWindow 收到的 winW/winH 是按
    // 客户区算的，但 WS_THICKFRAME 会在四周补上非客户区边框，于是真实客户区
    // 比 cols*cellW 窄了大约一个单元格（通常 7-8px，DWM 隐形扩展边框也在内），
    // 最右边那列画出去被裁掉——症状是长行不折行、末尾字符缺一块。
    RECT cr = { 0, 0, clientW, clientH };
    if (!AdjustWindowRectEx(&cr, style, FALSE, 0)) {
        cr.right = clientW;
        cr.bottom = clientH;
    }
    winW = cr.right - cr.left;
    winH = cr.bottom - cr.top;

    HWND hwnd = CreateWindowExW(0, kClassName, L"EWSL", style,
                                x, y, winW, winH,
                                NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, LS(L"创建窗口失败。"), L"EWSL", MB_OK | MB_ICONERROR);
        delete app.term;
        delete app.rend;
        delete app.ed;
        delete app.ui;
        DeleteCriticalSection(&app.cs);
        return 1;
    }

    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIconSm);
    SetClassLongPtrW(hwnd, GCLP_HICON, (LONG_PTR)hAppIcon);
    SetClassLongPtrW(hwnd, GCLP_HICONSM, (LONG_PTR)hAppIconSm);

    applyDwm(hwnd);

    ShowWindow(hwnd, (nCmdShow == 0) ? SW_SHOWNORMAL : nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    bbRelease();

    delete app.term;
    delete app.rend;
    delete app.ed;
    delete app.ui;
    g_app = NULL;

    return (int)msg.wParam;
}
