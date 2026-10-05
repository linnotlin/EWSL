// 反向验证：guest 侧真实的 Linux 终端输出（UTF-8）绝不能被判成 UTF-16。
// 这是本次改动最大的回归风险——探测判据一旦过宽，正常的 shell 输出就变乱码。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "src/terminal.h"

using namespace wslterm;

static std::wstring lineText(Terminal& t, int row, int limit) {
    const std::vector<Cell>& r = t.visibleLine(row, 0);
    std::wstring s;
    for (int i = 0; i < limit && i < (int)r.size(); ++i) {
        const Cell& c = r[(size_t)i];
        if (c.ch[0] == CELL_TAIL) continue;
        if (c.ch[0] == CELL_EMPTY) { s.push_back(L' '); continue; }
        s.push_back(c.ch[0]);
    }
    while (!s.empty() && s[s.size() - 1] == L' ') s.erase(s.size() - 1);
    return s;
}

static int failures = 0;

static void dumpCp(const char* tag, const std::wstring& s) {
    printf("       %s:", tag);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= 0x20 && s[i] < 0x7F) printf(" '%c'", (char)s[i]);
        else printf(" %04X", (unsigned)s[i]);
    }
    printf("\n");
}

static void expect(const char* name, const char* utf8, const wchar_t* want) {
    // 每种输入都用三种切分方式跑，任何一种判成 UTF-16 都算失败
    for (int mode = 0; mode < 3; ++mode) {
        Terminal t(120, 8);
        size_t n = strlen(utf8);
        if (mode == 0) {
            t.feed(utf8, n);
        } else if (mode == 1) {
            for (size_t i = 0; i < n; i += 5) {
                size_t k = n - i; if (k > 5) k = 5;
                t.feed(utf8 + i, k);
            }
        } else {
            for (size_t i = 0; i < n; ++i) t.feed(utf8 + i, 1);
        }
        t.flushEncoding();

        std::wstring got = lineText(t, 0, 120);
        // 行尾空格是显示宽度的一部分，比较前先裁掉
        std::wstring w(want);
        while (!w.empty() && w[w.size() - 1] == L' ') w.erase(w.size() - 1);

        if (got != w) {
            printf("[FAIL] %s (mode %d)\n", name, mode);
            dumpCp("want", w);
            dumpCp("got ", got);
            ++failures;
            return;
        }
    }
    printf("[PASS] %s\n", name);
}

int main() {
    // 真实 Arch / pacman 输出的典型片段
    expect("pacman ASCII line",
           ":: Synchronizing package databases...\n",
           L":: Synchronizing package databases...");

    expect("shell prompt with CJK hostname",
           "[\xE5\xAE\x89\xE5\x8D\x93@arch ~]$ ",
           L"[\x5B89\x5353@arch ~]$ ");

    expect("CJK ls output",
           "\xE6\x96\x87\xE4\xBB\xB6  \xE5\xAE\x89\xE8\xA3\x85  \xE7\x9A\x84\n",
           L"\x6587\x4EF6  \x5B89\x88C5  \x7684");

    expect("mixed CJK + ASCII progress",
           "\xE4\xB8\x8B\xE8\xBD\xBD linux 1234/5678 (21%) [\xE2\x96\x88\xE2\x96\x88\xE2\x96\x91] 12.3 MiB\n",
           L"\x4E0B\x8F7D linux 1234/5678 (21%) [\x2588\x2588\x2591] 12.3 MiB");

    expect("CJK error message",
           "\xE6\x89\x93\xE5\xBC\x80\xE5\xA4\xB1\xE8\xB4\xA5" ": \xE6\xAD\xA3\xE5\x9C\xA8\xE5\xAE\x89\xE8\xA3\x85\n",
           L"\x6253\x5F00\x5931\x8D25: \x6B63\x5728\x5B89\x88C5");

    expect("pure ASCII no CJK",
           "total 1234\r\n 5 installed\r\n",
           L"total 1234");

    expect("ASCII that pairs into CJK codepoints",
           "PA SS ID QQ ZZ",
           L"PA SS ID QQ ZZ");

    expect("UTF-8 box drawing + CJK",
           "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80 \xE4\xB8\xAD\xE6\x96\x87 \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n",
           L"\x250C\x2500\x2500 \x4E2D\x6587 \x2500\x2500\x2500");

    // ANSI 转义 + CJK 混排
    expect("ANSI colored CJK",
           "\x1b[32m\xE5\xAE\x89\xE8\xA3\x85\xE5\xAE\x8C\xE6\x88\x90\x1b[0m\n",
           L"\x5B89\x88C5\x5B8C\x6210");

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
