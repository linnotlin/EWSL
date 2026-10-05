// 用真实抓到的 guest 字节流验证：Terminal 画出来对不对。
//
// 抓到的序列（已简化标注）：
//   <CSI ?2004h> [root@PC-...]#  <BEL> ~123 <CR><LF> <CSI ?2004l> <CR>
//   <OSC 3008..> \ bash: ~123: command not found <CR><LF>
//   <OSC 0;root@PC..> <CSI ?2004h> [root@PC-...]# 85/
//
// 关注点：<BEL> 落在 ST_GROUND 里会不会被当普通字符写出去、
// 以及提示符有没有被写到错误的列上。

#include "src/terminal.h"

#include <cstdio>
#include <string>

using namespace wslterm;

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
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

static void dump(Terminal& t, const char* tag) {
    printf("  [%s] cursor=(%d,%d)\n", tag, t.cursorX(), t.cursorY());
    for (int r = 0; r < t.rows(); ++r) {
        std::wstring s = rowText(t, r);
        while (!s.empty() && s[s.size() - 1] == L' ') s.erase(s.size() - 1);
        if (s.empty()) continue;
        printf("r%d: [%ls]\n", r, s.c_str());
    }
}

static void feed(Terminal& t, const std::string& s) {
    t.feed(s.data(), s.size());
    t.flushEncoding();
}

int main() {
    // 真实场景：提示符 + BEL + 用户输入 + 回车
    {
        Terminal t(80, 6);
        feed(t, std::string("\x1b[?2004h") + "[root@PC-20261005EZUS EWSL-main]# ");
        feed(t, "\x07");                       // 裸 BEL
        feed(t, "~123");
        feed(t, "\r\n");
        feed(t, "\x1b[?2004l\r");
        feed(t, "\x1b]3008;start=x\x1b\\");
        feed(t, "bash: ~123: command not found");
        feed(t, "\r\n");
        feed(t, "\x1b]3008;end=x\x1b\\");
        feed(t, "\x1b]3008;start=x\x1b\\");
        feed(t, "\x1b]0;root@PC-20261005EZUS:/mnt/c\x07");
        feed(t, "\x1b[?2004h");
        feed(t, "[root@PC-20261005EZUS EWSL-main]# ");
        dump(t, "after enter");

        // r0 = 提示符 + 输入, r1 = 报错, r2 = 新提示符。
        // 三行各归其位，光标停在 r2 末尾等输入。
        check(t.cursorY() == 2, "cursor sits on the new prompt line (r2)");
        std::wstring r0 = rowText(t, 0);
        std::wstring r1 = rowText(t, 1);
        std::wstring r2 = rowText(t, 2);
        while (!r1.empty() && r1[r1.size() - 1] == L' ') r1.erase(r1.size() - 1);
        while (!r2.empty() && r2[r2.size() - 1] == L' ') r2.erase(r2.size() - 1);
        check(r0.compare(0, 6, L"[root@") == 0, "row 0 starts with the prompt");
        check(r1 == L"bash: ~123: command not found", "row 1 is the error message");
        check(r2.compare(0, 6, L"[root@") == 0,
              "the new prompt starts at column 0 of its own row");
        check(r2.find(L"~123") == std::wstring::npos,
              "no leftover input on the new prompt line");
        check(t.cursorX() == 34, "cursor waits right after the new prompt");
    }

    // 单独验证：ST_GROUND 下的裸 BEL 不该写出可见字符
    {
        Terminal t(20, 3);
        feed(t, "abc");
        feed(t, "\x07");
        feed(t, "de");
        dump(t, "BEL in ground");
        std::wstring r0 = rowText(t, 0);
        while (!r0.empty() && r0[r0.size() - 1] == L' ') r0.erase(r0.size() - 1);
        check(r0 == L"abcde", "bare BEL in ground writes nothing visible");
        check(t.cursorX() == 5, "BEL does not move the cursor");
    }

    // OSC 被 BEL 正确终止，且 OSC 里的内容不落到屏上
    {
        Terminal t(30, 3);
        feed(t, "\x1b]0;window-title-here\x07");
        feed(t, "X");
        std::wstring r0 = rowText(t, 0);
        while (!r0.empty() && r0[r0.size() - 1] == L' ') r0.erase(r0.size() - 1);
        check(r0 == L"X", "OSC terminated by BEL leaves nothing behind");
    }

    // OSC 用 ST(ESC\\) 终止（bash 大量使用 \x1b]3008;...\x1b\\）
    {
        Terminal t(40, 3);
        feed(t, "\x1b]3008;start=abc;type=command\x1b\\");
        feed(t, "ok");
        std::wstring r0 = rowText(t, 0);
        while (!r0.empty() && r0[r0.size() - 1] == L' ') r0.erase(r0.size() - 1);
        check(r0 == L"ok", "OSC terminated by ST leaves nothing behind");
    }

    // 关键回归：连续多个 OSC 之后状态机不能坏掉
    {
        Terminal t(40, 4);
        for (int i = 0; i < 20; ++i) {
            feed(t, "\x1b]3008;start=xyz\x1b\\");
            feed(t, "\x1b]0;title\x07");
        }
        feed(t, "READY");
        dump(t, "20x OSC then text");
        std::wstring r0 = rowText(t, 0);
        while (!r0.empty() && r0[r0.size() - 1] == L' ') r0.erase(r0.size() - 1);
        check(r0 == L"READY", "OSC handling stays stable across repeats");
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
