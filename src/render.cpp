#include "render.h"

#include <algorithm>
#include <cstring>

namespace wslterm {

#define SEL_BG 0x264F78u

static inline COLORREF toRef(uint32_t c) {
    return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static inline uint32_t dimColor(uint32_t c) {
    uint32_t r = ((c >> 16) & 0xFFu) * 6u / 10u;
    uint32_t g = ((c >> 8) & 0xFFu) * 6u / 10u;
    uint32_t b = (c & 0xFFu) * 6u / 10u;
    return (r << 16) | (g << 8) | b;
}

static HFONT makeFont(const wchar_t* face, int pixelHeight, bool bold, DWORD pitch) {
    return CreateFontW(-pixelHeight, 0, 0, 0,
                       bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET,
                       OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,
                       pitch,
                       face);
}

Renderer::Renderer()
    : m_latin(NULL),
      m_latinBold(NULL),
      m_cjk(NULL),
      m_cjkBold(NULL),
      m_memDC(NULL),
      m_memBmp(NULL),
      m_oldBmp(NULL),
      m_bufW(0),
      m_bufH(0),
      m_cellW(8),
      m_limitCols(0),
      m_cellH(16),
      m_baseY(12),
      m_latinAscent(12),
      m_cjkAscent(12),
      m_fontSize(16),
      m_lastRev(0),
      m_lastScroll(-1),
      m_lastW(0),
      m_lastH(0),
      m_lastSel(-2),
      m_lastFocus(false),
      m_lastCaret(false),
      m_lastFont(0) {}

Renderer::~Renderer() {
    destroy();
}

bool Renderer::create(int pixelHeight) {
    if (pixelHeight < 8) pixelHeight = 8;
    if (pixelHeight > 72) pixelHeight = 72;
    return rebuildFonts(pixelHeight);
}

void Renderer::destroy() {
    releaseBrushes();

    if (m_memDC) {
        if (m_oldBmp) SelectObject(m_memDC, m_oldBmp);
        if (m_memBmp) DeleteObject(m_memBmp);
        DeleteDC(m_memDC);
        m_memDC = NULL;
        m_memBmp = NULL;
        m_oldBmp = NULL;
        m_bufW = 0;
        m_bufH = 0;
    }

    HFONT* fonts[4] = { &m_latin, &m_latinBold, &m_cjk, &m_cjkBold };
    for (int i = 0; i < 4; ++i) {
        if (*fonts[i]) {
            DeleteObject(*fonts[i]);
            *fonts[i] = NULL;
        }
    }
}

bool Renderer::rebuildFonts(int pixelHeight) {
    HFONT* fonts[4] = { &m_latin, &m_latinBold, &m_cjk, &m_cjkBold };
    for (int i = 0; i < 4; ++i) {
        if (*fonts[i]) {
            DeleteObject(*fonts[i]);
            *fonts[i] = NULL;
        }
    }

    m_latin     = makeFont(L"Consolas",           pixelHeight, false, FIXED_PITCH   | FF_MODERN);
    m_latinBold = makeFont(L"Consolas",           pixelHeight, true,  FIXED_PITCH   | FF_MODERN);
    m_cjk       = makeFont(L"Microsoft YaHei",    pixelHeight, false, DEFAULT_PITCH | FF_DONTCARE);
    m_cjkBold   = makeFont(L"Microsoft YaHei",    pixelHeight, true,  DEFAULT_PITCH | FF_DONTCARE);

    if (!m_latin) m_latin = makeFont(L"Courier New", pixelHeight, false, FIXED_PITCH | FF_MODERN);
    if (!m_latinBold) m_latinBold = makeFont(L"Courier New", pixelHeight, true, FIXED_PITCH | FF_MODERN);
    if (!m_cjk) m_cjk = makeFont(L"SimSun", pixelHeight, false, DEFAULT_PITCH | FF_DONTCARE);
    if (!m_cjkBold) m_cjkBold = makeFont(L"SimSun", pixelHeight, true, DEFAULT_PITCH | FF_DONTCARE);

    HDC dc = GetDC(NULL);
    if (!dc) return false;

    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    HGDIOBJ old = SelectObject(dc, m_latin);
    GetTextMetricsW(dc, &tm);

    TEXTMETRICW tc;
    memset(&tc, 0, sizeof(tc));
    SelectObject(dc, m_cjk);
    GetTextMetricsW(dc, &tc);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);

    m_cellW = (int)tm.tmAveCharWidth;
    if (m_cellW < 1) m_cellW = pixelHeight / 2;
    if (m_cellW < 1) m_cellW = 1;

    int latinH = (int)tm.tmHeight;
    int cjkH   = (int)tc.tmHeight;
    m_cellH = (latinH > cjkH) ? latinH : cjkH;
    if (m_cellH < 1) m_cellH = pixelHeight;

    m_latinAscent = (int)tm.tmAscent;
    m_cjkAscent   = (int)tc.tmAscent;

    int baseLatin = m_latinAscent + (m_cellH - latinH) / 2;
    int baseCjk   = m_cjkAscent + (m_cellH - cjkH) / 2;
    m_baseY = (baseLatin > baseCjk) ? baseLatin : baseCjk;
    if (m_baseY < 1) m_baseY = m_cellH / 2;
    if (m_baseY >= m_cellH) m_baseY = m_cellH - 1;

    m_fontSize = pixelHeight;
    return true;
}

HBRUSH Renderer::brushFor(uint32_t color) {
    for (size_t i = 0; i < m_brushes.size(); ++i) {
        if (m_brushes[i].color == color) return m_brushes[i].brush;
    }
    BrushEntry e;
    e.color = color;
    e.brush = CreateSolidBrush(toRef(color));
    m_brushes.push_back(e);
    return e.brush;
}

void Renderer::releaseBrushes() {
    for (size_t i = 0; i < m_brushes.size(); ++i) {
        if (m_brushes[i].brush) DeleteObject(m_brushes[i].brush);
    }
    m_brushes.clear();
}

HFONT Renderer::pickFont(const Cell& c, bool bold) const {
    if (isWideCell(c)) return bold ? m_cjkBold : m_cjk;
    return bold ? m_latinBold : m_latin;
}

void Renderer::ensureBuffer(HDC ref, int w, int h) {
    if (m_memDC && m_bufW == w && m_bufH == h) return;

    if (m_memDC) {
        if (m_oldBmp) SelectObject(m_memDC, m_oldBmp);
        if (m_memBmp) DeleteObject(m_memBmp);
        DeleteDC(m_memDC);
        m_memDC = NULL;
        m_memBmp = NULL;
        m_oldBmp = NULL;
    }

    m_memDC = CreateCompatibleDC(ref);
    if (!m_memDC) return;
    m_memBmp = CreateCompatibleBitmap(ref, w, h);
    if (!m_memBmp) {
        DeleteDC(m_memDC);
        m_memDC = NULL;
        return;
    }
    m_oldBmp = (HBITMAP)SelectObject(m_memDC, m_memBmp);
    m_bufW = w;
    m_bufH = h;
}

void Renderer::drawRun(HDC dc, const std::vector<Cell>& row, int y,
                       int startCol, int endCol, bool wide, bool bold, uint32_t fg) {
    if (startCol >= endCol) return;

    SetTextColor(dc, toRef(fg));
    HGDIOBJ oldFont = SelectObject(dc, bold ? (wide ? m_cjkBold : m_latinBold)
                                            : (wide ? m_cjk     : m_latin));
    if (!oldFont) oldFont = SelectObject(dc, m_latin);

    int baseY = y + m_baseY;

    if (wide) {
        baseY -= m_cjkAscent;
        int x = startCol * m_cellW;
        for (int i = startCol; i < endCol; ++i) {
            const Cell& c = row[(size_t)i];
            if (c.ch[0] == CELL_TAIL || c.ch[0] == CELL_EMPTY) continue;
            // 宽字符要占两格。落在行末（i == cols-1）时后面那一格不存在，
            // 直接画出去就会盖住窗口右缘外的东西——宁可不画。
            if (c.ch[1] && i + 1 >= endCol) break;
            int units = c.ch[1] ? 2 : 1;
            TextOutW(dc, x, baseY, c.ch, units);
            x += 2 * m_cellW;
        }
    } else {
        baseY -= m_latinAscent;
        std::wstring s;
        s.reserve((size_t)(endCol - startCol));
        for (int i = startCol; i < endCol; ++i) {
            s.push_back(row[(size_t)i].ch[0]);
        }
        // 按可绘制宽度截断。TextOutW 一次画完整串，单个字符的墨水可能
        // 超出它的格子（Consolas 的斜体/字形外伸），最后几个字符就跑到
        // 窗口外去了。paint 里的 clip 是最后一道保险，这里先算准。
        if (m_limitCols > 0 && m_limitCols < endCol) endCol = m_limitCols;
        if (endCol > startCol) {
            TextOutW(dc, startCol * m_cellW, baseY, s.c_str(), endCol - startCol);
        }
    }

    if (oldFont) SelectObject(dc, oldFont);
}

void Renderer::paint(HDC target, int w, int h, Terminal& term, int scrollOffset,
                     bool focused, const Selection& sel) {
    if (w <= 0 || h <= 0) return;
    ensureBuffer(target, w, h);
    if (!m_memDC) return;

    if (m_memBmp &&
        m_lastRev == term.revision() &&
        m_lastScroll == scrollOffset &&
        m_lastW == w && m_lastH == h &&
        m_lastSel == sel.stateCode() &&
        m_lastFocus == focused &&
        m_lastCaret == term.cursorVisible() &&
        m_lastFont == m_fontSize) {
        BitBlt(target, 0, 0, w, h, m_memDC, 0, 0, SRCCOPY);
        return;
    }

    HDC dc = m_memDC;
    releaseBrushes();

    RECT all;
    all.left = 0;
    all.top = 0;
    all.right = w;
    all.bottom = h;
    FillRect(dc, &all, brushFor(defaultBg()));

    const int cols = term.cols();
    const int rows = term.rows();
    if (cols < 1 || rows < 1) return;

    SetBkMode(dc, TRANSPARENT);

    // 硬裁剪到可绘制区域。cols 是按 (availPx / cellW) 算的，理论上
    // cols*cellW <= availPx，但只要有任何一处对不齐（字体在create 之后
    // 又变了、DPI 切换、窗口还没布局完就paint），GDI 的 TextOutW 就会
    // 老老实实把字画到窗口外面去——GDI 不认任何逻辑边界。
    // 症状就是长行右侧被切掉一截、看着像「不折行」。
    // 有了这个 clip，多余的列最多看不见，不会污染窗口其余部分。
    int saveClip = SaveDC(dc);
    IntersectClipRect(dc, 0, 0, w, h);

    // 本帧能画几列。w 是渲染器拿到的真实像素宽，比外部算 cols 时用的
    // 估计值可靠；以它为准，超出的列直接不画。
    m_limitCols = (m_cellW > 0) ? (w / m_cellW) : cols;
    if (m_limitCols < 0) m_limitCols = 0;
    if (m_limitCols > cols) m_limitCols = cols;

    for (int r = 0; r < rows; ++r) {
        const std::vector<Cell>& row = term.visibleLine(r, scrollOffset);
        if ((int)row.size() < cols) continue;

        const int y = r * m_cellH;

        int c = 0;
        while (c < cols) {
            bool inSel = sel.contains(r, c);
            uint32_t color = inSel ? SEL_BG : row[(size_t)c].bg;
            int s = c;
            while (c < cols && sel.contains(r, c) == inSel &&
                   (inSel || row[(size_t)c].bg == row[(size_t)s].bg)) {
                ++c;
            }
            RECT rc;
            rc.left = s * m_cellW;
            rc.top = y;
            rc.right = c * m_cellW;
            rc.bottom = y + m_cellH;
            FillRect(dc, &rc, brushFor(color));
        }

        c = 0;
        while (c < cols) {
            const Cell& first = row[(size_t)c];
            if (first.ch[0] == CELL_EMPTY || first.ch[0] == CELL_TAIL) { ++c; continue; }
            if (first.fg == first.bg) { ++c; continue; }

            const uint16_t attr = first.attr;
            const uint32_t fgRaw = first.fg;
            const bool bold = (attr & A_BOLD) != 0;
            const bool wide = isWideCell(first);
            const uint32_t fg = (attr & A_DIM) ? dimColor(fgRaw) : fgRaw;

            int s = c;
            while (c < cols) {
                const Cell& cur = row[(size_t)c];
                if (cur.ch[0] == CELL_EMPTY || cur.ch[0] == CELL_TAIL) break;
                if (cur.attr != attr) break;
                if (cur.fg != fgRaw) break;
                if (isWideCell(cur) != wide) break;
                ++c;
            }

            drawRun(dc, row, y, s, c, wide, bold, fg);

            if (attr & (A_UNDERLINE | A_STRIKE)) {
                RECT lr;
                lr.left = s * m_cellW;
                lr.right = c * m_cellW;
                if (attr & A_UNDERLINE) {
                    lr.top = y + m_cellH - 2;
                    lr.bottom = y + m_cellH - 1;
                } else {
                    lr.top = y + m_cellH / 2;
                    lr.bottom = y + m_cellH / 2 + 1;
                }
                FillRect(dc, &lr, brushFor(fg));
            }
        }
    }

    if (focused && term.cursorVisible() && scrollOffset == 0) {
        const int cx = term.cursorX();
        const int cy = term.cursorY();
        if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
            RECT cr;
            cr.left = cx * m_cellW;
            cr.top = cy * m_cellH;
            cr.right = cr.left + m_cellW;
            cr.bottom = cr.top + m_cellH;
            FillRect(dc, &cr, brushFor(cursorColor()));

            const std::vector<Cell>& row = term.visibleLine(cy, 0);
            if ((int)row.size() >= cols) {
                const Cell& cur = row[(size_t)cx];
                if (cur.ch[0] != CELL_EMPTY && cur.ch[0] != CELL_TAIL) {
                    SetTextColor(dc, toRef(defaultBg()));
                    HGDIOBJ oldFont = SelectObject(dc, pickFont(cur, false));
                    int asc = (isWideCell(cur) || isWideCell(row[(size_t)cx])) ? m_cjkAscent
                                                                              : m_latinAscent;
                    TextOutW(dc, cr.left, cr.top + m_baseY - asc, cur.ch, cur.ch[1] ? 2 : 1);
                    if (oldFont) SelectObject(dc, oldFont);
                }
            }
        }
    }

    // 裁剪是在 SaveDC 之后设的，必须还原，否则这个 DC 下次复用时
    // 会带着上一帧的裁剪区（memDC 是复用的）。
    RestoreDC(dc, saveClip);

    m_lastRev = term.revision();
    m_lastScroll = scrollOffset;
    m_lastW = w;
    m_lastH = h;
    m_lastSel = sel.stateCode();
    m_lastFocus = focused;
    m_lastCaret = term.cursorVisible();
    m_lastFont = m_fontSize;

    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
}

}
