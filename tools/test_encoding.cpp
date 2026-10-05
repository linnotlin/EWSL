#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "src/terminal.h"

using namespace wslterm;

static int failures = 0;

static void check(const char* name, bool ok) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

static std::string wideToBytes(const wchar_t* s) {
    std::string out;
    size_t n = wcslen(s);
    for (size_t i = 0; i < n; ++i) {
        out.push_back((char)(s[i] & 0xFF));
        out.push_back((char)((s[i] >> 8) & 0xFF));
    }
    return out;
}

static std::string wideToBytesBE(const wchar_t* s) {
    std::string out;
    size_t n = wcslen(s);
    for (size_t i = 0; i < n; ++i) {
        out.push_back((char)((s[i] >> 8) & 0xFF));
        out.push_back((char)(s[i] & 0xFF));
    }
    return out;
}

static int expectedCols(const wchar_t* s) {
    int n = 0;
    size_t len = wcslen(s);
    for (size_t i = 0; i < len; ++i) {
        wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len) { ++i; n += 2; continue; }
        n += codepointWidth((uint32_t)c) == 2 ? 2 : 1;
    }
    return n;
}

// 重建某一行文本。upTo 是该行有效列数；传 0 表示扫到行尾。
//
// 逐格走：宽字符占两格，第二格是 CELL_TAIL（0xFFFF），必须跳过；
// 未写过的格子是 CELL_EMPTY，也就是空格，属于正文的一部分。
// cursorX 指向「下一个待写位置」，宽字符的尾格正好落在它之内，
// 所以不能用 cursorX 直接截断——那会丢掉最后一个字。
static std::wstring lineText(Terminal& t, int row, int upTo) {
    const std::vector<Cell>& r = t.visibleLine(row, 0);
    std::wstring s;
    int limit = (upTo > 0) ? upTo : t.cols();

    for (int i = 0; i < limit && i < (int)r.size(); ++i) {
        const Cell& c = r[(size_t)i];
        if (c.ch[0] == CELL_TAIL) continue;
        if (c.ch[0] == CELL_EMPTY) { s.push_back(L' '); continue; }
        s.push_back(c.ch[0]);
        if (c.ch[1]) s.push_back(c.ch[1]);
    }

    // 裁掉尾部没写过的空格。
    while (!s.empty() && s[s.size() - 1] == L' ') s.erase(s.size() - 1);
    return s;
}

static void feedAll(Terminal& t, const std::string& b) {
    t.feed(b.data(), b.size());
    t.flushEncoding();
}

static void expectLine(const char* name, Terminal& t, int row, int upTo,
                       const wchar_t* expected) {
    std::wstring got = lineText(t, row, upTo);
    bool textOk = (got == expected);
    bool widthOk = ((upTo > 0 ? upTo : t.cursorX()) == expectedCols(expected));
    if (!textOk || !widthOk) {
        printf("       text_ok=%d width_ok=%d got_cols=%d want_cols=%d\n",
               (int)textOk, (int)widthOk,
               (upTo > 0 ? upTo : t.cursorX()), expectedCols(expected));
    }
    check(name, textOk && widthOk);
}

int main() {
    {
        Terminal t(60, 3);
        feedAll(t, wideToBytes(L"安装 Linux 的 Windows 子系统"));
        expectLine("UTF-16LE CJK decodes correctly", t, 0, 0,
                   L"安装 Linux 的 Windows 子系统");
    }
    {
        // 短中文 UTF-16 流要认出来。2 对样本单独看确实和ASCII 不可区分
        // （"ABCD" 配出来也是两个汉字码位），但真实流由 wsl.exe 输出时
        // 必然带 BOM，或至少是一整句话——用这两种真实形态来测。
        Terminal t(60, 3);
        std::string b("\xFF\xFE", 2);
        b += wideToBytes(L"你好");
        feedAll(t, b);
        check("wide cell reports width 2", isWideCell(t.visibleLine(0, 0)[0]));
    }
    {
        // 无 BOM 但够长的中文流（wsl.exe 的报错就是这种形态）
        Terminal t(60, 3);
        feedAll(t, wideToBytes(L"安装后不启动发行版"));
        expectLine("BOM-less short CJK UTF-16LE decodes", t, 0, 0,
                   L"安装后不启动发行版");
    }
    {
        Terminal t(60, 3);
        std::string b = wideToBytes(L"启用或关闭 Windows 功能");
        for (size_t i = 0; i < b.size(); i += 3) {
            size_t n = b.size() - i;
            if (n > 3) n = 3;
            t.feed(b.data() + i, n);
        }
        t.flushEncoding();
        expectLine("UTF-16LE split across 3-byte reads", t, 0, 0,
                   L"启用或关闭 Windows 功能");
    }
    {
        Terminal t(60, 3);
        std::string b("\xFF\xFE", 2);
        b += wideToBytes(L"hello");
        feedAll(t, b);
        expectLine("UTF-16LE BOM consumed, not rendered", t, 0, 0, L"hello");
    }
    {
        Terminal t(60, 3);
        std::string b("\xFE\xFF", 2);
        b += wideToBytesBE(L"中文");
        feedAll(t, b);
        expectLine("UTF-16BE BOM decodes", t, 0, 0, L"中文");
    }
    {
        Terminal t(60, 3);
        feedAll(t, "Arch Linux \xe5\xae\x89\xe5\x8d\x93");
        expectLine("UTF-8 CJK unaffected (no false UTF-16)", t, 0, 0,
                   L"Arch Linux 安卓");
    }
    {
        // 回归：ASCII 两两配对会撞出真实汉字（'A''S' = 41 53 -> U+5341「吙」），
        // 4 字节窗口下无法区分，必须靠更长的窗口 + 「每一对都是 CJK」判据。
        Terminal t(60, 3);
        feedAll(t, "PASSTHROUGHWITHOUTANYTHING");
        expectLine("ASCII pairs hitting real CJK codepoints stay UTF-8",
                   t, 0, 0, L"PASSTHROUGHWITHOUTANYTHING");
    }
    {
        // 回归：纯 ASCII 短输入只经flushEncoding 放行，也不能被吃掉字符。
        Terminal t(60, 3);
        std::string b = "OK";
        t.feed(b.data(), b.size());
        t.flushEncoding();
        expectLine("2-byte pure ASCII flushed intact", t, 0, 0, L"OK");
    }
    {
        Terminal t(60, 3);
        feedAll(t, "ASCIIONLYTEXT");
        expectLine("pure ASCII stays UTF-8", t, 0, 0, L"ASCIIONLYTEXT");
    }
    {
        Terminal t(60, 3);
        std::string b = wideToBytes(L"第一行");
        b += wideToBytes(L"\r\n");
        b += wideToBytes(L"第二行");
        feedAll(t, b);
        expectLine("CRLF row 0", t, 0, 6, L"第一行");
        expectLine("CRLF row 1", t, 1, 6, L"第二行");
    }
    {
        Terminal t(60, 3);
        feedAll(t, wideToBytes(L"旧会话"));
        t.hardReset();
        feedAll(t, "new \xe4\xb8\xad\xe6\x96\x87");
        expectLine("hardReset re-probes encoding", t, 0, 0, L"new 中文");
    }
    {
        Terminal t(60, 3);
        std::string b = wideToBytes(L"AB");
        t.feed(b.data(), 1);
        t.feed(b.data() + 1, b.size() - 1);
        t.flushEncoding();
        expectLine("code unit split mid-byte survives", t, 0, 0, L"AB");
    }
    {
        Terminal t(60, 3);
        feedAll(t, wideToBytes(L"中文"));
        check("CJK pair both marked wide",
              isWideCell(t.visibleLine(0, 0)[0]) &&
              isWideCell(t.visibleLine(0, 0)[1]));
    }
    {
        // inbox wsl.exe 组件被禁用时的真实首行（实测 UTF-16LE，无 BOM）。
        Terminal t(80, 4);
        feedAll(t, wideToBytes(L"适用于 Linux 的 Windows 子系统"));
        expectLine("real inbox wsl.exe message renders", t, 0, 0,
                   L"适用于 Linux 的 Windows 子系统");
    }
    {
        // 探测窗口内只来2 字节就停：flushEncoding 必须放行，不能卡住。
        Terminal t(20, 2);
        std::string b = wideToBytes(L"AB");
        t.feed(b.data(), b.size());
        t.flushEncoding();
        expectLine("short input flushed by flushEncoding", t, 0, 0, L"AB");
    }
    {
        // buffer 边界：feed 不得读过调用方给的 len。
        Terminal t(40, 2);
        char buf[16];
        memcpy(buf, "HELLOWORLD!!", 13);
        t.feed(buf, 5);
        t.flushEncoding();
        check("feed respects caller length", t.cursorX() == 5);
    }
    {
        // 真实 wsl.exe --help 开头 16 字节（UTF-16LE，无 BOM）不应被误判成 UTF-8。
        Terminal t(80, 4);
        static const unsigned char raw[] = {
            0x0D,0x00, 0x0D,0x00, 0x0A,0x00, 0x48,0x00,
            0x72,0x00, 0x43,0x00, 0x67,0x00, 0x40,0x00
        };
        feedAll(t, std::string((const char*)raw, sizeof(raw)));
        // \r\r\n 之后是 "H" —— 正确解码的话第一列是空，第二列起是 H。
        check("real wsl.exe --help prefix not misdetected",
              t.cursorX() == 5);
    }

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}