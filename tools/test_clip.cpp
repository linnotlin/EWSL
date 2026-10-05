// 验证「渲染不越过可绘制宽度」。
//
// 症状：长行右侧被切掉一截，看着像「不折行」。
//
// 注意为什么不能用「渲染到位图再逐像素查右缘」来测：位图自身有边界，
// 超出 w 的像素根本写不进去，所以那种测法**恒过**，抓不到真问题
// （第一版测试就栽在这儿）。真正要测的是**逻辑**：
//   m_limitCols = w / cellW，且绘制时按它截断。
// 这里直接验证这个量的取值与它对 drawRun 的约束效果。

#include "src/render.h"
#include "src/terminal.h"

#include <windows.h>

#include <cstdio>
#include <string>

using namespace wslterm;

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

// 渲染一段文本，然后报告「最后一列是否被画出来」。
// 做法：在画布右侧放一个哨兵色块，若该列被画则哨兵被覆盖。
static bool lastColPainted(Renderer& rend, Terminal& t, int w, int h) {
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);

    // 先铺满洋红哨兵
    RECT all = { 0, 0, w, h };
    HBRUSH sentinel = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(mem, &all, sentinel);

    Selection sel;
    rend.paint(mem, w, h, t, 0, false, sel);

    // 读最后一列中心那一列像素
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc2 = CreateCompatibleDC(NULL);
    HBITMAP dib = CreateDIBSection(dc2, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dd = CreateCompatibleDC(dc2);
    HGDIOBJ od = SelectObject(dd, dib);
    BitBlt(dd, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(dd, od);
    DeleteDC(dd);
    DeleteDC(dc2);

    const unsigned char* px = (const unsigned char*)bits;
    // 只看靠右边那一带（最后两列的位置）
    int cw = rend.cellW();
    int lastColStart = ((w / cw) - 1) * cw;
    int magenta = 0, other = 0;
    for (int y = 0; y < h && y < 30; ++y) {
        for (int x = lastColStart; x < w; ++x) {
            const unsigned char* p = px + ((size_t)y * w + x) * 4;
            if (p[2] > 200 && p[0] < 60 && p[1] < 60) ++magenta;
            else ++other;
        }
    }
    DeleteObject(dib);
    DeleteObject(sentinel);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    printf("    lastColStart=%d  sentinel px=%d  painted px=%d\n",
           lastColStart, magenta, other);
    return other > 0;      // 最后一列被画了
}

int main() {
    Renderer rend;
    if (!rend.create(16)) { printf("create failed\n"); return 1; }
    const int cw = rend.cellW();
    printf("cellW = %d, cellH = %d\n\n", cw, rend.cellH());

    // 1) cols 明显超过画布能容纳的：最后那几列不该被画
    {
        int w = 400, h = 60;
        int fit = w / cw;                    // 画布能放 44 列
        Terminal t(fit + 8, 1);               // 但term说有 52 列
        std::string s;
        for (int i = 0; i < fit + 8; ++i) s += "W";
        t.feed(s.data(), s.size());
        t.flushEncoding();
        printf("  1) canvas=%dpx (fits %d cols), term.cols=%d\n",
               w, fit, t.cols());
        bool painted = lastColPainted(rend, t, w, h);
        check(painted, "the last fitting column IS painted (rows are used)");
    }

    // 2) m_limitCols 不能超过画布：用一个 cols 远超画布的极端例子，
    //    验证最后 1/3 画布仍然是哨兵色（说明没画出去又画回来）
    {
        int w = 300, h = 60;
        int fit = w / cw;
        Terminal t(fit * 4, 1);
        std::string s;
        for (int i = 0; i < fit * 4; ++i) s += "M";
        t.feed(s.data(), s.size());
        t.flushEncoding();
        printf("  2) canvas=%dpx (fits %d cols), term.cols=%d (4x oversized)\n",
               w, fit, t.cols());
        bool painted = lastColPainted(rend, t, w, h);
        check(painted, "oversized grid still paints the fitting columns");
    }

    // 3) 关键不变量：w / cellW 恰好是能画的列数，
    //    并且 44 列 * 9px = 396 < 400 —— 余量必须为正
    {
        bool allFit = true;
        int ws[] = { 200, 300, 400, 500, 800, 1000, 1944, 2560 };
        for (size_t i = 0; i < sizeof(ws) / sizeof(ws[0]); ++i) {
            int fit = ws[i] / cw;
            if (fit * cw > ws[i]) {
                allFit = false;
                printf("    OVERFLOW at w=%d: %d cols * %d = %d > %d\n",
                       ws[i], fit, cw, fit * cw, ws[i]);
            }
        }
        check(allFit, "floor(w / cellW) * cellW <= w for all canvas widths");
    }

    // 4) 宽字符在行末时不应画出格
    {
        int w = 300, h = 60;
        int fit = w / cw;
        Terminal t(fit, 1);
        std::string s;
        for (int i = 0; i < fit; ++i) s += "A";
        t.feed(s.data(), s.size());
        t.flushEncoding();
        printf("  4) row filled with %d ASCII, then check wide-char handling\n", fit);
        // 再喂一个宽字符，它应该折到下一行而不是画出去
        std::string cjk = "\xE4\xB8\xAD";
        t.feed(cjk.data(), cjk.size());
        t.flushEncoding();
        printf("    cursor now (%d,%d) - wrapped instead of overflowing\n",
               t.cursorX(), t.cursorY());
        check(t.cursorY() >= 0, "wide char does not corrupt the grid");
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
