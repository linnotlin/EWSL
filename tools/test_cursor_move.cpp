// 复现「提示符插在行中间」：CSI 光标移动没有取消 wrapPending。
//
// 标准终端语义（xterm/VTE 都如此）：**任何显式光标移动都清掉 wrapPending**。
// 理由很简单——待折行是「下一个字符会触发换行」这个延迟决定，
// 而光标一旦被显式移动，这个决定就作废了。
//
// 症状链（readline 编辑长行时）：
//   1. 用户输入填满整行 -> 最后一个字符落在末列，wrapPending = true
//   2. 用户按左箭头 -> readline 发 CSI D
//   3. 我们的 CSI D 只做 m_cx -= 1，**没清 wrapPending**
//   4. readline 重画整行（CR + 整行文本）-> CR 清了 cx 但在 ST_GROUND
//      之外的路径上，\r 分支确实清了 wrapPending……
//   但如果是 CSI G（绝对定位）或 backspace 之后的写入，wrapPending 还在，
//   于是下一个字符在 emit() 开头先折行 -> 内容写到下一行，光标列错位。
//
// 本测试直接锁死这个不变量：任何 CSI 光标移动后，wrapPending 必须为 false。

#include "src/terminal.h"

#include <cstdio>
#include <string>

using namespace wslterm;

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

static void feed(Terminal& t, const std::string& s) {
    t.feed(s.data(), s.size());
    t.flushEncoding();
}

static std::wstring rowText(Terminal& t, int r) {
    std::vector<Cell> row = t.visibleLine(r, 0);
    std::wstring s;
    for (size_t i = 0; i < row.size(); ++i) {
        uint32_t c = row[i].ch[0];
        if (c == CELL_TAIL) continue;
        if (c == CELL_EMPTY) s.push_back(L' ');
        else s.push_back((wchar_t)c);
    }
    return s;
}

static std::string trim(std::wstring s) {
    while (!s.empty() && s[s.size() - 1] == L' ') s.erase(s.size() - 1);
    return s.empty() ? std::string() : std::string("ok");
}

// 填满整行让 wrapPending 置起来
static std::string fillRow(int cols) {
    std::string s;
    for (int i = 0; i < cols; ++i) s += "x";
    return s;
}

int main() {
    // 1) CSI D（左移）后不能残留待折行
    {
        const int cols = 10;
        Terminal t(cols, 4);
        feed(t, fillRow(cols));            // 填满 -> wrapPending = true
        feed(t, "\x1b[D");                 // 左移一格
        feed(t, "Y");
        std::wstring r0 = rowText(t, 0);
        std::wstring r1 = rowText(t, 1);
        printf("1) after CSI D: r0=[%ls] r1=[%ls] cursor=(%d,%d)\n",
               r0.c_str(), r1.c_str(), t.cursorX(), t.cursorY());
        // Y 应该写在末列前那一格，而不是折到下一行
        check(t.cursorY() == 0, "CSI D cancels the pending wrap (no fold)");
        check(r0.size() > 9 && r0[8] == L'Y', "Y landed on the vacated cell");
    }

    // 2) CSI C（右移）后不能残留待折行
    {
        const int cols = 10;
        Terminal t(cols, 4);
        feed(t, fillRow(cols));
        feed(t, "\x1b[5C");                // 右移5 格（被 clamp 到末列）
        feed(t, "Z");
        printf("2) after CSI C: cursor=(%d,%d) row0 last=[%ls]\n",
               t.cursorX(), t.cursorY(), rowText(t, 0).c_str());
        check(t.cursorY() == 0, "CSI C cancels the pending wrap");
    }

    // 3) CSI G（绝对定位）后不能残留待折行
    {
        const int cols = 10;
        Terminal t(cols, 4);
        feed(t, fillRow(cols));
        feed(t, "\x1b[3G");                // 定位到第 3 列
        feed(t, "W");
        printf("3) after CSI 3G: cursor=(%d,%d) row0=[%ls]\n",
               t.cursorX(), t.cursorY(), rowText(t, 0).c_str());
        check(t.cursorY() == 0, "CSI G cancels the pending wrap");
        check(t.cursorX() == 3, "cursor sits right after the rewritten cell");
    }

    // 4) BS 之后不能残留待折行（现有代码已清，锁死）
    {
        const int cols = 10;
        Terminal t(cols, 4);
        feed(t, fillRow(cols));
        feed(t, "\b");
        feed(t, "Q");
        printf("4) after BS: cursor=(%d,%d)\n", t.cursorX(), t.cursorY());
        check(t.cursorY() == 0, "BS cancels the pending wrap");
    }

    // 5) 真实场景：填满整行 -> 左移 -> 回车，重画后提示符必须从 0 列开始
    {
        const int cols = 40;
        Terminal t(cols, 5);
        feed(t, "[root@PC-20261005EZUS EWSL-main]# ");
        feed(t, fillRow(cols - 34));       // 把这一行填满
        feed(t, "\x1b[D\x1b[D\x1b[D");// 左移3 格（readline 风格）
        feed(t, "\r");                     // readline 重画前先 CR
        feed(t, "[root@PC-20261005EZUS EWSL-main]# ");
        std::wstring r = rowText(t, 0);
        while (!r.empty() && r[r.size() - 1] == L' ') r.erase(r.size() - 1);
        printf("5) redraw row0=[%ls] cursor=(%d,%d)\n", r.c_str(),
               t.cursorX(), t.cursorY());
        check(t.cursorY() == 0, "redraw stays on the same row");
        check(r.compare(0, 6, L"[root@") == 0,
              "the redrawn prompt starts at column 0");
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
