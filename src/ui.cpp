#include "ui.h"

#include "lang.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Gdiplus;

namespace wslterm {

static ULONG_PTR g_token = 0;

static Color kBar       (255, 0xFB, 0xFB, 0xFD);
static Color kWindowBg  (255, 0xF2, 0xF2, 0xF7);
static Color kSideBg    (255, 0xF7, 0xF7, 0xFA);
static Color kCard      (255, 0xFF, 0xFF, 0xFF);
static Color kSep       (255, 0xE5, 0xE5, 0xEA);
static Color kText      (255, 0x1C, 0x1C, 0x1E);
static Color kText2     (255, 0x8E, 0x8E, 0x93);
static Color kText3     (255, 0xC0, 0xC0, 0xC8);
static Color kAccent    (255, 0x00, 0x7A, 0xFF);
static Color kAccentDark(255, 0x00, 0x62, 0xCC);
static Color kAccentSoft(255, 0xE1, 0xEE, 0xFF);
static Color kHoverSel  (255, 0xEC, 0xEC, 0xF1);
static Color kHoverBtn  (255, 0xE8, 0xE8, 0xED);
static Color kWhite     (255, 0xFF, 0xFF, 0xFF);
static Color kDanger    (255, 0xFF, 0x3B, 0x30);
static Color kPanelLine (255, 0xD8, 0xD8, 0xDE);
static Color kTrack     (255, 0xE4, 0xE4, 0xEA);
static Color kOk        (255, 0x34, 0xC7, 0x59);

void Ui::applyTheme(int theme) {
    if (theme == 1) {
        kBar       = Color(255, 0x1E, 0x1E, 0x22);
        kWindowBg  = Color(255, 0x14, 0x14, 0x17);
        kSideBg    = Color(255, 0x1A, 0x1A, 0x1E);
        kCard      = Color(255, 0x24, 0x24, 0x29);
        kSep       = Color(255, 0x33, 0x33, 0x39);
        kText      = Color(255, 0xF2, 0xF2, 0xF5);
        kText2     = Color(255, 0x9A, 0x9A, 0xA3);
        kText3     = Color(255, 0x66, 0x66, 0x70);
        kAccent    = Color(255, 0x0A, 0x84, 0xFF);
        kAccentDark= Color(255, 0x00, 0x62, 0xCC);
        kAccentSoft= Color(255, 0x14, 0x33, 0x59);
        kHoverSel  = Color(255, 0x2A, 0x2A, 0x31);
        kHoverBtn  = Color(255, 0x32, 0x32, 0x3A);
        kWhite     = Color(255, 0xFF, 0xFF, 0xFF);
        kDanger    = Color(255, 0xFF, 0x45, 0x3A);
        kPanelLine = Color(255, 0x3A, 0x3A, 0x42);
        kTrack     = Color(255, 0x33, 0x33, 0x3A);
        kOk        = Color(255, 0x30, 0xD1, 0x58);
    } else {
        kBar       = Color(255, 0xFB, 0xFB, 0xFD);
        kWindowBg  = Color(255, 0xF2, 0xF2, 0xF7);
        kSideBg    = Color(255, 0xF7, 0xF7, 0xFA);
        kCard      = Color(255, 0xFF, 0xFF, 0xFF);
        kSep       = Color(255, 0xE5, 0xE5, 0xEA);
        kText      = Color(255, 0x1C, 0x1C, 0x1E);
        kText2     = Color(255, 0x8E, 0x8E, 0x93);
        kText3     = Color(255, 0xC0, 0xC0, 0xC8);
        kAccent    = Color(255, 0x00, 0x7A, 0xFF);
        kAccentDark= Color(255, 0x00, 0x62, 0xCC);
        kAccentSoft= Color(255, 0xE1, 0xEE, 0xFF);
        kHoverSel  = Color(255, 0xEC, 0xEC, 0xF1);
        kHoverBtn  = Color(255, 0xE8, 0xE8, 0xED);
        kWhite     = Color(255, 0xFF, 0xFF, 0xFF);
        kDanger    = Color(255, 0xFF, 0x3B, 0x30);
        kPanelLine = Color(255, 0xD8, 0xD8, 0xDE);
        kTrack     = Color(255, 0xE4, 0xE4, 0xEA);
        kOk        = Color(255, 0x34, 0xC7, 0x59);
    }
}

static COLORREF refOf(const Color& c) {
    return RGB(c.GetR(), c.GetG(), c.GetB());
}

static HFONT g_hTitle = NULL;
static HFONT g_hBody  = NULL;
static HFONT g_hBold  = NULL;
static HFONT g_hSmall = NULL;
static HFONT g_hBig   = NULL;
static HFONT g_hMono  = NULL;

static HFONT makeUiFont(int dpi, double px, bool bold) {
    double s = dpi / 96.0;
    int h = (int)(px * s + 0.5);
    if (h < 1) h = 1;
    return CreateFontW(-h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

// 界面里的等宽片段（设置页 Arch 命令之类）用它。字号只跟界面字号走，
// 绝不能拿终端字号来画——终端字号最大 40，会把卡片撑爆。
static HFONT makeMonoFont(int dpi, double px) {
    double s = dpi / 96.0;
    int h = (int)(px * s + 0.5);
    if (h < 1) h = 1;
    return CreateFontW(-h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

static void releaseFonts() {
    HFONT* all[6] = { &g_hTitle, &g_hBody, &g_hBold, &g_hSmall, &g_hBig, &g_hMono };
    for (int i = 0; i < 6; ++i) {
        if (*all[i]) { DeleteObject(*all[i]); *all[i] = NULL; }
    }
}

static void buildFonts(int dpi, int uiPx) {
    releaseFonts();

    double f = uiPx / 14.0;
    if (f < 0.85) f = 0.85;
    if (f > 1.40) f = 1.40;

    g_hTitle = makeUiFont(dpi, 13.0 * f, false);
    g_hBody  = makeUiFont(dpi, 14.0 * f, false);
    g_hBold  = makeUiFont(dpi, 14.0 * f, true);
    g_hSmall = makeUiFont(dpi, 12.0 * f, false);
    g_hBig   = makeUiFont(dpi, 19.0 * f, true);
    g_hMono  = makeMonoFont(dpi, 12.5 * f);
}

// 量一段文字在当前字体下的宽度，用来做右对齐和「放不放得下」的判断。
static int textWidth(HDC dc, HFONT f, const std::wstring& s) {
    if (!dc || !f || s.empty()) return 0;
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz;
    memset(&sz, 0, sizeof(sz));
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    if (old) SelectObject(dc, old);
    return sz.cx;
}

static Color mixColor(const Color& a, const Color& b, double t) {
    if (t <= 0.0) return a;
    if (t >= 1.0) return b;
    double r = a.GetR() + (b.GetR() - a.GetR()) * t;
    double g = a.GetG() + (b.GetG() - a.GetG()) * t;
    double bl = a.GetB() + (b.GetB() - a.GetB()) * t;
    double al = a.GetA() + (b.GetA() - a.GetA()) * t;
    return Color((BYTE)(al + 0.5), (BYTE)(r + 0.5), (BYTE)(g + 0.5), (BYTE)(bl + 0.5));
}

static double easeOutCubic(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    double u = 1.0 - t;
    return 1.0 - u * u * u;
}

static double easeOutQuart(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    double u = 1.0 - t;
    return 1.0 - u * u * u * u;
}

static double easeInOutCubic(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    if (t < 0.5) { double u = t * 2.0; return 0.5 * u * u * u; }
    double u = (1.0 - t) * 2.0;
    return 1.0 - 0.5 * u * u * u;
}

static void springStep(double& x, double& v, double target,
                       double omega, double zeta, double dtSec) {
    if (dtSec <= 0.0) return;
    double steps = dtSec / 0.007;
    if (steps < 1.0) steps = 1.0;
    if (steps > 8.0) steps = 8.0;
    double h = dtSec / steps;
    for (int i = 0; i < (int)steps; ++i) {
        double a = omega * omega * (target - x) - 2.0 * zeta * omega * v;
        v += a * h;
        x += v * h;
    }
}

static inline int slotForBtn(int id)   { return (id >= 0 && id < 27) ? id : -1; }
static inline int slotForNav(int idx)  { return (idx >= 0 && idx < 4) ? 24 + idx : -1; }
static inline int slotForMenu(int idx) { return (idx >= 0 && idx < 16) ? 32 + idx : -1; }
static inline int slotForTree(int idx) { return (idx >= 0 && idx < 16) ? 48 + idx : -1; }
static inline int slotForInstRow(int idx) { return (idx >= 0 && idx < 16) ? 32 + idx : -1; }

static inline int navIndexForPage(int page) {
    if (page == PAGE_INSTALL) return 0;
    if (page == PAGE_DISTRO)  return 2;
    if (page < 0 || page > 3) return -1;
    return page;
}

static void textAt(HDC dc, const std::wstring& s, HFONT f, const Color& c,
                   int x, int y, int w, int h, int align) {
    if (!f || s.empty() || w <= 0 || h <= 0) return;

    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, refOf(c));
    SetBkMode(dc, TRANSPARENT);

    UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
    if (align == 1) fmt |= DT_CENTER;
    else if (align == 2) fmt |= DT_RIGHT;

    RECT r;
    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;

    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt);

    if (old) SelectObject(dc, old);
}

static void addRound(GraphicsPath& p, REAL x, REAL y, REAL w, REAL h, REAL rad) {
    REAL d = rad * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d < 1) d = 1;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static void fillRound(Graphics& g, int l, int t, int r, int b, int rad, const Color& c) {
    REAL w = (REAL)(r - l);
    REAL h = (REAL)(b - t);
    if (w <= 0 || h <= 0) return;
    GraphicsPath p;
    addRound(p, (REAL)l, (REAL)t, w, h, (REAL)rad);
    SolidBrush br(c);
    g.FillPath(&br, &p);
}

static void strokeRound(Graphics& g, int l, int t, int r, int b, int rad,
                        const Color& c, REAL th) {
    REAL w = (REAL)(r - l);
    REAL h = (REAL)(b - t);
    if (w <= 0 || h <= 0) return;
    GraphicsPath p;
    addRound(p, (REAL)l, (REAL)t, w, h, (REAL)rad);
    Pen pen(c, th);
    g.DrawPath(&pen, &p);
}

static void iconClose(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL d = 4.6f * sc;
    g.DrawLine(&p, cx - d, cy - d, cx + d, cy + d);
    g.DrawLine(&p, cx + d, cy - d, cx - d, cy + d);
}

static void iconMin(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL d = 5.0f * sc;
    g.DrawLine(&p, cx - d, cy, cx + d, cy);
}

static void iconMax(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    REAL d = 4.4f * sc;
    g.DrawRectangle(&p, cx - d, cy - d, d * 2, d * 2);
}

static void iconCheck(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawLine(&p, cx - 4.4f * sc, cy + 0.2f * sc, cx - 1.2f * sc, cy + 3.2f * sc);
    g.DrawLine(&p, cx - 1.2f * sc, cy + 3.2f * sc, cx + 4.4f * sc, cy - 3.2f * sc);
}

static void iconChevron(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c,
                        REAL th, bool down) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL d = 3.2f * sc;
    if (down) {
        g.DrawLine(&p, cx - d, cy - d * 0.6f, cx, cy + d * 0.6f);
        g.DrawLine(&p, cx, cy + d * 0.6f, cx + d, cy - d * 0.6f);
    } else {
        g.DrawLine(&p, cx - d * 0.6f, cy - d, cx + d * 0.6f, cy);
        g.DrawLine(&p, cx + d * 0.6f, cy, cx - d * 0.6f, cy + d);
    }
}

static void iconTerminal(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL w = 7.0f * sc;
    REAL h = 6.0f * sc;
    g.DrawRectangle(&p, cx - w, cy - h, w * 2, h * 2);
    g.DrawLine(&p, cx - w * 0.45f, cy - h * 0.3f, cx - w * 0.05f, cy);
    g.DrawLine(&p, cx - w * 0.05f, cy, cx - w * 0.45f, cy + h * 0.3f);
    g.DrawLine(&p, cx + w * 0.15f, cy + h * 0.35f, cx + w * 0.5f, cy + h * 0.35f);
}

static void iconFolder(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL w = 7.2f * sc;
    REAL h = 5.4f * sc;
    GraphicsPath path;
    path.AddLine(cx - w, cy + h, cx - w, cy - h * 0.75f);
    path.AddLine(cx - w, cy - h * 0.75f, cx - w * 0.2f, cy - h * 0.75f);
    path.AddLine(cx - w * 0.2f, cy - h * 0.75f, cx - w * 0.02f, cy - h * 0.25f);
    path.AddLine(cx - w * 0.02f, cy - h * 0.25f, cx + w, cy - h * 0.25f);
    path.AddLine(cx + w, cy - h * 0.25f, cx + w, cy + h);
    path.CloseFigure();
    g.DrawPath(&p, &path);
}

static void iconGear(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawEllipse(&p, cx - 2.6f * sc, cy - 2.6f * sc, 5.2f * sc, 5.2f * sc);
    for (int i = 0; i < 6; ++i) {
        double a = i * 3.14159265 / 3.0;
        REAL x0 = cx + (REAL)(cos(a) * 4.4 * sc);
        REAL y0 = cy + (REAL)(sin(a) * 4.4 * sc);
        REAL x1 = cx + (REAL)(cos(a) * 6.6 * sc);
        REAL y1 = cy + (REAL)(sin(a) * 6.6 * sc);
        g.DrawLine(&p, x0, y0, x1, y1);
    }
}

static void iconFile(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL w = 5.0f * sc;
    REAL h = 6.4f * sc;
    GraphicsPath path;
    path.AddLine(cx - w, cy - h, cx + w * 0.35f, cy - h);
    path.AddLine(cx + w * 0.35f, cy - h, cx + w, cy - h * 0.35f);
    path.AddLine(cx + w, cy - h * 0.35f, cx + w, cy + h);
    path.AddLine(cx + w, cy + h, cx - w, cy + h);
    path.CloseFigure();
    g.DrawPath(&p, &path);
}

static void iconLayers(Graphics& g, REAL cx, REAL cy, REAL sc, const Color& c, REAL th) {
    Pen p(c, th);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    REAL w = 5.4f * sc;
    REAL h = 3.0f * sc;
    REAL dy = 3.2f * sc;

    for (int k = 0; k < 2; ++k) {
        REAL yy = cy + (k == 0 ? -dy : dy) - h;
        GraphicsPath path;
        path.AddLine(cx - w, yy, cx, yy + h);
        path.AddLine(cx, yy + h, cx + w, yy);
        path.CloseFigure();
        g.DrawPath(&p, &path);
    }
}

UiModel::UiModel()
    : page(PAGE_TERMINAL), hasTerminal(false), wslMissing(false),
      installing(false), checking(true), loadingOnline(false),
      fixStage(0), fixTick(0), fixStartTick(0), fixElapsed(0),
      wslg(WSLG_NO_DLL), wslgFixed(false),
      distroMenuOpen(false), hasFolder(false), editorOpen(false),
      editorDirty(false), fontSize(16), termFontSize(16), theme(0),
      terminalMode(0), activeMode(-1),
      instStage(INST_IDLE), instPercent(-1), instGot(0), instTotal(0),
      instCancelable(false), instLocked(false), toastTick(0) {}

Ui::Ui()
    : m_dpi(96), m_titleH(46), m_sideW(200), m_barH(42), m_navH(40),
      m_pad(14), m_radius(10), m_treeW(236), m_rowH(26), m_menuW(268),
      m_hTitle(NULL), m_hBody(NULL), m_hBold(NULL), m_hSmall(NULL), m_hBig(NULL),
      m_edFont(NULL), m_edFontBold(NULL), m_edFontH(0), m_edCharW(8),
      m_edLineH(18), m_edFontSize(0),
      m_hoverNav(-1), m_hoverBtn(0), m_hoverMenu(-1), m_hoverTree(-1),
      m_treeScroll(0), m_menuScroll(0), m_menuMaxScroll(0), m_inited(false),
      m_uiFontPx(14),
      m_aNavPill(0.0), m_navPillInit(false), m_aMenu(0.0), m_aBarPct(0.0),
      m_barInit(false), m_menuTgt(false), m_barTgt(0.0),
      m_pageAnim(false), m_pageT(1.0),
      m_pageFrom(PAGE_TERMINAL), m_pageTo(PAGE_TERMINAL) {
    for (int i = 0; i < 64; ++i) {
        m_aHover[i] = 0.0;
        m_hoverTgt[i] = 0.0;
        m_hoverVel[i] = 0.0;
        m_aPress[i] = 0.0;
    }
    m_navPillVel = 0.0;
    m_menuVel = 0.0;
    m_barVel = 0.0;
}

Ui::~Ui() {
    shutdown();
}

bool Ui::init(int dpi) {
    if (!m_inited) {
        GdiplusStartupInput in;
        if (GdiplusStartup(&g_token, &in, NULL) != Ok) return false;
        m_inited = true;
    }
    setDpi(dpi);
    return true;
}

void Ui::shutdown() {
    if (m_edFont) { DeleteObject(m_edFont); m_edFont = NULL; }
    if (m_edFontBold) { DeleteObject(m_edFontBold); m_edFontBold = NULL; }

    if (!m_inited) return;
    releaseFonts();
    GdiplusShutdown(g_token);
    g_token = 0;
    m_inited = false;
}

void Ui::setDpi(int dpi) {
    if (dpi < 72) dpi = 96;
    m_dpi = dpi;

    double s = dpi / 96.0;
    m_titleH = (int)(46 * s + 0.5);
    m_barH   = (int)(42 * s + 0.5);
    m_navH   = (int)(44 * s + 0.5);
    m_pad    = (int)(14 * s + 0.5);
    m_radius = (int)(10 * s + 0.5);
    m_rowH   = (int)(26 * s + 0.5);
    m_sideW  = (int)(200 * s + 0.5);
    m_treeW  = (int)(236 * s + 0.5);
    m_menuW  = (int)(268 * s + 0.5);

    if (m_edFont) { DeleteObject(m_edFont); m_edFont = NULL; }
    if (m_edFontBold) { DeleteObject(m_edFontBold); m_edFontBold = NULL; }
    m_edFontSize = 0;

    buildFonts(dpi, m_uiFontPx);
}

void Ui::setUiFontSize(int fontSize) {
    if (fontSize < 10) fontSize = 10;
    if (fontSize > 32) fontSize = 32;
    if (fontSize == m_uiFontPx) return;
    m_uiFontPx = fontSize;
    buildFonts(m_dpi, m_uiFontPx);
}

void Ui::tickAnim(int dtMs, const UiModel& m) {
    if (dtMs < 1) dtMs = 1;
    if (dtMs > 80) dtMs = 80;

    m_menuTgt = m.distroMenuOpen;
    if (m.page == PAGE_INSTALL && m.instPercent >= 0) m_barTgt = m.instPercent;

    int navIdx = navIndexForPage(m.page);
    if (!m_navPillInit) {
        m_aNavPill = (navIdx < 0) ? 0.0 : (double)navIdx;
        m_navPillInit = true;
    }

    const double dtSec = (double)dtMs / 1000.0;

    const double kHoverW = 12.5, kHoverZ = 0.62;
    const double kNavW   = 10.5, kNavZ   = 0.88;
    const double kMenuW  =  9.0, kMenuZ  = 0.90;
    const double kBarW   =  7.0, kBarZ   = 1.00;

    for (int i = 0; i < 64; ++i) {
        double d = m_hoverTgt[i] - m_aHover[i];
        if (d > 0.0004 || d < -0.0004 || m_hoverVel[i] != 0.0)
            springStep(m_aHover[i], m_hoverVel[i], m_hoverTgt[i], kHoverW, kHoverZ, dtSec);
        double dd = m_hoverTgt[i] - m_aHover[i];
        if (dd > -0.0004 && dd < 0.0004 && m_hoverVel[i] > -0.004 && m_hoverVel[i] < 0.004) {
            m_aHover[i] = m_hoverTgt[i];
            m_hoverVel[i] = 0.0;
        }

        if (m_aPress[i] > 0.0) {
            m_aPress[i] -= dtMs / 340.0;
            if (m_aPress[i] < 0.0) m_aPress[i] = 0.0;
        }
    }

    if (navIdx >= 0) {
        springStep(m_aNavPill, m_navPillVel, (double)navIdx, kNavW, kNavZ, dtSec);
        double nd = (double)navIdx - m_aNavPill;
        if (nd > -0.0008 && nd < 0.0008 && m_navPillVel > -0.01 && m_navPillVel < 0.01) {
            m_aNavPill = (double)navIdx;
            m_navPillVel = 0.0;
        }
    }

    double tg = m_menuTgt ? 1.0 : 0.0;
    springStep(m_aMenu, m_menuVel, tg, kMenuW, kMenuZ, dtSec);
    if (m_aMenu < 0.0) { m_aMenu = 0.0; if (m_menuVel < 0.0) m_menuVel = 0.0; }
    if (m_aMenu > 1.0) { m_aMenu = 1.0; if (m_menuVel > 0.0) m_menuVel = 0.0; }

    if (!m_barInit) {
        m_aBarPct = m_barTgt;
        m_barInit = true;
    } else {
        springStep(m_aBarPct, m_barVel, m_barTgt, kBarW, kBarZ, dtSec);
        double bd = m_barTgt - m_aBarPct;
        if (bd > -0.002 && bd < 0.002 && m_barVel > -0.004 && m_barVel < 0.004) {
            m_aBarPct = m_barTgt;
            m_barVel = 0.0;
        }
    }

    if (m_pageAnim) {
        m_pageT += dtMs / 420.0;
        if (m_pageT >= 1.0) { m_pageT = 1.0; m_pageAnim = false; }
    }
}

bool Ui::animActive() const {
    if (m_pageAnim) return true;
    if (m_aMenu > 0.0008 && m_aMenu < 0.9992) return true;
    for (int i = 0; i < 64; ++i) {
        double d = m_hoverTgt[i] - m_aHover[i];
        if (d > 0.004 || d < -0.004) return true;
        if (m_hoverVel[i] > 0.004 || m_hoverVel[i] < -0.004) return true;
        if (m_aPress[i] > 0.001) return true;
    }
    double nd = (double)navIndexForPage(m_pageTo) - m_aNavPill;
    if (nd > 0.004 || nd < -0.004) return true;
    if (m_navPillVel > 0.01 || m_navPillVel < -0.01) return true;
    double bd = m_barTgt - m_aBarPct;
    if (bd > 0.02 || bd < -0.02) return true;
    if (m_barVel > 0.02 || m_barVel < -0.02) return true;
    if (m_aMenu > 0.0015 && m_aMenu < 0.9985) return true;
    if (m_menuVel > 0.01 || m_menuVel < -0.01) return true;
    return false;
}

void Ui::beginPageTransition(int fromPage, int toPage) {
    if (fromPage == toPage) return;
    m_pageFrom = fromPage;
    m_pageTo   = toPage;
    m_pageT    = 0.0;
    m_pageAnim = true;
}

int Ui::pageSlideX() const {
    if (!m_pageAnim) return 0;
    double e = easeOutQuart(m_pageT);
    int a = navIndexForPage(m_pageFrom);
    int b = navIndexForPage(m_pageTo);
    int dir = (b >= a) ? 1 : -1;
    double base = (double)m_sideW * 0.55;
    if (base > 300 * (m_dpi / 96.0)) base = 300 * (m_dpi / 96.0);
    if (base < 46) base = 46;
    return (int)(dir * base * (1.0 - e));
}

void Ui::notePress(int hoverId) {
    int s = slotForBtn(hoverId);
    if (s >= 0) m_aPress[s] = 1.0;
}

double Ui::pageFade() const {
    if (!m_pageAnim) return 1.0;
    return easeOutQuart(m_pageT);
}

void Ui::paintVeil(HDC dc, int l, int t, int r, int b, int alpha, int theme) {
    if (alpha <= 0 || r <= l || b <= t) return;
    if (alpha > 255) alpha = 255;

    BYTE cr = (theme == 1) ? 0x14 : 0xF2;
    BYTE cg = cr;
    BYTE cb = (theme == 1) ? 0x17 : 0xF7;

    Graphics g(dc);
    SolidBrush veil(Color((BYTE)alpha, cr, cg, cb));
    g.FillRectangle(&veil, (REAL)l, (REAL)t, (REAL)(r - l), (REAL)(b - t));
}

double Ui::hoverOf(int slot) const {
    if (slot < 0 || slot >= 64) return 0.0;
    return m_aHover[slot];
}

void Ui::setHoverTarget(int slot, bool on) {
    if (slot < 0 || slot >= 64) return;
    m_hoverTgt[slot] = on ? 1.0 : 0.0;
}

void Ui::prepare(int fontSize) {
    ensureEditorFont(fontSize);
}

void Ui::ensureEditorFont(int fontSize) {
    if (fontSize < 8) fontSize = 8;
    if (fontSize > 40) fontSize = 40;
    if (m_edFont && m_edFontSize == fontSize) return;

    if (m_edFont) DeleteObject(m_edFont);
    if (m_edFontBold) DeleteObject(m_edFontBold);

    double s = m_dpi / 96.0;
    int h = (int)(fontSize * s + 0.5);
    if (h < 8) h = 8;

    m_edFont = CreateFontW(-h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    m_edFontBold = CreateFontW(-h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    if (!m_edFont) m_edFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    if (!m_edFontBold) m_edFontBold = m_edFont;

    HDC dc = GetDC(NULL);
    if (dc) {
        HGDIOBJ old = SelectObject(dc, m_edFont);
        TEXTMETRICW tm;
        memset(&tm, 0, sizeof(tm));
        GetTextMetricsW(dc, &tm);

        int cw = 0;
        if (!GetCharWidth32W(dc, L'0', L'0', &cw) || cw <= 0) cw = tm.tmAveCharWidth;
        if (cw <= 0) cw = (h + 1) / 2;

        m_edCharW  = cw;
        m_edFontH  = tm.tmHeight;
        m_edLineH  = tm.tmHeight + (int)(3 * s + 0.5);

        SelectObject(dc, old);
        ReleaseDC(NULL, dc);
    }

    if (m_edLineH < 8) m_edLineH = h + 3;
    if (m_edCharW < 1) m_edCharW = 1;
    m_edFontSize = fontSize;
}

void Ui::contentRect(int w, int h, int& l, int& t, int& r, int& b,
                      int page) const {
    (void)page;
    l = m_sideW;
    t = m_titleH;
    r = w;
    b = h;
}

EdLayout Ui::edLayout(int w, int h) const {
    double s = m_dpi / 96.0;
    EdLayout L;

    L.l = m_sideW + m_treeW;
    L.t = m_titleH;
    L.r = w;
    L.b = h;
    L.toolH   = (int)(38 * s + 0.5);
    L.gutterW = (int)(54 * s + 0.5);
    L.charW   = m_edCharW;
    L.lineH   = m_edLineH;

    int bodyT = L.t + L.toolH;
    int bodyH = L.b - bodyT;
    if (bodyH < 1) bodyH = 1;
    L.viewLines = (L.lineH > 0) ? bodyH / L.lineH : 1;
    if (L.viewLines < 1) L.viewLines = 1;

    int textW = L.r - L.l - L.gutterW - (int)(16 * s);
    if (textW < 1) textW = 1;
    L.viewCols = (L.charW > 0) ? textW / L.charW : 1;
    if (L.viewCols < 1) L.viewCols = 1;

    return L;
}

int Ui::treeMaxScroll(int h, const UiModel& m) const {
    Geom ge;
    layout(1920, h, m, ge);
    int vis = (int)ge.treeRows.size();
    int total = (int)m.tree.size();
    if (total <= vis) return 0;
    return total - vis;
}

void Ui::treeScrollBy(int d, int h, const UiModel& m) {
    int maxS = treeMaxScroll(h, m);
    m_treeScroll += d;
    if (m_treeScroll < 0) m_treeScroll = 0;
    if (m_treeScroll > maxS) m_treeScroll = maxS;
}

void Ui::clearHover() {
    m_hoverNav = -1;
    m_hoverBtn = 0;
    m_hoverMenu = -1;
    m_hoverTree = -1;
    syncHoverTargets();
}

static void scrollbar(Graphics& g, int x, int top, int bottom, int vis, int total, int pos) {
    if (total <= vis || total <= 0) return;
    int trackH = bottom - top;
    int barH = trackH * vis / total;
    if (barH < 24) barH = 24;
    if (barH > trackH) barH = trackH;
    int maxPos = total - vis;
    int y = top + ((maxPos > 0) ? (trackH - barH) * pos / maxPos : 0);
    fillRound(g, x, y, x + 5, y + barH, 3, Color(90, 0x8E, 0x8E, 0x93));
}

// 设置页横向分栏。左侧固定 560（控件都挂在它右边缘，所以必须与 layout 里
// 用的是同一组数字），剩下的给右侧说明卡片；窄窗口时左右平分，再窄就放弃右栏。
int Ui::settingsColumns(const Geom& ge, int& listW, int& sideW) const {
    double s = m_dpi / 96.0;
    int padX = (int)(20 * s + 0.5);
    int gap  = (int)(22 * s + 0.5);
    int minL = (int)(220 * s + 0.5);

    int avail = ge.body.w() - 2 * padX;
    if (avail < minL) {
        listW = avail > 0 ? avail : 0;
        sideW = 0;
        return padX;
    }

    int want = (int)(560 * s + 0.5);
    listW = (want < avail) ? want : avail;
    sideW = avail - listW - gap;

    if (sideW < (int)(230 * s + 0.5)) {
        sideW = (avail - gap) / 2;
        if (sideW < 0) sideW = 0;
        listW = avail - gap - sideW;
        if (listW < minL) {
            listW = minL;
            sideW = avail - gap - listW;
        }
    }
    if (sideW < (int)(150 * s + 0.5)) sideW = 0;

    return padX;
}

void Ui::layout(int w, int h, const UiModel& m, Geom& ge) const {
    double s = m_dpi / 96.0;

    m_menuTgt = m.distroMenuOpen;
    if (m.page == PAGE_INSTALL && m.instPercent >= 0) m_barTgt = m.instPercent;

    ge.title = Box{ 0, 0, w, m_titleH };

    int bs = (int)(32 * s + 0.5);
    int ty = (m_titleH - bs) / 2;
    int gap = (int)(2 * s + 0.5);

    ge.btnClose = Box{ w - m_pad - bs, ty, w - m_pad, ty + bs };
    ge.btnMax   = Box{ ge.btnClose.l - bs - gap, ty, ge.btnClose.l - gap, ty + bs };
    ge.btnMin   = Box{ ge.btnMax.l - bs - gap, ty, ge.btnMax.l - gap, ty + bs };

    ge.side    = Box{ 0, m_titleH, m_sideW, h };
    ge.content = Box{ m_sideW, m_titleH, w, h };

    bool hasBar = (m.page == PAGE_TERMINAL);
    ge.bar  = Box{ 0, 0, 0, 0 };
    ge.body = Box{ m_sideW, m_titleH, w, h };

    int navTop = m_titleH + (int)(8 * s + 0.5);
    for (int i = 0; i < 4; ++i) {
        ge.nav[i] = Box{ 0, navTop + i * m_navH, m_sideW, navTop + (i + 1) * m_navH };
    }

    ge.distroBtn = Box{ 0, 0, 0, 0 };
    if (hasBar) {
        int bh = (int)(26 * s + 0.5);
        int by = (m_titleH - bh) / 2;
        int bw = (int)(176 * s + 0.5);
        int right = ge.btnMin.l - (int)(8 * s + 0.5);
        if (bw > right - m_sideW - m_pad * 2) bw = right - m_sideW - m_pad * 2;
        if (bw < (int)(96 * s + 0.5)) bw = (int)(96 * s + 0.5);
        ge.distroBtn = Box{ right - bw, by, right, by + bh };
    }

    int cw = ge.body.r - ge.body.l;
    int chh = ge.body.h();
    if (cw < 1) cw = 1;
    if (chh < 1) chh = 1;

    int btnW2 = (int)(104 * s + 0.5);
    int btnH2 = (int)(38 * s + 0.5);
    int cyBody = ge.body.t + chh / 2;
    int btnY = cyBody + (int)(56 * s + 0.5);
    ge.emptyBtn = Box{ ge.body.l + cw / 2 - btnW2, btnY,
                       ge.body.l + cw / 2 + btnW2, btnY + btnH2 };

    // 启用组件时的进度条：副文案在 layout 里占 cy+16..cy+40，进度条必须让开
    // 这段，否则文字和条压在一起（实测过，重叠 10px）。原按钮在 cy+56，
    // 进度条比按钮矮，正好填进按钮上沿那块空档。
    int barW = (int)(260 * s + 0.5);
    if (barW > cw - (int)(40 * s + 0.5)) barW = cw - (int)(40 * s + 0.5);
    if (barW < (int)(120 * s + 0.5)) barW = (int)(120 * s + 0.5);
    int barH = (int)(8 * s + 0.5);
    if (barH < 6) barH = 6;
    int barY2 = cyBody + (int)(48 * s + 0.5);
    ge.fixBar = Box{ ge.body.l + cw / 2 - barW / 2, barY2,
                     ge.body.l + cw / 2 + barW / 2, barY2 + barH };

    // 重启按钮放回原按钮的位（cy+56），只在有进度条时才有意义
    int rbW = (int)(118 * s + 0.5);
    int rbH = (int)(38 * s + 0.5);
    int rbY = barY2 + (int)(barH + 14 * s + 0.5);
    ge.fixReboot = Box{ ge.body.l + cw / 2 - rbW / 2, rbY,
                        ge.body.l + cw / 2 + rbW / 2, rbY + rbH };

    ge.treeRows.clear();
    ge.treeUp = Box{ 0, 0, 0, 0 };
    ge.edArea = Box{ 0, 0, 0, 0 };
    ge.edTool = Box{ 0, 0, 0, 0 };
    ge.edClose = ge.edTool;
    ge.edSave = ge.edTool;
    ge.edNew = ge.edTool;
    ge.ed = EdLayout();

    if (m.hasFolder && m.page == PAGE_PROJECT) {
        int toolH = (int)(36 * s + 0.5);
        int toolbarT = ge.body.t + (int)(8 * s + 0.5);
        int rowsTop = toolbarT + toolH + (int)(4 * s + 0.5);

        ge.treeUp = Box{ ge.body.l + m_treeW - (int)(68 * s + 0.5), toolbarT,
                         ge.body.l + m_treeW - (int)(8 * s + 0.5), toolbarT + toolH };

        int rowH = m_rowH;
        int avail = ge.body.b - rowsTop;
        int n = (avail > 0) ? avail / rowH : 0;
        if (n < 0) n = 0;

        int start = m_treeScroll;
        for (int i = 0; i < n; ++i) {
            int idx = start + i;
            if (idx >= (int)m.tree.size()) break;
            ge.treeRows.push_back(Box{ ge.body.l, rowsTop + i * rowH,
                                       ge.body.l + m_treeW, rowsTop + (i + 1) * rowH });
        }

        EdLayout L = edLayout(w, h);
        ge.ed = L;

        ge.edTool = Box{ L.l, L.t, L.r, L.t + L.toolH };

        int btnH = (int)(24 * s + 0.5);
        int btnY = L.t + (L.toolH - btnH) / 2;
        int btnW = (int)(56 * s + 0.5);

        ge.edClose = Box{ L.r - m_pad - btnW, btnY, L.r - m_pad, btnY + btnH };
        ge.edSave  = Box{ ge.edClose.l - 8 - btnW, btnY, ge.edClose.l - 8, btnY + btnH };
        ge.edNew   = Box{ ge.edSave.l - 8 - btnW, btnY, ge.edSave.l - 8, btnY + btnH };

        ge.edArea = Box{ L.l, L.t + L.toolH, L.r, L.b };
    }

    ge.dInstRows.clear();
    ge.dOnlineRows.clear();
    ge.dList = Box{ 0, 0, 0, 0 };
    ge.dHdrInst = Box{ 0, 0, 0, 0 };
    ge.dHdrOnline = Box{ 0, 0, 0, 0 };
    ge.dRefresh = Box{ 0, 0, 0, 0 };
    ge.dHint = Box{ 0, 0, 0, 0 };
    ge.dOnlineFirst = 0;
    ge.dOnlineShown = 0;
    ge.dTotal = 0;

    if (m.page == PAGE_DISTRO) {
        int dl = ge.body.l + (int)(20 * s + 0.5);
        int dr = ge.body.r - (int)(20 * s + 0.5);
        int hdrH = (int)(30 * s + 0.5);
        int rowH = (int)(44 * s + 0.5);
        int sepG = (int)(6 * s + 0.5);

        int top = ge.body.t + (int)(16 * s + 0.5);

        int rbw = (int)(72 * s + 0.5);
        int rbh = (int)(26 * s + 0.5);
        ge.dRefresh = Box{ dr - rbw, top, dr, top + rbh };

        int winBottom = ge.body.b - (int)(26 * s + 0.5);
        if (winBottom < top + (int)(60 * s + 0.5))
            winBottom = top + (int)(60 * s + 0.5);

        int y0 = top + rbh + (int)(8 * s + 0.5);

        int nInst = (int)m.installed.size();
        int nOnl  = (int)m.online.size();

        ge.dInstRows.clear();
        ge.dOnlineRows.clear();
        ge.dList = Box{ dl, y0, dr, winBottom };
        ge.dHint = Box{ dl, ge.body.b - (int)(22 * s + 0.5),
                        dr, ge.body.b - (int)(4 * s + 0.5) };
        ge.dTotal = nInst + nOnl;
        ge.dInstFirst = 0;
        ge.dInstShown = 0;
        ge.dOnlineFirst = 0;
        ge.dOnlineShown = 0;

        if (nInst == 0 && nOnl == 0) {
            ge.dHdrInst = Box{ 0, 0, 0, 0 };
            ge.dHdrOnline = Box{ 0, 0, 0, 0 };
            m_distroMaxScroll = 0;
            return;
        }

        ge.dHdrInst = Box{ dl, y0, dr, y0 + hdrH };

        int instWinTop = 0, instWinBot = 0, onlHdrTop = 0, onlWinTop = 0, onlWinBot = 0;

        int avail = winBottom - y0;

        int hardFloor = (int)(26 * s + 0.5);
        if (hardFloor < 24) hardFloor = 24;

        int instRowH = rowH;
        int instRowsH = 0;

        if (nInst > 0) {
            int hdrN = (nOnl > 0) ? 2 : 1;
            int hdrTotal = hdrN * hdrH + ((nOnl > 0) ? sepG : 0);

            int keepOnl = (nOnl > 0) ? (2 * rowH + sepG) : 0;
            int budget  = avail - hdrTotal - keepOnl;
            if (budget < 0) budget = 0;

            int need = nInst * rowH;
            instRowH = (need > budget) ? (budget / nInst) : rowH;
            if (instRowH < hardFloor) instRowH = hardFloor;
            instRowsH = nInst * instRowH;
        }

        if (nInst > 0) {
            instWinTop = y0 + hdrH;
            instWinBot = instWinTop + instRowsH;
            if (instWinBot > winBottom) instWinBot = winBottom;
            if (instWinBot < instWinTop) instWinBot = instWinTop;
        }
        if (nOnl > 0) {
            onlHdrTop = (nInst > 0) ? (instWinBot + sepG) : y0;
            onlWinTop = onlHdrTop + hdrH;
            onlWinBot = winBottom;
            if (onlWinBot < onlWinTop) onlWinBot = onlWinTop;
        }

        int onlFit = nOnl > 0 ? (onlWinBot - onlWinTop) / rowH : 0;
        if (onlFit < 0) onlFit = 0;

        int instFitRows = nInst > 0 ? (instWinBot - instWinTop) / instRowH : 0;
        if (instFitRows < 0) instFitRows = 0;
        int maxInstScroll = (nInst > instFitRows) ? (nInst - instFitRows) : 0;
        if (maxInstScroll < 0) maxInstScroll = 0;

        int maxScroll = maxInstScroll + ((nOnl > onlFit) ? (nOnl - onlFit) : 0);
        if (m_distroScroll > maxScroll) m_distroScroll = maxScroll;
        if (m_distroScroll < 0) m_distroScroll = 0;
        m_distroMaxScroll = maxScroll;

        {
            int sInst = (m_distroScroll < maxInstScroll) ? m_distroScroll : maxInstScroll;
            if (sInst < 0) sInst = 0;
            ge.dInstFirst = sInst;
            if (nInst > 0) {
                int y = instWinTop - sInst * instRowH;
                ge.dInstShown = 0;
                for (int i = 0; i < nInst; ++i) {
                    if (y + instRowH > instWinTop && y < instWinBot) {
                        ge.dInstRows.push_back(Box{ dl, y, dr, y + instRowH });
                        ++ge.dInstShown;
                    }
                    y += instRowH;
                }
            }
        }

        int sOnl = m_distroScroll - ge.dInstFirst;
        if (sOnl < 0) sOnl = 0;
        if (sOnl > maxScroll - maxInstScroll) sOnl = maxScroll - maxInstScroll;
        if (sOnl < 0) sOnl = 0;

        if (nOnl > 0) {
            ge.dHdrOnline = Box{ dl, onlHdrTop, dr, onlHdrTop + hdrH };
            int y = onlWinTop - sOnl * rowH;
            ge.dOnlineFirst = sOnl;
            ge.dOnlineShown = 0;
            for (int i = 0; i < nOnl; ++i) {
                if (y + rowH > onlWinTop && y < onlWinBot) {
                    ge.dOnlineRows.push_back(Box{ dl, y, dr, y + rowH });
                    ++ge.dOnlineShown;
                }
                y += rowH;
            }
        } else {
            ge.dHdrOnline = Box{ 0, 0, 0, 0 };
        }
    }

    // 40 rather than 46: the language row made it seven rows, and the shortcut
    // list below is already sitting on the bottom edge of a default-height
    // window. Seven at 40 lands within a few pixels of where six at 46 did.
    int setRowH = (int)(40 * s + 0.5);
    int setTop = ge.body.t + (int)(18 * s + 0.5);
    int setListTop = setTop + (int)(34 * s + 0.5);

    int setListW = 0, setSideW = 0;
    int setPadX = settingsColumns(ge, setListW, setSideW);
    int setLeft = ge.body.l + setPadX;
    int setRight = setLeft + setListW;
    int setGapX = (int)(22 * s + 0.5);

    ge.setList = Box{ setLeft, setListTop, setRight, setListTop + 7 * setRowH };
    ge.setSide = (setSideW > 0)
                     ? Box{ setRight + setGapX, setListTop,
                            setRight + setGapX + setSideW, ge.body.b }
                     : Box{ 0, 0, 0, 0 };

    int ctlW = (int)(30 * s + 0.5);
    int ctlH = (int)(26 * s + 0.5);

    auto mkCtl = [&](int row, int slot, Box& out) {
        int y0 = setListTop + row * setRowH + (setRowH - ctlH) / 2;
        int x1 = setRight - (int)(14 * s + 0.5) - slot * (ctlW + 6) - (slot > 0 ? (int)(44 * s) : 0);
        out = Box{ x1 - ctlW, y0, x1, y0 + ctlH };
    };

    ge.setFontDec = ge.setFontInc = ge.setTermFontDec = ge.setTermFontInc = ge.edTool;
    mkCtl(0, 1, ge.setFontDec);
    mkCtl(0, 0, ge.setFontInc);
    mkCtl(1, 1, ge.setTermFontDec);
    mkCtl(1, 0, ge.setTermFontInc);

    {
        int y0 = setListTop + 2 * setRowH + (setRowH - ctlH) / 2;
        int w2 = (int)(72 * s + 0.5);
        ge.setDark = Box{ setRight - (int)(14 * s + 0.5) - w2, y0,
                          setRight - (int)(14 * s + 0.5), y0 + ctlH };
        ge.setLight = Box{ ge.setDark.l - 8 - w2, y0, ge.setDark.l - 8, y0 + ctlH };
    }

    {
        int y0 = setListTop + 3 * setRowH + (setRowH - ctlH) / 2;
        int w3 = (int)(62 * s + 0.5);
        int gap3 = (int)(6 * s + 0.5);
        ge.setTermPipe = Box{ setRight - (int)(14 * s + 0.5) - w3, y0,
                              setRight - (int)(14 * s + 0.5), y0 + ctlH };
        ge.setTermPty = Box{ ge.setTermPipe.l - gap3 - w3, y0,
                             ge.setTermPipe.l - gap3, y0 + ctlH };
        ge.setTermAuto = Box{ ge.setTermPty.l - gap3 - w3, y0,
                              ge.setTermPty.l - gap3, y0 + ctlH };
    }

    {
        // Row 4 is the language selector. Three segments right-aligned like the
        // theme row, sized for the widest label ("跟随系统" and "English") so the
        // buttons do not change width when the language flips.
        //
        // 72 rather than 78: three segments reach further left than the theme
        // row's pair, and at 78 the label column ran out of room -- English
        // "Language" came out as "Langua...". 72 still clears 跟随系统 by ~26 px.
        int y0 = setListTop + 4 * setRowH + (setRowH - ctlH) / 2;
        int w5 = (int)(72 * s + 0.5);
        int gap5 = (int)(6 * s + 0.5);
        ge.setLangEn = Box{ setRight - (int)(14 * s + 0.5) - w5, y0,
                            setRight - (int)(14 * s + 0.5), y0 + ctlH };
        ge.setLangZh = Box{ ge.setLangEn.l - gap5 - w5, y0,
                            ge.setLangEn.l - gap5, y0 + ctlH };
        ge.setLangAuto = Box{ ge.setLangZh.l - gap5 - w5, y0,
                              ge.setLangZh.l - gap5, y0 + ctlH };
    }

    {
        int y0 = setListTop + 6 * setRowH + (setRowH - ctlH) / 2;
        // 68 rather than 96: the two buttons have to leave room for the WSL
        // version string in front of them. At 96 the version column came out
        // 85 px wide in Chinese and rendered as a bare "W..." -- which reads as
        // a bug rather than as a truncation. 68 still fits "修复 WSL"/"Re-check".
        int w4 = (int)(68 * s + 0.5);
        int gap4 = (int)(8 * s + 0.5);
        ge.setRecheck = Box{ setRight - (int)(14 * s + 0.5) - w4, y0,
                             setRight - (int)(14 * s + 0.5), y0 + ctlH };
        ge.setFix = Box{ ge.setRecheck.l - gap4 - w4, y0,
                         ge.setRecheck.l - gap4, y0 + ctlH };
    }

    ge.instBar = ge.instCancel = ge.instRetry = ge.instClose = ge.instLog = ge.edTool;
    if (m.page == PAGE_INSTALL) {
        int cardL = ge.body.l + (int)(24 * s + 0.5);
        int cardR = ge.body.r - (int)(24 * s + 0.5);
        if (cardR > cardL + (int)(640 * s)) cardR = cardL + (int)(640 * s);
        int cardW = cardR - cardL;

        int barH = (int)(12 * s + 0.5);
        int barT = ge.body.t + (int)(196 * s + 0.5);
        ge.instBar = Box{ cardL, barT, cardR, barT + barH };

        int logT = barT + (int)(84 * s + 0.5);
        int logB = ge.body.b - (int)(84 * s + 0.5);
        if (logB < logT + (int)(60 * s + 0.5)) logB = logT + (int)(60 * s + 0.5);
        ge.instLog = Box{ cardL, logT, cardR, logB };

        int bh = (int)(38 * s + 0.5);
        int bw = (int)(110 * s + 0.5);
        int by = ge.body.b - (int)(58 * s + 0.5);
        ge.instClose = Box{ cardR - bw, by, cardR, by + bh };
        ge.instRetry = Box{ ge.instClose.l - (int)(10 * s + 0.5) - bw, by,
                            ge.instClose.l - (int)(10 * s + 0.5), by + bh };
        ge.instCancel = ge.instRetry;
        (void)cardW;
    }

    ge.menuRows.clear();
    ge.menuKind.clear();
    ge.menuIndex.clear();
    ge.menuPanel = Box{ 0, 0, 0, 0 };
    ge.menuHdr1 = ge.menuHdr2 = ge.menuSep = ge.menuHint = Box{ 0, 0, 0, 0 };
    ge.menuTotal = (int)m.online.size();
    ge.menuFirst = 0;
    ge.menuShown = 0;
    m_menuMaxScroll = 0;

    if (m.distroMenuOpen && hasBar) {
        int rowH  = (int)(32 * s + 0.5);
        int hdrH  = (int)(26 * s + 0.5);
        int padY  = (int)(6 * s + 0.5);
        int sepH  = (int)(9 * s + 0.5);
        int hintH = (int)(22 * s + 0.5);

        bool hasInst = !m.installed.empty();
        int  nInst   = (int)m.installed.size();
        int  nOnl    = (int)m.online.size();

        int px = ge.distroBtn.l;
        int pw = m_menuW;
        if (px + pw > w - (int)(8 * s + 0.5)) px = w - (int)(8 * s + 0.5) - pw;
        if (px < m_sideW + (int)(4 * s + 0.5)) px = m_sideW + (int)(4 * s + 0.5);

        int py = m_titleH + (int)(4 * s + 0.5);

        int maxH = h - (int)(8 * s + 0.5) - py;
        int minH = (int)(74 * s + 0.5);
        if (maxH < minH) maxH = minH;

        int fixed = padY * 2
                  + (hasInst ? (hdrH + nInst * rowH) : 0)
                  + ((hasInst && nOnl > 0) ? sepH : 0)
                  + (nOnl > 0 ? hdrH : 0);

        int maxRows = (maxH - fixed) / rowH;
        if (maxRows < 1) maxRows = 1;

        bool scroll = (nOnl > maxRows);
        if (scroll) fixed += hintH;

        int visible = (nOnl < maxRows) ? nOnl : maxRows;
        if (visible < 0) visible = 0;

        m_menuMaxScroll = (nOnl > visible) ? (nOnl - visible) : 0;
        if (m_menuScroll > m_menuMaxScroll) m_menuScroll = m_menuMaxScroll;
        if (m_menuScroll < 0) m_menuScroll = 0;

        int totalH = fixed + visible * rowH;
        if (totalH > maxH) totalH = maxH;

        ge.menuPanel = Box{ px, py, px + pw, py + totalH };

        int yy = py + padY;
        if (hasInst) {
            ge.menuHdr1 = Box{ px, yy, px + pw, yy + hdrH };
            yy += hdrH;
            for (int i = 0; i < nInst; ++i) {
                ge.menuRows.push_back(Box{ px, yy, px + pw, yy + rowH });
                ge.menuKind.push_back(0);
                ge.menuIndex.push_back(i);
                yy += rowH;
            }
        }
        if (hasInst && nOnl > 0) {
            ge.menuSep = Box{ px + (int)(10 * s + 0.5), yy + (int)(4 * s + 0.5),
                              px + pw - (int)(10 * s + 0.5), yy + (int)(5 * s + 0.5) };
            yy += sepH;
        }
        if (nOnl > 0) {
            ge.menuHdr2 = Box{ px, yy, px + pw, yy + hdrH };
            yy += hdrH;
            for (int i = 0; i < visible; ++i) {
                ge.menuRows.push_back(Box{ px, yy, px + pw, yy + rowH });
                ge.menuKind.push_back(1);
                ge.menuIndex.push_back(m_menuScroll + i);
                yy += rowH;
            }
        }
        if (scroll) {
            ge.menuHint = Box{ px, yy, px + pw, yy + hintH };
        }

        ge.menuFirst = m_menuScroll;
        ge.menuShown = visible;
    }
}

bool Ui::menuScrollBy(int delta, int w, int h, const UiModel& m) {
    Geom ge;
    layout(w, h, m, ge);

    if (m_menuMaxScroll <= 0) return false;

    int before = m_menuScroll;
    m_menuScroll += delta;
    if (m_menuScroll < 0) m_menuScroll = 0;
    if (m_menuScroll > m_menuMaxScroll) m_menuScroll = m_menuMaxScroll;

    return m_menuScroll != before;
}

int Ui::menuShownCount(int w, int h, const UiModel& m) {
    Geom ge;
    layout(w, h, m, ge);
    return ge.menuShown;
}

bool Ui::distroScrollBy(int delta, int w, int h, const UiModel& m) {
    Geom ge;
    layout(w, h, m, ge);

    int before = m_distroScroll;
    if (m_distroMaxScroll <= 0) return false;

    m_distroScroll += delta;
    if (m_distroScroll < 0) m_distroScroll = 0;
    if (m_distroScroll > m_distroMaxScroll) m_distroScroll = m_distroMaxScroll;

    return m_distroScroll != before;
}

int Ui::distroMaxScroll(int w, int h, const UiModel& m) const {
    Geom ge;
    layout(w, h, m, ge);
    return m_distroMaxScroll;
}

void Ui::paintTitleBar(HDC dc, int w, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    {
        Graphics g(dc);
        SolidBrush br(kBar);
        g.FillRectangle(&br, (REAL)0, (REAL)0, (REAL)w, (REAL)m_titleH);

        Pen sep(kSep, 1.0f);
        g.DrawLine(&sep, 0.0f, (REAL)m_titleH - 0.5f, (REAL)w, (REAL)m_titleH - 0.5f);

        struct BtnRef { const Box* b; int id; };
        BtnRef btns[3] = { { &ge.btnMin, 1 }, { &ge.btnMax, 2 }, { &ge.btnClose, 3 } };

        for (int i = 0; i < 3; ++i) {
            const Box* b = btns[i].b;
            double hv = hoverOf(slotForBtn(btns[i].id));
            REAL cx = (b->l + b->r) / 2.0f;
            REAL cy = (b->t + b->b) / 2.0f;

            if (btns[i].id == 3) {
                if (hv > 0.01) {
                    fillRound(g, b->l, b->t, b->r, b->b, (int)(7 * s),
                              mixColor(kBar, kDanger, hv));
                }
                iconClose(g, cx, cy, s, mixColor(kText2, kWhite, hv), 1.6f);
            } else {
                if (hv > 0.01) {
                    fillRound(g, b->l, b->t, b->r, b->b, (int)(7 * s),
                              mixColor(kBar, kHoverBtn, hv));
                }
                if (btns[i].id == 1) iconMin(g, cx, cy, s, kText2, 1.6f);
                else iconMax(g, cx, cy, s, kText2, 1.6f);
            }
        }
    }

    // 左上角常驻品牌：圆点 + EWSL（EasyWSL）。页面标题照旧居中。
    int dotS = (int)(8 * s + 0.5);
    {
        Graphics g(dc);
        int dy = m_titleH / 2 - dotS / 2;
        fillRound(g, m_pad, dy, m_pad + dotS, dy + dotS, (int)(3 * s), kAccent);
    }
    int brandX = m_pad + dotS + (int)(7 * s + 0.5);
    int brandW = (int)(46 * s + 0.5);
    textAt(dc, L"EWSL", g_hBold, kText, brandX, 0, brandW, m_titleH, 0);

    if (!m.hint.empty()) {
        int hx = brandX + brandW + (int)(6 * s + 0.5);
        int avail = w / 2 - hx - (int)(120 * s);
        if (avail > (int)(30 * s + 0.5)) {
            textAt(dc, m.hint, g_hSmall, kText2, hx, 0, avail, m_titleH, 0);
        }
    }

    textAt(dc, m.title, g_hBold, kText, 0, 0, w, m_titleH, 1);
}

void Ui::paintSidebar(HDC dc, int h, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    // Not static: the labels have to be re-resolved when the language changes.
    const wchar_t* kNavTitle[4] = { LS(L"终端"), LS(L"项目"), LS(L"发行版"), LS(L"设置") };

    {
        Graphics g(dc);
        SolidBrush br(kSideBg);
        g.FillRectangle(&br, (REAL)ge.side.l, (REAL)ge.side.t,
                        (REAL)ge.side.w(), (REAL)ge.side.h());

        Pen sep(kSep, 1.0f);
        g.DrawLine(&sep, (REAL)ge.side.r - 0.5f, (REAL)ge.side.t,
                   (REAL)ge.side.r - 0.5f, (REAL)ge.side.b);

        for (int i = 0; i < 4; ++i) {
            const Box& b = ge.nav[i];
            if (b.t >= h) break;

            int slot = slotForNav(i);
            double hv = hoverOf(slot);
            double act = 1.0 - ::fabs(m_aNavPill - (double)i);
            if (act < 0.0) act = 0.0;
            if (act > 1.0) act = 1.0;

            if (act < 0.02 && hv > 0.01) {
                fillRound(g, 8, b.t + 3, ge.side.r - 8, b.b - 3, (int)(9 * s),
                          mixColor(kSideBg, kHoverSel, hv));
            }
        }

        {
            double a = m_aNavPill;
            int ai = (int)::floor(a + 0.5);
            if (ai < 0) ai = 0;
            if (ai > 3) ai = 3;

            int y0 = ge.nav[ai].t + 3;
            int y1 = ge.nav[ai].b - 3;

            double frac = a - (double)ai;
            if (frac > 0.002 || frac < -0.002) {
                int ni = ai + ((frac > 0) ? 1 : -1);
                if (ni >= 0 && ni <= 3) {
                    double f = (frac > 0) ? frac : -frac;
                    int ny0 = ge.nav[ni].t + 3;
                    int ny1 = ge.nav[ni].b - 3;
                    y0 = (int)(y0 + (ny0 - y0) * f);
                    y1 = (int)(y1 + (ny1 - y1) * f);
                }
            }

            fillRound(g, 8, y0, ge.side.r - 8, y1, (int)(9 * s), kAccentSoft);
            fillRound(g, 8, y0, 8 + (int)(3 * s + 0.5), y1, 2, kAccent);
        }

        for (int i = 0; i < 4; ++i) {
            const Box& b = ge.nav[i];
            if (b.t >= h) break;

            double act = 1.0 - ::fabs(m_aNavPill - (double)i);
            if (act < 0.0) act = 0.0;
            if (act > 1.0) act = 1.0;

            double hv = hoverOf(slotForNav(i));
            Color tc = mixColor(mixColor(kText, kAccent, act), kAccent, hv * 0.55);
            REAL cy = (REAL)(b.t + b.h() / 2);
            REAL cx = (REAL)(m_pad + 11 + 1.7 * s * ((act > hv) ? act : hv));

            if (i == 0)      iconTerminal(g, cx, cy, s, tc, 1.6f);
            else if (i == 1) iconFolder(g, cx, cy, s, tc, 1.6f);
            else if (i == 2) iconLayers(g, cx, cy, s, tc, 1.6f);
            else             iconGear(g, cx, cy, s, tc, 1.6f);

            if (i == 0 && m.instLocked) {
                REAL dx = (REAL)(ge.side.r - m_pad - 5);
                fillRound(g, (int)dx - (int)(4 * s), (int)cy - (int)(4 * s),
                          (int)dx + (int)(4 * s), (int)cy + (int)(4 * s),
                          (int)(4 * s), kAccent);
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        const Box& b = ge.nav[i];
        if (b.t >= h) break;

        double act = 1.0 - ::fabs(m_aNavPill - (double)i);
        if (act < 0.0) act = 0.0;
        if (act > 1.0) act = 1.0;

        double hv = hoverOf(slotForNav(i));
        double tw = (act > hv) ? act : hv;
        Color tc = mixColor(mixColor(kText, kAccent, act), kAccent, hv * 0.55);
        int txo = (int)(1.7 * s * tw + 0.5);

        textAt(dc, kNavTitle[i], (act > 0.5) ? g_hBold : g_hBody, tc,
               m_pad + 28 + txo, b.t, ge.side.r - m_pad - 40 - txo, b.h(), 0);
    }
}

void Ui::paintTermBar(HDC dc, int w, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;
    if (ge.distroBtn.w() <= 0) return;

    double hvBtn = hoverOf(slotForBtn(9));

    {
        Graphics g(dc);

        if (ge.distroBtn.w() > 0) {
            double hi = (m.distroMenuOpen ? 1.0 : 0.0);
            if (hvBtn > hi) hi = hvBtn;
            double press = (slotForBtn(9) >= 0) ? m_aPress[slotForBtn(9)] : 0.0;
            fillRound(g, ge.distroBtn.l, ge.distroBtn.t, ge.distroBtn.r, ge.distroBtn.b,
                      (int)(8 * s), mixColor(kHoverSel, kHoverBtn, hi * (1.0 - press * 0.5)));
            if (m.distroMenuOpen) {
                strokeRound(g, ge.distroBtn.l, ge.distroBtn.t, ge.distroBtn.r, ge.distroBtn.b,
                            (int)(8 * s), kAccent, 1.4f);
            }

            REAL cy = (REAL)(ge.distroBtn.t + ge.distroBtn.h() / 2);
            iconTerminal(g, (REAL)(ge.distroBtn.l + (int)(16 * s + 0.5)), cy, s * 0.72,
                         m.hasTerminal ? kAccent : kText2, 1.4f);
            iconChevron(g, (REAL)(ge.distroBtn.r - (int)(14 * s + 0.5)), cy, s, kText2, 1.5f, true);
        }
    }

    std::wstring label;
    if (!m.activeDistro.empty()) label = m.activeDistro;
    else if (!m.installed.empty()) label = m.installed[0];
    else label = m.wslMissing ? LS(L"未检测到 WSL") : LS(L"未安装发行版");

    if (ge.distroBtn.w() > 0) {
        int tx = ge.distroBtn.l + (int)(32 * s + 0.5);
        int tw = ge.distroBtn.w() - (int)(32 * s + 0.5) - (int)(26 * s + 0.5);
        double hb = hoverOf(slotForBtn(9));
        Color lc = mixColor(m.hasTerminal ? kText : kText2, kAccent, hb * 0.5);
        int tdx = (int)(1.4 * s * hb + 0.5);
        textAt(dc, label, g_hBody, lc,
               tx + tdx, ge.distroBtn.t, tw - tdx, ge.distroBtn.h(), 0);
    }

    std::wstring right;
    if (m.installing) right = LS(L"正在安装…");
    else if (m.loadingOnline) right = LS(L"正在获取列表…");
    if (!right.empty()) {
        int rx = ge.distroBtn.l - (int)(12 * s + 0.5);
        int rw = rx - (w / 2 + m_pad);
        if (rw > 0)
            textAt(dc, right, g_hSmall, kAccent, rx - rw, m_titleH * 0.5 - 14,
                   rw, 28, 1);
    }
}

static bool modelIsBroken(const UiModel& m, const std::wstring& id) {
    for (size_t i = 0; i < m.brokenDistros.size(); ++i) {
        if (lstrcmpiW(m.brokenDistros[i].c_str(), id.c_str()) == 0) return true;
    }
    return false;
}

void Ui::paintDistroMenu(HDC dc, int w, int h, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;
    if (ge.menuPanel.w() <= 0) return;

    if (ge.menuPanel.r > w) return;

    double reveal = easeOutCubic(m_aMenu);
    if (reveal < 0.02) reveal = 0.02;
    int revealH = (int)(ge.menuPanel.h() * reveal + 0.5);

    int svClip = SaveDC(dc);
    IntersectClipRect(dc, ge.menuPanel.l - 2, ge.menuPanel.t - 6,
                      ge.menuPanel.r + 6, ge.menuPanel.t + revealH + 5);

    {
        Graphics g(dc);

        int shA = (int)(38 * reveal + 0.5);
        fillRound(g, ge.menuPanel.l + (int)(2 * s), ge.menuPanel.t + (int)(4 * s),
                  ge.menuPanel.r + (int)(2 * s), ge.menuPanel.b + (int)(4 * s),
                  m_radius + 2, Color(shA, 0x00, 0x00, 0x00));
        fillRound(g, ge.menuPanel.l, ge.menuPanel.t, ge.menuPanel.r, ge.menuPanel.b,
                  m_radius, kCard);
        strokeRound(g, ge.menuPanel.l, ge.menuPanel.t, ge.menuPanel.r, ge.menuPanel.b,
                    m_radius, kPanelLine, 1.0f);

        if (ge.menuSep.w() > 0) {
            Pen sep(kSep, 1.0f);
            g.DrawLine(&sep, (REAL)ge.menuSep.l, (REAL)ge.menuSep.t,
                       (REAL)ge.menuSep.r, (REAL)ge.menuSep.t);
        }

        for (size_t i = 0; i < ge.menuRows.size(); ++i) {
            double hv = hoverOf(slotForMenu((int)i));
            if (hv < 0.01) continue;
            const Box& b = ge.menuRows[i];
            fillRound(g, b.l + (int)(6 * s), b.t, b.r - (int)(6 * s), b.b,
                      (int)(7 * s), mixColor(kCard, kHoverSel, hv));
        }

        for (size_t i = 0; i < ge.menuRows.size(); ++i) {
            if (ge.menuKind[i] != 0) continue;
            int idx = ge.menuIndex[i];
            if (idx < 0 || idx >= (int)m.installed.size()) continue;
            if (lstrcmpiW(m.installed[idx].c_str(), m.activeDistro.c_str()) != 0) continue;

            const Box& b = ge.menuRows[i];
            iconCheck(g, (REAL)(b.r - (int)(20 * s + 0.5)), (REAL)(b.t + b.h() / 2),
                      s, kAccent, 1.8f);
        }
    }

    if (ge.menuHdr1.w() > 0) {
        textAt(dc, LS(L"已安装"), g_hSmall, kText2,
               ge.menuHdr1.l + (int)(14 * s + 0.5), ge.menuHdr1.t,
               ge.menuHdr1.w() - (int)(20 * s), ge.menuHdr1.h(), 0);
    }
    if (ge.menuHdr2.w() > 0) {
        textAt(dc, LS(L"可安装"), g_hSmall, kText2,
               ge.menuHdr2.l + (int)(14 * s + 0.5), ge.menuHdr2.t,
               ge.menuHdr2.w() - (int)(20 * s), ge.menuHdr2.h(), 0);
    }

    for (size_t i = 0; i < ge.menuRows.size(); ++i) {
        const Box& b = ge.menuRows[i];
        int kind = ge.menuKind[i];
        int idx  = ge.menuIndex[i];
        if (idx < 0) continue;

        std::wstring name;
        std::wstring hint;
        Color hintColor = kText3;
        if (kind == 0) {
            if (idx >= (int)m.installed.size()) continue;
            name = m.installed[idx];
            hint = LS(L"启动");
        } else {
            if (idx >= (int)m.online.size()) continue;
            name = m.online[idx].label;
            if (name.empty()) name = m.online[idx].id;
            if (modelIsBroken(m, m.online[idx].id)) {
                hint = LS(L"重新安装");
                hintColor = kDanger;
            } else {
                hint = LS(L"下载安装");
                hintColor = kAccent;
            }
        }

        int hw = (int)(72 * s + 0.5);
        int hr = b.r - (int)((kind == 0 ? 34 : 14) * s + 0.5);
        int tx = b.l + (int)(14 * s + 0.5);
        int tw = hr - hw - tx;
        if (tw < (int)(40 * s + 0.5)) tw = (int)(40 * s + 0.5);
        textAt(dc, name, g_hBody, kText, tx, b.t, tw, b.h(), 0);

        textAt(dc, hint, g_hSmall, hintColor, hr - hw, b.t, hw, b.h(), 2);
    }

    if (ge.menuHint.w() > 0) {
        int lo = ge.menuFirst + 1;
        int hi = ge.menuFirst + ge.menuShown;
        wchar_t buf[96];
        wsprintfW(buf, LS(L"%d–%d / %d  滚轮查看更多"), lo, hi, ge.menuTotal);
        textAt(dc, buf, g_hSmall, kText2,
               ge.menuHint.l, ge.menuHint.t, ge.menuHint.w(), ge.menuHint.h(), 1);
    }

    if (ge.menuRows.empty()) {
        std::wstring msg = m.loadingOnline ? LS(L"正在获取发行版列表…")
                                           : LS(L"暂无可用发行版，请检查网络");
        textAt(dc, msg, g_hSmall, kText2,
               ge.menuPanel.l + (int)(14 * s + 0.5), ge.menuPanel.t,
               ge.menuPanel.w() - (int)(28 * s + 0.5), ge.menuPanel.h(), 1);
    }

    RestoreDC(dc, svClip);
}

void Ui::paintToast(HDC dc, int w, int h, const UiModel& m, const Geom& ge) {
    if (m.toast.empty()) return;

    double s = m_dpi / 96.0;

    unsigned long now = GetTickCount();
    unsigned long held = (now >= m.toastTick) ? (now - m.toastTick) : 0;
    const unsigned long kLife = 2600;
    if (held >= kLife) return;

    double a = 1.0;
    if (held < 180) a = held / 180.0;
    else if (held > kLife - 320) a = (double)(kLife - held) / 320.0;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    int ch = (int)(26 * s + 0.5);
    int cy = ge.body.b - m_pad - ch - (int)(14 * s + 0.5);
    int cx = ge.body.l + ge.body.w() / 2;

    HDC dcTmp = dc;
    SIZE sz;
    sz.cx = 0;
    sz.cy = 0;
    HGDIOBJ oldF = SelectObject(dcTmp, g_hBody);
    GetTextExtentPoint32W(dcTmp, m.toast.c_str(), (int)m.toast.size(), &sz);
    SelectObject(dcTmp, oldF);

    int tw = sz.cx + (int)(40 * s + 0.5);
    int maxW = ge.body.w() - (int)(48 * s + 0.5);
    if (tw > maxW) tw = maxW;
    if (tw < (int)(90 * s + 0.5)) tw = (int)(90 * s + 0.5);

    int l = cx - tw / 2;
    int t = cy;
    int r = l + tw;
    int b = t + ch;

    {
        Graphics g(dc);
        int al = (int)(a * 235 + 0.5);
        int sh = (int)(a * 40 + 0.5);
        fillRound(g, l + (int)(2 * s), b - (int)(2 * s), r + (int)(2 * s), b + (int)(5 * s),
                  ch / 2, Color(sh, 0x00, 0x00, 0x00));
        Color bgToast = (m.theme == 1) ? Color(al, 0x3A, 0x3A, 0x42)
                                       : Color(al, 0x24, 0x24, 0x2A);
        fillRound(g, l, t, r, b, ch / 2, bgToast);
    }

    int ta = (int)(a * 255 + 0.5);
    textAt(dc, m.toast, g_hBody, Color(ta, 0xFF, 0xFF, 0xFF), l, t, tw, ch, 1);
}

void Ui::paintEmptyState(HDC dc, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    const Box& B = ge.body;
    int cx = B.l + B.w() / 2;
    int cy = B.t + B.h() / 2;

    if (m.checking) {
        {
            Graphics g(dc);
            iconGear(g, (REAL)cx, (REAL)(cy - (int)(78 * s)), s * 1.6, kAccent, 2.2f);
        }
        textAt(dc, LS(L"正在检测 WSL 环境…"), g_hBody, kText2,
               B.l, cy - (int)(12 * s), B.w(), (int)(26 * s + 0.5), 1);
        return;
    }

    {
        Graphics g(dc);
        iconFolder(g, (REAL)cx, (REAL)(cy - (int)(78 * s)), s * 2.2, kAccent, 2.2f);
    }

    int titleY = cy - (int)(22 * s + 0.5);
    int titleH = (int)(32 * s + 0.5);
    int subY   = cy + (int)(16 * s + 0.5);
    int subH   = (int)(24 * s + 0.5);

    // 启用组件的流程有话要说，就把按钮换成进度条。toast 只活2.6 秒，
    // 而 wsl --install 要跑 UAC、启用系统组件，常常还要几分钟甚至要重启，
    // 靠 toast 等于什么都没告诉用户。
    if (m.fixStage == 1 || m.fixStage == 2 || m.fixStage == 3) {
        bool uac = (m.fixStage == 1);
        bool reboot = (m.fixStage == 3);

        std::wstring title = uac ? LS(L"等待管理员授权")
                        : reboot ? LS(L"需要重启系统")
                                 : LS(L"正在启用 WSL 组件…");
        textAt(dc, title, g_hBig, kText, B.l, titleY, B.w(), titleH, 1);

        // 秒表：让用户知道程序还在跑，而不是卡死了。
        // LS() 是查表翻译，带格式化结果的字符串查不到会原样返回中文，
        // 所以数字单独拼，单位走翻译。
        WCHAR secs[32];
        wsprintfW(secs, L"%d", m.fixElapsed);
        std::wstring clock = LS(L"已用") + std::wstring(secs) + LS(L"秒");

        // 两段文案之间留个间隔符。用全角空格而不是普通空格：中文正文里
        // 普通空格紧贴汉字会显得挤，而且它没必要进翻译表。
        std::wstring sub = m.fixMsg.empty()
            ? (uac ? LS(L"请在弹出的 UAC 窗口点「是」") : clock)
            : (m.fixMsg + LS(L"　") + clock);
        textAt(dc, sub, g_hSmall, kText2, B.l, subY, B.w(), subH, 1);

        int barY = ge.fixBar.t;
        int barH = ge.fixBar.h();

        {
            Graphics g(dc);
            fillRound(g, ge.fixBar.l, barY, ge.fixBar.r, barY + barH,
                      barH / 2, kTrack);
        }

        if (uac) {
            // 不知道进度，等着就行：来回扫的高亮块表达「在等用户操作」
            int inner = ge.fixBar.w() - barH;
            if (inner > barH) {
                double ph = (double)(GetTickCount() % 1500) / 1500.0;
                int sw = barH;
                int travel = inner + sw;
                int sx = ge.fixBar.l + barH / 2 - sw / 2 + (int)(ph * travel);
                int l = sx;
                int r = sx + sw;
                if (l < ge.fixBar.l) l = ge.fixBar.l;
                if (r > ge.fixBar.r - barH) r = ge.fixBar.r - barH;
                if (r > l) {
                    Graphics g(dc);
                    SolidBrush sb(mixColor(kTrack, kAccent, 0.75));
                    g.FillRectangle(&sb, (REAL)l, (REAL)barY,
                                    (REAL)(r - l), (REAL)barH);
                }
            }
        } else if (reboot) {
            int fillW = (int)(ge.fixBar.w() * 0.999 + 0.5);
            Graphics g(dc);
            fillRound(g, ge.fixBar.l, barY, ge.fixBar.l + fillW, barY + barH,
                      barH / 2, kOk);
        } else {
            // 执行中：wsl --install 不上报百分比，只能做不确定进度。
            // 按耗时往前推，逼近而不越过 95%，剩下的留给收尾阶段。
            double t = (double)m.fixElapsed;
            double pct = 8.0 + 87.0 * (1.0 - exp(-t / 75.0));
            if (pct > 95.0) pct = 95.0;
            int fillW = (int)((double)ge.fixBar.w() * pct / 100.0 + 0.5);
            if (fillW > 0) {
                Graphics g(dc);
                fillRound(g, ge.fixBar.l, barY, ge.fixBar.l + fillW, barY + barH,
                          barH / 2, kAccent);

                // 扫光，让「还在动」这件事看得见
                if (fillW > (int)(26 * s)) {
                    double ph = (double)(GetTickCount() % 1400) / 1400.0;
                    int sw = (int)(54 * s + 0.5);
                    int sx = ge.fixBar.l + (int)(ph * (double)(fillW + sw)) - sw;
                    int l = sx;
                    int r = sx + sw;
                    if (l < ge.fixBar.l) l = ge.fixBar.l;
                    if (r > ge.fixBar.l + fillW) r = ge.fixBar.l + fillW;
                    if (r > l) {
                        int sv2 = SaveDC(dc);
                        IntersectClipRect(dc, ge.fixBar.l, barY,
                                          ge.fixBar.l + fillW, barY + barH);
                        SolidBrush sb(Color(46, 0xFF, 0xFF, 0xFF));
                        g.FillRectangle(&sb, (REAL)l, (REAL)barY,
                                        (REAL)(r - l), (REAL)barH);
                        RestoreDC(dc, sv2);
                    }
                }
            }

            // 百分比摆在条的右边、与条垂直居中。放在条上方会压到副文案
            // （副文案下沿只比条上沿高 8px，字号一压就糊在一起）。
            WCHAR pctTxt[32];
            wsprintfW(pctTxt, L"%d%%", (int)(pct + 0.5));
            textAt(dc, pctTxt, g_hSmall, kText3,
                   ge.fixBar.r + (int)(8 * s), barY - (int)(3 * s),
                   (int)(44 * s), barH + (int)(6 * s), 2);
        }

        // 组件装完必须重启才生效，这是最容易被忽略的一步，给个按钮
        if (reboot) {
            bool hv = (m_hoverBtn == 4);
            {
                Graphics g(dc);
                fillRound(g, ge.fixReboot.l, ge.fixReboot.t,
                          ge.fixReboot.r, ge.fixReboot.b,
                          ge.fixReboot.h() / 2, hv ? kAccentDark : kAccent);
            }
            textAt(dc, LS(L"立即重启"), g_hBody, kWhite,
                   ge.fixReboot.l, ge.fixReboot.t,
                   ge.fixReboot.w(), ge.fixReboot.h(), 1);
        }
        return;
    }

    std::wstring title = m.wslMissing ? LS(L"未启用 WSL 组件") : LS(L"尚未安装 Linux 发行版");
    textAt(dc, title, g_hBig, kText, B.l, titleY, B.w(), titleH, 1);

    std::wstring sub;
    if (m.wslMissing) {
        sub = (m.wslState == WSL_NO_VMP)
            ? LS(L"已装 WSL，但虚拟机平台未启用（WSL2 需要它）。点下方按钮一键开启。")
            : LS(L"系统里只有 inbox 版 wsl.exe，组件没启用。点下方按钮一键开启。");
    } else {
        sub = LS(L"点击下方按钮，或使用上方发行版菜单选择下载");
    }

    // 上一次失败了就把原因留在这儿，别让用户干瞪眼
    if (m.fixStage == 4 && !m.fixMsg.empty()) {
        sub = m.fixMsg;
    }
    textAt(dc, sub, g_hSmall, m.fixStage == 4 ? kDanger : kText2,
           B.l, subY, B.w(), subH, 1);

    bool hv = (m_hoverBtn == 4);
    {
        Graphics g(dc);
        fillRound(g, ge.emptyBtn.l, ge.emptyBtn.t, ge.emptyBtn.r, ge.emptyBtn.b,
                  ge.emptyBtn.h() / 2, hv ? kAccentDark : kAccent);
    }
    textAt(dc, m.wslMissing ? LS(L"启用 WSL 组件") : LS(L"选择发行版安装"), g_hBody, kWhite,
           ge.emptyBtn.l, ge.emptyBtn.t, ge.emptyBtn.w(), ge.emptyBtn.h(), 1);
}

void Ui::paintProjectEmpty(HDC dc, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    const Box& B = ge.body;
    int cx = B.l + B.w() / 2;
    int cy = B.t + B.h() / 2;

    {
        Graphics g(dc);
        iconFolder(g, (REAL)cx, (REAL)(cy - (int)(78 * s)), s * 2.2, kAccent, 2.2f);
    }

    textAt(dc, LS(L"打开一个文件夹开始"), g_hBig, kText,
           B.l, cy - (int)(22 * s + 0.5), B.w(), (int)(32 * s + 0.5), 1);
    textAt(dc, LS(L"浏览目录结构，双击文件即可打开编辑（支持 cpp / mm / m / h 等高亮）"),
           g_hSmall, kText2, B.l, cy + (int)(16 * s + 0.5), B.w(), (int)(24 * s + 0.5), 1);

    bool hv = (m_hoverBtn == 4);
    {
        Graphics g(dc);
        fillRound(g, ge.emptyBtn.l, ge.emptyBtn.t, ge.emptyBtn.r, ge.emptyBtn.b,
                  ge.emptyBtn.h() / 2, hv ? kAccentDark : kAccent);
    }
    textAt(dc, LS(L"打开文件夹"), g_hBody, kWhite,
           ge.emptyBtn.l, ge.emptyBtn.t, ge.emptyBtn.w(), ge.emptyBtn.h(), 1);
}

void Ui::paintTree(HDC dc, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    int tl = ge.body.l;
    int tr = ge.body.l + m_treeW;

    {
        Graphics g(dc);
        SolidBrush br(kBar);
        g.FillRectangle(&br, (REAL)tl, (REAL)ge.body.t, (REAL)m_treeW, (REAL)ge.body.h());

        Pen sep(kSep, 1.0f);
        g.DrawLine(&sep, (REAL)tr - 0.5f, (REAL)ge.body.t, (REAL)tr - 0.5f, (REAL)ge.body.b);

        double hvUp = hoverOf(slotForBtn(8));
        if (hvUp > 0.01) fillRound(g, ge.treeUp.l, ge.treeUp.t, ge.treeUp.r, ge.treeUp.b,
                                   (int)(7 * s), mixColor(kBar, kHoverSel, hvUp));

        for (size_t i = 0; i < ge.treeRows.size(); ++i) {
            const Box& b = ge.treeRows[i];
            int idx = m_treeScroll + (int)i;
            if (idx >= (int)m.tree.size()) break;

            const TreeRow& r = m.tree[(size_t)idx];
            double hover = hoverOf(slotForTree((int)i));
            bool sel = r.isOpenFile;

            if (sel)        fillRound(g, tl + 6, b.t + 1, tr - 8, b.b - 1, (int)(7 * s), kAccentSoft);
            else if (hover > 0.01)
                            fillRound(g, tl + 6, b.t + 1, tr - 8, b.b - 1, (int)(7 * s),
                                      mixColor(kBar, kHoverSel, hover));

            REAL cy = (REAL)(b.t + b.h() / 2);
            REAL ix = (REAL)(tl + m_pad + r.depth * (int)(14 * s + 0.5));

            if (r.isDir) {
                iconChevron(g, ix, cy, s, kText2, 1.4f, r.expanded);
                iconFolder(g, (REAL)(ix + 16), cy, s * 0.85, kAccent, 1.4f);
            } else {
                iconFile(g, (REAL)(ix + 16), cy, s * 0.85, kText2, 1.3f);
            }
        }

        int vis = (int)ge.treeRows.size();
        if (!ge.treeRows.empty()) {
            scrollbar(g, tr - 10, ge.treeRows.front().t, ge.treeRows.back().b,
                      vis, (int)m.tree.size(), m_treeScroll);
        }
    }

    std::wstring folder = m.folderPath;
    size_t sl = folder.find_last_of(L"\\/");
    if (sl != std::wstring::npos && sl + 1 < folder.size()) folder = folder.substr(sl + 1);
    if (folder.empty()) folder = m.folderPath;

    textAt(dc, folder, g_hBold, kText,
           tl + m_pad, ge.body.t + (int)(8 * s + 0.5),
           m_treeW - m_pad - (int)(80 * s), (int)(36 * s + 0.5), 0);

    textAt(dc, LS(L"上级"), g_hSmall, kAccent,
           ge.treeUp.l, ge.treeUp.t, ge.treeUp.w(), ge.treeUp.h(), 1);

    for (size_t i = 0; i < ge.treeRows.size(); ++i) {
        const Box& b = ge.treeRows[i];
        int idx = m_treeScroll + (int)i;
        if (idx >= (int)m.tree.size()) break;

        const TreeRow& r = m.tree[(size_t)idx];
        bool sel = r.isOpenFile;
        REAL ix = (REAL)(tl + m_pad + r.depth * (int)(14 * s + 0.5));

        double rhv = hoverOf(slotForTree((int)i));
        Color rc = sel ? kAccent : mixColor(kText, kAccent, rhv * 0.5);
        int tdx = (int)(1.5 * s * rhv + 0.5);

        textAt(dc, r.name, sel ? g_hBold : g_hBody, rc,
               (int)ix + 30 + tdx, b.t, tr - (int)ix - 38 - tdx, b.h(), 0);
    }
}

void Ui::paintSettings(HDC dc, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    int setLeft = ge.setList.l;
    int setRight = ge.setList.r;

    int top = ge.body.t + (int)(18 * s + 0.5);
    int listTop = ge.setList.t;
    const int kRows = 7;

    // Derived from the box layout() handed over rather than recomputed from the
    // same constant: the two used to be separate literals, and changing the row
    // height in one place silently slid every label off its control.
    int rowH = (ge.setList.b - ge.setList.t) / kRows;

    auto rowBox = [&](int row) {
        int y0 = listTop + row * rowH;
        return Box{ setLeft, y0, setRight, y0 + rowH };
    };

    {
        Graphics g(dc);

        fillRound(g, setLeft, listTop, setRight, listTop + kRows * rowH, (int)(12 * s), kCard);

        for (int i = 1; i < kRows; ++i) {
            Pen sep(kSep, 1.0f);
            g.DrawLine(&sep, (REAL)(setLeft + m_pad), (REAL)(listTop + i * rowH),
                       (REAL)(setRight - m_pad), (REAL)(listTop + i * rowH));
        }

        Color ctlBg = (m.theme == 1) ? Color(255, 0x2C, 0x2C, 0x33)
                                     : Color(255, 0xF2, 0xF2, 0xF7);

        auto seg = [&](const Box& b, bool on, int id) {
            double hv = hoverOf(slotForBtn(id));
            double pr = (slotForBtn(id) >= 0) ? m_aPress[slotForBtn(id)] : 0.0;
            Color base = on ? kAccent : kHoverSel;
            if (on) {
                base = mixColor(kAccent, kAccentDark, pr * 0.55);
            } else {
                base = mixColor(ctlBg, kHoverBtn, hv * (1.0 - pr * 0.5));
            }
            fillRound(g, b.l, b.t, b.r, b.b, (int)(7 * s), base);
        };

        auto minusPlus = [&](const Box& b, bool plus, int id) {
            double hv = hoverOf(slotForBtn(id));
            double pr = (slotForBtn(id) >= 0) ? m_aPress[slotForBtn(id)] : 0.0;
            fillRound(g, b.l, b.t, b.r, b.b, (int)(7 * s),
                      mixColor(ctlBg, kHoverBtn, hv * (1.0 - pr * 0.5)));
            REAL cx = (b.l + b.r) / 2.0f;
            REAL cy = (b.t + b.b) / 2.0f;
            Pen p(kAccent, 1.8f);
            p.SetStartCap(LineCapRound);
            p.SetEndCap(LineCapRound);
            double ext = 5.0 * s * (1.0 - pr * 0.25);
            g.DrawLine(&p, (REAL)(cx - ext), cy, (REAL)(cx + ext), cy);
            if (plus) g.DrawLine(&p, cx, (REAL)(cy - ext), cx, (REAL)(cy + ext));
        };

        minusPlus(ge.setFontDec,       false, 11);
        minusPlus(ge.setFontInc,       true,  12);
        minusPlus(ge.setTermFontDec,   false, 22);
        minusPlus(ge.setTermFontInc,   true,  23);

        seg(ge.setLight, m.theme == 0, 15);
        seg(ge.setDark,  m.theme == 1, 16);

        seg(ge.setTermAuto, m.terminalMode == 0, 17);
        seg(ge.setTermPty,  m.terminalMode == 1, 18);
        seg(ge.setTermPipe, m.terminalMode == 2, 19);

        seg(ge.setLangAuto, m.lang == 0, 24);
        seg(ge.setLangZh,   m.lang == 1, 25);
        seg(ge.setLangEn,   m.lang == 2, 26);

        seg(ge.setFix,     false, 20);
        seg(ge.setRecheck, false, 21);

        Pen bp(kAccent, 1.4f);
        g.DrawRectangle(&bp, (REAL)ge.setFix.l, (REAL)ge.setFix.t,
                        (REAL)(ge.setFix.w() - 1), (REAL)(ge.setFix.h() - 1));
        g.DrawRectangle(&bp, (REAL)ge.setRecheck.l, (REAL)ge.setRecheck.t,
                        (REAL)(ge.setRecheck.w() - 1), (REAL)(ge.setRecheck.h() - 1));
    }

    textAt(dc, LS(L"设置"), g_hBig, kText,
           setLeft, ge.body.t + (int)(16 * s + 0.5),
           setRight - setLeft, (int)(28 * s + 0.5), 0);

    for (int i = 0; i < kRows; ++i) {
        Box rb = rowBox(i);
        std::wstring label;
        std::wstring val;

        if (i == 0)      { label = LS(L"界面字号"); val = std::to_wstring(m.fontSize) + L" px"; }
        else if (i == 1) { label = LS(L"终端字号"); val = std::to_wstring(m.termFontSize) + L" px"; }
        else if (i == 2) { label = LS(L"界面主题"); }
        else if (i == 3) { label = LS(L"终端模式"); }
        else if (i == 4) { label = LS(L"界面语言"); }
        else if (i == 5) { label = LS(L"WSL 位置"); }
        else             { label = LS(L"WSL 环境"); }

        // 列表宽度跟着窗口走（窄的时候会和右侧说明栏平分），所以标签/数值这两栏
        // 也得按比例算，不能按 560 的固定宽度硬留。
        int listW = setRight - setLeft;
        int ctlReserve = (int)(150 * s + 0.5);
        if (ctlReserve > listW / 3) ctlReserve = listW / 3;

        int lw;
        if (i >= 5) {
            // The two WSL rows share a column, but sized to the longer label
            // rather than a fixed 170: in English "WSL environment" ate the
            // whole value column and the version came out as "W...".
            int a1 = textWidth(dc, g_hBody, LS(L"WSL 位置"));
            int a2 = textWidth(dc, g_hBody, LS(L"WSL 环境"));
            lw = m_pad + (a1 > a2 ? a1 : a2) + (int)(14 * s + 0.5);
        } else if (i <= 1) {
            // 字号行右边要塞「数值 + 预览字形」，标签栏就只留标签自己够用的
            // 宽度；照 listW/2 分的话数值栏剩不到 160，预览字形永远放不下。
            lw = m_pad + textWidth(dc, g_hBody, label) + (int)(14 * s + 0.5);
        } else {
            lw = listW / 2;
        }
        if (lw > listW - ctlReserve - (int)(60 * s + 0.5)) {
            lw = listW - ctlReserve - (int)(60 * s + 0.5);
        }
        if (lw < (int)(60 * s + 0.5)) lw = (int)(60 * s + 0.5);

        // Rows with segmented controls have to stop short of the first pill.
        // Labels are painted after the pills, so in a narrow window -- where the
        // language row's three segments reach much further left than the
        // two-segment rows -- the label ended up drawn on top of its own control.
        if (i == 2 || i == 3 || i == 4) {
            const Box& first = (i == 2) ? ge.setLight
                             : (i == 3) ? ge.setTermAuto
                                        : ge.setLangAuto;
            int cap = first.l - setLeft - m_pad - (int)(10 * s + 0.5);
            if (lw > cap) lw = cap;
        }

        textAt(dc, label, g_hBody, kText, setLeft + m_pad, rb.t, lw, rb.h(), 0);

        int vx = setLeft + lw;
        int vw = listW - lw - ctlReserve - (int)(8 * s + 0.5);
        if (vw < 40) vw = 40;
        {
            int avail = ge.setFontDec.l - (int)(12 * s + 0.5) - vx;
            if (avail > vw) vw = avail;
        }

        if (i == 0 || i == 1) {
            // 数值串一律用界面字号，预览字形才用「被预览的那个字号」：
            //   界面字号行 → 界面字体本身（g_hBody 就是按界面字号建的）
            //   终端字号行 → 终端字体（m_edFont）
            // 以前整串都用 m_edFont，于是「界面字号 16」也按终端字号画，两个数
            // 一样大而且撑破行。
            int   nowPx = (i == 0) ? m.fontSize : m.termFontSize;
            HFONT prev  = (i == 0) ? g_hBody : m_edFont;
            int   gapPx = (int)(10 * s + 0.5);

            std::wstring txt = val;
            int tw = textWidth(dc, g_hBody, txt);
            if (tw > vw) {
                std::wstring num = std::to_wstring(nowPx);
                if (textWidth(dc, g_hBody, num) <= vw) {
                    txt = num;
                    tw  = textWidth(dc, g_hBody, txt);
                }
            }

            int gw = prev ? textWidth(dc, prev, L"Aa") : 0;
            if (tw + gapPx + gw <= vw) {
                textAt(dc, txt, g_hBody, kText2,
                       vx + vw - tw, rb.t, tw, rb.h(), 0);
                textAt(dc, L"Aa", prev, kText3,
                       vx, rb.t, vw - tw - gapPx, rb.h(), 2);
            } else {
                textAt(dc, txt, g_hBody, kText2, vx, rb.t, vw, rb.h(), 2);
            }
        } else if (!val.empty()) {
            textAt(dc, val, g_hBody, kText2, vx, rb.t, vw, rb.h(), 2);
        }

        if (i == 5) {
            std::wstring p = m.wslPath.empty() ? LS(L"未检测到") : m.wslPath;
            int x = setLeft + m_pad + lw;
            textAt(dc, p, g_hSmall, kText2,
                   x, rb.t, setRight - m_pad - x, rb.h(), 2);
        } else if (i == 6) {
            // `wsl --version` prints "WSL 版本: 2.6.1.0" on a Chinese system and
            // "WSL version: 2.6.1.0" on an English one -- the prefix is localised
            // by wsl.exe itself, so there is no string here to translate. The row
            // label already says what the number is; keep only the number, which
            // also stops it from eliding against the buttons.
            std::wstring v = m.wslVersion.empty() ? LS(L"未检测到 WSL") : m.wslVersion;
            size_t c = v.find(L':');
            if (c == std::wstring::npos) c = v.find(L'：');
            if (c != std::wstring::npos) {
                size_t s0 = c + 1;
                while (s0 < v.size() && v[s0] == L' ') ++s0;
                if (s0 < v.size()) v = v.substr(s0);
            }

            int x = setLeft + m_pad + lw;
            int rr = ge.setFix.l - (int)(10 * s + 0.5);
            if (rr < x + 40) rr = x + 40;
            // Draw it only when the whole thing fits. DT_END_ELLIPSIS would
            // otherwise chop it down to a bare "W...", which reads as a broken
            // control rather than as a truncation; an empty cell reads fine.
            int avail = rr - x;
            if (textWidth(dc, g_hSmall, v) <= avail)
                textAt(dc, v, g_hSmall, kText2, x, rb.t, avail, rb.h(), 2);
        }
    }

    textAt(dc, LS(L"浅色"), g_hSmall, (m.theme == 0) ? kWhite : kText2,
           ge.setLight.l, ge.setLight.t, ge.setLight.w(), ge.setLight.h(), 1);
    textAt(dc, LS(L"深色"), g_hSmall, (m.theme == 1) ? kWhite : kText2,
           ge.setDark.l, ge.setDark.t, ge.setDark.w(), ge.setDark.h(), 1);

    textAt(dc, LS(L"自动"),   g_hSmall, (m.terminalMode == 0) ? kWhite : kText2,
           ge.setTermAuto.l, ge.setTermAuto.t, ge.setTermAuto.w(), ge.setTermAuto.h(), 1);
    textAt(dc, L"PTY", g_hSmall, (m.terminalMode == 1) ? kWhite : kText2,
           ge.setTermPty.l, ge.setTermPty.t, ge.setTermPty.w(), ge.setTermPty.h(), 1);
    textAt(dc, LS(L"兼容"),   g_hSmall, (m.terminalMode == 2) ? kWhite : kText2,
           ge.setTermPipe.l, ge.setTermPipe.t, ge.setTermPipe.w(), ge.setTermPipe.h(), 1);

    textAt(dc, LS(L"跟随系统"), g_hSmall, (m.lang == 0) ? kWhite : kText2,
           ge.setLangAuto.l, ge.setLangAuto.t, ge.setLangAuto.w(), ge.setLangAuto.h(), 1);
    textAt(dc, LS(L"中文"), g_hSmall, (m.lang == 1) ? kWhite : kText2,
           ge.setLangZh.l, ge.setLangZh.t, ge.setLangZh.w(), ge.setLangZh.h(), 1);
    textAt(dc, L"English", g_hSmall, (m.lang == 2) ? kWhite : kText2,
           ge.setLangEn.l, ge.setLangEn.t, ge.setLangEn.w(), ge.setLangEn.h(), 1);

    textAt(dc, LS(L"修复 WSL"), g_hSmall, kAccent,
           ge.setFix.l, ge.setFix.t, ge.setFix.w(), ge.setFix.h(), 1);
    textAt(dc, LS(L"重新检测"), g_hSmall, kAccent,
           ge.setRecheck.l, ge.setRecheck.t, ge.setRecheck.w(), ge.setRecheck.h(), 1);

    int infoT = listTop + kRows * rowH + (int)(18 * s + 0.5);
    textAt(dc, LS(L"快捷键"), g_hBold, kText,
           setLeft, infoT, setRight - setLeft, (int)(22 * s + 0.5), 0);

    struct KeyRow { const wchar_t* k; const wchar_t* d; };
    static const KeyRow keys[] = {
        { L"Ctrl + S",         LS(L"保存当前文件（项目页）") },
        { LS(L"Ctrl + 滚轮"),      LS(L"缩放字体大小") },
        { L"Ctrl + Shift + C", LS(L"复制终端选区") },
        { L"Ctrl + Shift + V", LS(L"粘贴到终端") },
        { L"Ctrl + A",         LS(L"编辑器全选") },
        { L"Ctrl + L / Ctrl+C", LS(L"清屏 / 中断当前命令") },
        { L"Esc",              LS(L"关闭发行版下拉菜单") }
    };

    int keyW = (int)(176 * s + 0.5);
    int keyRowH = (int)(24 * s + 0.5);
    for (int i = 0; i < 7; ++i) {
        int y = infoT + (int)(26 * s + 0.5) + i * keyRowH;
        textAt(dc, keys[i].k, g_hSmall, kText,
               setLeft, y, keyW, keyRowH, 0);
        textAt(dc, keys[i].d, g_hSmall, kText2,
               setLeft + keyW, y, setRight - setLeft - keyW, keyRowH, 0);
    }

    // ---- 右侧说明列：Arch 密钥初始化 ----
    // ge.setSide 是 layout 算好的列；窗口太窄时它是空的，这时把卡片挪到快捷键
    // 下面（只在还有富余高度时才画，避免糊到窗口外）。
    int keysBottom = infoT + (int)(26 * s + 0.5) + 7 * keyRowH;
    int nx = ge.setSide.w() > 0 ? ge.setSide.l : setLeft;
    int nw = ge.setSide.w() > 0 ? ge.setSide.w() : (setRight - setLeft);
    int ny = ge.setSide.w() > 0 ? ge.setSide.t : (keysBottom + (int)(18 * s + 0.5));

    int cardPad  = (int)(16 * s + 0.5);
    int lineH    = (int)(22 * s + 0.5);
    int cardTitleH = (int)(22 * s + 0.5);
    int subH     = (int)(21 * s + 0.5);
    int codeGap  = (int)(10 * s + 0.5);

    int archH = cardPad * 2 + cardTitleH + 2 * subH + codeGap + 3 * lineH;

    if (nw > (int)(150 * s + 0.5) && ge.body.b - ny >= archH) {
        static const wchar_t* kCmd[3] = {
            L"pacman-key --init",
            L"pacman-key --populate",
            L"pacman -Sy archlinux-keyring"
        };

        {
            Graphics g(dc);
            fillRound(g, nx, ny, nx + nw, ny + archH, (int)(12 * s), kCard);
        }

        int cy = ny + cardPad;
        textAt(dc, LS(L"Arch Linux 密钥初始化"), g_hBold, kText,
               nx + cardPad, cy, nw - cardPad * 2, cardTitleH, 0);
        cy += cardTitleH;
        textAt(dc, LS(L"首次使用请先初始化密钥环，"), g_hSmall, kText2,
               nx + cardPad, cy, nw - cardPad * 2, subH, 0);
        cy += subH;
        textAt(dc, LS(L"否则 pacman 装不了软件。"), g_hSmall, kText2,
               nx + cardPad, cy, nw - cardPad * 2, subH, 0);
        cy += subH + codeGap;

        {
            Graphics g(dc);
            fillRound(g, nx + cardPad, cy, nx + nw - cardPad, cy + 3 * lineH,
                      (int)(8 * s), kHoverSel);
        }

        // 固定用界面等宽字号（g_hMono），不跟终端字号走。
        int codePadX = (int)(10 * s + 0.5);
        int codeW    = nw - cardPad * 2 - codePadX * 2;
        int codeX    = nx + cardPad + codePadX;

        bool dollar = g_hMono != NULL;
        if (dollar) {
            std::wstring probe = L"$ " + std::wstring(kCmd[2]);
            if (textWidth(dc, g_hMono, probe) > codeW) dollar = false;
        }

        for (int i = 0; i < 3; ++i) {
            std::wstring line = dollar ? (L"$ " + std::wstring(kCmd[i]))
                                       : std::wstring(kCmd[i]);
            textAt(dc, line, g_hMono, kAccent,
                   codeX, cy + i * lineH, codeW, lineH, 0);
        }
    }
}


void Ui::paintDistroPage(HDC dc, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    {
        Graphics g(dc);
        SolidBrush bg(kWindowBg);
        g.FillRectangle(&bg, (REAL)ge.body.l, (REAL)ge.body.t,
                        (REAL)ge.body.w(), (REAL)ge.body.h());
    }

    double hb = hoverOf(slotForMenu(15));
    if (ge.dRefresh.w() > 0) {
        Graphics g(dc);
        fillRound(g, ge.dRefresh.l, ge.dRefresh.t, ge.dRefresh.r, ge.dRefresh.b,
                  (int)(10 * s),
                  m.distroBusy ? mixColor(kCard, kHoverBtn, 0.5)
                               : mixColor(kCard, kAccentSoft, hb));
        strokeRound(g, ge.dRefresh.l, ge.dRefresh.t, ge.dRefresh.r, ge.dRefresh.b,
                    (int)(10 * s), kPanelLine, 1.0f);
        textAt(dc, m.distroBusy ? LS(L"读取中") : LS(L"刷新"),
               m.distroBusy ? g_hSmall : g_hBody,
               m.distroBusy ? kText2 : kAccent,
               ge.dRefresh.l, ge.dRefresh.t, ge.dRefresh.w(), ge.dRefresh.h(), 1);
    }

    int titleW = ge.dRefresh.w() > 0 ? (ge.dRefresh.l - ge.body.l - (int)(16 * s + 0.5))
                                     : (ge.body.w() - (int)(40 * s + 0.5));
    textAt(dc, m.distroMsg.empty() ? LS(L"发行版") : m.distroMsg, g_hBold, kText,
           ge.body.l + (int)(20 * s + 0.5), ge.body.t + (int)(10 * s + 0.5),
           titleW, (int)(30 * s + 0.5), 0);

    {
        Graphics g(dc);
        {
            RectF clip((REAL)ge.body.l, (REAL)ge.body.t,
                       (REAL)ge.body.w(), (REAL)ge.body.h());
            g.SetClip(clip, CombineModeReplace);

        if (ge.dHdrInst.w() > 0) {
            textAt(dc, LS(L"已安装"), g_hSmall, kText2,
                   ge.dHdrInst.l, ge.dHdrInst.t, ge.dHdrInst.w(), ge.dHdrInst.h(), 0);
        }

        for (size_t i = 0; i < ge.dInstRows.size(); ++i) {
            const Box& b = ge.dInstRows[i];

            int ri = (int)i + ge.dInstFirst;
            if (ri < 0) ri = 0;
            if (ri >= (int)m.installed.size()) continue;

            const std::wstring& name = m.installed[(size_t)ri];

            double rowHv = hoverOf(slotForInstRow((int)i));
            if (rowHv < 0.0) rowHv = 0.0;
            if (rowHv > 1.0) rowHv = 1.0;

            Color rowBg = mixColor(kCard, kHoverSel, rowHv * 0.16);
            if (rowHv > 0.004) rowBg = mixColor(rowBg, kAccentSoft, rowHv * 0.42);
            fillRound(g, b.l, b.t + (int)(3 * s + 0.5), b.r, b.b - (int)(3 * s + 0.5),
                      (int)(10 * s), rowBg);
            strokeRound(g, b.l, b.t + (int)(3 * s + 0.5), b.r, b.b - (int)(3 * s + 0.5),
                        (int)(10 * s), mixColor(kSep, kHoverSel, rowHv), 1.0f);

            if (rowHv > 0.01) {
                int barW = (int)(3 * s + 0.5);
                int grow = (int)((9 - 3 * barW) * rowHv);
                fillRound(g, b.l + grow, b.t + (int)(3 * s + 0.5),
                          b.l + grow + barW, b.b - (int)(3 * s + 0.5),
                          (int)(2 * s + 0.5),
                          mixColor(kAccentSoft, kAccent, rowHv));
            }

            bool isActive = (lstrcmpiW(name.c_str(), m.activeDistro.c_str()) == 0);
            bool isDef = false;
            bool running = false;
            for (size_t k = 0; k < m.statuses.size(); ++k) {
                if (lstrcmpiW(m.statuses[k].name.c_str(), name.c_str()) == 0) {
                    isDef    = m.statuses[k].isDefault;
                    running  = m.statuses[k].running;
                    break;
                }
            }

            int badH = (int)(21 * s + 0.5);
            int badY = b.t + (b.h() - badH) / 2;
            int badW = (int)(54 * s + 0.5);
            int badX = b.l + (int)(10 * s + 0.5);

            if (running) {
                fillRound(g, badX, badY, badX + badW, badY + badH, (int)(7 * s + 0.5),
                          mixColor(kOk, kWhite, 0.9));
                strokeRound(g, badX, badY, badX + badW, badY + badH, (int)(7 * s + 0.5),
                            mixColor(kOk, kWhite, 0.5), 1.0f);
                textAt(dc, LS(L"运行中"), g_hSmall, mixColor(kOk, kText, 0.25),
                       badX, badY, badW, badH, 1);
            } else {
                fillRound(g, badX, badY, badX + badW, badY + badH, (int)(7 * s + 0.5),
                          mixColor(kText3, kWhite, 0.78));
                textAt(dc, LS(L"已停止"), g_hSmall, mixColor(kText2, kWhite, 0.35),
                       badX, badY, badW, badH, 1);
            }

            int tx = badX + badW + (int)(12 * s + 0.5);
            int rx = b.r;
            int bw = (int)(66 * s + 0.5);
            int bh = (int)(24 * s + 0.5);
            int by = b.t + (b.h() - bh) / 2;

            rx -= (int)(6 * s + 0.5);

            fillRound(g, rx - bw, by, rx, by + bh, (int)(8 * s),
                      isActive ? mixColor(kAccent, kAccentDark, 0.35) : kHoverBtn);
            textAt(dc, isActive ? LS(L"已连接") : LS(L"连接"), g_hSmall,
                   isActive ? kWhite : kText,
                   rx - bw, by, bw, bh, 1);
            rx -= bw + (int)(6 * s + 0.5);

            fillRound(g, rx - bw, by, rx, by + bh, (int)(8 * s), kHoverBtn);
            strokeRound(g, rx - bw, by, rx, by + bh, (int)(8 * s), kPanelLine, 1.0f);
            textAt(dc, isDef ? LS(L"默认") : LS(L"设为默认"), g_hSmall,
                   isDef ? kAccent : kText2,
                   rx - bw, by, bw, bh, 1);
            rx -= bw + (int)(6 * s + 0.5);

            bool busyDel = m.unregisterBusy;
            fillRound(g, rx - bw, by, rx, by + bh, (int)(8 * s),
                      busyDel ? mixColor(kHoverBtn, kText3, 0.55) : kHoverBtn);
            strokeRound(g, rx - bw, by, rx, by + bh, (int)(8 * s), kPanelLine, 1.0f);
            textAt(dc, busyDel ? LS(L"注销中") : LS(L"删除"), g_hSmall,
                   busyDel ? kText3 : kDanger, rx - bw, by, bw, bh, 1);

            textAt(dc, name, g_hBody, mixColor(kText, kWhite, rowHv * 0.12),
                   tx, b.t, rx - tx - (int)(12 * s + 0.5), b.h(), 0);
        }

        if (ge.dHdrOnline.w() > 0) {
            textAt(dc, LS(L"可安装"), g_hSmall, kText2,
                   ge.dHdrOnline.l, ge.dHdrOnline.t, ge.dHdrOnline.w(),
                   ge.dHdrOnline.h(), 0);
            Pen sep(kSep, 1.0f);
            g.DrawLine(&sep, (REAL)(ge.dHdrOnline.l + (int)(8 * s + 0.5)),
                       (REAL)(ge.dHdrOnline.b - (int)(8 * s + 0.5)),
                       (REAL)(ge.dHdrOnline.r - (int)(8 * s + 0.5)),
                       (REAL)(ge.dHdrOnline.b - (int)(8 * s + 0.5)));
        }

        for (size_t i = 0; i < ge.dOnlineRows.size(); ++i) {
            const Box& b = ge.dOnlineRows[i];
            int oi = ge.dOnlineFirst + (int)i;
            if (oi < 0 || oi >= (int)m.online.size()) continue;

            const DistroEntry& e = m.online[(size_t)oi];
            bool broken = modelIsBroken(m, e.id);

            double rowHv = hoverOf(slotForTree((int)i));
            if (rowHv < 0.0) rowHv = 0.0;
            if (rowHv > 1.0) rowHv = 1.0;

            Color rowBg = mixColor(kCard, kHoverSel, rowHv * 0.16);
            if (rowHv > 0.004) rowBg = mixColor(rowBg, kAccentSoft, rowHv * 0.42);
            fillRound(g, b.l, b.t + (int)(3 * s + 0.5), b.r, b.b - (int)(3 * s + 0.5),
                      (int)(10 * s), rowBg);
            strokeRound(g, b.l, b.t + (int)(3 * s + 0.5), b.r, b.b - (int)(3 * s + 0.5),
                        (int)(10 * s), mixColor(kSep, kHoverSel, rowHv), 1.0f);

            if (rowHv > 0.01) {
                int barW = (int)(3 * s + 0.5);
                int grow = (int)((9 - 3 * barW) * rowHv);
                fillRound(g, b.l + grow, b.t + (int)(3 * s + 0.5),
                          b.l + grow + barW, b.b - (int)(3 * s + 0.5),
                          (int)(2 * s + 0.5),
                          mixColor(kAccentSoft, kAccent, rowHv));
            }

            std::wstring name = e.label.empty() ? e.id : e.label;
            textAt(dc, name, g_hBody, mixColor(kText, kWhite, rowHv * 0.12),
                   b.l + (int)(14 * s + 0.5), b.t,
                   b.r - b.l - (int)(110 * s + 0.5), b.h(), 0);

            int bw = (int)(78 * s + 0.5);
            int bh = (int)(24 * s + 0.5);
            int by = b.t + (b.h() - bh) / 2;
            int bx = b.r - (int)(14 * s + 0.5) - bw;

            fillRound(g, bx, by, bx + bw, by + bh, (int)(8 * s),
                      mixColor(broken ? kDanger : kAccent, kWhite, 0.12));
            textAt(dc, broken ? LS(L"重新安装") : LS(L"下载安装"), g_hSmall, kWhite,
                   bx, by, bw, bh, 1);

            if (broken) {
                iconChevron(g, (REAL)(b.r - (int)(20 * s + 0.5)),
                            REAL(b.t + b.h() / 2), s, kDanger, 1.6f, false);
            }
        }

        if (m.distroLoading && m.installed.empty() && m.online.empty()) {
            textAt(dc, LS(L"正在读取发行版列表…"), g_hBody, kText2,
                   ge.body.l, ge.body.t + (int)(120 * s + 0.5), ge.body.w(),
                   (int)(28 * s + 0.5), 1);
        }

            g.SetClip(clip, CombineModeReplace);
        }
    }

    if (ge.dHint.w() > 0) {
        std::wstring t;
        if (ge.dTotal > 0) {
            wchar_t buf[128];
            wsprintfW(buf, LS(L"共 %d 项 · 已安装 %d · 可安装 %d"),
                      ge.dTotal, (int)m.installed.size(), (int)m.online.size());
            t = buf;
        }
        if (t.empty()) t = LS(L"读取不到发行版列表，试试右上角刷新");
        textAt(dc, t, g_hSmall, kText2, ge.dHint.l, ge.dHint.t, ge.dHint.w(),
               ge.dHint.h(), 1);
    }
}

void Ui::paintInstall(HDC dc, const UiModel& m, const Geom& ge) {
    double s = m_dpi / 96.0;

    int cardL = ge.body.l + (int)(24 * s + 0.5);
    int cardR = ge.body.r - (int)(24 * s + 0.5);
    if (cardR > cardL + (int)(640 * s)) cardR = cardL + (int)(640 * s);
    int cardW = cardR - cardL;

    std::wstring title = LS(L"安装 ") + (m.instName.empty() ? std::wstring(LS(L"发行版")) : m.instName);
    textAt(dc, title, g_hBig, kText, cardL, ge.body.t + (int)(14 * s + 0.5),
           cardW, (int)(30 * s + 0.5), 0);

    static const wchar_t* kSteps[] = {
        LS(L"1  解析镜像地址"),
        LS(L"2  下载镜像"),
        LS(L"3  导入 WSL"),
        LS(L"4  注册并启动")
    };

    int stageRow = (int)(30 * s + 0.5);
    int stepsTop = ge.body.t + (int)(56 * s + 0.5);

    int doneUpTo = 0;
    switch (m.instStage) {
    case INST_RESOLVE:    doneUpTo = 0; break;
    case INST_DOWNLOAD:   doneUpTo = 1; break;
    case INST_IMPORT:     doneUpTo = 2; break;
    case INST_UNREGISTER: doneUpTo = 2; break;
    case INST_VERIFY:     doneUpTo = 3; break;
    case INST_DONE:       doneUpTo = 4; break;
    default:              doneUpTo = 0; break;
    }

    for (int i = 0; i < 4; ++i) {
        Box b = ge.instBar;
        int y = stepsTop + i * stageRow;
        bool done = (i < doneUpTo);
        bool active = (i == doneUpTo && m.instStage != INST_DONE &&
                       m.instStage != INST_FAILED && m.instStage != INST_CANCELED);

        Color txt = done ? kText2 : (active ? kText : kText3);
        textAt(dc, kSteps[i], g_hSmall, txt,
               cardL + (int)(26 * s + 0.5), y, cardW - (int)(146 * s), stageRow, 0);

        if (done || active) {
            Graphics g(dc);
            REAL cy = (REAL)(y + stageRow / 2);
            REAL cx = (REAL)(cardL + (int)(10 * s + 0.5));

            if (done) {
                Pen p(kOk, 1.7f);
                p.SetStartCap(LineCapRound);
                p.SetEndCap(LineCapRound);
                g.DrawLine(&p, cx - 4 * (REAL)s, cy,
                           cx - 1 * (REAL)s, cy + 4 * (REAL)s);
                g.DrawLine(&p, cx - 1 * (REAL)s, cy + 4 * (REAL)s,
                           cx + 5 * (REAL)s, cy - 4 * (REAL)s);
            } else {
                double ang = (double)(GetTickCount() % 1100) / 1100.0 * 360.0;
                Pen p(kAccent, 1.8f);
                p.SetStartCap(LineCapRound);
                p.SetEndCap(LineCapRound);
                g.DrawArc(&p, cx - 5 * (REAL)s, cy - 5 * (REAL)s,
                          10 * (REAL)s, 10 * (REAL)s, (REAL)ang, 250.0f);
            }
        }

        std::wstring mark;
        if (done)        mark = LS(L"完成");
        else if (active) mark = LS(L"进行中");
        Color mc = done ? kOk : (active ? kAccent : kText3);
        textAt(dc, mark, g_hSmall, mc,
               cardR - (int)(100 * s), y, (int)(100 * s), stageRow, 2);
        (void)b;
    }

    {
        Graphics g(dc);
        fillRound(g, ge.instBar.l, ge.instBar.t, ge.instBar.r, ge.instBar.b,
                  (int)(6 * s), kTrack);

        double pctD = m_aBarPct;
        if (m.instStage == INST_DONE || m.instStage == INST_VERIFY) pctD = 100.0;
        else if (m.instStage == INST_CANCELED) pctD = 0.0;
        else if (m.instStage == INST_FAILED) pctD = m_aBarPct;
        if (pctD < 0.0) pctD = 0.0;
        if (pctD > 100.0) pctD = 100.0;

        int fillW = (int)((double)ge.instBar.w() * pctD / 100.0 + 0.5);
        if (fillW > 0) {
            Color bc = (m.instStage == INST_FAILED) ? kDanger
                     : (m.instStage == INST_DONE ? kOk : kAccent);
            fillRound(g, ge.instBar.l, ge.instBar.t, ge.instBar.l + fillW, ge.instBar.b,
                      (int)(6 * s), bc);

            if (m.instStage == INST_DOWNLOAD && fillW > (int)(24 * s)) {
                double ph = (double)(GetTickCount() % 1400) / 1400.0;
                int sw = (int)(54 * s + 0.5);
                int sx = ge.instBar.l + (int)(ph * (double)(fillW + sw)) - sw;
                int l = sx;
                int r = sx + sw;
                if (l < ge.instBar.l) l = ge.instBar.l;
                if (r > ge.instBar.l + fillW) r = ge.instBar.l + fillW;
                if (r > l) {
                    int sv2 = SaveDC(dc);
                    IntersectClipRect(dc, ge.instBar.l, ge.instBar.t,
                                      ge.instBar.l + fillW, ge.instBar.b);
                    SolidBrush sb(Color(46, 0xFF, 0xFF, 0xFF));
                    g.FillRectangle(&sb, (REAL)l, (REAL)ge.instBar.t,
                                    (REAL)(r - l), (REAL)ge.instBar.h());
                    RestoreDC(dc, sv2);
                }
            }
        }
    }

    int detailY = ge.instBar.b + (int)(10 * s + 0.5);
    textAt(dc, m.instStageText, g_hBody, kText,
           cardL, detailY, cardW, (int)(22 * s + 0.5), 0);

    if (!m.instDetail.empty()) {
        textAt(dc, m.instDetail, g_hSmall, kText2,
               cardL, detailY + (int)(22 * s + 0.5), cardW, (int)(20 * s + 0.5), 0);
    }

    {
        Graphics g(dc);
        fillRound(g, ge.instLog.l, ge.instLog.t, ge.instLog.r, ge.instLog.b,
                  (int)(10 * s), (m.theme == 1) ? Color(255, 0x1A, 0x1A, 0x1E)
                                                : Color(255, 0x1C, 0x1C, 0x22));
    }

    int lineH = (int)(18 * s + 0.5);
    int pad = (int)(12 * s + 0.5);
    int maxLines = (ge.instLog.h() - pad * 2) / lineH;
    if (maxLines < 1) maxLines = 1;

    int first = (int)m.instLog.size() - maxLines;
    if (first < 0) first = 0;

    int ly = ge.instLog.t + pad;
    for (size_t i = (size_t)first; i < m.instLog.size(); ++i) {
        const std::wstring& ln = m.instLog[i];
        Color lc(255, 0xD4, 0xD4, 0xDC);
        if (!ln.empty() && ln[0] == L'!') lc = Color(255, 0xFF, 0x8A, 0x80);
        else if (!ln.empty() && ln[0] == L'+') lc = Color(255, 0x7E, 0xE0, 0x9A);

        textAt(dc, (ln.empty() || ln[0] == L'!' || ln[0] == L'+') ? ln.substr(ln.empty() ? 0 : 1) : ln,
               g_hSmall, lc, ge.instLog.l + pad, ly, ge.instLog.w() - pad * 2, lineH, 0);
        ly += lineH;
        if (ly + lineH > ge.instLog.b - pad + lineH) break;
    }

    if (m.instLog.empty()) {
        textAt(dc, LS(L"（等待输出…）"), g_hSmall, Color(255, 0x7A, 0x7A, 0x86),
               ge.instLog.l + pad, ge.instLog.t + pad,
               ge.instLog.w() - pad * 2, lineH, 0);
    }

    bool busy = (m.instStage == INST_RESOLVE || m.instStage == INST_DOWNLOAD ||
                 m.instStage == INST_IMPORT || m.instStage == INST_VERIFY);

    if (busy && m.instCancelable) {
        Graphics g(dc);
        double hv = hoverOf(slotForBtn(22));
        fillRound(g, ge.instCancel.l, ge.instCancel.t, ge.instCancel.r, ge.instCancel.b,
                  (int)(9 * s), mixColor(kHoverBtn, kHoverSel, hv));
        textAt(dc, LS(L"取消"), g_hBody, kText,
               ge.instCancel.l, ge.instCancel.t, ge.instCancel.w(), ge.instCancel.h(), 1);
    } else if (!busy && m.instStage != INST_IDLE) {
        Graphics g(dc);
        double hv = hoverOf(slotForBtn(23));
        fillRound(g, ge.instRetry.l, ge.instRetry.t, ge.instRetry.r, ge.instRetry.b,
                  (int)(9 * s), mixColor(kHoverBtn, kHoverSel, hv));
        textAt(dc, LS(L"重试"), g_hBody, kText,
               ge.instRetry.l, ge.instRetry.t, ge.instRetry.w(), ge.instRetry.h(), 1);
    }

    {
        Graphics g(dc);
        if (busy) {
            fillRound(g, ge.instClose.l, ge.instClose.t, ge.instClose.r, ge.instClose.b,
                      (int)(9 * s), mixColor(kHoverBtn, kText3, 0.35));
            textAt(dc, LS(L"安装中…"), g_hBody, kText2,
                   ge.instClose.l, ge.instClose.t, ge.instClose.w(), ge.instClose.h(), 1);
        } else {
            double hv = hoverOf(slotForBtn(10));
            double pr = (slotForBtn(10) >= 0) ? m_aPress[slotForBtn(10)] : 0.0;
            fillRound(g, ge.instClose.l, ge.instClose.t, ge.instClose.r, ge.instClose.b,
                      (int)(9 * s),
                      mixColor(mixColor(kAccent, kAccentDark, hv * 0.30), kAccentDark,
                               pr * 0.5));
            textAt(dc, m.instStage == INST_DONE ? LS(L"进入终端") : LS(L"返回"), g_hBody, kWhite,
                   ge.instClose.l, ge.instClose.t, ge.instClose.w(), ge.instClose.h(), 1);
        }
    }
}

struct EdPalette {
    COLORREF bg, gutBg, gutFg, gutActive, curLine, caret, selBg, fg;
    COLORREF tk[TK_KIND_COUNT];
};

static const EdPalette kLight = {
    RGB(0xFF, 0xFF, 0xFF), RGB(0xF7, 0xF7, 0xFA), RGB(0x9A, 0x9A, 0xA0),
    RGB(0x1C, 0x1C, 0x1E), RGB(0xF4, 0xF8, 0xFF), RGB(0x1C, 0x1C, 0x1E),
    RGB(0xCC, 0xE2, 0xFF), RGB(0x1C, 0x1C, 0x1E),
    {
        RGB(0x1C, 0x1C, 0x1E),
        RGB(0x00, 0x00, 0xFF),
        RGB(0x26, 0x7F, 0x99),
        RGB(0x80, 0x00, 0x00),
        RGB(0xA3, 0x15, 0x15),
        RGB(0x09, 0x86, 0x58),
        RGB(0x00, 0x80, 0x00),
        RGB(0x79, 0x5E, 0x26),
        RGB(0xAF, 0x00, 0xDB),
        RGB(0x00, 0x00, 0x00),
        RGB(0x00, 0x70, 0xC1)
    }
};

static const EdPalette kDark = {
    RGB(0x1E, 0x1E, 0x1E), RGB(0x25, 0x25, 0x26), RGB(0x85, 0x85, 0x85),
    RGB(0xC6, 0xC6, 0xC6), RGB(0x2A, 0x2D, 0x2E), RGB(0xAE, 0xAF, 0xAD),
    RGB(0x26, 0x4F, 0x78), RGB(0xD4, 0xD4, 0xD4),
    {
        RGB(0xD4, 0xD4, 0xD4),
        RGB(0x56, 0x9C, 0xD6),
        RGB(0x4E, 0xC9, 0xB0),
        RGB(0xC5, 0x86, 0xC0),
        RGB(0xCE, 0x91, 0x78),
        RGB(0xB5, 0xCE, 0xA8),
        RGB(0x6A, 0x99, 0x55),
        RGB(0xDC, 0xDC, 0xAA),
        RGB(0xC5, 0x86, 0xC0),
        RGB(0xD4, 0xD4, 0xD4),
        RGB(0x4F, 0xC1, 0xFF)
    }
};

void Ui::paintEditorGdi(HDC dc, const UiModel& m, const Editor* ed,
                        const Geom& ge, bool caretOn) {
    if (!ed || !ed->isOpen()) return;

    const EdLayout& L = ge.ed;
    if (L.r <= L.l || L.b <= L.t) return;

    const EdPalette& P = (m.theme == 1) ? kDark : kLight;
    double s = m_dpi / 96.0;

    RECT all = { L.l, L.t, L.r, L.b };
    HBRUSH hb = CreateSolidBrush(P.bg);
    FillRect(dc, &all, hb);
    DeleteObject(hb);

    RECT tool = { L.l, L.t, L.r, L.t + L.toolH };
    HBRUSH htb = CreateSolidBrush((m.theme == 1) ? RGB(0x2D, 0x2D, 0x30) : RGB(0xFA, 0xFA, 0xFC));
    FillRect(dc, &tool, htb);
    DeleteObject(htb);

    {
        Graphics g(dc);

        Color tSep = (m.theme == 1) ? Color(255, 0x3A, 0x3A, 0x3D) : kSep;
        Pen pen(tSep, 1.0f);
        g.DrawLine(&pen, (REAL)L.l, (REAL)(L.t + L.toolH) - 0.5f,
                   (REAL)L.r, (REAL)(L.t + L.toolH) - 0.5f);

        auto btnShape = [&](const Box& b, bool primary, bool hv) {
            Color bgc = primary ? (hv ? kAccentDark : kAccent)
                                : ((m.theme == 1) ? Color(255, 0x3A, 0x3A, 0x3D)
                                                  : (hv ? kHoverBtn : Color(255, 0xEC, 0xEC, 0xF1)));
            fillRound(g, b.l, b.t, b.r, b.b, (int)(6 * s), bgc);
        };

        btnShape(ge.edNew, false, m_hoverBtn == 7);
        btnShape(ge.edSave, true, m_hoverBtn == 5);
        btnShape(ge.edClose, false, m_hoverBtn == 6);
    }

    {
        Color tFg = (m.theme == 1) ? Color(255, 0xE8, 0xE8, 0xEA) : kText;
        std::wstring name = m.editorName.empty() ? LS(L"未命名") : m.editorName;

        textAt(dc, name, g_hBody, tFg,
               L.l + m_pad, L.t, L.r - L.l - (int)(230 * s), L.toolH, 0);

        std::wstring meta = m.editorLang;
        if (m.editorDirty) meta += L"  ●";
        textAt(dc, meta, g_hSmall, kText2,
               L.l + m_pad + (int)(160 * s), L.t, L.r - L.l - (int)(390 * s), L.toolH, 0);

        textAt(dc, LS(L"新建"), g_hSmall, tFg, ge.edNew.l, ge.edNew.t, ge.edNew.w(), ge.edNew.h(), 1);
        textAt(dc, LS(L"保存"), g_hSmall, kWhite, ge.edSave.l, ge.edSave.t, ge.edSave.w(), ge.edSave.h(), 1);
        textAt(dc, LS(L"关闭"), g_hSmall, tFg, ge.edClose.l, ge.edClose.t, ge.edClose.w(), ge.edClose.h(), 1);
    }

    int bodyT = L.t + L.toolH;
    RECT gut = { L.l, bodyT, L.l + L.gutterW, L.b };
    HBRUSH gb = CreateSolidBrush(P.gutBg);
    FillRect(dc, &gut, gb);
    DeleteObject(gb);

    RECT clip = { L.l, bodyT, L.r, L.b };
    int saved = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);

    SetBkMode(dc, TRANSPARENT);

    const int textX = L.l + L.gutterW + (int)(8 * s);
    const int scrollX = ed->scrollX();
    const int scrollY = ed->scrollY();
    const int tabW = ed->tabWidth();

    HFONT oldFont = (HFONT)SelectObject(dc, m_edFont);
    int baseOff = (L.lineH - m_edFontH) / 2;

    std::vector<Token> toks;
    std::vector<TokenKind> kinds;

    for (int i = 0; i < L.viewLines; ++i) {
        int li = scrollY + i;
        if (li >= ed->lineCount()) break;

        int y = bodyT + i * L.lineH;

        bool curLine = (li == ed->cursorLine());
        if (curLine) {
            RECT lb = { L.l + 1, y, L.r, y + L.lineH };
            HBRUSH cb = CreateSolidBrush(P.curLine);
            FillRect(dc, &lb, cb);
            DeleteObject(cb);
        }

        int selL0 = 0, selC0 = 0, selL1 = 0, selC1 = 0;
        bool hasSel = ed->hasSelection();
        if (hasSel) ed->selectionRange(selL0, selC0, selL1, selC1);

        const std::wstring& line = ed->line(li);

        if (hasSel && li >= selL0 && li <= selL1) {
            int a = (li == selL0) ? selC0 : 0;
            int b = (li == selL1) ? selC1 : (int)line.size();
            int va = ed->visualCol(li, a);
            int vb = ed->visualCol(li, b);
            if (vb <= va && li < selL1) vb = va + 1;
            int xa = textX + (va - scrollX) * L.charW;
            int xb = textX + (vb - scrollX) * L.charW;
            if (xa < L.l + L.gutterW) xa = L.l + L.gutterW;
            if (xb > L.r) xb = L.r;
            if (xb > xa) {
                RECT sb = { xa, y, xb, y + L.lineH };
                HBRUSH sh = CreateSolidBrush(P.selBg);
                FillRect(dc, &sb, sh);
                DeleteObject(sh);
            }
        }

        {
            wchar_t num[24];
            wsprintfW(num, L"%d", li + 1);
            SetTextColor(dc, curLine ? P.gutActive : P.gutFg);
            RECT nr = { L.l, y, L.l + L.gutterW - (int)(8 * s), y + L.lineH };
            DrawTextW(dc, num, -1, &nr,
                      DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        uint8_t endSt = 0;
        tokenizeLine(line, ed->lineStartState(li), ed->lang(), toks, endSt);

        kinds.assign(line.size() + 1, TK_TEXT);
        for (size_t t = 0; t < toks.size(); ++t) {
            int a = toks[t].start;
            int b = a + toks[t].len;
            if (a < 0) a = 0;
            if (b > (int)line.size()) b = (int)line.size();
            for (int kk = a; kk < b; ++kk) kinds[(size_t)kk] = toks[t].kind;
        }

        int vcol = 0;
        size_t ci = 0;

        while (ci < line.size()) {
            wchar_t ch = line[(size_t)ci];

            if (ch == L'\t') {
                int adv = tabW - (vcol % tabW);
                vcol += adv;
                ++ci;
                continue;
            }

            int cw = charDisplayWidth(ch);
            if (cw != 1) {
                int x = textX + (vcol - scrollX) * L.charW;
                if (x + cw * L.charW > L.l + L.gutterW && x < L.r) {
                    SetTextColor(dc, P.tk[kinds[(size_t)ci]]);
                    TextOutW(dc, x, y + baseOff, &ch, 1);
                }
                vcol += cw;
                ++ci;
                continue;
            }

            TokenKind k = kinds[(size_t)ci];
            std::wstring run;
            int runV = vcol;
            while (ci < line.size()) {
                wchar_t c2 = line[(size_t)ci];
                if (c2 == L'\t' || charDisplayWidth(c2) != 1) break;
                if (kinds[(size_t)ci] != k) break;
                run.push_back(c2);
                ++ci;
                ++vcol;
            }

            if (!run.empty()) {
                int x = textX + (runV - scrollX) * L.charW;
                if (x + (int)run.size() * L.charW > L.l + L.gutterW && x < L.r) {
                    SetTextColor(dc, P.tk[k]);
                    TextOutW(dc, x, y + baseOff, run.c_str(), (int)run.size());
                }
            }
        }

        if (caretOn && curLine) {
            int vc = ed->visualCol(li, ed->cursorCol());
            int x = textX + (vc - scrollX) * L.charW;
            if (x >= L.l + L.gutterW && x < L.r) {
                RECT cb = { x, y + 2, x + (int)(2 * s + 0.5), y + L.lineH - 2 };
                HBRUSH ch2 = CreateSolidBrush(P.caret);
                FillRect(dc, &cb, ch2);
                DeleteObject(ch2);
            }
        }
    }

    if (oldFont) SelectObject(dc, oldFont);
    RestoreDC(dc, saved);
}

void Ui::paintChrome(HDC dc, int w, int h, const UiModel& m) {
    Geom ge;
    layout(w, h, m, ge);
    paintSidebar(dc, h, m, ge);
    paintTitleBar(dc, w, m, ge);
    // The terminal bar (the distro selector) lives in the title bar, so it
    // must be painted after the title bar's opaque fill, not from paintContent
    // (whose clip starts below the title bar and would hide it).
    if (m.page == PAGE_TERMINAL) paintTermBar(dc, w, m, ge);
}

void Ui::paintContent(HDC dc, int w, int h, const UiModel& m,
                      const Editor* ed, bool caretOn) {
    Geom ge;
    layout(w, h, m, ge);

    // The page slide is applied by the caller as a plain BitBlt offset. Doing it
    // here with SetViewportOrgEx only moved the GDI half of the drawing: GDI+
    // (round rects, icons, the filled cards) renders in device units and ignores
    // a DC viewport origin, so the glyphs slid while the cards stood still.
    int sv = SaveDC(dc);
    IntersectClipRect(dc, ge.body.l, ge.body.t, ge.body.r, ge.body.b);

    if (m.page == PAGE_TERMINAL) {
        if (!m.hasTerminal) paintEmptyState(dc, m, ge);
    } else if (m.page == PAGE_PROJECT) {
        if (!m.hasFolder) paintProjectEmpty(dc, m, ge);
        else paintTree(dc, m, ge);
    } else if (m.page == PAGE_INSTALL) {
        paintInstall(dc, m, ge);
    } else if (m.page == PAGE_DISTRO) {
        paintDistroPage(dc, m, ge);
    } else {
        paintSettings(dc, m, ge);
    }

    if (m.page == PAGE_PROJECT && m.hasFolder) {
        if (ed && ed->isOpen()) {
            paintEditorGdi(dc, m, ed, ge, caretOn);
        } else {
            int cy = (ge.ed.t + ge.ed.b) / 2;
            textAt(dc, LS(L"从右侧目录树选择一个文件打开"), g_hBody, kText2,
                   ge.ed.l, cy - 12, ge.ed.r - ge.ed.l, 24, 1);
        }
    }

    RestoreDC(dc, sv);
}

void Ui::paintOverlay(HDC dc, int w, int h, const UiModel& m) {
    Geom ge;
    layout(w, h, m, ge);

    if (m.distroMenuOpen || m_aMenu > 0.002) paintDistroMenu(dc, w, h, m, ge);
    paintToast(dc, w, h, m, ge);
}

void Ui::paint(HDC dc, int w, int h, const UiModel& m, const Editor* ed, bool caretOn) {
    applyTheme(m.theme);

    if (m.page != PAGE_TERMINAL || !m.hasTerminal) {
        Geom ge;
        layout(w, h, m, ge);
        Graphics g(dc);
        SolidBrush bg(kWindowBg);
        g.FillRectangle(&bg, (REAL)0, (REAL)0, (REAL)w, (REAL)h);
    }

    paintContent(dc, w, h, m, ed, caretOn);
    paintChrome(dc, w, h, m);
    paintOverlay(dc, w, h, m);
}

UiClick Ui::hitTest(int w, int h, int x, int y, const UiModel& m) {
    UiClick c;
    Geom ge;
    layout(w, h, m, ge);
    double s = m_dpi / 96.0;

    if (m.distroMenuOpen) {
        for (size_t i = 0; i < ge.menuRows.size(); ++i) {
            if (!ge.menuRows[i].has(x, y)) continue;

            c.index = (int)i;
            int kind = ge.menuKind[i];
            int idx  = ge.menuIndex[i];

            if (kind == 0) {
                if (idx >= 0 && idx < (int)m.installed.size()) {
                    c.action = UI_SELECT_DISTRO;
                    c.arg = m.installed[(size_t)idx];
                }
            } else {
                if (idx >= 0 && idx < (int)m.online.size()) {
                    c.arg = m.online[(size_t)idx].id;
                    c.action = m.online[(size_t)idx].installed ? UI_SELECT_DISTRO
                                                               : UI_INSTALL_DISTRO;
                }
            }
            return c;
        }

        if (ge.menuPanel.has(x, y)) {
            c.action = UI_NONE;
            return c;
        }

        c.action = UI_DISTRO_MENU;
        return c;
    }

    if (ge.btnClose.has(x, y)) { c.action = UI_CLOSE; return c; }
    if (ge.btnMax.has(x, y)) { c.action = UI_MAXIMIZE; return c; }
    if (ge.btnMin.has(x, y)) { c.action = UI_MINIMIZE; return c; }
    // 发行版按钮也画在标题栏里，「整条标题栏 = 拖动窗口」的判断必须先让开一步，
    // 否则它被 UI_DRAG 吃掉：点不动、悬停也不亮。
    if (ge.distroBtn.w() > 0 && ge.distroBtn.has(x, y)) {
        c.action = UI_DISTRO_MENU;
        return c;
    }
    if (ge.title.has(x, y)) { c.action = UI_DRAG; return c; }

    for (int i = 0; i < 4; ++i) {
        if (!ge.nav[i].has(x, y)) continue;
        c.action = (i == 0) ? UI_NAV_TERMINAL : (i == 1 ? UI_NAV_PROJECT
                                               : (i == 2 ? UI_NAV_DISTRO : UI_NAV_SETTINGS));
        c.index = i;
        return c;
    }

    if (!ge.content.has(x, y)) {
        c.action = UI_NONE;
        return c;
    }

    if (m.page == PAGE_TERMINAL) {
        if (m.instLocked) return c;

        // 启用流程进行中：整页不再响应启用点击，避免连点起一堆提权进程。
        if (m.fixStage == 1 || m.fixStage == 2) return c;

        // 装完了等重启：只认「立即重启」那一个按钮
        if (m.fixStage == 3) {
            if (ge.fixReboot.has(x, y)) c.action = UI_FIX_REBOOT;
            return c;
        }

        if (m.wslMissing) {
            // 原来的写法是整个终端区都算按钮，点空白处也会弹 UAC。
            // 这里收回成只有按钮本身可点。
            if (ge.emptyBtn.has(x, y)) c.action = UI_ENABLE_WSL;
        }
        else if (!m.hasTerminal && ge.emptyBtn.has(x, y)) c.action = UI_EMPTY_INSTALL;
        return c;
    }

    if (m.page == PAGE_SETTINGS) {
        if (ge.setFontDec.has(x, y)) c.action = UI_SET_FONT_DEC;
        else if (ge.setFontInc.has(x, y)) c.action = UI_SET_FONT_INC;
        else if (ge.setTermFontDec.has(x, y)) c.action = UI_SET_TERMFONT_DEC;
        else if (ge.setTermFontInc.has(x, y)) c.action = UI_SET_TERMFONT_INC;
        else if (ge.setLight.has(x, y)) c.action = UI_SET_THEME_LIGHT;
        else if (ge.setDark.has(x, y)) c.action = UI_SET_THEME_DARK;
        else if (ge.setTermAuto.has(x, y)) c.action = UI_SET_TERM_AUTO;
        else if (ge.setTermPty.has(x, y)) c.action = UI_SET_TERM_PTY;
        else if (ge.setTermPipe.has(x, y)) c.action = UI_SET_TERM_PIPE;
        else if (ge.setLangAuto.has(x, y)) c.action = UI_SET_LANG_AUTO;
        else if (ge.setLangZh.has(x, y)) c.action = UI_SET_LANG_ZH;
        else if (ge.setLangEn.has(x, y)) c.action = UI_SET_LANG_EN;
        else if (ge.setFix.has(x, y)) c.action = UI_FIX_WSL;
        else if (ge.setRecheck.has(x, y)) c.action = UI_RECHECK;
        return c;
    }

    if (m.page == PAGE_INSTALL) {
        bool busy = (m.instStage == INST_RESOLVE || m.instStage == INST_DOWNLOAD ||
                     m.instStage == INST_IMPORT || m.instStage == INST_VERIFY);
        if (busy) {
            if (m.instCancelable && ge.instCancel.has(x, y)) c.action = UI_INSTALL_CANCEL;
            return c;
        }
        if (m.instStage != INST_IDLE && ge.instRetry.has(x, y)) {
            c.action = UI_INSTALL_RETRY;
            return c;
        }
        if (ge.instClose.has(x, y)) c.action = UI_INSTALL_CLOSE;
        return c;
    }

    if (m.page == PAGE_DISTRO) {
        if (ge.dRefresh.has(x, y)) { c.action = UI_DISTRO_REFRESH; return c; }

        for (size_t i = 0; i < ge.dInstRows.size(); ++i) {
            const Box& b = ge.dInstRows[i];
            if (!b.has(x, y)) continue;

            int rowIdx = (int)i + ge.dInstFirst;
            if (rowIdx < 0 || rowIdx >= (int)m.installed.size()) break;
            c.arg = m.installed[(size_t)rowIdx];

            int bw = (int)(66 * s + 0.5);
            int bh = (int)(24 * s + 0.5);
            int by = b.t + (b.h() - bh) / 2;
            int rx = b.r;

            rx -= (int)(6 * s + 0.5);
            if (x >= rx - bw && x < rx && y >= by && y < by + bh) {
                c.action = UI_DISTRO_OPEN;
                return c;
            }
            rx -= bw + (int)(6 * s + 0.5);
            if (x >= rx - bw && x < rx && y >= by && y < by + bh) {
                c.action = UI_DISTRO_DEFAULT;
                return c;
            }
            rx -= bw + (int)(6 * s + 0.5);
            if (x >= rx - bw && x < rx && y >= by && y < by + bh) {
                c.action = UI_DISTRO_UNREGISTER;
                return c;
            }
            return c;
        }

        for (size_t i = 0; i < ge.dOnlineRows.size(); ++i) {
            if (!ge.dOnlineRows[i].has(x, y)) continue;

            int oi = ge.dOnlineFirst + (int)i;
            if (oi < 0 || oi >= (int)m.online.size()) break;

            int bw = (int)(78 * s + 0.5);
            int bh = (int)(24 * s + 0.5);
            int by = ge.dOnlineRows[i].t + (ge.dOnlineRows[i].h() - bh) / 2;
            int bx = ge.dOnlineRows[i].r - (int)(14 * s + 0.5) - bw;

            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                c.action = UI_DISTRO_INSTALL;
                c.arg = m.online[(size_t)oi].id;
                return c;
            }
            return c;
        }

        return c;
    }

    if (m.page == PAGE_PROJECT) {
        if (!m.hasFolder) {
            if (ge.emptyBtn.has(x, y)) c.action = UI_OPEN_FOLDER;
            return c;
        }

        if (ge.treeUp.has(x, y)) { c.action = UI_TREE_UP; return c; }

        for (size_t i = 0; i < ge.treeRows.size(); ++i) {
            if (ge.treeRows[i].has(x, y)) {
                c.action = UI_TREE_ENTRY;
                c.index = m_treeScroll + (int)i;
                return c;
            }
        }

        if (ge.edNew.has(x, y)) { c.action = UI_EDITOR_NEW; return c; }
        if (ge.edSave.has(x, y)) { c.action = UI_EDITOR_SAVE; return c; }
        if (ge.edClose.has(x, y)) { c.action = UI_EDITOR_CLOSE; return c; }
        if (ge.edArea.has(x, y)) { c.action = UI_EDITOR_CLICK; return c; }

        c.action = UI_NONE;
        return c;
    }

    c.action = UI_NONE;
    return c;
}

bool Ui::updateHover(int w, int h, int x, int y, const UiModel& m) {
    Geom ge;
    layout(w, h, m, ge);

    int nav = -1, btn = 0, menu = -1, tree = -1;

    if (m.distroMenuOpen) {
        for (size_t i = 0; i < ge.menuRows.size(); ++i) {
            if (ge.menuRows[i].has(x, y)) { menu = (int)i; break; }
        }
    }

    if (menu < 0) {
        if (ge.btnMin.has(x, y)) btn = 1;
        else if (ge.btnMax.has(x, y)) btn = 2;
        else if (ge.btnClose.has(x, y)) btn = 3;

        for (int i = 0; i < 4; ++i) {
            if (ge.nav[i].has(x, y)) { nav = i; break; }
        }

        // 发行版按钮在标题栏里，不在 ge.content 内，得单独判一次。
        if (ge.distroBtn.w() > 0 && ge.distroBtn.has(x, y)) {
            btn = 9;
        } else if (ge.content.has(x, y)) {
            if (m.page == PAGE_TERMINAL) {
                if (m.fixStage == 3) {
                    if (ge.fixReboot.has(x, y)) btn = 4;
                }
                else if (m.fixStage != 1 && m.fixStage != 2 &&
                         m.wslMissing && ge.emptyBtn.has(x, y)) {
                    btn = 4;
                }
                else if (!m.wslMissing && !m.hasTerminal && ge.emptyBtn.has(x, y)) {
                    btn = 4;
                }
            } else if (m.page == PAGE_PROJECT) {
                if (!m.hasFolder) {
                    if (ge.emptyBtn.has(x, y)) btn = 4;
                } else {
                    if (ge.treeUp.has(x, y)) btn = 8;
                    else if (ge.edNew.has(x, y)) btn = 7;
                    else if (ge.edSave.has(x, y)) btn = 5;
                    else if (ge.edClose.has(x, y)) btn = 6;

                    if (nav < 0 && btn == 0) {
                        for (size_t i = 0; i < ge.treeRows.size(); ++i) {
                            if (ge.treeRows[i].has(x, y)) { tree = (int)i; break; }
                        }
                    }
                }
            } else if (m.page == PAGE_SETTINGS) {
                if (ge.setFontDec.has(x, y)) btn = 11;
                else if (ge.setFontInc.has(x, y)) btn = 12;
                else if (ge.setTermFontDec.has(x, y)) btn = 22;
                else if (ge.setTermFontInc.has(x, y)) btn = 23;
                else if (ge.setLight.has(x, y)) btn = 15;
                else if (ge.setDark.has(x, y)) btn = 16;
                else if (ge.setTermAuto.has(x, y)) btn = 17;
                else if (ge.setTermPty.has(x, y)) btn = 18;
                else if (ge.setTermPipe.has(x, y)) btn = 19;
                else if (ge.setLangAuto.has(x, y)) btn = 24;
                else if (ge.setLangZh.has(x, y)) btn = 25;
                else if (ge.setLangEn.has(x, y)) btn = 26;
                else if (ge.setFix.has(x, y)) btn = 20;
                else if (ge.setRecheck.has(x, y)) btn = 21;
            } else if (m.page == PAGE_INSTALL) {
                bool busy = (m.instStage == INST_RESOLVE || m.instStage == INST_DOWNLOAD ||
                             m.instStage == INST_IMPORT || m.instStage == INST_VERIFY);
                if (busy && m.instCancelable && ge.instCancel.has(x, y)) btn = 22;
                else if (!busy && m.instStage != INST_IDLE && ge.instRetry.has(x, y)) btn = 23;
                else if (ge.instClose.has(x, y)) btn = 10;
            } else if (m.page == PAGE_DISTRO) {
                if (ge.dRefresh.has(x, y)) btn = 15;
                else if (ge.dList.has(x, y)) {
                    int ri = -1;
                    int fromInst = 0;
                    for (size_t i = 0; i < ge.dInstRows.size(); ++i) {
                        if (ge.dInstRows[i].has(x, y)) { ri = (int)i; fromInst = 1; break; }
                    }
                    if (ri < 0) {
                        for (size_t i = 0; i < ge.dOnlineRows.size(); ++i) {
                            if (ge.dOnlineRows[i].has(x, y)) { ri = (int)i; fromInst = 0; break; }
                        }
                    }
                    tree = (ri >= 0) ? (ri + (fromInst ? 16 : 0)) : -1;
                }
            }
        }
    }

    bool changed = !(nav == m_hoverNav && btn == m_hoverBtn &&
                     menu == m_hoverMenu && tree == m_hoverTree);

    m_hoverNav = nav;
    m_hoverBtn = btn;
    m_hoverMenu = menu;
    m_hoverTree = tree;
    syncHoverTargets();
    return changed;
}

void Ui::syncHoverTargets() {
    for (int i = 0; i < 64; ++i) m_hoverTgt[i] = 0.0;
    setHoverTarget(slotForNav(m_hoverNav), true);
    setHoverTarget(slotForBtn(m_hoverBtn), true);
    setHoverTarget(slotForMenu(m_hoverMenu), true);
    if (m_hoverTree >= 16) setHoverTarget(slotForInstRow(m_hoverTree - 16), true);
    else setHoverTarget(slotForTree(m_hoverTree), true);
}

}
