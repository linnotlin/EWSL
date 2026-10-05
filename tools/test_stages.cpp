#include <windows.h>
#include <windowsx.h>
#include <string>
#include <cstdio>
#include "ui.h"

using namespace wslterm;

static void saveBmp(const wchar_t* path, HDC srcDC, int w, int h) {
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP out = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC d2 = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(d2, out);
    BitBlt(d2, 0, 0, w, h, srcDC, 0, 0, SRCCOPY);
    SelectObject(d2, old);
    DeleteDC(d2);
    DeleteDC(dc);
    BITMAPFILEHEADER fh;
    memset(&fh, 0, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bi);
    fh.bfSize = (DWORD)(fh.bfOffBits + (size_t)w * h * 4);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n = 0;
    WriteFile(f, &fh, sizeof(fh), &n, NULL);
    WriteFile(f, &bi, sizeof(bi), &n, NULL);
    WriteFile(f, bits, (DWORD)((size_t)w * h * 4), &n, NULL);
    CloseHandle(f);
    DeleteObject(out);
}

static void render(Ui& ui, UiModel& m, int w, int h, const char* tag,
                   int stage, const wchar_t* msg, int elapsed) {
    m.page = PAGE_TERMINAL;
    m.hasTerminal = false;
    m.checking = false;
    m.wslMissing = true;
    m.wslState = WSL_NO_COMPONENT;
    m.fixStage = stage;
    m.fixMsg = msg;
    m.fixElapsed = elapsed;
    m.fixTick = 0;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT all = { 0, 0, w, h };
    FillRect(mem, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));

    ui.paint(mem, w, h, m, NULL, false);

    wchar_t path[512];
    swprintf(path, 512, L"C:\\Users\\Administrator\\Desktop\\EWSL-main\\tools\\stage_%hs.bmp", tag);
    saveBmp(path, mem, w, h);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    printf("wrote %s\n", tag);
}

int main() {
    int W = 1440, H = 900;
    Ui ui;
    if (!ui.init(96)) { printf("ui.init failed\n"); return 1; }
    ui.prepare(14);

    UiModel m;

    render(ui, m, W, H, "1_uac", 1, L"请在弹出的 UAC 窗口点「是」", 7);
    render(ui, m, W, H, "2_running", 2, L"正在启用系统组件，可能需要几分钟", 48);
    render(ui, m, W, H, "3_reboot", 3, L"组件已安装，重启后即可使用", 96);
    render(ui, m, W, H, "4_failed", 4, L"已取消授权，没有做任何更改", 0);
    render(ui, m, W, H, "0_idle", 0, L"", 0);

    ui.shutdown();
    return 0;
}
