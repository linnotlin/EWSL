#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wslterm {

enum TokenKind {
    TK_TEXT = 0,
    TK_KEYWORD,
    TK_TYPE,
    TK_PREPROC,
    TK_STRING,
    TK_NUMBER,
    TK_COMMENT,
    TK_FUNC,
    TK_OBJC,
    TK_OP,
    TK_CONSTANT,
    TK_KIND_COUNT
};

struct Token {
    int       start;
    int       len;
    TokenKind kind;

    Token() : start(0), len(0), kind(TK_TEXT) {}
    Token(int s, int l, TokenKind k) : start(s), len(l), kind(k) {}
};

enum LangKind {
    LANG_NONE = 0,
    LANG_C,
    LANG_CPP,
    LANG_OBJC,
    LANG_OBJCXX,
    LANG_MARKUP
};

LangKind    langFromPath(const std::wstring& path);
const wchar_t* langName(LangKind k);

int charDisplayWidth(wchar_t c);

class Editor {
public:
    Editor();

    bool open(const std::wstring& path);
    bool save();
    bool saveAs(const std::wstring& path);
    void newFile();
    void close();

    bool isOpen() const { return m_open; }
    bool dirty() const { return m_dirty; }
    const std::wstring& path() const { return m_path; }
    std::wstring fileName() const;
    LangKind lang() const { return m_lang; }

    int lineCount() const { return (int)m_lines.size(); }
    const std::wstring& line(int i) const;

    int cursorLine() const { return m_cy; }
    int cursorCol() const { return m_cx; }
    int preferredCol() const { return m_prefCol; }
    void setCursor(int line, int col, bool extendSel);

    int  scrollY() const { return m_scrollY; }
    int  scrollX() const { return m_scrollX; }
    void setScroll(int y, int x) { m_scrollY = y; m_scrollX = x; }
    void ensureCursorVisible(int viewLines, int viewCols);

    bool hasSelection() const { return m_selActive; }
    void selectionRange(int& l0, int& c0, int& l1, int& c1) const;
    void selectAll();
    void clearSelection() { m_selActive = false; }
    bool isSelected(int line, int col) const;
    void updateSelRange();
    void beginMove(bool sel);

    void moveLeft(bool sel);
    void moveRight(bool sel);
    void moveUp(int lines, bool sel);
    void moveDown(int lines, bool sel);
    void moveHome(bool sel);
    void moveEnd(bool sel);
    void moveDocStart(bool sel);
    void moveDocEnd(bool sel);

    void insertChar(wchar_t c);
    void insertString(const std::wstring& s);
    void newline();
    void backspace();
    void deleteForward();
    void deleteSelection();
    std::wstring selectedText() const;
    void insertTab();

    int  tabWidth() const { return m_tab; }
    void setTabWidth(int n);

    int  visualCol(int lineIdx, int col) const;
    int  colFromVisual(int lineIdx, int vcol) const;

    uint8_t lineStartState(int i) const;

private:
    void makeSingleLine();
    void reflowStates(int fromLine);
    void markDirty();

    std::wstring              m_path;
    std::vector<std::wstring> m_lines;
    std::vector<uint8_t>      m_state;

    bool     m_open;
    bool     m_dirty;
    bool     m_utf16;
    bool     m_crlf;
    LangKind m_lang;

    int m_cx;
    int m_cy;
    int m_prefCol;

    int m_scrollY;
    int m_scrollX;

    bool m_selActive;
    int  m_selL0, m_selC0, m_selL1, m_selC1;
    int  m_anchorLine, m_anchorCol;

    int m_tab;
};

void tokenizeLine(const std::wstring& text, uint8_t startState,
                  LangKind lang, std::vector<Token>& out, uint8_t& endState);

}
