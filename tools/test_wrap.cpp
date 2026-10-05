// 复现用户报告的现象：长行「折行到本行」、光标压在字符上、信息被吃。
//
// 关键点：真实 PTY 输出是分小块来的，而feed() 在 m_enc == ENC_PROBE 时
// 会先把数据攒进探测窗口（32 字节），定不下编码就不写入。
// 所以必须模拟真实的分块喂入，验证总字符数守恒。

#include "src/terminal.h"

#include <cstdio>
#include <string>

using namespace wslterm;

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

static int countFilled(Terminal& t) {
    int n = 0;
    for (int r = 0; r < t.rows(); ++r) {
        std::vector<Cell> row = t.visibleLine(r, 0);
        for (size_t i = 0; i < row.size(); ++i) {
            uint32_t c = row[i].ch[0];
            if (c != CELL_EMPTY && c != CELL_TAIL && c != ' ') ++n;
        }
    }
    return n;
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

int main() {
    // 1) 真实场景：分小块喂 ASCII 长行，数据必须守恒
    {
        Terminal t(100, 8);
        std::string s;
        for (int i = 0; i < 12; ++i) s += "fuhwihwefw";   // 120 字符
        // 模拟 PTY：每次 7 字节
        size_t pos = 0;
        while (pos < s.size()) {
            size_t n = (s.size() - pos < 7) ? (s.size() - pos) : 7;
            t.feed(s.data() + pos, n);
            pos += n;
        }
        int filled = countFilled(t);
        printf("1) 120 chars in7-byte chunks: filled=%d cursor=(%d,%d)\n",
               filled, t.cursorX(), t.cursorY());
        dump(t, "chunked");
        check(filled == 120, "chunked ASCII feed loses nothing");
    }

    // 2) 一次性喂足量（>= 探测窗口），折行必须发生
    {
        Terminal t(20, 6);
        std::string s = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";
        t.feed(s.data(), s.size());
        int filled = countFilled(t);
        printf("\n2) 62 chars, cols=20: filled=%d cursor=(%d,%d)\n",
               filled, t.cursorX(), t.cursorY());
        dump(t, "wrap");
        check(t.cursorY() >= 3, "long line wrapped onto several rows");
        check(filled == 62, "single-shot feed loses nothing");
        check(rowText(t, 0) == L"ABCDEFGHIJKLMNOPQRST", "row 0 holds the first 20");
        check(rowText(t, 1).substr(0, 20) == L"UVWXYZ0123456789abcd", "row 1 continues");
    }

    // 3) 窄网格（1 列）不应崩、不应死循环
    {
        Terminal t(1, 3);
        std::string s = "XYZ";
        t.feed(s.data(), s.size());
        t.flushEncoding();
        int filled = countFilled(t);
        printf("\n3) cols=1, 3 chars: filled=%d cursor=(%d,%d)\n",
               filled, t.cursorX(), t.cursorY());
        // 1 列 x 3 行 = 正好 3 格，3 个字符全装得下，一个都不用滚
        check(filled == 3, "1-column grid: 3 chars fit exactly in 3 rows");
        check(t.cursorY() == 2, "1-column grid walks one row per char");
    }

    // 4) 宽字符在最后一格时不应压扁覆盖前一个字符
    {
        Terminal t(10, 3);
        // 9个 ASCII + 1 个宽字符：宽字符落在第 10 格（需要两格）
        std::string s = "12345678\xE4\xB8\xAD";   // 8 ASCII + 中文
        t.feed(s.data(), s.size());
        t.flushEncoding();
        printf("\n4) 8 ASCII + 1 CJK in cols=10: filled=%d\n", countFilled(t));
        std::wstring r0 = rowText(t, 0);
        printf("   row0 = [%ls]\n", r0.c_str());
        // 中文要占两格（第 9、10 格），不该把第 8 格覆盖掉
        check(r0.size() >= 9, "wide char does not clobber the preceding cell");
        check(r0.substr(0, 8) == L"12345678", "the 8 ASCII cells survive intact");
    }

    // 5) 折行后光标必须在下一行行首，不能停在原行末
    {
        Terminal t(10, 4);
        std::string s = "ABCDEFGHIJKLMNO";
        t.feed(s.data(), s.size());
        // 15 字节不足一窗（32），编码仍在探测里。真程序里定时器会兜底调一次
        // flushEncoding 把攒着的内容冲出来，这里照做。
        t.flushEncoding();
        printf("\n5) 15 chars, cols=10: cursor=(%d,%d)\n",
               t.cursorX(), t.cursorY());
        dump(t, "wrap15");
        check(rowText(t, 0) == L"ABCDEFGHIJ", "row 0 not overwritten");
        // rowText 会补空格到满宽（10），后面还有扫描留下的空网格
        std::wstring r1 = rowText(t, 1);
        while (!r1.empty() && r1[r1.size() - 1] == L' ') r1.erase(r1.size() - 1);
        check(r1 == L"KLMNO", "row 1 continues the line");
    }

    // 6) 回归：ASCII 两两配对能撞出真汉字，判据三不能因此误判 UTF-16
    //    "fuhwihwefw" 里 'f'(66)+'h'(68) -> U+6668「晚」，hi=0x66。
    //    旧判据用 hi >= 0x4E 会被骗，整段被当 UTF-16、字符数砍半。
    {
        Terminal t(60, 4);
        std::string s;
        for (int i = 0; i < 4; ++i) s += "fuhwihwefw";   // 40 字节，够填一窗
        // 分 7 字节喂，逼真走探测窗口
        size_t pos = 0;
        while (pos < s.size()) {
            size_t n = (s.size() - pos < 7) ? (s.size() - pos) : 7;
            t.feed(s.data() + pos, n);
            pos += n;
        }
        t.flushEncoding();
        int filled = countFilled(t);
        printf("\n6) 40 ASCII chars that pair into real CJK: filled=%d\n", filled);
        check(filled == 40, "ASCII pairing into CJK codepoints must not halve the text");
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
