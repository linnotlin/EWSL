// GUI regression check for the real dist/EWSL.exe:
//   1. one Backspace deletes exactly one character (not two)
//   2. the page slide moves glyphs and cards by the same amount
//   3. the settings page renders its right-hand column (Arch keyring hint + author)
//
//   verify.exe <pngPrefix> [distro] [exe] [quick]
//
// `quick` skips 1 and 2 and only walks the pages, so a theme switch in
// %APPDATA%\WslEmbed\settings.ini can be checked in one short run.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace Gdiplus;

static const wchar_t* kClass = L"EWSLWndClass";

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pInfo = (ImageCodecInfo*)malloc(size);
    if (!pInfo) return -1;
    GetImageEncoders(num, size, pInfo);
    int found = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(pInfo[i].MimeType, format) == 0) {
            *pClsid = pInfo[i].Clsid;
            found = (int)i;
            break;
        }
    }
    free(pInfo);
    return found;
}

// Bring the window back on screen before a timed run, so the capture itself can
// stay as short as possible and land inside the 420 ms page transition.
//
// Shot() reads pixels straight off the screen, so the window has to be fully
// inside the work area or the part hanging off reads back as black. Keep the
// size the app picked, just re-centre it.
static void Prep(HWND hwnd) {
    if (!IsWindow(hwnd)) return;
    if (IsIconic(hwnd)) { ShowWindow(hwnd, SW_RESTORE); Sleep(400); }

    RECT wr;
    memset(&wr, 0, sizeof(wr));
    GetWindowRect(hwnd, &wr);
    RECT wa;
    memset(&wa, 0, sizeof(wa));
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    int availW = wa.right - wa.left;
    int availH = wa.bottom - wa.top;
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;
    if (w > availW) w = availW;
    if (h > availH) h = availH;
    int x = wa.left + (availW - w) / 2;
    int y = wa.top + (availH - h) / 2;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    Sleep(250);

    // Report the landed geometry. A shot taken while any part of the window sits
    // outside the work area reads back black there, and that is silent otherwise:
    // the PNG is still full size, just half empty.
    RECT got;
    memset(&got, 0, sizeof(got));
    GetWindowRect(hwnd, &got);
    printf("prep: [%ld,%ld,%ld,%ld] %ldx%ld (work %dx%d@%ld,%ld)%s\n",
           got.left, got.top, got.right, got.bottom,
           got.right - got.left, got.bottom - got.top,
           availW, availH, wa.left, wa.top,
           (got.left < wa.left || got.top < wa.top || got.right > wa.right ||
            got.bottom > wa.bottom) ? "  ** OFF WORK AREA **" : "");
    fflush(stdout);
}

static void Shot(HWND hwnd, const char* path, bool verbose) {
    if (!IsWindow(hwnd)) { printf("shot(%s): window gone\n", path); return; }

    RECT rc;
    memset(&rc, 0, sizeof(rc));
    if (!GetWindowRect(hwnd, &rc)) { printf("shot: no rect\n"); return; }
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (verbose || w < 800 || h < 600)
        printf("shot(%s): rect=[%ld,%ld,%ld,%ld] %dx%d\n", path, rc.left, rc.top,
               rc.right, rc.bottom, w, h);
    if (w <= 0 || h <= 0) { printf("shot: bad rect\n"); return; }

    if (verbose) {
        RECT cr;
        memset(&cr, 0, sizeof(cr));
        GetClientRect(hwnd, &cr);
        printf("  geom: win=[%ld,%ld,%ld,%ld] client=%ldx%ld\n",
               rc.left, rc.top, rc.right, rc.bottom, cr.right, cr.bottom);
        fflush(stdout);
    }

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);

    std::vector<unsigned char> px((size_t)w * (size_t)h * 4);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    GetDIBits(mem, bmp, 0, (UINT)h, px.data(), &bi, DIB_RGB_COLORS);

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 511);

    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) >= 0) {
        Bitmap b(w, h, w * 4, PixelFormat32bppRGB, px.data());
        if (b.Save(wpath, &png, NULL) != Ok) printf("shot: save failed %s\n", path);
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void Type(HWND hwnd, const char* utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 1) return;
    std::vector<wchar_t> w((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    for (int i = 0; i < n - 1; ++i) {
        PostMessageW(hwnd, WM_CHAR, (WPARAM)w[(size_t)i], 0);
        Sleep(6);
    }
}

// A real keystroke is only the key down: the app's own TranslateMessage turns it
// into the one WM_CHAR the keyboard would produce. Posting a WM_CHAR as well
// would inject a second, duplicate keystroke.
static void KeyPress(HWND hwnd, int vk) {
    UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    LPARAM lp = 1 | ((LPARAM)(sc & 0xFF) << 16);
    PostMessageW(hwnd, WM_KEYDOWN, (WPARAM)vk, lp);
    Sleep(40);
}

typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

static int WinDpi(HWND hwnd) {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN_GetDpiForWindow f =
        u ? (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow") : NULL;
    int d = f ? (int)f(hwnd) : 96;
    if (d < 72) d = 96;
    return d;
}

static void Click(HWND hwnd, int x, int y) {
    LPARAM lp = MAKELPARAM(x, y);
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(25);
    PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
}

// Centre of the 浅色 / 深色 segmented buttons on the settings page, recomputed
// with the same arithmetic src/ui.cpp uses (settingsColumns + mkCtl layout).
static void ThemeBtnCenters(HWND hwnd, int dpi, int& lightX, int& darkX, int& y) {
    double s = dpi / 96.0;
    RECT cr;
    memset(&cr, 0, sizeof(cr));
    GetClientRect(hwnd, &cr);

    int bodyL = (int)(200 * s + 0.5);
    int padX  = (int)(20 * s + 0.5);
    int gap   = (int)(22 * s + 0.5);
    int avail = (cr.right - bodyL) - 2 * padX;
    int minL  = (int)(220 * s + 0.5);

    int listW = (int)(560 * s + 0.5);
    if (listW > avail) listW = avail;
    int sideW = avail - listW - gap;
    if (sideW < (int)(230 * s + 0.5)) {
        sideW = (avail - gap) / 2;
        if (sideW < 0) sideW = 0;
        listW = avail - gap - sideW;
        if (listW < minL) { listW = minL; sideW = avail - gap - listW; }
    }
    if (sideW < (int)(150 * s + 0.5)) sideW = 0;

    int setLeft  = bodyL + padX;
    int setRight = setLeft + listW;

    int titleH   = (int)(46 * s + 0.5);
    int setRowH  = (int)(46 * s + 0.5);
    int ctlH     = (int)(26 * s + 0.5);
    int listTop  = titleH + (int)(18 * s + 0.5) + (int)(34 * s + 0.5);

    y = listTop + 2 * setRowH + (setRowH - ctlH) / 2 + ctlH / 2;

    int w2    = (int)(72 * s + 0.5);
    int darkR = setRight - (int)(14 * s + 0.5);
    int darkL = darkR - w2;
    int liteR = darkL - (int)(8 * s + 0.5);
    int liteL = liteR - w2;

    lightX = (liteL + liteR) / 2;
    darkX  = (darkL + darkR) / 2;
}

int main(int argc, char** argv) {
    const char* prefix = (argc > 1) ? argv[1] : "tools/v";
    const char* distro = (argc > 2) ? argv[2] : "FedoraLinux-44";
    bool quick = (argc > 4 && strcmp(argv[4], "quick") == 0);
    bool themeMode = (argc > 4 && strcmp(argv[4], "theme") == 0);
    bool narrowMode = (argc > 4 && strcmp(argv[4], "narrow") == 0);
    bool shotsMode = (argc > 4 && strcmp(argv[4], "shots") == 0);

    SetProcessDPIAware();
    // Screenshots read the real screen, so keep the display from sleeping.
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

    ULONG_PTR token = 0;
    GdiplusStartupInput gsi;
    GdiplusStartup(&token, &gsi, NULL);

    wchar_t target[512];
    {
        const char* exeArg = (argc > 3) ? argv[3] : "dist/EWSL.exe";
        MultiByteToWideChar(CP_UTF8, 0, exeArg, -1, target, 511);
    }

    wchar_t exePath[MAX_PATH];
    GetFullPathNameW(target, MAX_PATH, exePath, NULL);

    std::wstring cmd = L"\"";
    cmd += exePath;
    cmd += L"\" -d ";
    {
        wchar_t d[256];
        MultiByteToWideChar(CP_UTF8, 0, distro, -1, d, 255);
        cmd += d;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    if (!CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("CreateProcess failed err=%lu\n", GetLastError());
        return 1;
    }
    printf("launched pid=%lu\n", pi.dwProcessId);

    HWND hwnd = NULL;
    for (int i = 0; i < 200; ++i) {
        hwnd = FindWindowW(kClass, NULL);
        if (hwnd) break;
        Sleep(100);
    }
    if (!hwnd) {
        printf("window not found\n");
        TerminateProcess(pi.hProcess, 0);
        return 1;
    }

    // Put the window fully inside the work area before anything is captured.
    // Shot() reads pixels straight off the screen, so a window that is still
    // hanging off the edge - or one that is bigger than the work area - comes
    // back with a black band in the PNG. Prep() keeps the size the app chose and
    // only re-centres (clamping if the app overshot), so it is safe to call here
    // and again before each later burst.
    ShowWindow(hwnd, SW_SHOW);
    Prep(hwnd);
    SetForegroundWindow(hwnd);
    Sleep(1200);

    int dpi = WinDpi(hwnd);
    double s = dpi / 96.0;
    int titleH = (int)(46 * s + 0.5);
    int navH   = (int)(44 * s + 0.5);
    int sideW  = (int)(200 * s + 0.5);
    int navTop = titleH + (int)(8 * s + 0.5);
    printf("dpi=%d titleH=%d navH=%d sideW=%d navTop=%d\n", dpi, titleH, navH,
           sideW, navTop);
    fflush(stdout);

    printf("waiting for first prompt...\n");
    fflush(stdout);
    Sleep(10000);

    char p[512];
    sprintf(p, "%s-term.png", prefix);
    Shot(hwnd, p, true);

    int navX = sideW / 2;
    int navY[4];
    for (int i = 0; i < 4; ++i) navY[i] = navTop + (int)((i + 0.5) * navH);

    if (themeMode) {
        // ---- live theme switch: click 浅色 / 深色 and watch the terminal ----
        int lx = 0, dx = 0, ty = 0;
        ThemeBtnCenters(hwnd, dpi, lx, dx, ty);
        printf("theme buttons: light=(%d,%d) dark=(%d,%d)\n", lx, ty, dx, ty);
        fflush(stdout);

        Prep(hwnd);
        Click(hwnd, navX, navY[3]);
        Sleep(1500);
        sprintf(p, "%s-set-dark.png", prefix);
        Shot(hwnd, p, false);

        Click(hwnd, lx, ty);
        Sleep(1200);
        sprintf(p, "%s-set-light.png", prefix);
        Shot(hwnd, p, false);

        Click(hwnd, navX, navY[0]);
        Sleep(1200);
        sprintf(p, "%s-term-light.png", prefix);
        Shot(hwnd, p, true);

        Click(hwnd, navX, navY[3]);
        Sleep(1200);
        Click(hwnd, dx, ty);
        Sleep(1200);
        Click(hwnd, navX, navY[0]);
        Sleep(1200);
        sprintf(p, "%s-term-dark.png", prefix);
        Shot(hwnd, p, true);

        printf("done, terminating\n");
        fflush(stdout);
        TerminateProcess(pi.hProcess, 0);
        GdiplusShutdown(token);
        return 0;
    }

    if (shotsMode) {
        // ---- README 截图集：走一遍四个页面 + 发行版浮层 ----------------------
        // 标题栏里的发行版按钮：与 src/ui.cpp 的 layout() 用同一组算式。
        RECT cr;
        memset(&cr, 0, sizeof(cr));
        GetClientRect(hwnd, &cr);
        int pad  = (int)(14 * s + 0.5);
        int bs   = (int)(32 * s + 0.5);
        int g2   = (int)(2 * s + 0.5);
        int rgt  = cr.right - pad - 3 * bs - 2 * g2 - (int)(8 * s + 0.5);
        int bw   = (int)(176 * s + 0.5);
        if (bw > rgt - sideW - pad * 2) bw = rgt - sideW - pad * 2;
        if (bw < (int)(96 * s + 0.5)) bw = (int)(96 * s + 0.5);
        int dbx = rgt - bw / 2;
        int dby = titleH / 2;
        printf("distro button=(%d,%d) navX=%d\n", dbx, dby, navX);
        fflush(stdout);

        Prep(hwnd);
        Click(hwnd, navX, navY[0]);
        Sleep(1200);
        Type(hwnd, "uname -srm && echo ready");
        KeyPress(hwnd, VK_RETURN);
        Sleep(2200);
        sprintf(p, "%s-terminal.png", prefix);
        Shot(hwnd, p, false);
        printf("wrote %s\n", p);

        Prep(hwnd);
        Click(hwnd, dbx, dby);
        // 在线清单走 `wsl --list --online`，超时上限 25s，等它落地再抓。
        Sleep(27000);
        sprintf(p, "%s-distro-menu.png", prefix);
        Shot(hwnd, p, false);
        printf("wrote %s\n", p);
        KeyPress(hwnd, VK_ESCAPE);
        Sleep(700);

        Prep(hwnd);
        Click(hwnd, navX, navY[2]);
        Sleep(2800);
        sprintf(p, "%s-distro-page.png", prefix);
        Shot(hwnd, p, false);
        printf("wrote %s\n", p);

        Prep(hwnd);
        Click(hwnd, navX, navY[1]);
        Sleep(1600);
        sprintf(p, "%s-project.png", prefix);
        Shot(hwnd, p, false);
        printf("wrote %s\n", p);

        Prep(hwnd);
        Click(hwnd, navX, navY[3]);
        Sleep(1800);
        sprintf(p, "%s-settings.png", prefix);
        Shot(hwnd, p, false);
        printf("wrote %s\n", p);

        printf("done, terminating\n");
        fflush(stdout);
        TerminateProcess(pi.hProcess, 0);
        GdiplusShutdown(token);
        return 0;
    }

    if (narrowMode) {
        // ---- narrow window: the settings list gives up its right-hand column --
        int w = (int)(900 * s + 0.5);
        int h = (int)(720 * s + 0.5);
        Prep(hwnd);
        Click(hwnd, navX, navY[3]);
        Sleep(1800);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, w, h, SWP_NOMOVE);
        Sleep(1400);
        sprintf(p, "%s-narrow.png", prefix);
        Shot(hwnd, p, true);
        printf("done, terminating\n");
        fflush(stdout);
        TerminateProcess(pi.hProcess, 0);
        GdiplusShutdown(token);
        return 0;
    }

    if (!quick) {
        // ---- 1. Backspace --------------------------------------------------
        printf("backspace: type 11111, BS, X, Enter\n");
        fflush(stdout);
        Type(hwnd, "echo 11111");
        KeyPress(hwnd, VK_BACK);
        Type(hwnd, "X");
        KeyPress(hwnd, VK_RETURN);
        Sleep(1500);

        printf("backspace: type 22222, BS BS, Y, Enter\n");
        fflush(stdout);
        Type(hwnd, "echo 22222");
        KeyPress(hwnd, VK_BACK);
        KeyPress(hwnd, VK_BACK);
        Type(hwnd, "Y");
        KeyPress(hwnd, VK_RETURN);
        Sleep(1500);

        sprintf(p, "%s-bs.png", prefix);
        Shot(hwnd, p, false);
        printf("wrote %s\n", p);

        // ---- 2. Page slide -------------------------------------------------
        // Visit both pages once so their async content is already loaded; the
        // slide frames then only differ from the settled ones by the transition.
        printf("warm up pages\n");
        Prep(hwnd);
        Click(hwnd, navX, navY[3]);
        Sleep(1800);
        Click(hwnd, navX, navY[2]);
        Sleep(2600);
        Click(hwnd, navX, navY[0]);
        Sleep(900);

        for (int round = 0; round < 2; ++round) {
            int nav = round ? navY[2] : navY[3];
            const char* tag = round ? "distro" : "set";

            Prep(hwnd);
            printf("click nav, burst shots (%s)\n", tag);
            fflush(stdout);
            Click(hwnd, navX, nav);
            for (int i = 0; i < 5; ++i) {
                sprintf(p, "%s-%s-s%d.png", prefix, tag, i);
                Shot(hwnd, p, false);
            }
            Sleep(1000);
            sprintf(p, "%s-%s-rest.png", prefix, tag);
            Shot(hwnd, p, false);

            Click(hwnd, navX, navY[0]);
            Sleep(900);
        }
    }

    // ---- 3. Settings page: brand + right-hand info column -------------------
    Prep(hwnd);
    Click(hwnd, navX, navY[3]);
    Sleep(1800);
    sprintf(p, "%s-settings.png", prefix);
    Shot(hwnd, p, true);

    Prep(hwnd);
    Click(hwnd, navX, navY[0]);
    Sleep(1200);
    sprintf(p, "%s-light.png", prefix);
    Shot(hwnd, p, true);

    printf("done, terminating\n");
    fflush(stdout);
    TerminateProcess(pi.hProcess, 0);
    GdiplusShutdown(token);
    return 0;
}
