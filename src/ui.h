#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "editor.h"
#include "wsl.h"

namespace wslterm {

enum UiPage {
    PAGE_TERMINAL = 0,
    PAGE_PROJECT  = 1,
    PAGE_DISTRO   = 2,
    PAGE_SETTINGS = 3,
    PAGE_INSTALL  = 4
};

enum UiAction {
    UI_NONE = 0,
    UI_DRAG,
    UI_MINIMIZE,
    UI_MAXIMIZE,
    UI_CLOSE,
    UI_NAV_TERMINAL,
    UI_NAV_PROJECT,
    UI_NAV_DISTRO,
    UI_NAV_SETTINGS,
    UI_DISTRO_MENU,
    UI_SELECT_DISTRO,
    UI_INSTALL_DISTRO,
    UI_EMPTY_INSTALL,
    UI_OPEN_FOLDER,
    UI_TREE_UP,
    UI_TREE_ENTRY,
    UI_EDITOR_CLOSE,
    UI_EDITOR_SAVE,
    UI_EDITOR_NEW,
    UI_EDITOR_CLICK,
    UI_TREE_SCROLL,
    UI_SET_FONT_DEC,
    UI_SET_FONT_INC,
    UI_SET_TERMFONT_DEC,
    UI_SET_TERMFONT_INC,
    UI_SET_THEME_LIGHT,
    UI_SET_THEME_DARK,
    UI_SET_TERM_AUTO,
    UI_SET_LANG_AUTO,
    UI_SET_LANG_ZH,
    UI_SET_LANG_EN,
    UI_SET_TERM_PTY,
    UI_SET_TERM_PIPE,
    UI_FIX_WSL,
    UI_RECHECK,
    UI_INSTALL_CANCEL,
    UI_INSTALL_RETRY,
    UI_INSTALL_CLOSE,
    UI_DISTRO_OPEN,
    UI_DISTRO_DEFAULT,
    UI_DISTRO_UNREGISTER,
    UI_DISTRO_INSTALL,
    UI_DISTRO_REFRESH,
    UI_DISTRO_SCROLL,
    UI_ENABLE_WSL,
    UI_COPY_WSL_CMD,
    UI_FIX_REBOOT        // 组件装好了，让用户一键重启
};

enum InstallStage {
    INST_IDLE = 0,
    INST_RESOLVE,
    INST_DOWNLOAD,
    INST_IMPORT,
    INST_UNREGISTER,
    INST_VERIFY,
    INST_DONE,
    INST_FAILED,
    INST_CANCELED
};

struct UiClick {
    UiAction     action;
    std::wstring arg;
    int          index;

    UiClick() : action(UI_NONE), index(-1) {}
};

struct TreeRow {
    std::wstring name;
    std::wstring path;
    int          depth;
    bool         isDir;
    bool         expanded;
    bool         isOpenFile;

    TreeRow() : depth(0), isDir(false), expanded(false), isOpenFile(false) {}
};

struct UiModel {
    std::wstring title;
    int          page;

    bool         hasTerminal;
    bool         wslMissing;
    int          wslState;
    bool         installing;
    bool         checking;
    bool         loadingOnline;
    bool         distroMenuOpen;
    bool         distroBusy;
    bool         distroLoading;
    std::wstring activeDistro;
    std::vector<std::wstring> installed;
    std::vector<std::wstring> brokenDistros;
    std::vector<DistroEntry>  online;
    std::vector<DistroStatus> statuses;

    // 启用 WSL 组件的进度。wsl --install 要跑UAC 提权、系统组件启用甚至重启，
    // 全程可能好几分钟，只给一个 2.6 秒的 toast 等于什么都没说。
    // 阶段：0 空闲 / 1 等待 UAC / 2 执行中 / 3 需要重启 / 4 失败。
    int          fixStage;
    unsigned long fixTick;        // 进入当前阶段的时刻，用于算已用时
    unsigned long fixStartTick;   // 整个流程的开始时刻
    int          fixElapsed;      // 已用秒数，由定时器刷新
    std::wstring fixMsg;

    int          distroView;
    int          distroScroll;
    std::wstring distroMsg;

    // WSLg（Linux 图形界面）状态。msrdc 加载 rdclientax.dll 失败时会无限弹
    // 「无法加载远程桌面服务 ActiveX 控件」，和终端能不能用毫无关系。
    // 探测到坏掉就自动写 .wslconfig 关掉它——代价只是不能跑 Linux GUI 程序。
    int          wslg;
    bool         wslgFixed;      // 这一轮刚替用户关掉了，重启 WSL 后才生效

    bool         hasFolder;
    std::wstring folderPath;
    std::vector<TreeRow> tree;

    bool         editorOpen;
    std::wstring editorName;
    std::wstring editorLang;
    bool         editorDirty;
    bool         unregisterBusy;

    int          fontSize;
    int          termFontSize;
    int          theme;
    std::wstring wslPath;
    std::wstring wslVersion;
    std::wstring hint;

    int          terminalMode;
    int          activeMode;
    int          lang;

    int          instStage;
    int          instPercent;
    unsigned long long instGot;
    unsigned long long instTotal;
    std::wstring instName;
    std::wstring instStageText;
    std::wstring instDetail;
    std::wstring instUrl;
    std::wstring instErr;
    std::wstring instHint;
    std::vector<std::wstring> instLog;
    bool         instCancelable;
    bool         instLocked;

    std::wstring toast;
    unsigned long toastTick;

    UiModel();
};

struct EdLayout {
    int l, t, r, b;
    int toolH;
    int gutterW;
    int charW;
    int lineH;
    int viewLines;
    int viewCols;

    EdLayout() : l(0), t(0), r(0), b(0), toolH(0), gutterW(0),
                 charW(8), lineH(16), viewLines(1), viewCols(1) {}
};

class Ui {
public:
    Ui();
    ~Ui();

    bool init(int dpi);
    void shutdown();

    void setDpi(int dpi);
    int  dpi() const { return m_dpi; }
    int  titleH() const { return m_titleH; }
    int  sideW() const { return m_sideW; }
    int  barH() const { return m_barH; }

    void prepare(int fontSize);
    void setUiFontSize(int fontSize);

    void contentRect(int w, int h, int& l, int& t, int& r, int& b,
                     int page) const;
    EdLayout edLayout(int w, int h) const;
    int treeW() const { return m_treeW; }

    void paint(HDC dc, int w, int h, const UiModel& m,
               const Editor* ed, bool caretOn);

    void paintChrome(HDC dc, int w, int h, const UiModel& m);
    void paintContent(HDC dc, int w, int h, const UiModel& m,
                      const Editor* ed, bool caretOn);
    void paintOverlay(HDC dc, int w, int h, const UiModel& m);

    void beginPageTransition(int fromPage, int toPage);
    void resetBarAnim() { m_barInit = false; m_aBarPct = 0.0; }
    bool   transitionActive() const { return m_pageAnim; }
    int    pageSlideX() const;
    double pageFade() const;
    void   paintVeil(HDC dc, int l, int t, int r, int b, int alpha, int theme);
    bool   animActive() const;
    void   tickAnim(int dtMs, const UiModel& m);

    static void applyTheme(int theme);

    UiClick hitTest(int w, int h, int x, int y, const UiModel& m);
    bool    updateHover(int w, int h, int x, int y, const UiModel& m);
    void    clearHover();
    void    notePress(int hoverId);
    int     hoverBtnId() const { return m_hoverBtn; }

    int  treeMaxScroll(int h, const UiModel& m) const;
    int  treeScroll() const { return m_treeScroll; }
    void treeScrollBy(int d, int h, const UiModel& m);

    bool menuScrollBy(int delta, int w, int h, const UiModel& m);
    void resetMenuScroll() { m_menuScroll = 0; }
    int  menuMaxScroll() const { return m_menuMaxScroll; }
    int  menuShownCount(int w, int h, const UiModel& m);

    bool distroScrollBy(int delta, int w, int h, const UiModel& m);
    void resetDistroScroll() { m_distroScroll = 0; }
    int  distroMaxScroll(int w, int h, const UiModel& m) const;

private:
    struct Box {
        int l, t, r, b;
        bool has(int x, int y) const { return x >= l && x < r && y >= t && y < b; }
        int  w() const { return r - l; }
        int  h() const { return b - t; }
    };

    struct Geom {
        Box title;
        Box btnMin, btnMax, btnClose;
        Box side;
        Box nav[4];
        Box content;
        Box body;
        Box bar;
        Box distroBtn;
        Box menuPanel;
        Box menuHdr1, menuHdr2, menuSep, menuHint;
        std::vector<Box> menuRows;
        std::vector<int> menuKind;
        std::vector<int> menuIndex;
        int menuTotal;
        int menuFirst;
        int menuShown;
        Box emptyBtn;
        Box fixBar, fixReboot;      // 启用组件的进度条 / 一键重启按钮
        std::vector<Box> treeRows;
        Box treeUp;
        Box edTool, edClose, edSave, edNew, edArea;
        EdLayout ed;
        Box setList, setSide;
        Box setFontDec, setFontInc, setTermFontDec, setTermFontInc;
        Box setLight, setDark;
        Box setTermAuto, setTermPty, setTermPipe;
        Box setLangAuto, setLangZh, setLangEn;
        Box setFix, setRecheck;
        Box instBar, instCancel, instRetry, instClose, instLog;
        Box dList, dHdrInst, dHdrOnline, dRefresh, dHint;
        std::vector<Box> dInstRows;
        std::vector<Box> dOnlineRows;
        int dOnlineFirst;
        int dOnlineShown;
        int dInstFirst;
        int dInstShown;
        int dTotal;

        Geom()
            : title(), btnMin(), btnMax(), btnClose(), side(), content(),
              body(), bar(), distroBtn(), menuPanel(), menuHdr1(), menuHdr2(),
              menuSep(), menuHint(), menuTotal(0), menuFirst(0), menuShown(0),
              emptyBtn(), treeUp(), edTool(), edClose(), edSave(), edNew(),
              edArea(), ed(), setList(), setSide(), setFontDec(), setFontInc(),
              setTermFontDec(), setTermFontInc(),
                              setLight(), setDark(), setTermAuto(), setTermPty(),
                              setTermPipe(), setLangAuto(), setLangZh(), setLangEn(),
                              setFix(), setRecheck(), instBar(), instCancel(),
                              instRetry(), instClose(), instLog(),
                              dList(), dHdrInst(), dHdrOnline(), dRefresh(), dHint(),
              dOnlineFirst(0), dOnlineShown(0), dInstFirst(0),
              dInstShown(0), dTotal(0) {
            for (int i = 0; i < 4; ++i) nav[i] = Box{ 0, 0, 0, 0 };
        }
    };

    void layout(int w, int h, const UiModel& m, Geom& ge) const;
    // 设置页两栏：左侧设置列表固定 560，右侧放说明卡片；窗口不够宽时左右平分。
    int  settingsColumns(const Geom& ge, int& listW, int& sideW) const;
    void ensureEditorFont(int dpi);
    double hoverOf(int slot) const;
    void   setHoverTarget(int slot, bool on);
    void   syncHoverTargets();

    void paintTitleBar(HDC dc, int w, const UiModel& m, const Geom& ge);
    void paintSidebar(HDC dc, int h, const UiModel& m, const Geom& ge);
    void paintTermBar(HDC dc, int w, const UiModel& m, const Geom& ge);
    void paintDistroMenu(HDC dc, int w, int h, const UiModel& m, const Geom& ge);
    void paintEmptyState(HDC dc, const UiModel& m, const Geom& ge);
    void paintProjectEmpty(HDC dc, const UiModel& m, const Geom& ge);
    void paintTree(HDC dc, const UiModel& m, const Geom& ge);
    void paintSettings(HDC dc, const UiModel& m, const Geom& ge);
    void paintDistroPage(HDC dc, const UiModel& m, const Geom& ge);
    void paintInstall(HDC dc, const UiModel& m, const Geom& ge);
    void paintToast(HDC dc, int w, int h, const UiModel& m, const Geom& ge);
    void paintEditorGdi(HDC dc, const UiModel& m, const Editor* ed,
                        const Geom& ge, bool caretOn);

    int m_dpi;
    int m_titleH;
    int m_sideW;
    int m_barH;
    int m_navH;
    int m_pad;
    int m_radius;
    int m_treeW;
    int m_rowH;
    int m_menuW;

    HFONT m_hTitle;
    HFONT m_hBody;
    HFONT m_hBold;
    HFONT m_hSmall;
    HFONT m_hBig;

    HFONT m_edFont;
    HFONT m_edFontBold;
    int   m_edFontH;
    int   m_edCharW;
    int   m_edLineH;
    int   m_edFontSize;

    int  m_hoverNav;
    int  m_hoverBtn;
    int  m_hoverMenu;
    int  m_hoverTree;
    int  m_treeScroll;
    mutable int m_menuScroll;
    mutable int m_menuMaxScroll;
    mutable int m_distroScroll;
    mutable int m_distroMaxScroll;
    bool m_inited;

    int  m_uiFontPx;

    double m_aHover[64];
    double m_hoverTgt[64];
    double m_hoverVel[64];
    double m_aPress[64];
    double m_navPillVel;
    double m_menuVel;
    double m_barVel;
    double m_aNavPill;
    bool   m_navPillInit;
    double m_aMenu;
    double m_aBarPct;
    bool   m_barInit;
    mutable bool   m_menuTgt;
    mutable double m_barTgt;
    bool   m_pageAnim;
    double m_pageT;
    int    m_pageFrom;
    int    m_pageTo;
};

}
