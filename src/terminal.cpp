#include "terminal.h"

#include <algorithm>
#include <cstring>

namespace wslterm {

// 0 = 深色（默认），1 = 浅色。主题由主程序在启动与「设置 → 界面主题」时写入，
// 终端格子里的颜色是快照，所以切换主题时要靠 Terminal::retheme 把旧值重映射。
static const ThemePalette kThemeDark = {
    0xE5E5E5, 0x1E1E1E, 0xE5E5E5,
    {
        0x1E1E1E, 0xCD3131, 0x0DBC79, 0xE5E510,
        0x2472C8, 0xBC3FBC, 0x11A8CD, 0xE5E5E5,
        0x666666, 0xF14C4C, 0x23D18B, 0xF5F543,
        0x3B8EEA, 0xD670D6, 0x29B8DB, 0xE5E5E5
    }
};

static const ThemePalette kThemeLight = {
    0x24292F, 0xFFFFFF, 0x24292F,
    {
        0x24292F, 0xCF222E, 0x116329, 0x9A6700,
        0x0969DA, 0x8250DF, 0x1B7C83, 0x6E7781,
        0x57606A, 0xA40E26, 0x1A7F37, 0xBF8700,
        0x0550AE, 0x6639BA, 0x3192AA, 0x57606A
    }
};

static int g_termTheme = 0;

const ThemePalette& themePalette(int theme) {
    return (theme == 1) ? kThemeLight : kThemeDark;
}

int  terminalTheme()        { return g_termTheme; }
void setTerminalTheme(int t) { g_termTheme = (t == 1) ? 1 : 0; }

const uint32_t* palette()    { return themePalette(g_termTheme).pal; }
uint32_t defaultFg()         { return themePalette(g_termTheme).fg;  }
uint32_t defaultBg()         { return themePalette(g_termTheme).bg;  }
uint32_t cursorColor()       { return themePalette(g_termTheme).cursor; }

void encodeUtf16(uint32_t cp, wchar_t out[2]) {
    if (cp <= 0xFFFF) {
        out[0] = (wchar_t)cp;
        out[1] = 0;
    } else if (cp <= 0x10FFFF) {
        uint32_t v = cp - 0x10000;
        out[0] = (wchar_t)(0xD800 + (v >> 10));
        out[1] = (wchar_t)(0xDC00 + (v & 0x3FF));
    } else {
        out[0] = L'?';
        out[1] = 0;
    }
}

uint32_t decodeUtf16(const wchar_t* in, int* units) {
    wchar_t c0 = in[0];
    if (c0 >= 0xD800 && c0 <= 0xDBFF) {
        wchar_t c1 = in[1];
        if (c1 >= 0xDC00 && c1 <= 0xDFFF) {
            if (units) *units = 2;
            return 0x10000 + (((uint32_t)(c0 - 0xD800) << 10) | (uint32_t)(c1 - 0xDC00));
        }
    }
    if (units) *units = 1;
    return (uint32_t)c0;
}

int codepointWidth(uint32_t cp) {
    if (cp == 0)        return 0;
    if (cp < 32)        return 0;
    if (cp == 0x7F)     return 0;
    if (cp < 0x1100)    return 1;

    if (cp >= 0x1100  && cp <= 0x115F)  return 2;
    if (cp >= 0x2E80  && cp <= 0x303E)  return 2;
    if (cp >= 0x3041  && cp <= 0x33FF)  return 2;
    if (cp >= 0x3400  && cp <= 0x4DBF)  return 2;
    if (cp >= 0x4E00  && cp <= 0x9FFF)  return 2;
    if (cp >= 0xA000  && cp <= 0xA4CF)  return 2;
    if (cp >= 0xAC00  && cp <= 0xD7A3)  return 2;
    if (cp >= 0xF900  && cp <= 0xFAFF)  return 2;
    if (cp >= 0xFE30  && cp <= 0xFE6F)  return 2;
    if (cp >= 0xFF00  && cp <= 0xFF60)  return 2;
    if (cp >= 0xFFE0  && cp <= 0xFFE6)  return 2;
    if (cp >= 0x1F300 && cp <= 0x1F64F) return 2;
    if (cp >= 0x1F900 && cp <= 0x1F9FF) return 2;
    if (cp >= 0x20000 && cp <= 0x3FFFD) return 2;
    return 1;
}

bool isWideCell(const Cell& c) {
    if (c.ch[0] == CELL_TAIL) return true;
    int units = 1;
    uint32_t cp = decodeUtf16(c.ch, &units);
    if (units == 2) return true;
    return codepointWidth(cp) == 2;
}

static uint32_t xterm256(int idx) {
    if (idx < 0)   idx = 0;
    if (idx > 255) idx = 255;
    if (idx < 16)  return palette()[idx];
    if (idx < 232) {
        static const int steps[6] = { 0, 95, 135, 175, 215, 255 };
        int v = idx - 16;
        int r = v / 36;
        int g = (v % 36) / 6;
        int b = v % 6;
        return ((uint32_t)steps[r] << 16) | ((uint32_t)steps[g] << 8) | (uint32_t)steps[b];
    }
    int v = 8 + (idx - 232) * 10;
    return ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
}

static int clamp255(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return v;
}

Terminal::Terminal(int cols, int rows)
    : m_cols(cols),
      m_rows(rows),
      m_altActive(false),
      m_cx(0),
      m_cy(0),
      m_wrapPending(false),
      m_cursorVisible(true),
      m_autowrap(true),
      m_bracketedPaste(false),
      m_lfImpliesCr(false),
      m_scrollTop(0),
      m_scrollBottom(rows - 1),
      m_fg(defaultFg()),
      m_bg(defaultBg()),
      m_attr(0),
      m_savedX(0),
      m_savedY(0),
      m_savedFg(defaultFg()),
      m_savedBg(defaultBg()),
      m_savedAttr(0),
      m_state(ST_GROUND),
      m_oscEsc(false),
      m_utf8Acc(0),
      m_utf8Need(0),
      m_rev(0) {
    m_lines = makeBlankScreen();
}

std::vector<Cell> Terminal::makeBlankLine() const {
    std::vector<Cell> row((size_t)m_cols);
    for (int i = 0; i < m_cols; ++i) {
        row[(size_t)i].ch[0] = CELL_EMPTY;
        row[(size_t)i].ch[1] = 0;
        row[(size_t)i].fg = defaultFg();
        row[(size_t)i].bg = defaultBg();
        row[(size_t)i].attr = 0;
    }
    return row;
}

std::vector<std::vector<Cell>> Terminal::makeBlankScreen() const {
    std::vector<std::vector<Cell>> s;
    s.reserve((size_t)m_rows);
    for (int r = 0; r < m_rows; ++r) s.push_back(makeBlankLine());
    return s;
}

Cell Terminal::blankCell() const {
    uint32_t f, b;
    currentColors(f, b);
    Cell c;
    c.ch[0] = CELL_EMPTY;
    c.ch[1] = 0;
    c.fg = f;
    c.bg = b;
    c.attr = m_attr;
    return c;
}

Cell Terminal::makeCell(uint32_t cp, uint32_t f, uint32_t b) const {
    Cell c;
    c.ch[0] = CELL_EMPTY;
    c.ch[1] = 0;
    encodeUtf16(cp, c.ch);
    c.fg = f;
    c.bg = b;
    c.attr = m_attr;
    return c;
}

void Terminal::currentColors(uint32_t& f, uint32_t& b) const {
    f = m_fg;
    b = m_bg;
    if (m_attr & A_REVERSE) {
        uint32_t t = f;
        f = b;
        b = t;
    }
}

void Terminal::clampCursor() {
    if (m_cx < 0) m_cx = 0;
    if (m_cx > m_cols - 1) m_cx = m_cols - 1;
    if (m_cy < 0) m_cy = 0;
    if (m_cy > m_rows - 1) m_cy = m_rows - 1;
}

void Terminal::hardReset() {
    ++m_rev;
    m_lines = makeBlankScreen();
    m_scrollback.clear();
    m_mainSaved.clear();
    m_altSaved.clear();
    m_altActive = false;
    m_cx = 0;
    m_cy = 0;
    m_wrapPending = false;
    m_cursorVisible = true;
    m_autowrap = true;
    m_bracketedPaste = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_fg = defaultFg();
    m_bg = defaultBg();
    m_attr = 0;
    m_savedX = 0;
    m_savedY = 0;
    m_savedFg = defaultFg();
    m_savedBg = defaultBg();
    m_savedAttr = 0;
    m_state = ST_GROUND;
    m_csi.clear();
    m_osc.clear();
    m_utf8Acc = 0;
    m_utf8Need = 0;
}

// Every cell carries a colour snapshot, so a theme switch has to walk the whole
// buffer and swap the old palette for the new one - otherwise the screen keeps
// the colours it was painted with (dark) while the chrome turns light.
void Terminal::retheme(const ThemePalette& from, const ThemePalette& to) {
    struct Mapper {
        const ThemePalette& f;
        const ThemePalette& t;
        bool bg;
        uint32_t operator()(uint32_t c) const {
            if (bg) {
                if (c == f.bg) return t.bg;
            } else if (c == f.fg) {
                return t.fg;
            }
            for (int i = 0; i < 16; ++i) {
                if (c == f.pal[i]) return t.pal[i];
            }
            return c;
        }
    };

    Mapper mf = { from, to, false };
    Mapper mb = { from, to, true };

    auto fix = [&](std::vector<Cell>& row) {
        for (size_t i = 0; i < row.size(); ++i) {
            row[i].fg = mf(row[i].fg);
            row[i].bg = mb(row[i].bg);
        }
    };
    auto fixScreen = [&](std::vector<std::vector<Cell> >& screen) {
        for (size_t r = 0; r < screen.size(); ++r) fix(screen[r]);
    };

    fixScreen(m_lines);
    fixScreen(m_mainSaved);
    fixScreen(m_altSaved);
    for (std::deque<std::vector<Cell> >::iterator it = m_scrollback.begin();
         it != m_scrollback.end(); ++it) {
        fix(*it);
    }

    m_fg = mf(m_fg);
    m_bg = mb(m_bg);
    m_savedFg = mf(m_savedFg);
    m_savedBg = mb(m_savedBg);

    ++m_rev;
}

static Cell makeBlank(uint32_t fg, uint32_t bg) {
    Cell c;
    c.ch[0] = CELL_EMPTY;
    c.ch[1] = 0;
    c.fg = fg;
    c.bg = bg;
    c.attr = 0;
    return c;
}

static void fitRow(std::vector<Cell>& row, int cols) {
    int old = (int)row.size();
    if (old == cols) return;
    if (old > cols) {
        row.resize((size_t)cols);
        if (cols > 0 && row[(size_t)(cols - 1)].ch[0] == CELL_TAIL) {
            row[(size_t)(cols - 1)] = makeBlank(row[(size_t)(cols - 1)].fg,
                                                row[(size_t)(cols - 1)].bg);
            if (cols > 1) row[(size_t)(cols - 2)] = makeBlank(row[(size_t)(cols - 2)].fg,
                                                              row[(size_t)(cols - 2)].bg);
        }
        return;
    }
    uint32_t fg = row.empty() ? defaultFg() : row.back().fg;
    uint32_t bg = row.empty() ? defaultBg() : row.back().bg;
    row.resize((size_t)cols);
    for (int c = old; c < cols; ++c) row[(size_t)c] = makeBlank(fg, bg);
}

void Terminal::resize(int cols, int rows) {
    if (cols < 1 || rows < 1) return;
    ++m_rev;
    if (cols == m_cols && rows == m_rows) return;

    if (!m_altActive) {
        int overflow = (int)m_lines.size() - rows;
        if (overflow > 0) {
            int take = (overflow > m_cy) ? m_cy : overflow;
            for (int i = 0; i < take; ++i) {
                m_scrollback.push_back(m_lines[(size_t)i]);
            }
            if (take > 0) m_lines.erase(m_lines.begin(), m_lines.begin() + take);
            m_cy -= take;
            while ((int)m_lines.size() > rows) m_lines.pop_back();
        }
        while ((int)m_lines.size() < rows && !m_scrollback.empty()) {
            m_lines.insert(m_lines.begin(), m_scrollback.back());
            m_scrollback.pop_back();
            ++m_cy;
        }
    }

    while ((int)m_lines.size() < rows) m_lines.push_back(makeBlankLine());
    if ((int)m_lines.size() > rows) {
        int overflow = (int)m_lines.size() - rows;
        m_lines.erase(m_lines.begin(), m_lines.begin() + overflow);
        m_cy -= overflow;
        if (m_cy < 0) m_cy = 0;
    }

    for (size_t i = 0; i < m_lines.size(); ++i)      fitRow(m_lines[i], cols);
    for (size_t i = 0; i < m_scrollback.size(); ++i) fitRow(m_scrollback[i], cols);
    for (size_t i = 0; i < m_mainSaved.size(); ++i)  fitRow(m_mainSaved[i], cols);
    for (size_t i = 0; i < m_altSaved.size(); ++i)   fitRow(m_altSaved[i], cols);

    m_cols = cols;
    m_rows = rows;
    m_scrollTop = 0;
    m_scrollBottom = rows - 1;
    m_wrapPending = false;
    clampCursor();
}

const std::vector<Cell>& Terminal::visibleLine(int index, int scrollOffset) const {
    static const std::vector<Cell> kFallback;

    if (scrollOffset > 0) {
        if (index < scrollOffset) {
            int idx = (int)m_scrollback.size() - scrollOffset + index;
            if (idx < 0) idx = 0;
            if (idx >= (int)m_scrollback.size()) return kFallback;
            return m_scrollback[(size_t)idx];
        }
        int r = index - scrollOffset;
        if (r < 0 || r >= (int)m_lines.size()) return kFallback;
        return m_lines[(size_t)r];
    }
    if (index < 0 || index >= (int)m_lines.size()) return kFallback;
    return m_lines[(size_t)index];
}

void Terminal::feed(const char* data, size_t len) {
    if (len > 0) ++m_rev;
    for (size_t i = 0; i < len; ++i) {
        unsigned char b = (unsigned char)data[i];

        if (m_utf8Need > 0) {
            if ((b & 0xC0) == 0x80) {
                m_utf8Acc = (m_utf8Acc << 6) | (uint32_t)(b & 0x3F);
                if (--m_utf8Need == 0) {
                    uint32_t cp = m_utf8Acc;
                    m_utf8Acc = 0;
                    processChar(cp);
                }
                continue;
            }
            m_utf8Acc = 0;
            m_utf8Need = 0;
        }

        if (b < 0x80) {
            processChar((uint32_t)b);
        } else if (b >= 0xC2 && b <= 0xDF) {
            m_utf8Acc = (uint32_t)(b & 0x1F);
            m_utf8Need = 1;
        } else if (b >= 0xE0 && b <= 0xEF) {
            m_utf8Acc = (uint32_t)(b & 0x0F);
            m_utf8Need = 2;
        } else if (b >= 0xF0 && b <= 0xF4) {
            m_utf8Acc = (uint32_t)(b & 0x07);
            m_utf8Need = 3;
        }
    }
}

void Terminal::processChar(uint32_t c) {
    ++m_rev;

    switch (m_state) {

    case ST_GROUND:
        if (c == 0x1B) { m_state = ST_ESC; return; }
        if (c == '\n') {
            if (m_lfImpliesCr) { m_cx = 0; m_wrapPending = false; }
            lineFeed();
            return;
        }
        if (c == '\r') { m_cx = 0; m_wrapPending = false; return; }
        if (c == '\b') { if (m_cx > 0) --m_cx; m_wrapPending = false; return; }
        if (c == '\t') { tabForward(); return; }
        if (c == 0x07) return;
        if (c == 0x0E || c == 0x0F) return;
        if (c < 32) return;
        emit(c);
        return;

    case ST_ESC:
        switch (c) {
        case '[':
            m_csi.clear();
            m_state = ST_CSI;
            return;
        case ']':
            m_osc.clear();
            m_oscEsc = false;
            m_state = ST_OSC;
            return;
        case 'P':
            m_state = ST_DCS;
            return;
        case '7':
            m_savedX = m_cx;
            m_savedY = m_cy;
            m_savedFg = m_fg;
            m_savedBg = m_bg;
            m_savedAttr = m_attr;
            m_state = ST_GROUND;
            return;
        case '8':
            m_cx = m_savedX;
            m_cy = m_savedY;
            m_fg = m_savedFg;
            m_bg = m_savedBg;
            m_attr = m_savedAttr;
            clampCursor();
            m_state = ST_GROUND;
            return;
        case 'D':
            lineFeed();
            m_state = ST_GROUND;
            return;
        case 'M':
            reverseIndex();
            m_state = ST_GROUND;
            return;
        case 'E':
            m_cx = 0;
            lineFeed();
            m_state = ST_GROUND;
            return;
        case 'c':
            hardReset();
            m_state = ST_GROUND;
            return;
        case '(': case ')': case '*': case '+': case '-': case '.': case '/':
            m_state = ST_ESC_INT;
            return;
        default:
            m_state = ST_GROUND;
            return;
        }

    case ST_ESC_INT:
        m_state = ST_GROUND;
        return;

    case ST_CSI:
        if (c >= 0x20 && c <= 0x3F) {
            if (m_csi.size() < 128) m_csi.push_back((char)c);
            return;
        }
        if (c >= 0x40 && c <= 0x7E) {
            executeCSI((char)c);
            m_state = ST_GROUND;
            return;
        }
        m_state = ST_GROUND;
        return;

    case ST_OSC:
        if (c == 0x07) {
            finishOSC();
            m_state = ST_GROUND;
            return;
        }
        if (m_oscEsc) {
            if (c == '\\') {
                finishOSC();
                m_state = ST_GROUND;
            } else {
                m_oscEsc = false;
            }
            return;
        }
        if (c == 0x1B) { m_oscEsc = true; return; }
        if (c < 32) return;
        if (m_osc.size() < 512) m_osc.push_back((wchar_t)c);
        return;

    case ST_DCS:
        if (c == 0x1B) m_state = ST_DCS_ESC;
        return;

    case ST_DCS_ESC:
        m_state = (c == '\\') ? ST_GROUND : ST_DCS;
        return;

    case ST_OSC_ESC:
        m_state = (c == '\\') ? ST_GROUND : ST_OSC;
        return;
    }
}

void Terminal::finishOSC() {
    if (m_osc.size() < 3) return;
    if (m_osc[1] != L';') return;
    if (m_osc[0] == L'0' || m_osc[0] == L'2') {
        m_title.assign(m_osc.begin() + 2, m_osc.end());
    }
}

void Terminal::tabForward() {
    m_wrapPending = false;
    int next = ((m_cx / 8) + 1) * 8;
    m_cx = (next >= m_cols) ? (m_cols - 1) : next;
}

void Terminal::emit(uint32_t cp) {
    int w = codepointWidth(cp);
    if (w <= 0) w = 1;

    if (m_wrapPending) {
        m_wrapPending = false;
        if (m_autowrap) {
            m_cx = 0;
            lineFeed();
        } else {
            m_cx = m_cols - 1;
        }
    }

    if (w == 2 && m_cx >= m_cols - 1) {
        if (m_autowrap) {
            m_cx = 0;
            lineFeed();
        } else {
            m_cx = (m_cols >= 2) ? m_cols - 2 : 0;
        }
    }

    if (m_cx < 0) m_cx = 0;
    if (m_cy < 0) m_cy = 0;
    if (m_cy > m_rows - 1) m_cy = m_rows - 1;
    if (m_cx > m_cols - 1) m_cx = m_cols - 1;
    if ((int)m_lines.size() != m_rows) m_lines = makeBlankScreen();

    uint32_t f, b;
    currentColors(f, b);

    std::vector<Cell>& row = m_lines[(size_t)m_cy];

    if (w == 2 && m_cx + 1 < m_cols) {
        row[(size_t)m_cx] = makeCell(cp, f, b);
        Cell tail;
        tail.ch[0] = CELL_TAIL;
        tail.ch[1] = 0;
        tail.fg = f;
        tail.bg = b;
        tail.attr = m_attr;
        row[(size_t)(m_cx + 1)] = tail;
        m_cx += 2;
    } else {
        row[(size_t)m_cx] = makeCell(cp, f, b);
        m_cx += 1;
    }

    if (m_cx >= m_cols) {
        m_cx = m_cols - 1;
        if (m_autowrap) m_wrapPending = true;
    }
}

void Terminal::lineFeed() {
    m_wrapPending = false;
    if (m_cy == m_scrollBottom) {
        scrollUp(1);
    } else if (m_cy < m_rows - 1) {
        ++m_cy;
    }
}

void Terminal::reverseIndex() {
    m_wrapPending = false;
    if (m_cy == m_scrollTop) {
        scrollDown(1);
    } else if (m_cy > 0) {
        --m_cy;
    }
}

void Terminal::scrollUp(int n) {
    if (n <= 0) return;
    if (n > m_scrollBottom - m_scrollTop + 1) n = m_scrollBottom - m_scrollTop + 1;

    for (int k = 0; k < n; ++k) {
        if (m_scrollTop == 0 && !m_altActive) {
            m_scrollback.push_back(m_lines[0]);
            while ((int)m_scrollback.size() > kMaxScrollback) m_scrollback.pop_front();
        }
        m_lines.erase(m_lines.begin() + m_scrollTop);
        m_lines.insert(m_lines.begin() + m_scrollBottom, makeBlankLine());
    }
}

void Terminal::scrollDown(int n) {
    if (n <= 0) return;
    if (n > m_scrollBottom - m_scrollTop + 1) n = m_scrollBottom - m_scrollTop + 1;

    for (int k = 0; k < n; ++k) {
        m_lines.erase(m_lines.begin() + m_scrollBottom);
        m_lines.insert(m_lines.begin() + m_scrollTop, makeBlankLine());
    }
}

void Terminal::setAltScreen(bool on) {
    if (on == m_altActive) return;
    ++m_rev;

    if (on) {
        m_mainSaved = m_lines;
        if (m_altSaved.empty() || (int)m_altSaved.size() != m_rows) {
            m_altSaved = makeBlankScreen();
        }
        m_lines = m_altSaved;
        m_savedX = m_cx;
        m_savedY = m_cy;
        m_cx = 0;
        m_cy = 0;
        m_altActive = true;
    } else {
        m_altSaved = m_lines;
        if (!m_mainSaved.empty() && (int)m_mainSaved.size() == m_rows) {
            m_lines = m_mainSaved;
        } else {
            m_lines = makeBlankScreen();
        }
        m_cx = m_savedX;
        m_cy = m_savedY;
        m_altActive = false;
    }
    m_wrapPending = false;
    clampCursor();
}

void Terminal::executeCSI(char fin) {
    std::string s = m_csi;
    char priv = 0;
    size_t i = 0;
    if (!s.empty() && (s[0] == '?' || s[0] == '>' || s[0] == '<' || s[0] == '=')) {
        priv = s[0];
        i = 1;
    }

    std::vector<int> p;
    int cur = -1;
    for (; i < s.size(); ++i) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            cur = (cur < 0 ? 0 : cur) * 10 + (ch - '0');
        } else if (ch == ';' || ch == ':') {
            p.push_back(cur < 0 ? 0 : cur);
            cur = -1;
        }
    }
    if (cur >= 0) p.push_back(cur);

    if (priv == '?') {
        for (size_t k = 0; k < p.size(); ++k) {
            switch (p[k]) {
            case 7:
                m_autowrap = (fin == 'h');
                break;
            case 25:
                m_cursorVisible = (fin == 'h');
                break;
            case 47:
            case 1047:
            case 1049:
                if (fin == 'h') setAltScreen(true);
                else if (fin == 'l') setAltScreen(false);
                break;
            case 2004:
                m_bracketedPaste = (fin == 'h');
                break;
            default:
                break;
            }
        }
        return;
    }

    size_t np = p.size();
    int n1 = (np > 0 && p[0] > 0) ? p[0] : 1;
    int n2 = (np > 1 && p[1] > 0) ? p[1] : 1;

    switch (fin) {
    case 'A': m_cy -= n1; clampCursor(); break;
    case 'B': m_cy += n1; clampCursor(); break;
    case 'C': m_cx += n1; clampCursor(); break;
    case 'D': m_cx -= n1; clampCursor(); break;
    case 'E': m_cy += n1; m_cx = 0; clampCursor(); break;
    case 'F': m_cy -= n1; m_cx = 0; clampCursor(); break;
    case 'G':
    case '`': m_cx = n1 - 1; clampCursor(); break;
    case 'H':
    case 'f': m_cy = n1 - 1; m_cx = n2 - 1; clampCursor(); break;
    case 'd': m_cy = n1 - 1; clampCursor(); break;
    case 'J': eraseInDisplay(np > 0 ? p[0] : 0); break;
    case 'K': eraseInLine(np > 0 ? p[0] : 0); break;
    case 'L': insertLines(n1); break;
    case 'M': deleteLines(n1); break;
    case 'P': deleteChars(n1); break;
    case '@': insertChars(n1); break;
    case 'X': eraseChars(n1); break;
    case 'S': scrollUp(n1); break;
    case 'T': scrollDown(n1); break;
    case 'm': applySGR(p); break;
    case 'r': {
        int top = (np > 0 && p[0] > 0) ? p[0] - 1 : 0;
        int bot = (np > 1 && p[1] > 0) ? p[1] - 1 : m_rows - 1;
        if (bot > m_rows - 1) bot = m_rows - 1;
        if (top < 0) top = 0;
        if (top >= bot) {
            top = 0;
            bot = m_rows - 1;
        }
        m_scrollTop = top;
        m_scrollBottom = bot;
        m_cx = 0;
        m_cy = m_scrollTop;
        m_wrapPending = false;
        break;
    }
    case 's':
        m_savedX = m_cx;
        m_savedY = m_cy;
        break;
    case 'u':
        m_cx = m_savedX;
        m_cy = m_savedY;
        clampCursor();
        break;
    default:
        break;
    }
}

void Terminal::applySGR(const std::vector<int>& p) {
    if (p.empty()) {
        m_fg = defaultFg();
        m_bg = defaultBg();
        m_attr = 0;
        return;
    }

    for (size_t i = 0; i < p.size(); ++i) {
        int v = p[i];
        if (v == 0) {
            m_fg = defaultFg();
            m_bg = defaultBg();
            m_attr = 0;
        } else if (v == 1) {
            m_attr |= A_BOLD;
        } else if (v == 2) {
            m_attr |= A_DIM;
        } else if (v == 3) {
            m_attr |= A_ITALIC;
        } else if (v == 4) {
            m_attr |= A_UNDERLINE;
        } else if (v == 7) {
            m_attr |= A_REVERSE;
        } else if (v == 9) {
            m_attr |= A_STRIKE;
        } else if (v == 21 || v == 22) {
            m_attr &= (uint16_t)~(A_BOLD | A_DIM);
        } else if (v == 23) {
            m_attr &= (uint16_t)~A_ITALIC;
        } else if (v == 24) {
            m_attr &= (uint16_t)~A_UNDERLINE;
        } else if (v == 27) {
            m_attr &= (uint16_t)~A_REVERSE;
        } else if (v == 29) {
            m_attr &= (uint16_t)~A_STRIKE;
        } else if (v >= 30 && v <= 37) {
            m_fg = palette()[v - 30];
        } else if (v == 38) {
            if (i + 1 < p.size() && p[i + 1] == 5 && i + 2 < p.size()) {
                m_fg = xterm256(p[i + 2]);
                i += 2;
            } else if (i + 1 < p.size() && p[i + 1] == 2 && i + 4 < p.size()) {
                m_fg = ((uint32_t)clamp255(p[i + 2]) << 16) |
                       ((uint32_t)clamp255(p[i + 3]) << 8) |
                        (uint32_t)clamp255(p[i + 4]);
                i += 4;
            }
        } else if (v == 39) {
            m_fg = defaultFg();
        } else if (v >= 40 && v <= 47) {
            m_bg = palette()[v - 40];
        } else if (v == 48) {
            if (i + 1 < p.size() && p[i + 1] == 5 && i + 2 < p.size()) {
                m_bg = xterm256(p[i + 2]);
                i += 2;
            } else if (i + 1 < p.size() && p[i + 1] == 2 && i + 4 < p.size()) {
                m_bg = ((uint32_t)clamp255(p[i + 2]) << 16) |
                       ((uint32_t)clamp255(p[i + 3]) << 8) |
                        (uint32_t)clamp255(p[i + 4]);
                i += 4;
            }
        } else if (v == 49) {
            m_bg = defaultBg();
        } else if (v >= 90 && v <= 97) {
            m_fg = palette()[8 + v - 90];
        } else if (v >= 100 && v <= 107) {
            m_bg = palette()[8 + v - 100];
        }
    }
}

void Terminal::clearCells(int r1, int c1, int r2, int c2) {
    if (r1 < 0) r1 = 0;
    if (c1 < 0) c1 = 0;
    if (r2 > m_rows - 1) r2 = m_rows - 1;
    if (c2 > m_cols - 1) c2 = m_cols - 1;

    Cell fill = blankCell();

    for (int r = r1; r <= r2; ++r) {
        int cs = (r == r1) ? c1 : 0;
        int ce = (r == r2) ? c2 : m_cols - 1;
        for (int c = cs; c <= ce; ++c) {
            m_lines[(size_t)r][(size_t)c] = fill;
        }
    }
}

void Terminal::eraseInDisplay(int mode) {
    m_wrapPending = false;
    if (mode == 0) {
        clearCells(m_cy, m_cx, m_rows - 1, m_cols - 1);
    } else if (mode == 1) {
        clearCells(0, 0, m_cy, m_cx);
    } else if (mode == 2) {
        clearCells(0, 0, m_rows - 1, m_cols - 1);
    } else if (mode == 3) {
        m_scrollback.clear();
    }
}

void Terminal::eraseInLine(int mode) {
    m_wrapPending = false;
    if (mode == 0) {
        clearCells(m_cy, m_cx, m_cy, m_cols - 1);
    } else if (mode == 1) {
        clearCells(m_cy, 0, m_cy, m_cx);
    } else if (mode == 2) {
        clearCells(m_cy, 0, m_cy, m_cols - 1);
    }
}

void Terminal::insertLines(int n) {
    if (n <= 0) return;
    if (m_cy < m_scrollTop || m_cy > m_scrollBottom) return;
    if (n > m_scrollBottom - m_cy + 1) n = m_scrollBottom - m_cy + 1;

    m_wrapPending = false;
    for (int k = 0; k < n; ++k) {
        m_lines.erase(m_lines.begin() + m_scrollBottom);
        m_lines.insert(m_lines.begin() + m_cy, makeBlankLine());
    }
}

void Terminal::deleteLines(int n) {
    if (n <= 0) return;
    if (m_cy < m_scrollTop || m_cy > m_scrollBottom) return;
    if (n > m_scrollBottom - m_cy + 1) n = m_scrollBottom - m_cy + 1;

    m_wrapPending = false;
    for (int k = 0; k < n; ++k) {
        m_lines.erase(m_lines.begin() + m_cy);
        m_lines.insert(m_lines.begin() + m_scrollBottom, makeBlankLine());
    }
}

void Terminal::insertChars(int n) {
    if (n <= 0) return;
    if (m_cx >= m_cols) return;
    if (n > m_cols - m_cx) n = m_cols - m_cx;

    m_wrapPending = false;
    std::vector<Cell>& row = m_lines[(size_t)m_cy];
    for (int k = m_cols - 1; k >= m_cx + n; --k) row[(size_t)k] = row[(size_t)(k - n)];
    Cell fill = blankCell();
    for (int k = m_cx; k < m_cx + n; ++k) row[(size_t)k] = fill;
}

void Terminal::deleteChars(int n) {
    if (n <= 0) return;
    if (m_cx >= m_cols) return;
    if (n > m_cols - m_cx) n = m_cols - m_cx;

    m_wrapPending = false;
    std::vector<Cell>& row = m_lines[(size_t)m_cy];
    for (int k = m_cx; k + n < m_cols; ++k) row[(size_t)k] = row[(size_t)(k + n)];
    Cell fill = blankCell();
    for (int k = m_cols - n; k < m_cols; ++k) row[(size_t)k] = fill;
}

void Terminal::eraseChars(int n) {
    if (n <= 0) return;
    if (m_cx >= m_cols) return;
    if (n > m_cols - m_cx) n = m_cols - m_cx;

    m_wrapPending = false;
    std::vector<Cell>& row = m_lines[(size_t)m_cy];
    Cell fill = blankCell();
    for (int k = m_cx; k < m_cx + n; ++k) row[(size_t)k] = fill;
}

}
