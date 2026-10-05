#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace wslterm {

const wchar_t CELL_EMPTY = L' ';
const wchar_t CELL_TAIL  = 0xFFFF;

enum : uint16_t {
    A_BOLD      = 0x0001,
    A_DIM       = 0x0002,
    A_ITALIC    = 0x0004,
    A_UNDERLINE = 0x0008,
    A_REVERSE   = 0x0010,
    A_STRIKE    = 0x0020
};

struct Cell {
    wchar_t  ch[2];
    uint32_t fg;
    uint32_t bg;
    uint16_t attr;
};

struct ThemePalette {
    uint32_t fg;
    uint32_t bg;
    uint32_t cursor;
    uint32_t pal[16];
};

const ThemePalette& themePalette(int theme);
int  terminalTheme();
void setTerminalTheme(int theme);

const uint32_t* palette();
uint32_t defaultFg();
uint32_t defaultBg();
uint32_t cursorColor();

int  codepointWidth(uint32_t cp);
bool isWideCell(const Cell& c);
void encodeUtf16(uint32_t cp, wchar_t out[2]);
uint32_t decodeUtf16(const wchar_t* in, int* units);

class Terminal {
public:
    Terminal(int cols, int rows);

    void feed(const char* data, size_t len);
    void flushEncoding();
    void resize(int cols, int rows);
    void hardReset();
    void setLfImpliesCr(bool on) { m_lfImpliesCr = on; }

    void retheme(const ThemePalette& from, const ThemePalette& to);

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    const std::vector<Cell>& visibleLine(int index, int scrollOffset) const;
    int scrollbackSize() const { return (int)m_scrollback.size(); }

    unsigned long revision() const { return m_rev; }

    int  cursorX() const { return m_cx; }
    int  cursorY() const { return m_cy; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool bracketedPaste() const { return m_bracketedPaste; }

    const std::wstring& title() const { return m_title; }

private:
    enum State {
        ST_GROUND,
        ST_ESC,
        ST_ESC_INT,
        ST_CSI,
        ST_OSC,
        ST_OSC_ESC,
        ST_DCS,
        ST_DCS_ESC
    };

    Cell  blankCell() const;
    Cell  makeCell(uint32_t cp, uint32_t f, uint32_t b) const;
    void  currentColors(uint32_t& f, uint32_t& b) const;
    void  clampCursor();

    void  processChar(uint32_t c);
    void  feedUtf8(const char* data, size_t len);
    void  feedUtf16(const char* data, size_t len, bool bigEndian);
    void  detectEncoding(const char* data, size_t len);
    void  emit(uint32_t cp);
    void  lineFeed();
    void  reverseIndex();
    void  tabForward();
    void  scrollUp(int n);
    void  scrollDown(int n);
    void  setAltScreen(bool on);

    void  executeCSI(char fin);
    void  applySGR(const std::vector<int>& p);
    void  clearCells(int r1, int c1, int r2, int c2);
    void  eraseInDisplay(int mode);
    void  eraseInLine(int mode);
    void  insertLines(int n);
    void  deleteLines(int n);
    void  insertChars(int n);
    void  deleteChars(int n);
    void  eraseChars(int n);
    void  finishOSC();

    std::vector<Cell>              makeBlankLine() const;
    std::vector<std::vector<Cell>> makeBlankScreen() const;

    int m_cols;
    int m_rows;

    std::vector<std::vector<Cell>> m_lines;
    std::vector<std::vector<Cell>> m_mainSaved;
    std::vector<std::vector<Cell>> m_altSaved;
    std::deque<std::vector<Cell>>  m_scrollback;
    bool m_altActive;

    int  m_cx;
    int  m_cy;
    bool m_wrapPending;
    bool m_cursorVisible;
    bool m_autowrap;
    bool m_bracketedPaste;
    bool m_lfImpliesCr;

    int  m_scrollTop;
    int  m_scrollBottom;

    uint32_t m_fg;
    uint32_t m_bg;
    uint16_t m_attr;

    int      m_savedX;
    int      m_savedY;
    uint32_t m_savedFg;
    uint32_t m_savedBg;
    uint16_t m_savedAttr;

    State       m_state;
    std::string m_csi;
    std::wstring m_osc;
    bool        m_oscEsc;

    uint32_t m_utf8Acc;
    int      m_utf8Need;

    // inbox wsl.exe 在组件被禁用 / 出错时把消息按 UTF-16LE 写进 stdout（实测
    // 奇数位零字节占比 ~81%）。只按 UTF-8 解会把「安装 Linux 的 Windows 子系统」
    // 变成一堆 CJK 字符里夹着拉丁字母的乱码，正是截图里的样子。首块数据先探一次
    // 编码，之后锁定不再重复判断。
    enum Encoding { ENC_PROBE = 0, ENC_UTF8, ENC_UTF16LE, ENC_UTF16BE };
    int         m_enc;
    int         m_probeLen;
    unsigned char m_probe[32];
    std::string m_encBuf;
    bool        m_encHighByte;   // UTF-16 码元被 ReadFile 切断时的暂存
    unsigned char m_encHi;

    std::wstring m_title;

    unsigned long m_rev;

    static const int kMaxScrollback = 10000;
};

}
