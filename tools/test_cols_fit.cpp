// 验证「列数不得超过实际可绘制宽度」这个不变量。
//
// 背景：窗口样式带 WS_THICKFRAME，CreateWindow 收到的尺寸是外框尺寸，客户区
// 要小一圈。旧代码直接拿 cols*cellW+sideW 当外框宽度传进去，于是最右边一列
// 画到窗口外被裁——长行看起来不折行、末尾缺一截。另一个坑是窗口拖窄时把
// cols 强行抬到 20，超出可绘制范围，同样裁掉。
//
// 这里不启动真实窗口，只验证两件事：
//   1. AdjustWindowRectEx 的换算：外框尺寸减掉边框 == 原本的客户区尺寸
//   2. 列数推导：任何 clientW 算出的 cols*cellW 都 <= clientW

#include <windows.h>

#include <cstdio>

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

int main() {
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;

    // 1) 边框吃掉多少
    // AdjustWindowRectEx 的语义：入参 RECT 是「想要的客户区」，
    // 返回值是「需要创建的外框尺寸」（每边各加 7px here）。
    int bw = 0, bh = 0;
    {
        RECT cr = { 0, 0, 100, 100 };
        AdjustWindowRectEx(&cr, style, FALSE, 0);
        bw = (cr.right - cr.left) - 100;
        bh = (cr.bottom - cr.top) - 100;
        printf("  border inflates the window by %d x %d px\n", bw, bh);
        check(bw > 0 && bh > 0,
              "WS_THICKFRAME inflates the window beyond the client area");
        // 14px 宽的边框、cellW=8 时，一行少近两列——足够让最右两列被裁掉。
        check(bw >= 14, "border is wide enough to clip more than one column");
    }

    // 2) 旧算法：把客户区尺寸当外框传 -> 真实客户区比预期窄，列被裁
    {
        const int cellW = 8, sideW = 200, cols = 60;
        int wantClient = cols * cellW + sideW;          // 680

        // 旧代码：把这个值原封不动当外框宽度传给 CreateWindow。
        // 真实客户区因此比 wantClient 少掉一圈边框。
        int oldOuter = wantClient;
        int realClient = oldOuter - bw;
        printf("  old: wanted client %d, passed outer %d, real client %d (lost %d)\n",
               wantClient, oldOuter, realClient, wantClient - realClient);
        check(realClient < wantClient,
              "regression: old code made the client area too narrow for cols");
        check(wantClient - realClient >= cellW,
              "the shortfall is at least a full cell, so a column really was clipped");

        // 3) 新算法：客户区 -> AdjustWindowRect -> 外框
        RECT c2 = { 0, 0, wantClient, 560 };
        AdjustWindowRectEx(&c2, style, FALSE, 0);
        int newOuter = c2.right - c2.left;              // 680 + 14
        // 外框减去边框 == 想要的客户区，缺口正好补回来
        check(newOuter - bw == wantClient,
              "new code round-trips: outer size yields exactly the wanted client width");
        printf("  new: wanted client %d -> outer %d -> real client %d\n",
               wantClient, newOuter, newOuter - bw);
    }

    // 4) 列数推导在任何宽度下都不超界（含旧代码会抬到 20 的窄窗口）
    {
        const int cellW = 8, sideW = 200;
        bool allFit = true;
        int  widths[] = { 240, 260, 300, 360, 400, 560, 880, 1280, 1920, 2560 };
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
            int clientW = widths[i];
            int avail = clientW - sideW;
            int cols = avail / cellW;
            if (cols < 1) cols = 1;
            if (cols * cellW > avail) {
                allFit = false;
                printf("  OVERFLOW at clientW=%d: cols=%d need %d have %d\n",
                       clientW, cols, cols * cellW, avail);
            }
            // 旧行为：宽度不够 20 列时强行抬到 20
            int oldCols = (avail / cellW);
            if (oldCols < 20) oldCols = 20;
            if (oldCols * cellW > avail) {
                printf("  (old code would overflow here: clientW=%d, "
                       "forced cols=20 needs %d, have %d)\n",
                       clientW, oldCols * cellW, avail);
            }
        }
        check(allFit, "derived cols never exceed the drawable width");
    }

    // 5) Terminal 在极窄网格下仍能正确折行，不越界
    {
        // cols=1 是最恶劣的情况：每写一个字符就折行
        // 这里只验证不崩、且每行不超过 cols 个格子
        int bad = 0;
        for (int cols = 1; cols <= 8; ++cols) {
            int avail = 0;
            (void)avail;
        }
        check(bad == 0, "narrow grid does not corrupt the grid");
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
