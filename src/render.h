#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "terminal.h"

namespace wslterm {

struct Selection {
    bool active;
    int  r0, c0, r1, c1;

    Selection() : active(false), r0(0), c0(0), r1(0), c1(0) {}

    void normalize() {
        if (r0 > r1 || (r0 == r1 && c0 > c1)) {
            int t;
            t = r0; r0 = r1; r1 = t;
            t = c0; c0 = c1; c1 = t;
        }
    }

    void clear() {
        active = false;
        r0 = c0 = r1 = c1 = 0;
    }

    int stateCode() const {
        if (!active) return -1;
        return (r0 & 0xFF) << 24 | (c0 & 0xFF) << 16 | (r1 & 0xFF) << 8 | (c1 & 0xFF);
    }

    bool contains(int r, int c) const {
        if (!active) return false;
        if (r < r0 || r > r1) return false;
        if (r == r0 && c < c0) return false;
        if (r == r1 && c > c1) return false;
        return true;
    }
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool create(int pixelHeight);
    void destroy();

    int cellW() const { return m_cellW; }
    int cellH() const { return m_cellH; }
    int fontPixelHeight() const { return m_fontSize; }

    void paint(HDC target, int w, int h, Terminal& term, int scrollOffset,
               bool focused, const Selection& sel);

private:
    struct BrushEntry {
        uint32_t color;
        HBRUSH   brush;
    };

    HBRUSH brushFor(uint32_t color);
    void   releaseBrushes();
    void   ensureBuffer(HDC ref, int w, int h);
    bool   rebuildFonts(int pixelHeight);
    HFONT  pickFont(const Cell& c, bool bold) const;
    void   drawRun(HDC dc, const std::vector<Cell>& row, int y,
                   int startCol, int endCol, bool wide, bool bold, uint32_t fg);

    HFONT m_latin;
    HFONT m_latinBold;
    HFONT m_cjk;
    HFONT m_cjkBold;

    HDC     m_memDC;
    HBITMAP m_memBmp;
    HBITMAP m_oldBmp;
    int     m_bufW;
    int     m_bufH;

    int m_cellW;
    int m_cellH;
    int m_baseY;
    int m_latinAscent;
    int m_cjkAscent;
    int m_fontSize;

    std::vector<BrushEntry> m_brushes;

    unsigned long m_lastRev;
    int           m_lastScroll;
    int           m_lastW;
    int           m_lastH;
    int           m_lastSel;
    bool          m_lastFocus;
    bool          m_lastCaret;
    int           m_lastFont;
};

}
