#include "editor.h"

#include "fs.h"

#include <algorithm>
#include <set>

namespace wslterm {

static const std::set<std::wstring>& keywordSet() {
    static const std::set<std::wstring> s = {
        L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"default",
        L"break", L"continue", L"return", L"goto", L"sizeof", L"new", L"delete",
        L"throw", L"try", L"catch", L"using", L"namespace", L"class", L"struct",
        L"union", L"enum", L"typedef", L"template", L"typename", L"public",
        L"private", L"protected", L"virtual", L"override", L"final", L"static",
        L"const", L"constexpr", L"consteval", L"constinit", L"volatile", L"mutable",
        L"explicit", L"friend", L"operator", L"inline", L"extern", L"register",
        L"thread_local", L"auto", L"decltype", L"co_await", L"co_return",
        L"co_yield", L"alignas", L"alignof", L"static_assert", L"noexcept",
        L"concept", L"requires", L"export", L"module", L"import", L"and", L"or",
        L"not", L"xor", L"this", L"super", L"self", L"in", L"is", L"as", L"let",
        L"var", L"func", L"guard", L"defer", L"repeat", L"where", L"extension",
        L"protocol", L"init", L"deinit", L"subscript", L"lazy", L"weak",
        L"unowned", L"mutating", L"nonmutating", L"convenience", L"required",
        L"indirect", L"internal", L"fileprivate", L"open"
    };
    return s;
}

static const std::set<std::wstring>& typeSet() {
    static const std::set<std::wstring> s = {
        L"void", L"bool", L"char", L"short", L"int", L"long", L"float",
        L"double", L"signed", L"unsigned", L"wchar_t", L"char8_t", L"char16_t",
        L"char32_t", L"size_t", L"ssize_t", L"ptrdiff_t", L"intptr_t",
        L"uintptr_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t",
        L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t", L"va_list", L"FILE",
        L"std", L"string", L"vector", L"map", L"set", L"unordered_map",
        L"unordered_set", L"shared_ptr", L"unique_ptr", L"weak_ptr", L"pair",
        L"tuple", L"optional", L"variant", L"function", L"atomic", L"mutex",
        L"thread", L"String", L"Int", L"Double", L"Float", L"Bool", L"Character",
        L"Array", L"Dictionary", L"Optional", L"Any", L"AnyObject", L"Void",
        L"id", L"instancetype", L"Class", L"SEL", L"IMP", L"BOOL", L"NSInteger",
        L"NSUInteger", L"CGFloat", L"NSString", L"NSMutableString", L"NSArray",
        L"NSMutableArray", L"NSDictionary", L"NSMutableDictionary", L"NSNumber",
        L"NSObject", L"NSError", L"NSData", L"NSURL", L"NSDate", L"NSTimer",
        L"UIView", L"UIViewController", L"NSView", L"NSWindow", L"dispatch_queue_t",
        L"uint", L"ulong", L"ushort", L"byte", L"uintptr", L"nint", L"nuint"
    };
    return s;
}

static const std::set<std::wstring>& constantSet() {
    static const std::set<std::wstring> s = {
        L"true", L"false", L"nullptr", L"NULL", L"nil", L"Nil", L"YES", L"NO",
        L"self", L"Self", L"true_", L"__func__", L"__FILE__", L"__LINE__",
        L"M_PI", L"INT_MAX", L"INT_MIN", L"UINT_MAX", L"LONG_MAX", L"LONG_MIN"
    };
    return s;
}

int charDisplayWidth(wchar_t c) {
    unsigned cp = (unsigned)c;
    if (cp >= 0x1100 && cp <= 0x115F) return 2;
    if (cp >= 0x2E80 && cp <= 0x303E) return 2;
    if (cp >= 0x3041 && cp <= 0x33FF) return 2;
    if (cp >= 0x3400 && cp <= 0x4DBF) return 2;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 2;
    if (cp >= 0xA000 && cp <= 0xA4CF) return 2;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 2;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return 2;
    if (cp >= 0xFF00 && cp <= 0xFF60) return 2;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;
    return 1;
}

LangKind langFromPath(const std::wstring& path) {
    std::wstring e = pathExt(path);
    if (e.empty()) return LANG_NONE;

    if (e == L"c") return LANG_C;
    if (e == L"h") return LANG_C;

    if (e == L"cpp" || e == L"cc" || e == L"cxx" || e == L"c++" ||
        e == L"hpp" || e == L"hh" || e == L"hxx" || e == L"inl" ||
        e == L"ipp" || e == L"tcc" || e == L"tpp" || e == L"h++") {
        return LANG_CPP;
    }

    if (e == L"m") return LANG_OBJC;
    if (e == L"mm") return LANG_OBJCXX;

    if (e == L"swift") return LANG_CPP;

    if (e == L"xm" || e == L"xml" || e == L"plist" || e == L"json" ||
        e == L"yaml" || e == L"yml" || e == L"toml" || e == L"md" ||
        e == L"html" || e == L"htm" || e == L"svg" || e == L"sh" ||
        e == L"bash" || e == L"py" || e == L"java" || e == L"kt") {
        return LANG_MARKUP;
    }

    return LANG_NONE;
}

const wchar_t* langName(LangKind k) {
    switch (k) {
    case LANG_C:      return L"C";
    case LANG_CPP:    return L"C++";
    case LANG_OBJC:   return L"Objective-C";
    case LANG_OBJCXX: return L"Objective-C++";
    case LANG_MARKUP: return L"Markup";
    default:          return L"Text";
    }
}

static inline bool isDigitW(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

static inline bool isIdentStartW(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           c == L'_' || c == L'$' || (unsigned)c >= 0x80;
}

static inline bool isIdentCharW(wchar_t c) {
    return isIdentStartW(c) || isDigitW(c);
}

static inline bool isOpCharW(wchar_t c) {
    switch (c) {
    case L'+': case L'-': case L'*': case L'/': case L'%':
    case L'=': case L'!': case L'<': case L'>': case L'&':
    case L'|': case L'^': case L'~': case L'?': case L':':
    case L';': case L',': case L'.': case L'(': case L')':
    case L'[': case L']': case L'{': case L'}':
        return true;
    default:
        return false;
    }
}

static inline bool lineStartOnly(const std::wstring& s, int idx) {
    for (int i = 0; i < idx; ++i) {
        if (s[(size_t)i] != L' ' && s[(size_t)i] != L'\t') return false;
    }
    return true;
}

void tokenizeLine(const std::wstring& s, uint8_t startState, LangKind lang,
                  std::vector<Token>& out, uint8_t& endState) {
    out.clear();
    endState = 0;

    const int n = (int)s.size();
    if (n == 0) return;

    if (lang == LANG_NONE) {
        out.push_back(Token(0, n, TK_TEXT));
        return;
    }

    int i = 0;

    if (startState == 1) {
        int j = 0;
        int end = -1;
        while (j + 1 < n) {
            if (s[(size_t)j] == L'*' && s[(size_t)(j + 1)] == L'/') {
                end = j + 2;
                break;
            }
            ++j;
        }
        if (end < 0) {
            out.push_back(Token(0, n, TK_COMMENT));
            endState = 1;
            return;
        }
        out.push_back(Token(0, end, TK_COMMENT));
        i = end;
    }

    while (i < n) {
        wchar_t c = s[(size_t)i];

        if (c == L' ' || c == L'\t') {
            ++i;
            continue;
        }

        if (c == L'/' && i + 1 < n && s[(size_t)(i + 1)] == L'/') {
            out.push_back(Token(i, n - i, TK_COMMENT));
            return;
        }

        if (c == L'/' && i + 1 < n && s[(size_t)(i + 1)] == L'*') {
            int j = i + 2;
            int end = -1;
            while (j + 1 < n) {
                if (s[(size_t)j] == L'*' && s[(size_t)(j + 1)] == L'/') {
                    end = j + 2;
                    break;
                }
                ++j;
            }
            if (end < 0) {
                out.push_back(Token(i, n - i, TK_COMMENT));
                endState = 1;
                return;
            }
            out.push_back(Token(i, end - i, TK_COMMENT));
            i = end;
            continue;
        }

        if (c == L'#' && lineStartOnly(s, i)) {
            int j = i + 1;
            while (j < n && (s[(size_t)j] == L' ' || s[(size_t)j] == L'\t')) ++j;
            int ws = j;
            while (j < n && isIdentCharW(s[(size_t)j])) ++j;

            out.push_back(Token(i, j - i, TK_PREPROC));

            std::wstring directive = s.substr((size_t)ws, (size_t)(j - ws));
            if (j < n) {
                bool inc = (directive == L"include" || directive == L"import");
                out.push_back(Token(j, n - j, inc ? TK_STRING : TK_TEXT));
            }
            return;
        }

        if (c == L'"' || c == L'\'') {
            int j = i + 1;
            while (j < n) {
                if (s[(size_t)j] == L'\\') { j += 2; continue; }
                if (s[(size_t)j] == c) { ++j; break; }
                ++j;
            }
            if (j > n) j = n;
            out.push_back(Token(i, j - i, TK_STRING));
            i = j;
            continue;
        }

        if (c == L'@') {
            if (i + 1 < n && s[(size_t)(i + 1)] == L'"') {
                int j = i + 2;
                while (j < n) {
                    if (s[(size_t)j] == L'\\') { j += 2; continue; }
                    if (s[(size_t)j] == L'"') { ++j; break; }
                    ++j;
                }
                if (j > n) j = n;
                out.push_back(Token(i, j - i, TK_STRING));
                i = j;
                continue;
            }

            if (i + 1 < n && isIdentStartW(s[(size_t)(i + 1)])) {
                int j = i + 1;
                while (j < n && isIdentCharW(s[(size_t)j])) ++j;
                out.push_back(Token(i, j - i, TK_OBJC));
                i = j;
                continue;
            }
            ++i;
            continue;
        }

        if (isDigitW(c) || (c == L'.' && i + 1 < n && isDigitW(s[(size_t)(i + 1)]))) {
            int j = i;
            while (j < n) {
                wchar_t d = s[(size_t)j];
                if (isIdentCharW(d) || d == L'.' || d == L'\'') ++j;
                else break;
            }
            out.push_back(Token(i, j - i, TK_NUMBER));
            i = j;
            continue;
        }

        if (isIdentStartW(c)) {
            int j = i + 1;
            while (j < n && isIdentCharW(s[(size_t)j])) ++j;

            std::wstring word = s.substr((size_t)i, (size_t)(j - i));

            int k = j;
            while (k < n && (s[(size_t)k] == L' ' || s[(size_t)k] == L'\t')) ++k;
            bool call = (k < n && s[(size_t)k] == L'(');

            TokenKind kind = TK_TEXT;
            if (constantSet().count(word))     kind = TK_CONSTANT;
            else if (keywordSet().count(word)) kind = TK_KEYWORD;
            else if (typeSet().count(word))    kind = TK_TYPE;
            else if (call)                     kind = TK_FUNC;

            if (kind != TK_TEXT) out.push_back(Token(i, j - i, kind));

            i = j;
            continue;
        }

        if (isOpCharW(c)) {
            int j = i;
            while (j < n && isOpCharW(s[(size_t)j])) ++j;
            out.push_back(Token(i, j - i, TK_OP));
            i = j;
            continue;
        }

        ++i;
    }
}

Editor::Editor()
    : m_open(false), m_dirty(false), m_utf16(false), m_crlf(false), m_lang(LANG_NONE),
      m_cx(0), m_cy(0), m_prefCol(0), m_scrollY(0), m_scrollX(0),
      m_selActive(false), m_selL0(0), m_selC0(0), m_selL1(0), m_selC1(0),
      m_anchorLine(0), m_anchorCol(0), m_tab(4) {
    m_lines.push_back(std::wstring());
    m_state.push_back(0);
}

void Editor::makeSingleLine() {
    if (m_lines.empty()) m_lines.push_back(std::wstring());
    if (m_state.size() != m_lines.size()) m_state.resize(m_lines.size(), 0);
}

const std::wstring& Editor::line(int i) const {
    static const std::wstring empty;
    if (i < 0 || i >= (int)m_lines.size()) return empty;
    return m_lines[(size_t)i];
}

std::wstring Editor::fileName() const {
    if (m_path.empty()) return L"未命名";
    return pathFileName(m_path);
}

uint8_t Editor::lineStartState(int i) const {
    if (i < 0 || i >= (int)m_state.size()) return 0;
    return m_state[(size_t)i];
}

void Editor::setTabWidth(int n) {
    if (n < 1) n = 1;
    if (n > 16) n = 16;
    m_tab = n;
}

bool Editor::open(const std::wstring& path) {
    std::wstring text;
    bool utf16 = false;
    if (!readTextFile(path, text, utf16)) return false;

    m_lines.clear();

    m_crlf = (text.find(L"\r\n") != std::wstring::npos);

    std::wstring cur;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];
        if (ch == L'\n') {
            m_lines.push_back(cur);
            cur.clear();
            continue;
        }
        if (ch == L'\r') {
            m_lines.push_back(cur);
            cur.clear();
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            continue;
        }
        cur.push_back(ch);
    }
    m_lines.push_back(cur);

    m_path   = path;
    m_utf16  = utf16;
    m_lang   = langFromPath(path);
    m_open   = true;
    m_dirty  = false;
    m_cx     = 0;
    m_cy     = 0;
    m_prefCol = 0;
    m_scrollY = 0;
    m_scrollX = 0;
    m_selActive = false;

    m_state.assign(m_lines.size(), 0);
    reflowStates(0);

    return true;
}

void Editor::newFile() {
    m_path.clear();
    m_lines.assign(1, std::wstring());
    m_state.assign(1, 0);
    m_open = true;
    m_dirty = false;
    m_utf16 = false;
    m_crlf = true;
    m_lang = LANG_NONE;
    m_cx = m_cy = 0;
    m_prefCol = 0;
    m_scrollY = m_scrollX = 0;
    m_selActive = false;
}

void Editor::close() {
    m_path.clear();
    m_lines.assign(1, std::wstring());
    m_state.assign(1, 0);
    m_open = false;
    m_dirty = false;
    m_lang = LANG_NONE;
    m_cx = m_cy = 0;
    m_prefCol = 0;
    m_scrollY = m_scrollX = 0;
    m_selActive = false;
}

bool Editor::save() {
    if (!m_open || m_path.empty()) return false;

    std::wstring out;
    size_t est = 0;
    for (size_t i = 0; i < m_lines.size(); ++i) est += m_lines[i].size() + 2;
    out.reserve(est);

    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (i) out += (m_crlf ? L"\r\n" : L"\n");
        out += m_lines[i];
    }

    if (!writeTextFile(m_path, out, m_utf16)) return false;

    m_dirty = false;
    return true;
}

bool Editor::saveAs(const std::wstring& path) {
    if (!m_open) return false;

    std::wstring old = m_path;
    m_path = path;
    m_lang = langFromPath(path);

    if (!save()) {
        m_path = old;
        m_lang = langFromPath(path);
        return false;
    }
    return true;
}

void Editor::markDirty() {
    m_dirty = true;
}

void Editor::reflowStates(int from) {
    makeSingleLine();
    if (from < 0) from = 0;
    if (from >= (int)m_lines.size()) return;

    uint8_t st = m_state[(size_t)from];

    std::vector<Token> tmp;
    for (int i = from; i < (int)m_lines.size(); ++i) {
        m_state[(size_t)i] = st;
        uint8_t end = st;
        if (st == 1 || m_lang != LANG_NONE) {
            tokenizeLine(m_lines[(size_t)i], st, m_lang, tmp, end);
        }
        if (i + 1 < (int)m_state.size()) m_state[(size_t)(i + 1)] = end;
        st = end;
    }
}

int Editor::visualCol(int lineIdx, int col) const {
    if (lineIdx < 0 || lineIdx >= (int)m_lines.size()) return 0;
    const std::wstring& s = m_lines[(size_t)lineIdx];
    if (col > (int)s.size()) col = (int)s.size();

    int v = 0;
    for (int i = 0; i < col; ++i) {
        wchar_t c = s[(size_t)i];
        if (c == L'\t') v += m_tab - (v % m_tab);
        else v += charDisplayWidth(c);
    }
    return v;
}

int Editor::colFromVisual(int lineIdx, int vcol) const {
    if (lineIdx < 0 || lineIdx >= (int)m_lines.size()) return 0;
    const std::wstring& s = m_lines[(size_t)lineIdx];

    int v = 0;
    for (int i = 0; i < (int)s.size(); ++i) {
        if (v >= vcol) return i;
        wchar_t c = s[(size_t)i];
        if (c == L'\t') v += m_tab - (v % m_tab);
        else v += charDisplayWidth(c);
    }
    return (int)s.size();
}

void Editor::setCursor(int lineIdx, int col, bool extendSel) {
    if (!m_open) return;
    if (lineIdx < 0) lineIdx = 0;
    if (lineIdx >= (int)m_lines.size()) lineIdx = (int)m_lines.size() - 1;

    int len = (int)m_lines[(size_t)lineIdx].size();
    if (col < 0) col = 0;
    if (col > len) col = len;

    if (extendSel) {
        if (!m_selActive) {
            m_anchorLine = m_cy;
            m_anchorCol = m_cx;
            m_selActive = true;
        }
    } else {
        m_selActive = false;
    }

    m_cy = lineIdx;
    m_cx = col;
    m_prefCol = col;

    updateSelRange();
}

void Editor::beginMove(bool sel) {
    if (sel) {
        if (!m_selActive) {
            m_anchorLine = m_cy;
            m_anchorCol = m_cx;
            m_selActive = true;
        }
    } else {
        m_selActive = false;
    }
}

void Editor::updateSelRange() {
    if (!m_selActive) return;

    int l0 = m_anchorLine, c0 = m_anchorCol;
    int l1 = m_cy, c1 = m_cx;

    if (l0 > l1 || (l0 == l1 && c0 > c1)) {
        std::swap(l0, l1);
        std::swap(c0, c1);
    }

    m_selL0 = l0;
    m_selC0 = c0;
    m_selL1 = l1;
    m_selC1 = c1;

    if (m_selL0 == m_selL1 && m_selC0 == m_selC1) m_selActive = false;
}

void Editor::selectionRange(int& l0, int& c0, int& l1, int& c1) const {
    l0 = m_selL0;
    c0 = m_selC0;
    l1 = m_selL1;
    c1 = m_selC1;
}

bool Editor::isSelected(int lineIdx, int col) const {
    if (!m_selActive) return false;
    if (lineIdx < m_selL0 || lineIdx > m_selL1) return false;
    if (lineIdx == m_selL0 && col < m_selC0) return false;
    if (lineIdx == m_selL1 && col >= m_selC1) return false;
    return true;
}

void Editor::selectAll() {
    if (!m_open) return;
    m_anchorLine = 0;
    m_anchorCol = 0;
    m_cy = (int)m_lines.size() - 1;
    m_cx = (int)m_lines.back().size();
    m_selActive = true;
    updateSelRange();
}

std::wstring Editor::selectedText() const {
    if (!m_selActive) return std::wstring();

    std::wstring out;
    for (int i = m_selL0; i <= m_selL1; ++i) {
        const std::wstring& s = m_lines[(size_t)i];
        int a = (i == m_selL0) ? m_selC0 : 0;
        int b = (i == m_selL1) ? m_selC1 : (int)s.size();
        if (a > (int)s.size()) a = (int)s.size();
        if (b > (int)s.size()) b = (int)s.size();
        out += s.substr((size_t)a, (size_t)(b - a));
        if (i < m_selL1) out += (m_crlf ? L"\r\n" : L"\n");
    }
    return out;
}

void Editor::moveLeft(bool sel) {
    if (!m_open) return;
    beginMove(sel);

    if (m_cx > 0) {
        --m_cx;
    } else if (m_cy > 0) {
        --m_cy;
        m_cx = (int)m_lines[(size_t)m_cy].size();
    }
    m_prefCol = m_cx;
    updateSelRange();
}

void Editor::moveRight(bool sel) {
    if (!m_open) return;
    beginMove(sel);

    const int len = (int)m_lines[(size_t)m_cy].size();
    if (m_cx < len) {
        ++m_cx;
    } else if (m_cy + 1 < (int)m_lines.size()) {
        ++m_cy;
        m_cx = 0;
    }
    m_prefCol = m_cx;
    updateSelRange();
}

void Editor::moveUp(int n, bool sel) {
    if (!m_open) return;
    beginMove(sel);

    m_cy -= n;
    if (m_cy < 0) m_cy = 0;

    int len = (int)m_lines[(size_t)m_cy].size();
    m_cx = (m_prefCol < len) ? m_prefCol : len;
    updateSelRange();
}

void Editor::moveDown(int n, bool sel) {
    if (!m_open) return;
    beginMove(sel);

    m_cy += n;
    if (m_cy > (int)m_lines.size() - 1) m_cy = (int)m_lines.size() - 1;

    int len = (int)m_lines[(size_t)m_cy].size();
    m_cx = (m_prefCol < len) ? m_prefCol : len;
    updateSelRange();
}

void Editor::moveHome(bool sel) {
    if (!m_open) return;
    beginMove(sel);
    m_cx = 0;
    m_prefCol = 0;
    updateSelRange();
}

void Editor::moveEnd(bool sel) {
    if (!m_open) return;
    beginMove(sel);
    m_cx = (int)m_lines[(size_t)m_cy].size();
    m_prefCol = m_cx;
    updateSelRange();
}

void Editor::moveDocStart(bool sel) {
    if (!m_open) return;
    beginMove(sel);
    m_cy = 0;
    m_cx = 0;
    m_prefCol = 0;
    updateSelRange();
}

void Editor::moveDocEnd(bool sel) {
    if (!m_open) return;
    beginMove(sel);
    m_cy = (int)m_lines.size() - 1;
    m_cx = (int)m_lines[(size_t)m_cy].size();
    m_prefCol = m_cx;
    updateSelRange();
}

void Editor::deleteSelection() {
    if (!m_selActive) return;

    const int l0 = m_selL0, c0 = m_selC0, l1 = m_selL1, c1 = m_selC1;

    std::wstring head = m_lines[(size_t)l0].substr(0, (size_t)c0);
    std::wstring tail = m_lines[(size_t)l1].substr((size_t)c1);

    m_lines.erase(m_lines.begin() + l0, m_lines.begin() + l1 + 1);
    m_lines.insert(m_lines.begin() + l0, head + tail);

    m_state.erase(m_state.begin() + l0, m_state.begin() + l1 + 1);
    m_state.insert(m_state.begin() + l0, 0);

    m_cy = l0;
    m_cx = (int)head.size();
    m_prefCol = m_cx;
    m_selActive = false;

    markDirty();
    reflowStates(l0 - 1);
}

void Editor::insertChar(wchar_t c) {
    if (!m_open) return;

    if (c == L'\r' || c == L'\n') {
        newline();
        return;
    }
    if (c == L'\t') {
        insertTab();
        return;
    }
    if (c < 0x20) return;

    if (m_selActive) deleteSelection();
    makeSingleLine();

    std::wstring& l = m_lines[(size_t)m_cy];
    l.insert(l.begin() + m_cx, c);
    ++m_cx;
    m_prefCol = m_cx;

    markDirty();
    reflowStates(m_cy);
}

void Editor::insertString(const std::wstring& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r') {
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
            newline();
        } else if (c == L'\n') {
            newline();
        } else {
            insertChar(c);
        }
    }
}

void Editor::insertTab() {
    if (!m_open) return;
    if (m_selActive) deleteSelection();

    int v = visualCol(m_cy, m_cx);
    int spaces = m_tab - (v % m_tab);
    if (spaces <= 0) spaces = m_tab;

    std::wstring& l = m_lines[(size_t)m_cy];
    l.insert(m_cx, (size_t)spaces, L' ');
    m_cx += spaces;
    m_prefCol = m_cx;

    markDirty();
    reflowStates(m_cy);
}

void Editor::newline() {
    if (!m_open) return;
    if (m_selActive) deleteSelection();
    makeSingleLine();

    std::wstring& l = m_lines[(size_t)m_cy];

    std::wstring indent;
    for (size_t i = 0; i < l.size(); ++i) {
        if (l[i] == L' ' || l[i] == L'\t') indent.push_back(l[i]);
        else break;
    }

    bool brace = false;
    {
        int k = (int)l.size() - 1;
        while (k >= 0 && (l[(size_t)k] == L' ' || l[(size_t)k] == L'\t')) --k;
        if (k >= 0 && l[(size_t)k] == L'{') brace = true;
    }

    std::wstring rest = l.substr((size_t)m_cx);
    l.erase((size_t)m_cx);

    std::wstring next = indent;
    if (brace) next.append((size_t)m_tab, L' ');
    next += rest;

    m_lines.insert(m_lines.begin() + m_cy + 1, next);
    m_state.insert(m_state.begin() + m_cy + 1, 0);

    ++m_cy;
    m_cx = (int)(indent.size() + (brace ? (size_t)m_tab : 0));
    m_prefCol = m_cx;

    markDirty();
    reflowStates(m_cy - 1);
}

void Editor::backspace() {
    if (!m_open) return;

    if (m_selActive) {
        deleteSelection();
        return;
    }

    if (m_cx > 0) {
        std::wstring& l = m_lines[(size_t)m_cy];
        l.erase((size_t)(m_cx - 1), 1);
        --m_cx;
        m_prefCol = m_cx;
        markDirty();
        reflowStates(m_cy);
    } else if (m_cy > 0) {
        int prevLen = (int)m_lines[(size_t)(m_cy - 1)].size();
        m_lines[(size_t)(m_cy - 1)] += m_lines[(size_t)m_cy];
        m_lines.erase(m_lines.begin() + m_cy);
        m_state.erase(m_state.begin() + m_cy);
        --m_cy;
        m_cx = prevLen;
        m_prefCol = m_cx;
        markDirty();
        reflowStates(m_cy);
    }
}

void Editor::deleteForward() {
    if (!m_open) return;

    if (m_selActive) {
        deleteSelection();
        return;
    }

    std::wstring& l = m_lines[(size_t)m_cy];
    if (m_cx < (int)l.size()) {
        l.erase((size_t)m_cx, 1);
        markDirty();
        reflowStates(m_cy);
    } else if (m_cy + 1 < (int)m_lines.size()) {
        l += m_lines[(size_t)(m_cy + 1)];
        m_lines.erase(m_lines.begin() + m_cy + 1);
        m_state.erase(m_state.begin() + m_cy + 1);
        markDirty();
        reflowStates(m_cy);
    }
}

void Editor::ensureCursorVisible(int viewLines, int viewCols) {
    if (viewLines < 1) viewLines = 1;
    if (viewCols < 1) viewCols = 1;

    if (m_cy < m_scrollY) m_scrollY = m_cy;
    if (m_cy >= m_scrollY + viewLines) m_scrollY = m_cy - viewLines + 1;
    if (m_scrollY < 0) m_scrollY = 0;

    int vc = visualCol(m_cy, m_cx);
    if (vc < m_scrollX) m_scrollX = vc;
    if (vc >= m_scrollX + viewCols) m_scrollX = vc - viewCols + 1;
    if (m_scrollX < 0) m_scrollX = 0;
}

}
