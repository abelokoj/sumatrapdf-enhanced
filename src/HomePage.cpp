/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/AutoWin.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/Win.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/win/WebView.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "gui/VirtHost.h"
#include "gui/win/TabsCtrl.h"

#include "Settings.h"
#include "DocController.h"
#include "SumatraConfig.h"
#include "FileHistory.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Commands.h"
#include "Accelerators.h"
#include "CommandPalette.h"
#include "FilterHighlightDraw.h"
#include "FileThumbnails.h"
#include "Menu.h"
#include "Translations.h"
#include "Version.h"
#include "Theme.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "DarkMode.h"
#include "SvgIcons.h"
#include "PagePosition.h"
#include "HomeFileIconLayout.h"
#include "HomePage.h"
#include "Vocabulary.h"
#include "VocabularyDialog.h"
#if IS_DEBUG
#include "EngineBase.h"
#include "RenderCache.h"
#endif

// how the shared tip code (TipText.cpp) opens a url link
static void OpenTipUrl(Str url) {
    // documentation links open in the embedded manual browser
    if (!MaybeLaunchDocumentation(url)) {
        SumatraLaunchBrowser(url);
    }
}

// what the shared tip code (TipText.cpp) knows about our commands: it names
// them in (Key/Cmd...) and in link targets, but knows nothing about the command
// table itself
struct SumatraCommandsContext : CommandsContext {
    TempStr GetCommandShortcutTemp(Str cmdName) override {
        int cmdId = GetCommandIdByName(cmdName);
        if (cmdId <= 0) {
            return {}; // not a command: the markup stays literal text
        }
        TempStr accel = AppendAccelKeyToMenuStringTemp(StrL(""), cmdId);
        if (len(accel) == 0 || !*accel.s) {
            return str::DupTemp(cmdName); // a command, but unbound
        }
        // AppendAccelKeyToMenuStringTemp prepends 	, skip it
        if (accel.s[0] == '	') {
            return Str(accel.s + 1);
        }
        return accel;
    }

    // `cmd` can carry arguments (e.g. "CmdFixDefaultApp .pdf"); those go through
    // CreateCommandFromDefinition so FrameOnCommand sees a CustomCommand with args
    void ExecuteCommand(HWND hwnd, Str cmd) override {
        CustomCommand* custom = CreateCommandFromDefinition(cmd);
        if (custom) {
            HwndSendCommand(hwnd, custom->id);
            return;
        }
        int cmdId = GetCommandIdByName(cmd);
        if (cmdId > 0) {
            HwndSendCommand(hwnd, cmdId);
        }
    }
};

static SumatraCommandsContext gSumatraCommandsContext;

struct TipHookInstaller {
    TipHookInstaller() {
        gTipOpenUrl = OpenTipUrl;
        gCommandsContext = &gSumatraCommandsContext;
    }
};
static TipHookInstaller gTipHookInstaller;

#ifndef ABOUT_USE_LESS_COLORS
constexpr int kAboutLineOuterSize = 2;
#else
constexpr int kAboutLineOuterSize = 1;
#endif
constexpr int kAboutLineSepSize = 1;

// one tip per line; cmd/trans-dl.ts extracts each line for translation
static Str sumatraTips = StrL(R"tips(You can [customize scrollbar](CmdChangeScrollbar).
You can [customize keyboard shortcuts](Help/Customize-keyboard-shortcuts).
You can [customize toolbar](Help/Customize-toolbar).
Press (Kbd/(Key/CmdCommandPalette)) to open [command palette](CmdCommandPalette).
To open file from history open [command palette](CmdCommandPalette) with (Kbd/(Key/CmdCommandPalette)) and type (Kbd/#).
You can [extract text from PDF file](Help/Tool-x-extract-text-from-pdf).
You can [toggle menu bar](CmdToggleMenuBar) with (Kbd/(Key/CmdToggleMenuBar)).
You can [toggle toolbar](CmdToggleToolbar) with (Kbd/(Key/CmdToggleToolbar)).
You can [edit PDF annotations](Help/Editing-annotations).
You can enable [citation preview on hover](Help/Citation-hover-preview).
You can [have documents read aloud](Help/Read-Aloud).
You can [sign a PDF](Help/Sign-a-PDF).
You can [fill PDF forms](Help/Fill-PDF-forms).
You can [merge PDFs](Help/Merge-PDFs) and [reorder pages](Help/Reorder-PDF-pages).
You can [split a PDF](Help/Split-a-PDF).
You can [redact a PDF](Help/Redact-a-PDF).
You can [present a PDF](Help/Present-a-PDF) full screen.
You can [use SumatraPDF with LaTeX](Help/LaTeX-integration) for forward and inverse search.
You can [read comics and manga](Help/Comics-and-manga) right to left.
You can [bookmark pages as favorites](Help/Managing-favorites).
You can [chat with AI about a document](Help/AI-Chat-with-document).
You can [customize theme colors](Help/Customize-theme-colors).
You can [save a page region as an image](Help/Save-page-region-as-image).
You can [print selected pages](Help/Print-selected-pages).
)tips");

static Str sumatraPromos = StrL(R"promos(Try [Edna](https://edna.arslexis.io): a note taking web app for power users.
Try [MarkLexis](https://marklexis.arslexis.io): a bookmarking web application.
)promos");

static Str promoFromServer;

static void FreeHomeFileIcons();

// the tip markup, one line each; the selected one is parsed by the tip band
static StrVec gTipLines;
static StrVec gPromoLines;
static bool gTipsParsed = false;
static bool gSelectedIsPromo = false;
static int gSelectedTipIdx = -1;

static void CollectTipsFromString(Str src, StrVec* out) {
    StrVec lines;
    Split(&lines, src, StrL("\n"));
    for (int i = 0; i < len(lines); i++) {
        Str line = lines[i];
        if (str::IsEmptyOrWhiteSpace(line)) {
            continue;
        }
        out->Append(line);
    }
}

// the markup of the tip currently on show, {} when there is none
static Str SelectedTipLine() {
    if (!gSettings->showTips || gSelectedTipIdx < 0) {
        return {};
    }
    StrVec& v = gSelectedIsPromo ? gPromoLines : gTipLines;
    if (gSelectedTipIdx >= len(v)) {
        return {};
    }
    if (gSelectedIsPromo) {
        return v[gSelectedTipIdx];
    }
    // translated when shown, so a language change applies without re-parsing
    return str::JoinTemp(Tr("Tip:"), StrL(" "), Tr(v[gSelectedTipIdx]));
}

static void PickRandomTipOrPromo() {
    bool pickPromo = (len(gPromoLines) > 0) && (rand() % 100 < 30);
    if (pickPromo) {
        gSelectedIsPromo = true;
        gSelectedTipIdx = rand() % len(gPromoLines);
    } else if (len(gTipLines) > 0) {
        gSelectedIsPromo = false;
        gSelectedTipIdx = rand() % len(gTipLines);
    }
}

static void EnsureTipsParsed() {
    if (gTipsParsed) {
        return;
    }
    CollectTipsFromString(sumatraTips, &gTipLines);
    CollectTipsFromString(sumatraPromos, &gPromoLines);
    gTipsParsed = true;
    PickRandomTipOrPromo();
}

static void ClearHomeLayoutCache(MainWindow*);

void FreeHomePageTips() {
    if (gTipsParsed) {
        gTipLines.Reset();
        gPromoLines.Reset();
        gTipsParsed = false;
    }
    str::Free(promoFromServer);
    FreeHomeFileIcons();
    HomePageInvalidateLayoutCache();
}

static void PickAnotherRandomTip() {
    bool prevIsPromo = gSelectedIsPromo;
    int prev = gSelectedTipIdx;
    // keep picking until we get a different one
    int maxIter = 100;
    while (maxIter-- > 0) {
        PickRandomTipOrPromo();
        if (gSelectedIsPromo != prevIsPromo || gSelectedTipIdx != prev) {
            return;
        }
    }
}

constexpr Color kAboutBorderCol = kColBlack;

constexpr int kAboutLeftRightSpaceDx = 8;
constexpr int kAboutMarginDx = 10;
constexpr int kAboutBoxMarginDy = 6;
constexpr int kAboutTxtDy = 6;
constexpr int kAboutRectPadding = 8;

constexpr int kInnerPadding = 8;

static const Str kSumatraTxtFont = StrL("Segoe UI Semibold");
constexpr int kSumatraTxtFontSize = 48;

constexpr int kLayoutLtr = 0;

static ATOM gAtomAbout;
static HWND gHwndAbout;
static VirtRoot* gAboutRoot = nullptr;
static Tooltip* gAboutTooltip = nullptr;
static Str gClickedURL;

// one row of the About screen's two-column table
struct AboutRow {
    Str leftTxt;
    Str rightTxt;
    Str url;
};

static AboutRow gAboutRows[] = {
    // a null rightTxt means "the app version", filled in by Sync() because it
    // isn't known until runtime (32/64-bit, debug)
    {StrL("version"), {}, {}},
    {StrL("based on"), StrL("SumatraPDF " CURR_VERSION_STRA), {}},
    {StrL("built on"), StrL(__DATE__ " " __TIME__), {}},
    {StrL("manual"), StrL("SumatraPDF manual"), StrL("https://www.sumatrapdfreader.org/docs/SumatraPDF-documentation")},
    {StrL("version history"), StrL("What's new"), StrL("https://github.com/abelokoj/sumatrapdf-enhanced/releases")},
    {StrL("website"), StrL("SumatraPDF Enhanced"), StrL("https://github.com/abelokoj/sumatrapdf-enhanced")},
    {StrL("forums"), StrL("SumatraPDF forums"), StrL("https://github.com/sumatrapdfreader/sumatrapdf/discussions")},
    {StrL("licenses"), StrL("Various Open Source"),
     StrL("https://github.com/sumatrapdfreader/sumatrapdf/blob/master/AUTHORS")},
#ifdef GIT_COMMIT_ID_STR
    {StrL("last change"), StrL("git commit " GIT_COMMIT_ID_STR),
     StrL("https://github.com/sumatrapdfreader/sumatrapdf/commit/" GIT_COMMIT_ID_STR)},
#endif
#ifdef PRE_RELEASE_VER
    {StrL("a note"), StrL("Pre-release version, for testing only!"), {}},
#endif
#if IS_DEBUG
    {StrL("a note"), StrL("Debug version, for testing only!"), {}},
#endif
    {{}, {}, {}}};

// The About screen's two text columns: a Table (ILayout) whose left column
// is right-aligned and right column left-aligned. Rows with a url become
// VirtLink (owning the hit-testing, the hand cursor and the tooltip), the rest
// plain VirtText. Table is an ILayout child so ElementFromPoint walks the cells.
static Kind kindAboutCtrl = "aboutCtrl";

struct SumatraLogo;

struct AboutCtrl : VirtCtrl {
    // the two text columns; owned here (not a VirtCtrl child)
    Table* table = nullptr;
    // "Show frequently read", bottom right of the About page (not the window)
    VirtLink* showFreqRead = nullptr;
    // the colored app name on top of the box; hidden in the home-page dropdown
    SumatraLogo* logo = nullptr;
    // copies version / OS / machine info for bug reports (dialog and dropdown)
    VirtButton* copyInfoBtn = nullptr;
    bool hideLogo = false;

    // geometry, computed by UpdateLayout()
    Rect aboutRect;  // the framed box
    Size headerSize; // the "SumatraPDF" band on top of it
    int dividerX = 0;

    AboutCtrl();
    ~AboutCtrl() override;
    void Sync();
    void UpdateLayout(Rect clientRc);
    VirtText* LeftAt(int i);
    VirtText* RightAt(int i);
    void Paint(VirtPaintCtx&) override;
    void PaintChildren(VirtPaintCtx&) override;
    int LayoutChildCount() override;
    ILayout* LayoutChildAt(int) override;
};

static void OpenAboutUrl(VirtMouseEvent* ev) {
    auto* link = (VirtLink*)ev->target;
    OpenTipUrl(link->target);
}

void SetPromoString(Str s) {
    if (len(s) == 0) return;
    str::ReplaceWithCopy(&promoFromServer, s);
}

static TempStr GetAppVersionTemp() {
    TempStr s = str::DupTemp(StrL(ENHANCED_VERSION_STRA));
    if (IsProcess64()) {
        s = str::JoinTemp(s, StrL(" 64-bit"));
    } else {
        s = str::JoinTemp(s, StrL(" 32-bit"));
    }
    if (gIsDebugBuild) {
        s = str::JoinTemp(s, StrL(" (dbg)"));
    }
    return s;
}

#if IS_DEBUG
bool HomePage_UnitTestsVersion() {
    return str::StartsWith(GetAppVersionTemp(), StrL(ENHANCED_VERSION_STRA)) &&
           str::Eq(currentVersion, StrL(ENHANCED_VERSION_STRA)) &&
           str::Eq(gAboutRows[1].rightTxt, StrL("SumatraPDF " CURR_VERSION_STRA));
}
#endif

constexpr Color kCol1 = MkRgb(196, 64, 50);
constexpr Color kCol2 = MkRgb(227, 107, 35);
constexpr Color kCol3 = MkRgb(93, 160, 40);
constexpr Color kCol4 = MkRgb(69, 132, 190);
constexpr Color kCol5 = MkRgb(112, 115, 207);

static Kind kindSumatraLogo = "sumatraLogo";

// the app name centered in its bounds, each letter in a different color (so it
// can't be a VirtText). The version isn't part of it: it is the first row of
// the About table
struct SumatraLogo : VirtCtrl {
    PlatformFont* font = nullptr; // not owned
    bool homeIdentity = false;
    int maxWidth = 0;
    Pixmap* badge = nullptr;

    SumatraLogo();
    ~SumatraLogo() override;
    int BadgeSize();
    bool WrapTitle();
    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
};

SumatraLogo::SumatraLogo() {
    kind = kindSumatraLogo;
    flags |= vwfNoHitTest;
}

SumatraLogo::~SumatraLogo() {
    delete badge;
}

int SumatraLogo::BadgeSize() {
    return homeIdentity ? ClampI(PlatformFontLineHeight(font), UiScalePx(48), UiScalePx(96)) : 0;
}

bool SumatraLogo::WrapTitle() {
    return homeIdentity && maxWidth > 0 &&
           PlatformFontMeasureText(font, StrL(kAppDisplayName)).dx + BadgeSize() + UiScalePx(12 + kInnerPadding * 2) >
               maxWidth;
}

Size SumatraLogo::GetIdealSize() {
    Size sz = PlatformFontMeasureText(font, StrL(kAppDisplayName));
    if (WrapTitle()) {
        sz.dx = std::max(PlatformFontMeasureText(font, StrL("SumatraPDF")).dx,
                         PlatformFontMeasureText(font, StrL("Enhanced")).dx);
        sz.dy = PlatformFontLineHeight(font) * 2;
    }
    if (homeIdentity) {
        sz.dx += BadgeSize() + UiScalePx(12);
        sz.dy = std::max(sz.dy, BadgeSize());
    }
    sz.dy += UiScalePx(kAboutBoxMarginDy * 2);
    sz.dx += 2 * UiScalePx(kInnerPadding);
    return sz;
}

void SumatraLogo::Paint(VirtPaintCtx& ctx) {
    if (homeIdentity) {
        Rect content = ctx.bounds;
        content.SubLR(UiScalePx(kInnerPadding), UiScalePx(kInnerPadding));
        content.SubTB(UiScalePx(kAboutBoxMarginDy), UiScalePx(kAboutBoxMarginDy));
        int iconSize = BadgeSize();
        if (!badge || badge->width != iconSize) {
            delete badge;
            HICON icon = (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(GetAppIconID()), IMAGE_ICON,
                                           iconSize, iconSize, 0);
            badge = icon ? PixmapFromHICON(icon) : nullptr;
            if (icon) DestroyIcon(icon);
        }
        if (badge) ctx.gfx->DrawPixmap(badge, {content.x, content.y + (content.dy - iconSize) / 2, iconSize, iconSize});
        content.x += iconSize + UiScalePx(12);
        content.dx -= iconSize + UiScalePx(12);
        bool wrap = WrapTitle();
        Str name = wrap ? StrL("SumatraPDF") : StrL("SumatraPDF ");
        Size nameSize = PlatformFontMeasureText(font, name);
        Rect suffix = content;
        if (wrap) {
            content.dy = PlatformFontLineHeight(font);
            suffix.y += content.dy;
            suffix.dy = content.dy;
        } else {
            content.dx = nameSize.dx;
            suffix.x += nameSize.dx;
            suffix.dx -= nameSize.dx;
        }
        Color bg = ThemeMainWindowBackgroundColor();
        Color green = IsLightColor(bg) ? MkRgb(25, 128, 78) : MkRgb(66, 218, 133);
        Color accent = ThemeUsesHighContrastColors() ? ThemeWindowTextColor() : EnsureContrast(green, bg);
        ctx.gfx->DrawText(name, content, gfxTextVCenter, font, ThemeWindowTextColor());
        ctx.gfx->DrawText(StrL("Enhanced"), suffix, gfxTextVCenter, font, accent);
        return;
    }
    Size txtSize = PlatformFontMeasureText(font, StrL(kAppDisplayName));
    Rect r = ctx.bounds;
    Rect text{r.x + ((r.dx - txtSize.dx) / 2), r.y + ((r.dy - txtSize.dy) / 2), txtSize.dx, txtSize.dy};
    Str name = StrL("SumatraPDF ");
    Size nameSize = PlatformFontMeasureText(font, name);
    Rect suffix = text;
    suffix.x += nameSize.dx;
    suffix.dx -= nameSize.dx;
    text.dx = nameSize.dx;
    ctx.gfx->DrawText(name, text, gfxTextVCenter, font, ThemeWindowTextColor());
    ctx.gfx->DrawText(StrL("Enhanced"), suffix, gfxTextVCenter, font, ThemeBrandColor());
}

static TempStr TrimGitTemp(Str s) {
    if (gitCommidId && str::EndsWith(s, gitCommidId)) {
        int sLen = len(s);
        int gitLen = len(gitCommidId);
        return str::DupTemp(Str(s.s, sLen - gitLen - 7));
    }
    return s;
}

// the About screen's virtual controls for one HWND. Positions come from
// AboutCtrl::UpdateLayout(), so the root must not run a layout of its own
static AboutCtrl* EnsureAboutCtrl(VirtRoot** rootPtr, HWND hwnd, Rect clientRc) {
    VirtRoot* root = *rootPtr;
    if (!root) {
        root = new VirtRoot(hwnd);
        *rootPtr = root;
    }
    if (!IsVirtCtrlOfKind(root->owned, kindAboutCtrl)) {
        root->SetChild(new AboutCtrl());
    }
    root->bounds = clientRc;
    root->needsLayout = false;
    auto* about = (AboutCtrl*)root->owned;
    about->SetBounds(clientRc);
    return about;
}

static int AboutRowCount() {
    int n = 0;
    for (AboutRow* el = gAboutRows; el->leftTxt; el++) {
        n++;
    }
    return n;
}

AboutCtrl::AboutCtrl() {
    kind = kindAboutCtrl;
    flags |= vwfNoHitTest;
    table = new Table();
    logo = new SumatraLogo();
    AddChild(logo);
}

AboutCtrl::~AboutCtrl() {
    delete table;
    table = nullptr;
}

VirtText* AboutCtrl::LeftAt(int i) {
    return (VirtText*)table->GetCell(i, 0);
}

VirtText* AboutCtrl::RightAt(int i) {
    return (VirtText*)table->GetCell(i, 1);
}

// framed box: title band, body, then children, then the column divider
void AboutCtrl::Paint(VirtPaintCtx& ctx) {
    Rect rect = aboutRect;
    if (rect.IsEmpty()) {
        return;
    }
    Color lineCol = ThemeWindowTextColor();
    Color bgCol = ThemeMainWindowBackgroundColor();
    ctx.gfx->FillRect(rect, bgCol);

#ifndef ABOUT_USE_LESS_COLORS
    if (headerSize.dy > 0) {
        Rect titleRect(rect.TL(), headerSize);
        ctx.gfx->DrawRect({rect.x, rect.y + kAboutLineOuterSize, rect.dx, titleRect.dy}, lineCol, kAboutLineOuterSize);
        ctx.gfx->DrawRect({rect.x, rect.y + titleRect.dy, rect.dx, rect.dy - titleRect.dy}, lineCol,
                          kAboutLineOuterSize);
    } else {
        ctx.gfx->DrawRect(rect, lineCol, kAboutLineOuterSize);
    }
#endif
}

// paint logo (VirtCtrl children) and the table's VirtText / VirtLink cells
void AboutCtrl::PaintChildren(VirtPaintCtx& ctx) {
    VirtCtrl::PaintChildren(ctx);
    if (table) {
        for (int i = 0; i < table->LayoutChildCount(); i++) {
            VirtCtrl* v = table->LayoutChildAt(i)->AsVirtCtrl();
            if (!v) {
                continue;
            }
            v->SetRoot(root);
            // cells were given absolute window coords by Table::SetBounds
            v->PaintTree(ctx.gfx, {0, 0}, ctx.clip);
        }
    }

    if (table && !table->lastBounds.IsEmpty()) {
        Color lineCol = ThemeWindowTextColor();
        Rect t = table->lastBounds;
        ctx.gfx->DrawLine({dividerX, t.y, 0, t.dy}, lineCol, kAboutLineSepSize);
    }
}

// table is not a VirtCtrl child; expose it so ElementFromPoint walks the cells
int AboutCtrl::LayoutChildCount() {
    return VirtCtrl::LayoutChildCount() + (table ? 1 : 0);
}

ILayout* AboutCtrl::LayoutChildAt(int i) {
    int n = VirtCtrl::LayoutChildCount();
    if (i < n) {
        return VirtCtrl::LayoutChildAt(i);
    }
    return table;
}

// build the table once, then keep text, fonts and colors in step with the theme
// and the DPI. Sizing happens in UpdateLayout(), which measures what we set here
void AboutCtrl::Sync() {
    int n = AboutRowCount();
    bool canAccessDisk = CanAccessDisk();
    if (table->rows != n) {
        table->SetSize(n, 2);
        for (int i = 0; i < n; i++) {
            AboutRow* el = &gAboutRows[i];
            TableCell& left = table->SetCell(i, 0, new VirtText(el->leftTxt));
            // the left column is flush against the divider line
            left.alignH = CrossAxisAlign::CrossEnd;
            left.alignV = CrossAxisAlign::CrossCenter;

            VirtText* rightTxt;
            if (el->url) {
                auto* link = new VirtLink(el->rightTxt);
                link->SetTarget(el->url);
                link->SetTooltip(el->url);
                link->withUnderline = true;
                // the underline sat 3px above the bottom of the text box
                link->underlineOffsetY = -3;
                link->onClick = MkFunc1Void(OpenAboutUrl);
                rightTxt = link;
            } else {
                rightTxt = new VirtText(el->rightTxt);
            }
            TableCell& right = table->SetCell(i, 1, rightTxt);
            right.alignV = CrossAxisAlign::CrossCenter;
        }
    }
    logo->font = GetUserGuiFont(GetAppFontFamily(), UiFontSizePx(kSumatraTxtFontSize));
    PlatformFont* fontLeftTxt =
        IsAppFontSizeDefault() ? GetUserGuiFont(GetAppFontFamily(), UiFontSizePx(kLeftTextFontSize)) : GetAppFont();
    PlatformFont* fontRightTxt = fontLeftTxt;
    Color colText = ThemeWindowTextColor();
    Color colLink = ThemeWindowLinkColor();

    for (int i = 0; i < n; i++) {
        AboutRow* el = &gAboutRows[i];
        VirtText* left = LeftAt(i);
        left->font = fontLeftTxt;

        VirtText* right = RightAt(i);
        right->font = fontRightTxt;
        bool isLink = canAccessDisk && el->url;
        // the right column is a link when the row has a url we can open
        right->SetColor(kColText, isLink ? colLink : colText);
        // without disk access the url can't be opened, so it isn't a link
        right->withUnderline = isLink;
        right->SetFlag(vwfNoHitTest, !isLink);
        right->SetText(el->rightTxt ? TrimGitTemp(el->rightTxt) : Str(GetAppVersionTemp()));
    }
}

// the About box is the title band above the two-column table. This sizes it from
// the table, centers it in clientRc and positions the table inside it
void AboutCtrl::UpdateLayout(Rect clientRc) {
    bool showLogo = logo && logo->GetVisibility() != Visibility::Collapse;
    bool showCopy = copyInfoBtn && copyInfoBtn->GetVisibility() != Visibility::Collapse;
    headerSize = showLogo ? logo->GetIdealSize() : Size{};

    int leftRightSpaceDx = UiScalePx(kAboutLeftRightSpaceDx);
    int marginDx = UiScalePx(kAboutMarginDx);
    int aboutTxtDy = UiScalePx(kAboutTxtDy);

    table->colGap = 2 * leftRightSpaceDx;
    table->rowGap = aboutTxtDy;
    Size tableSize = table->Layout(ExpandInf());

    Size btnSz{};
    int gap = UiScalePx(12);
    int padBottom = UiScalePx(kAboutRectPadding);
    int copyBlockDy = 0;
    if (showCopy) {
        btnSz = copyInfoBtn->GetIdealSize();
        copyBlockDy = gap + btnSz.dy + padBottom;
    }

    Rect r;
    // the divider line is drawn inside the gap between the two columns
    r.dx = std::max(tableSize.dx + kAboutLineSepSize, headerSize.dx) + (2 * kAboutLineOuterSize) + (2 * marginDx);
    if (showCopy) {
        r.dx = std::max(r.dx, btnSz.dx + (2 * marginDx) + (2 * kAboutLineOuterSize));
    }
    // one extra row gap so the last row isn't flush against the frame
    r.dy = headerSize.dy + tableSize.dy + aboutTxtDy + (2 * kAboutLineOuterSize) + 4 + copyBlockDy;
    r.x = clientRc.x + ((clientRc.dx - r.dx) / 2);
    if (hideLogo) {
        r.y = clientRc.y;
    } else if (showCopy) {
        r.y = clientRc.y + UiScalePx(kAboutRectPadding);
    } else {
        r.y = clientRc.y + ((clientRc.dy - r.dy) / 2);
    }
    aboutRect = r;

    if (showLogo) {
        logo->SetBounds({r.x + ((r.dx - headerSize.dx) / 2), r.y, headerSize.dx, headerSize.dy});
    }

    int x = r.x + kAboutLineOuterSize + marginDx;
    int y = r.y + (showLogo ? headerSize.dy : kAboutLineOuterSize) + 4;
    table->SetBounds({x, y, tableSize.dx, tableSize.dy});
    dividerX = table->CellRect(0, 1).x - leftRightSpaceDx;

    if (showCopy) {
        int btnX = r.x + ((r.dx - btnSz.dx) / 2);
        int btnY = r.y + r.dy - padBottom - btnSz.dy;
        copyInfoBtn->SetBounds({btnX, btnY, btnSz.dx, btnSz.dy});
    }
}

// Version, OS, WebView2, memory and similar facts for a bug report.
static void AppendBugReportInfo(str::Builder& s) {
    s.Append(fmt("SumatraPDF Enhanced %s\n", GetAppVersionTemp()));
    s.Append(StrL("Based on SumatraPDF " CURR_VERSION_STRA "\n"));
    s.Append(fmt("Built on: %s %s\n", StrL(__DATE__), StrL(__TIME__)));
    if (gitCommidId) {
        s.Append(fmt("Git: %s\n", gitCommidId));
    }
    Str exeType = IsDllBuild() ? StrL("dll") : StrL("static");
    Str instType = IsRunningInPortableMode() ? StrL("portable") : StrL("installed");
    s.Append(fmt("Type: %s, %s\n", exeType, instType));
    if (gIsPreReleaseBuild) {
        s.Append(StrL("Pre-release: yes\n"));
    }
    if (gIsAsanBuild) {
        s.Append(StrL("ASan: yes\n"));
    }

    OSVERSIONINFOEX ver{};
    if (GetOsVersion(ver)) {
        TempStr os = OsNameFromVerTemp(ver);
        int buildNumber = (int)ver.dwBuildNumber & 0xFFFF;
        Str arch = StrL("64-bit");
        if (IsProcess32()) {
            arch = IsRunningInWow64() ? StrL("32-bit (Wow64)") : StrL("32-bit");
        }
        s.Append(fmt("OS: Windows %s, build %d, %s\n", os, buildNumber, arch));
    }
    if (IsOs64()) {
        s.Append(StrL("OS architecture: 64-bit\n"));
    } else {
        s.Append(StrL("OS architecture: 32-bit\n"));
    }

    TempStr wv = GetWebView2VersionTemp();
    if (len(wv) == 0) {
        s.Append(StrL("WebView2: not installed\n"));
    } else {
        s.Append(fmt("WebView2: %s\n", wv));
    }

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        float physMemGB = (float)ms.ullTotalPhys / (float)(1024 * 1024 * 1024);
        s.Append(fmt("Physical memory: %.2f GB (%d%% in use)\n", physMemGB, (int)ms.dwMemoryLoad));
    }

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    s.Append(fmt("Processors: %d\n", (int)si.dwNumberOfProcessors));
    TempStr cpuName = ReadRegStrTemp(HKEY_LOCAL_MACHINE, StrL(R"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)"),
                                     StrL("ProcessorNameString"));
    if (cpuName) {
        s.Append(fmt("Processor: %s\n", cpuName));
    }

    int screenDx = GetSystemMetrics(SM_CXSCREEN);
    int screenDy = GetSystemMetrics(SM_CYSCREEN);
    int dpi = DpiGet();
    s.Append(fmt("Screen: %dx%d, DPI %d (%d%%)\n", screenDx, screenDy, dpi, MulDiv(dpi, 100, 96)));

    char country[32] = {}, lang[32]{};
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, country, dimof(country) - 1);
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, lang, dimof(lang) - 1);
    s.Append(fmt("Locale: %s-%s\n", Str(lang), Str(country)));

    Str theme = ThemeGetNameAt(ThemeGetCurrentIndex());
    if (theme) {
        s.Append(fmt("Theme: %s\n", theme));
    }
    if (IsRunningOnWine()) {
        s.Append(StrL("Wine: yes\n"));
    }
}

static void CopyAboutInfoToClipboard() {
    str::Builder info;
    info.Reserve(1024);
    AppendBugReportInfo(info);
    CopyTextToClipboard(ToStr(info));
}

static void OnCopyProgramInfo(VirtMouseEvent*) {
    CopyAboutInfoToClipboard();
}

// prepares the About tree for hwnd and computes its geometry
static AboutCtrl* UpdateAboutLayout(VirtRoot** rootPtr, HWND hwnd, Rect clientRc, bool hover = false) {
    DpiSetFromHwnd(hwnd);
    AboutCtrl* about = EnsureAboutCtrl(rootPtr, hwnd, clientRc);
    about->hideLogo = hover;
    if (about->logo) {
        about->logo->SetVisibility(hover ? Visibility::Collapse : Visibility::Visible);
    }
    about->Sync();
    bool showCopy = hover || (hwnd == gHwndAbout);
    if (showCopy) {
        if (!about->copyInfoBtn) {
            about->copyInfoBtn =
                NewThemedButton(hwnd, Tr("Copy program and machine info to clipboard"), GetAppFont(), false);
            about->copyInfoBtn->onClick = MkFunc1Void(OnCopyProgramInfo);
            about->AddChild(about->copyInfoBtn);
        }
        about->copyInfoBtn->font = GetAppFont();
        about->copyInfoBtn->SetVisibility(Visibility::Visible);
    } else if (about->copyInfoBtn) {
        about->copyInfoBtn->SetVisibility(Visibility::Collapse);
    }
    about->UpdateLayout(clientRc);
    return about;
}

/* Draws the about screen. The text columns are painted by the AboutCtrl tree;
   this draws the frame around them. It transcribes the design I did in graphics
   software - hopeless to understand without seeing the design. */
static void DrawAbout(Gfx* gfx, VirtRoot* root, Rect clientRc) {
    auto* about = (AboutCtrl*)root->owned;
    Color bgCol = ThemeMainWindowBackgroundColor();
    gfx->FillRect(clientRc, bgCol);

#ifdef ABOUT_USE_LESS_COLORS
    Color lineCol = ThemeWindowTextColor();
    Rect titleRect(about->aboutRect.TL(), about->headerSize);
    Rect titleBgBand(0, about->aboutRect.y, clientRc.dx, titleRect.dy);
    gfx->FillRect(titleBgBand, bgCol);
    gfx->DrawLine(Rect(0, about->aboutRect.y, clientRc.dx, 0), lineCol);
    gfx->DrawLine(Rect(0, about->aboutRect.y + titleRect.dy, clientRc.dx, 0), lineCol);
#endif

    root->Paint(gfx, clientRc);
}

static void OnPaintAbout(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    SetLayout(hdc, kLayoutLtr);
    Rect clientRc = HwndClientRect(hwnd);
    UpdateAboutLayout(&gAboutRoot, hwnd, clientRc);
    Gfx* gfx = GfxCreate(hdc);
    DrawAbout(gfx, gAboutRoot, clientRc);
    delete gfx;
    EndPaint(hwnd, &ps);
}

static void CreateInfotipForLink(Str tooltip, const Rect& rc) {
    if (gAboutTooltip != nullptr) {
        return;
    }

    Tooltip::CreateArgs args;
    args.parent = gHwndAbout;
    args.font = GetAppFont();
    args.isRtl = IsUIRtl();

    gAboutTooltip = new Tooltip();
    gAboutTooltip->Create(args);
    gAboutTooltip->SetSingle(tooltip, rc, false);
}

static void DeleteInfotip() {
    if (gAboutTooltip == nullptr) {
        return;
    }
    // gAboutTooltip->Hide();
    delete gAboutTooltip;
    gAboutTooltip = nullptr;
}

void RefreshAboutWindowFont() {
    if (!gHwndAbout) return;
    DeleteInfotip();
    AboutCtrl* about = UpdateAboutLayout(&gAboutRoot, gHwndAbout, HwndClientRect(gHwndAbout));
    int padding = UiScalePx(kAboutRectPadding) * 2;
    ResizeHwndToClientArea(gHwndAbout, about->aboutRect.dx + padding, about->aboutRect.dy + padding, false);
    UpdateAboutLayout(&gAboutRoot, gHwndAbout, HwndClientRect(gHwndAbout));
    InvalidateRect(gHwndAbout, nullptr, true);
}

static LRESULT CALLBACK WndProcAbout(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Point pt;

    // the links are VirtLinks: let the tree hit-test, click and set the
    // cursor. Its GetTooltipTemp() drives this window's own Tooltip
    if (gAboutRoot && gAboutRoot->owned) {
        LRESULT res = 0;
        switch (msg) {
            case WM_MOUSEMOVE:
            case WM_MOUSELEAVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                if (gAboutRoot->OnMessage(msg, wp, lp, res)) {
                    return res;
                }
                break;
            case WM_SETCURSOR: {
                pt = HwndGetCursorPos(hwnd);
                Point ptLocal{0, 0};
                ILayout* el = ElementFromPoint(gAboutRoot, pt, &ptLocal);
                VirtCtrl* w = el ? el->AsVirtCtrl() : nullptr;
                if (w && w->OnSetCursor(ptLocal)) {
                    TempStr tip = w->GetTooltipTemp(ptLocal);
                    if (tip && *tip.s) {
                        Rect r = w->BoundsInWindow();
                        CreateInfotipForLink(tip, r);
                    }
                    return TRUE;
                }
                DeleteInfotip();
                return DefWindowProc(hwnd, msg, wp, lp);
            }
        }
    }

    switch (msg) {
        case WM_CREATE:
            ReportIf(gHwndAbout);
            DarkModeApplyToTitleBar(hwnd);
            break;

        case WM_ERASEBKGND:
            // do nothing, helps to avoid flicker
            return TRUE;

        case WM_PAINT:
            OnPaintAbout(hwnd);
            break;

        case WM_SETCURSOR:
            DeleteInfotip();
            return DefWindowProc(hwnd, msg, wp, lp);

        case WM_CHAR:
            if (VK_ESCAPE == wp) {
                DestroyWindow(hwnd);
            }
            break;

        case WM_KEYDOWN:
            if ('C' == wp && IsCtrlPressed()) {
                CopyAboutInfoToClipboard();
            }
            break;

        case WM_COMMAND:
            if (CmdCopySelection == LOWORD(wp)) {
                CopyAboutInfoToClipboard();
            }
            break;

        case WM_DESTROY:
            DeleteInfotip();
            delete gAboutRoot;
            gAboutRoot = nullptr;
            ReportIf(!gHwndAbout);
            gHwndAbout = nullptr;
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

constexpr const WCHAR* kAboutClassName = L"SUMATRA_PDF_ABOUT";

void ShowAboutWindow(MainWindow* win) {
    if (gHwndAbout) {
        SetActiveWindow(gHwndAbout);
        return;
    }

    if (!gAtomAbout) {
        WNDCLASSEX wcex;
        FillWndClassEx(wcex, kAboutClassName, WndProcAbout);
        HMODULE h = GetModuleHandleW(nullptr);
        wcex.hIcon = LoadIcon(h, MAKEINTRESOURCE(GetAppIconID()));
        gAtomAbout = RegisterClassEx(&wcex);
        ReportIf(!gAtomAbout);
    }

    WCHAR* title = CWStrTemp(Tr("About SumatraPDF"));
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int dx = CW_USEDEFAULT;
    int dy = CW_USEDEFAULT;
    HINSTANCE h = GetModuleHandleW(nullptr);
    gHwndAbout = CreateWindowExW(0, kAboutClassName, title, style, x, y, dx, dy, nullptr, nullptr, h, nullptr);
    if (!gHwndAbout) {
        return;
    }

    HwndSetRtl(gHwndAbout, IsUIRtl());

    // get the dimensions required for the about box's content
    AboutCtrl* about = UpdateAboutLayout(&gAboutRoot, gHwndAbout, HwndClientRect(gHwndAbout));
    int rectPadding = UiScalePx(kAboutRectPadding);
    dx = about->aboutRect.dx + (2 * rectPadding);
    dy = about->aboutRect.dy + (2 * rectPadding);

    // resize the new window to just match these dimensions
    Rect wRc = HwndWindowRect(gHwndAbout);
    Rect cRc = HwndClientRect(gHwndAbout);
    wRc.dx += dx - cRc.dx;
    wRc.dy += dy - cRc.dy;
    MoveWindow(gHwndAbout, wRc.x, wRc.y, wRc.dx, wRc.dy, FALSE);

    HwndPositionInCenterOf(gHwndAbout, win->hwndFrame);
    ShowWindow(gHwndAbout, SW_SHOW);
}

static void ShowFrequentlyRead(VirtMouseEvent* ev) {
    auto* win = (MainWindow*)ev->target->userData;
    gSettings->showStartPage = true;
    HomePageRelayout(win);
    win->RedrawAll(true);
}

void DrawAboutPage(MainWindow* win, Gfx* gfx) {
    HWND hwnd = win->hwndCanvas;
    Rect clientRc = HwndClientRect(hwnd);
    AboutCtrl* about = UpdateAboutLayout(&win->homeRoot, hwnd, clientRc);

    bool showLink = HasPermission(Perm::SavePreferences | Perm::DiskAccess) && SettingsRememberOpenedFiles();
    if (showLink && !about->showFreqRead) {
        auto* link = new VirtLink(Tr("Show frequently read"));
        link->withUnderline = true;
        link->isRtl = IsUIRtl();
        link->userData = (uintptr_t)win;
        link->onClick = MkFunc1Void(ShowFrequentlyRead);
        about->showFreqRead = link;
        about->AddChild(link);
    }
    if (about->showFreqRead) {
        VirtLink* link = about->showFreqRead;
        link->visibility = showLink ? Visibility::Visible : Visibility::Collapse;
        link->font = GetUserGuiFont(GetAppFontFamily(), UiFontSizePx(16));
        link->sz = {0, 0}; // re-measure: the font may have changed with the DPI
        Size txtSize = link->GetIdealSize(true);
        Rect r = {0, 0, txtSize.dx, txtSize.dy};
        PositionRB(clientRc, r);
        MoveXY(r, -UiScalePx(kInnerPadding), -UiScalePx(kInnerPadding));
        link->SetBounds(r);
    }
    DrawAbout(gfx, win->homeRoot, clientRc);
}

/* alternate static page to display when no document is loaded */

constexpr int kThumbsSeparatorDy = 2;
constexpr int kThumbsBorderDx = 1;
static int HomeTextLineDy();
#define kThumbsMarginLeft UiScalePx(40)
#define kThumbsMarginRight UiScalePx(40)
#define kThumbsMarginTop UiScalePx(50)
#define kThumbsMarginBottom UiScalePx(40)
#define kThumbsSpaceBetweenX UiScalePx(gSettings->homePageThumbnailPadding ? 38 : 24)
#define kThumbsSpaceBetweenY                                           \
    std::max(UiScalePx(gSettings->homePageThumbnailPadding ? 58 : 42), \
             kThumbsCaptionGapY + kThumbsCaptionDy + UiScalePx(gSettings->homePageThumbnailPadding ? 24 : 12))
// caption (file name + type icon) drawn below each thumbnail
#define kThumbsCaptionGapY UiScalePx(3)
#define kThumbsCaptionDy std::max(UiScalePx(20), HomeTextLineDy() + UiScalePx(4))
#define kThumbsBottomBoxDy UiScalePx(50)
#define kHomeListThumbDx UiScalePx(30)
#define kHomeListThumbDy UiScalePx(40)
#define kHomeListRowDy std::max(UiScalePx(46), std::max(kHomeListThumbDy, HomeTextLineDy()) + UiScalePx(6))
#define kHomeListRowGapDx UiScalePx(8)

// ThumbnailLayout::fileSize cache: AppendBlanks zero-fills, so set
// kSizeNotFetched after each AppendBlanks (default member init never runs).
constexpr i64 kSizeNotFetched = -2;
constexpr i64 kSizeFetchFail = -1;

struct ThumbnailLayout {
    Rect rcPage;
    Rect rcFileNumber;
    int displayNumber = 0;
    Size szThumb;
    Rect rcText;
    Rect rcListRow;
    Rect rcListThumb;
    Rect rcListFileName;
    Rect rcListPath;
    Rect rcListSize;
    Rect rcListProgress;
    Rect rcListRemove;
    Rect rcListPin;
    FileState* fs = nullptr; // info needed to draw the thumbnail
    // Cached file::GetSize() so we don't hit the disk on every paint.
    // AppendBlanks zero-fills, so set to kSizeNotFetched after AppendBlanks.
    i64 fileSize = kSizeNotFetched;
    // false until MeasureHomeListRowText() has split rcListFileName into name +
    // directory. Also relies on AppendBlanks zero-fill, so false must mean "not
    // measured yet"
    bool listTextMeasured = false;
};

static TempStr FileSizeForHomeListTemp(i64 size);

// true if r overlaps the visible thumbs band (optionally with a small margin)
static bool IsHomeThumbOnScreen(const Rect& r, const Rect& thumbsArea, int marginY = 0) {
    if (r.IsEmpty() || thumbsArea.IsEmpty()) {
        return false;
    }
    Rect band = thumbsArea;
    if (marginY > 0) {
        band.y -= marginY;
        band.dy += 2 * marginY;
    }
    return !r.Intersect(band).IsEmpty();
}

// HomePageViewMode setting ("thumbnails" or "list")
bool HomePageIsListView() {
    return gSettings && str::EqI(gSettings->homePageViewMode, StrL("list"));
}

void SetHomePageListView(bool listView) {
    Str mode = listView ? StrL("list") : StrL("thumbnails");
    str::ReplaceWithCopy(&gSettings->homePageViewMode, mode);
}

struct HomeActionsLayout {
    Rect items[6];
    int height = 0;

    void Offset(int dx, int dy) {
        for (Rect& item : items) {
            item.x += dx;
            item.y += dy;
        }
    }
};

struct HomePageLayout {
    // args in
    Gfx* gfx = nullptr;
    Rect rc;
    MainWindow* win = nullptr;

    Rect rcIconOpen;
    Rect rcIconListView;
    Rect rcIconThumbnailView;
    Rect rcIconHeaderPin;
    Rect rcIconFullPage;
    Rect rcLogo;
    Rect rcFeatures;
    HomeActionsLayout actions;
    Rect rcZoomControls;
    Rect rcCompletedHeader;
    Rect rcCompletedEmpty;
    int nRecent = 0;
    int gridColumns = 1;

    VirtText* freqRead = nullptr;
    VirtText* openDoc = nullptr;
    VirtText* hideShowFreqRead = nullptr;
    Vec<ThumbnailLayout> thumbnails; // info for each thumbnail
    int totalContentDy = 0;          // total height of all thumbnail rows
    int thumbsVisibleDy = 0;         // visible height for thumbnails area
    Rect rcThumbsArea;               // clip rect for thumbnails

    // search filter
    StrVec filterWords;
    Vec<u8> highlighted;
    Rect rcSearchBorder; // border rect drawn around the edit control

    // tip layout
    Rect rcTip;     // background rect for tip area
    Rect rcTipText; // where the markup goes inside the band
    bool hasTip = false;

    ~HomePageLayout();
};

// freqRead / openDoc are borrowed from the persistent chrome tree (owned by
// win->homeRoot), not created per layout
HomePageLayout::~HomePageLayout() = default;

// --- home page chrome as a VirtCtrl tree ---
// The chrome (header, view-mode buttons, "Open a document..." link, logo row)
// lives for as long as the window, so hover / pressed state survives the
// repaints that scrolling and filtering cause. Geometry still comes from
// LayoutHomePage(): HomePageSyncChrome() just feeds it into the tree. The
// [palette] logo [help] row is an HBox of virt controls.

// Leaf home-page controls: no MainWindow*. Wire onClick / hwndForCmds when
// building the chrome so the same VirtCtrl types stay reusable.

struct HomeViewIconCtrl : VirtCtrl {
    Pixmap* pixmap = nullptr; // not owned, from GetCachedPixmapForSvg()
    // true for the "show as list" button, false for "show as thumbnails"
    bool listView = false;

    HomeViewIconCtrl();
    void Paint(VirtPaintCtx&) override;
};

struct HomeOptionIconCtrl : VirtButton {
    Pixmap* pixmap = nullptr;
    bool fixedHeader = true;

    HomeOptionIconCtrl() : VirtButton(StrL("")) {}
    void Paint(VirtPaintCtx&) override;
};

struct HomeOpenDocCtrl : VirtCtrl {
    Pixmap* pixmap = nullptr; // not owned, from GetCachedPixmapForSvg()
    VirtText* text = nullptr; // child
    // icon position, relative to our bounds
    Rect rcIconLocal;

    HomeOpenDocCtrl();
    void Paint(VirtPaintCtx&) override;
};

// white-circle home-page buttons: "?" keyboard help and the command palette
struct HomeCircleBtnCtrl : VirtButton {
    Pixmap* pixmap = nullptr; // not owned, from GetCachedPixmapForSvg(); null → glyph
    Str glyph;                // not owned; used when pixmap is null ("?")
    bool keyboard = false;

    HomeCircleBtnCtrl();
    Size GetIdealSize() override;
    void Paint(VirtPaintCtx&) override;
};

// The title's HBox owns the logo; search actions are separate chrome controls.
struct HomeLogoRow : VirtCtrl {
    HBox* box = nullptr;

    HomeLogoRow();
    ~HomeLogoRow() override;

    void AddItem(VirtCtrl*);
    Size GetIdealSize() override;
    void SetBounds(Rect) override;
};

struct HomeEntryCtrl;

// the pin icon of a list-view row. It is drawn by DrawHomeListRow, so this is a
// hit target only (the row's ✕ is a VirtCloseButton, which draws itself)
struct HomeListIconCtrl : VirtCtrl {
    bool isPin = true;

    HomeListIconCtrl();
    void OnGetTooltip(VirtTooltipEvent*);
};

// one file entry (a thumbnail or a list row): hit-testing, hover, clicks and
// painting of the row / thumbnail content
struct HomeEntryCtrl : VirtCtrl {
    Str filePath; // owned
    int idx = 0;
    VirtCloseButton* closeBtn = nullptr;
    VirtCloseButton* removeBtn = nullptr;
    HomeListIconCtrl* pinBtn = nullptr;

    HomeEntryCtrl();
    ~HomeEntryCtrl() override;
    void Paint(VirtPaintCtx&) override;
};

// page-level list: still knows the MainWindow so it can wire entry actions and
// keep keyboard selection in sync
struct HomeEntriesCtrl : VirtCtrl {
    MainWindow* win = nullptr;
    // entry the mouse is on, -1 for none. Drives the ✕ button and the keyboard
    // selection, which follows the mouse
    int activeIdx = -1;
    Point lastHoverPt{-1, -1};
    bool keyboardOnlyForTest = false;
    const StrVec* filterWords = nullptr;
    Vec<u8>* highlighted = nullptr;

    HomeEntriesCtrl();
    void OnMouseMove(VirtMouseEvent*);

    HomeEntryCtrl* EntryAt(int idx);
    HomeEntryCtrl* EntryForCtrl(VirtCtrl*);
    void SetEntryCount(int n);
    void SetActiveEntry(int idx);
    void UpdateCloseBtnVisibility();
};

// the tip band at the bottom. The markup is its VirtRichText child, which draws
// itself and runs its own links; double-clicking the band anywhere else picks another
// tip
struct HomeTipCtrl : VirtCtrl {
    // for link commands inside the tip markup (like VirtRichText)
    HWND hwndForCmds = nullptr;
    // onDoubleClick (VirtCtrl): double-click outside a link picks another tip
    VirtRichText* rich = nullptr; // owned, as our only child
    Str richFor;                  // owned, the markup `rich` was parsed from

    ~HomeTipCtrl() override;
    void SetTipLine(Str line, PlatformFont* font);
    void Sync(const Rect& rcTip, const Rect& rcText);
    void Paint(VirtPaintCtx&) override;
};

// paints the border and background around the home search edit (the edit
// itself is a real HWND on top). Decoration only, never a click target
struct HomeSearchBorderCtrl : VirtCtrl {
    MainWindow* win = nullptr;
    HomeSearchBorderCtrl();
    void Paint(VirtPaintCtx&) override;
};

struct HomeLearningCtrl : VirtButton {
    HomeLearningCtrl() : VirtButton(StrL("")) {}
    void Paint(VirtPaintCtx&) override;
};

struct HomeDepthButton : VirtButton {
    const char* icon = nullptr;
    bool editActive = false;
    HomeDepthButton(Str label, PlatformFont* font) : VirtButton(label, font) {}
    void Paint(VirtPaintCtx&) override;
};

struct HomeFeatureButton : VirtButton {
    HomeFeatureButton(Str label, PlatformFont* font) : VirtButton(label, font) {}
    int MinIntrinsicHeight(int width) override;
    void Paint(VirtPaintCtx&) override;
};

struct HomeFeatureRow : VirtCtrl {
    VirtRichText* text = nullptr;
    const char* icon = nullptr;
    int MinIntrinsicHeight(int width) override;
    void SetBounds(Rect) override;
    void Paint(VirtPaintCtx&) override;
};

struct HomeFeatureBody : VirtCtrl {
    int MinIntrinsicHeight(int width) override;
    void SetBounds(Rect) override;
};

struct HomeFeatureScroll : VirtScroll {
    HomeFeatureScroll();
    void Key(VirtKeyEvent*);
    void Paint(VirtPaintCtx&) override;
};

struct HomeFeaturesCtrl : VirtCtrl {
    bool expanded = false;
    VirtButton* toggle = nullptr;
    HomeFeatureScroll* scroll = nullptr;
    HomeFeatureBody* body = nullptr;
    Vec<HomeFeatureRow*> rows;
    VirtButton* previous = nullptr;
    VirtButton* next = nullptr;
    void UpdateFonts();
    void Sync(Rect);
};

static Kind kindHomeChromeCtrl = "homeChromeCtrl";

struct HomeChromeCtrl : VirtCtrl {
    HomeTipCtrl* tip = nullptr;
    HomeSearchBorderCtrl* searchBorder = nullptr;
    HomeEntriesCtrl* entries = nullptr;
    VirtText* hdr = nullptr;
    VirtText* completedHdr = nullptr;
    VirtText* completedEmpty = nullptr;
    VirtCtrl* completedSection = nullptr;
    HomeLogoRow* logoRow = nullptr;
    SumatraLogo* logo = nullptr;
    HomeViewIconCtrl* thumbView = nullptr;
    HomeViewIconCtrl* listView = nullptr;
    HomeOptionIconCtrl* headerPin = nullptr;
    HomeOptionIconCtrl* fullPage = nullptr;
    HomeOpenDocCtrl* openDoc = nullptr;
    VirtButton* resumeBtn = nullptr;
    VirtButton* wordOfDay = nullptr;
    HomeLearningCtrl* learning = nullptr;
    HomeFeaturesCtrl* features = nullptr;
    VirtButton* dictionary = nullptr;
    VirtButton* smallerBtn = nullptr;
    VirtButton* largerBtn = nullptr;
    VirtButton* sizeLabel = nullptr;
    Edit* zoomEdit = nullptr;
    bool zoomEditing = false;
    HomeCircleBtnCtrl* paletteBtn = nullptr;
    HomeCircleBtnCtrl* helpBtn = nullptr;
    // chrome-less About dropdown under the logo; shown after the tooltip delay
    VirtHost* aboutHover = nullptr;

    ~HomeChromeCtrl() override;
};

static HomeChromeCtrl* EnsureHomeChrome(MainWindow* win);
static void DrawHomeBottomShadow(Gfx*, Rect, int radius);
static HomeChromeCtrl* HomeChrome(MainWindow* win);
static HomeEntriesCtrl* HomeEntries(MainWindow* win);
static void HideHomeAboutHover(MainWindow* win);
static void ShowHomeAboutHover(MainWindow* win);
static void HomePageSyncChrome(HomePageLayout& l);
static Rect HomeSelectionOutlineRect(const ThumbnailLayout& t);
static Rect HomeOutlinePaintClip(const Rect& thumbsArea, const Rect& searchBorder, const Rect& tip, bool hasTip);
static void StartHomeZoom(MainWindow* win, VirtMouseEvent*);
static bool ParseHomeZoom(Str, int&);
static int HomeGridMoveRow(MainWindow*, int idx, int direction);
enum class HomeZoomFinish {
    Apply,
    Cancel
};
static void EndHomeZoom(MainWindow*, HomeZoomFinish);

struct HomeZoomEdit : Edit {
    MainWindow* win = nullptr;
    void WndProc(ControlBase::WndProcEvent* ev) {
        if (ev->msg == WM_KEYDOWN && (ev->wparam == VK_RETURN || ev->wparam == VK_ESCAPE)) {
            EndHomeZoom(win, ev->wparam == VK_RETURN ? HomeZoomFinish::Apply : HomeZoomFinish::Cancel);
            ev->result = 0;
            ev->didHandle = true;
            return;
        }
        Edit::WndProc(ev);
    }
};

static int HomePageIconSize() {
    int sz = UiScalePx(gSettings->toolbarSize);
    if (sz < 1) {
        sz = UiScalePx(16);
    }
    return RoundUp(sz, 4);
}

constexpr int kThumbsMiddleMargin = 32;
// draw a gray separator line between list-view rows
static bool gShowListSeparatorLine = false;
constexpr int kSearchEditDy = 42;
constexpr int kSearchThumbnailsGapY = 12;
constexpr int kHomeZoomMin = 75;
constexpr int kHomeZoomMax = 250;
constexpr int kHomeActionTextSize = 18;
constexpr int kHomeSearchTextSize = 27;
constexpr int kHomeDocumentTextSize = 16;
constexpr int kHomeButtonPadX = 12;
constexpr int kHomeButtonPadY = 6;

static PlatformFont* HomeBoldFont(int size) {
    return GetUserGuiFontEx(GetAppFontFamily(), UiFontSizePx(size), true, false);
}

static PlatformFont* HomePageFont(int size) {
    return GetUserGuiFont(GetAppFontFamily(), UiFontSizePx(size));
}

static int HomeSearchFontPx(int dpi) {
    return std::max(1, MulDiv(UiFontSizePxForDpi(dpi, kHomeSearchTextSize), 4, 5));
}

static PlatformFont* HomeSearchFont() {
    return GetUserGuiFont(GetAppFontFamily(), HomeSearchFontPx(DpiGet()));
}

static float HomeEdgeWidth() {
    return 0.5f * DpiGet() / 72.0f * GetUiScale();
}

static Size HomeLabelSize(PlatformFont* font, Str text, int width = Inf) {
    Size measured = D2DMeasureText(font, text, width);
    return measured.IsEmpty() ? PlatformFontMeasureText(font, text, width) : measured;
}

static int HomeZoomControlDy() {
    return std::max(HomePageIconSize(), PlatformFontLineHeight(HomePageFont(kHomeActionTextSize)) + UiScalePx(6));
}

static int HomeZoomValueDx() {
    return std::max(HomeZoomControlDy(),
                    PlatformFontMeasureText(HomePageFont(kHomeActionTextSize), StrL("100%")).dx + UiScalePx(8));
}

static int HomeZoomValueDx(HomeChromeCtrl* chrome, int available) {
    int desired = HomeZoomValueDx();
    if (chrome->zoomEditing && chrome->zoomEdit) {
        TempStr text = chrome->zoomEdit->GetTextTemp();
        if (len(text) > 4)
            desired =
                std::max(desired, PlatformFontMeasureText(HomePageFont(kHomeActionTextSize), text).dx + UiScalePx(8));
    }
    return std::min(desired, std::max(1, available - HomeZoomControlDy() * 2 - UiScalePx(8)));
}

static int HomeSearchButtonDx() {
    return 2 * std::max(HomePageIconSize(), UiScalePx(32));
}

static int HomeThumbPercent() {
    return ClampI(gSettings->homePageThumbnailSize, kHomeZoomMin, kHomeZoomMax);
}

static int HomeThumbDx() {
    return UiScalePx(kThumbnailDx * HomeThumbPercent() / 100);
}
static int HomeThumbDy() {
    int height = GetThumbnailMode() == ThumbnailMode::FullPage ? kThumbnailFullPageDy : kThumbnailDy;
    return UiScalePx(height * HomeThumbPercent() / 100);
}

static int HomeTitleSize(Rect rc) {
    if (rc.dx < UiScalePx(650) || rc.dy < UiScalePx(520)) return 42;
    return 54;
}

static void SetHomeTitleFont(HomeChromeCtrl* chrome, Rect rc) {
    auto* logo = chrome->logo;
    logo->maxWidth = std::max(1, rc.dx - UiScalePx(48));
    int pixels = UiFontSizePx(chrome->features->expanded ? 42 : HomeTitleSize(rc));
    logo->font = GetUserGuiFontEx(GetAppFontFamily(), pixels, true, false);
    for (int i = 0; i < 3; i++) {
        Str fittedTitle = gSettings->uIFontSize > 0 ? StrL(kAppDisplayName) : StrL("SumatraPDF");
        int required =
            PlatformFontMeasureText(logo->font, fittedTitle).dx + logo->BadgeSize() + UiScalePx(12 + kInnerPadding * 2);
        if (required <= logo->maxWidth || pixels <= 1) break;
        pixels = std::max(1, MulDiv(pixels, logo->maxWidth - UiScalePx(8), required));
        logo->font = GetUserGuiFontEx(GetAppFontFamily(), pixels, true, false);
    }
}

static Str HomeSubtitle() {
    return Tr(
        "Your Enhanced Digital Library for Learning, Teaching, and Research.\nSearch, Open, or resume your reading");
}

static Rect HomeSubtitleBox(Rect rc, Rect logo) {
    int width = std::max(1, rc.dx - UiScalePx(48));
    Size size = PlatformFontMeasureText(HomePageFont(21), HomeSubtitle(), width);
    return {rc.x + (rc.dx - width) / 2, logo.Bottom() + UiScalePx(8), width, size.dy + UiScalePx(4)};
}

static Color HomeSubtitleColor() {
    if (ThemeUsesHighContrastColors()) return ThemeWindowTextColor();
    return EnsureContrast(ThemeWindowTextDisabledColor(), ThemeMainWindowBackgroundColor(), 150);
}

static int HomeTextLineDy() {
    static PlatformFont* measuredFont = nullptr;
    static int lineDy = 0;
    PlatformFont* font = HomePageFont(kHomeDocumentTextSize);
    if (font != measuredFont) {
        measuredFont = font;
        lineDy = PlatformFontLineHeight(font);
    }
    return lineDy;
}

static int HomeFileIconDy() {
    return std::max(UiScalePx(16), HomeTextLineDy() - UiScalePx(4));
}

static Size HomeFileNumberSize(int number) {
    Size text = HomeLabelSize(HomePageFont(11), fmt("%d", number));
    int height = text.dy + 2 * UiScalePx(2);
    return {std::max(height, text.dx + 2 * UiScalePx(4)), height};
}

#if IS_DEBUG
bool HomePage_UnitTestsTextSizing() {
    Settings* saved = gSettings;
    int savedDpiX = dpiX;
    int savedDpiY = dpiY;
    gSettings = NewSettings({});
    bool ok = true;
    for (Str family :
         {StrL("system"), StrL("Manrope"), StrL("Pretendard Std"), StrL("Public Sans"), StrL("Consolas")}) {
        str::ReplaceWithCopy(&gSettings->uIFontFamily, family);
        for (int dpi : {96, 144, 192}) {
            DpiSet(dpi, dpi);
            for (int scale : {100, 150, 200}) {
                gSettings->interfaceScale = scale;
                for (int fontSize : {0, 14, 28, 32, 64}) {
                    gSettings->uIFontSize = fontSize;
                    int lineDy = PlatformFontLineHeight(HomePageFont(14));
                    int iconDy = HomeFileIconDy();
                    Rect caption{1000, 100, 400, kThumbsCaptionDy};
                    for (Size source : {Size{16, 16}, Size{12, 16}, Size{16, 20}, Size{32, 32}, Size{48, 48}}) {
                        for (bool rtl : {false, true}) {
                            Rect icon = HomeFileIconBounds(caption, source, iconDy, UiScalePx(4), rtl);
                            ok &= icon.dy > 0 && icon.dx > 0;
                            ok &= icon.dx <= iconDy && icon.dy <= iconDy;
                            ok &= std::abs(2 * icon.y + icon.dy - 2 * caption.y - caption.dy) <= 1;
                            ok &= rtl ? icon.x == caption.Right() + UiScalePx(4)
                                      : icon.Right() == caption.x - UiScalePx(4);
                            ok &= caption.Intersect(icon).IsEmpty();
                        }
                    }
                    ok &= HomeFileIconBounds(caption, {0, 0}, iconDy, UiScalePx(4), false).IsEmpty();
                    ok &= HomeFileIconBounds(caption, {16, 16}, 0, UiScalePx(4), false).IsEmpty();
                    ok &= kThumbsCaptionDy >= lineDy + UiScalePx(4);
                    ok &= kHomeListRowDy >= lineDy + UiScalePx(6);
                    ok &= kHomeListRowDy >= kHomeListThumbDy + UiScalePx(6);
                    for (bool padded : {true, false}) {
                        gSettings->homePageThumbnailPadding = padded;
                        ok &= kThumbsSpaceBetweenY >= kThumbsCaptionGapY + kThumbsCaptionDy + UiScalePx(8);
                    }
                    ok &= HomeZoomControlDy() >= HomePageIconSize();
                    ok &= HomeZoomControlDy() >= lineDy + UiScalePx(4);
                    ok &=
                        HomeZoomValueDx() >= PlatformFontMeasureText(HomePageFont(14), StrL("100%")).dx + UiScalePx(8);
                }
            }
        }
    }
    DeleteSettings(gSettings);
    gSettings = saved;
    dpiX = savedDpiX;
    dpiY = savedDpiY;
    return ok;
}
#endif

static TempStr HomeLearningSummaryTemp() {
    return fmt("%s: %d %s", Tr("Learning Hub"), VocabularyDueCount({}), Tr("due for review"));
}

static Str HomeFeaturesLabel(bool expanded) {
    return expanded ? Tr("Enhanced Features  ▴  Hide") : Tr("Enhanced Features  ▾  Show");
}

// Each row keeps natural button widths; only insufficient space causes wrapping.
static int PlaceHomeActionRow(Rect* bounds, const Size* sizes, int width) {
    int gap = UiScalePx(12);
    int y = 0;
    for (int start = 0; start < 3;) {
        int end = start;
        int rowDx = 0;
        int rowDy = 0;
        while (end < 3) {
            int nextDx = rowDx + (end > start ? gap : 0) + sizes[end].dx;
            if (end > start && nextDx > width) break;
            rowDx = nextDx;
            rowDy = std::max(rowDy, sizes[end].dy);
            end++;
        }
        int x = (width - rowDx) / 2;
        for (int i = start; i < end; i++) {
            bounds[i] = {x, y, sizes[i].dx, rowDy};
            x += sizes[i].dx + gap;
        }
        y += rowDy;
        if (end < 3) y += gap;
        start = end;
    }
    return y;
}

static HomeActionsLayout MeasureHomeActions(int width, HomeChromeCtrl* chrome, Str summary = {}) {
    HomeActionsLayout actions;
    width = std::max(1, width);
    if (!len(summary)) summary = HomeLearningSummaryTemp();
    PlatformFont* font = HomePageFont(kHomeActionTextSize);
    Str labels[] = {Tr("Explore Files"),   Tr("Resume Last"),
                    Tr("Word of the Day"), summary,
                    Tr("Dictionary"),      HomeFeaturesLabel(chrome->features->expanded)};
    int padX = UiScalePx(kHomeButtonPadX);
    int padY = UiScalePx(kHomeButtonPadY);
    int icon = HomePageIconSize();
    int minDy = std::max(icon, HomeLabelSize(font, StrL("Ag")).dy) + 2 * padY;
    Size sizes[6];
    for (int i = 0; i < 6; i++) {
        int prefix = i == 0 ? icon + UiScalePx(8) : 0;
        int textDx = HomeLabelSize(font, labels[i]).dx;
        if (i == 5) {
            // Show/Hide keeps the action in the same place while toggling.
            textDx = std::max(HomeLabelSize(font, HomeFeaturesLabel(false)).dx,
                              HomeLabelSize(font, HomeFeaturesLabel(true)).dx);
        }
        int dx = std::min(width, textDx + prefix + 2 * padX);
        int dy = HomeLabelSize(font, labels[i], std::max(1, dx - prefix - 2 * padX)).dy + 2 * padY;
        sizes[i] = {dx, std::max(minDy, dy)};
    }
    int firstDy = PlaceHomeActionRow(actions.items, sizes, width);
    int secondDy = PlaceHomeActionRow(actions.items + 3, sizes + 3, width);
    for (int i = 3; i < 6; i++) actions.items[i].y += firstDy + UiScalePx(12);
    actions.height = firstDy + UiScalePx(12) + secondDy;
    return actions;
}

int HomeFeatureButton::MinIntrinsicHeight(int width) {
    return HomeLabelSize(font, s, std::max(1, width - UiScalePx(2 * kHomeButtonPadX))).dy +
           UiScalePx(2 * kHomeButtonPadY);
}

void HomeFeatureButton::Paint(VirtPaintCtx& ctx) {
    Color bg = HasFlag(vwfHovered) ? ThemeHotBackgroundColor() : ThemeControlBackgroundColor();
    Color fg = HasFlag(vwfEnabled) ? ThemeWindowTextColor() : ThemeWindowTextDisabledColor();
    fg = EnsureContrast(fg, bg);
    DrawHomeBottomShadow(ctx.gfx, ctx.bounds, UiScalePx(10));
    ctx.gfx->FillRoundedRect(ctx.bounds, UiScalePx(10), bg, ThemeEdgeColor(), HomeEdgeWidth());
    Rect text = ctx.bounds;
    text.SubLR(UiScalePx(kHomeButtonPadX), UiScalePx(kHomeButtonPadX));
    text.SubTB(UiScalePx(kHomeButtonPadY), UiScalePx(kHomeButtonPadY));
    ctx.gfx->DrawText(s, text, gfxTextCenter | gfxTextWrap, font, fg);
    if (HasFlag(vwfFocused)) {
        ctx.gfx->DrawRect(ctx.bounds, fg);
    }
}

void HomeFeaturesCtrl::UpdateFonts() {
    auto* font = HomePageFont(kHomeActionTextSize);
    toggle->font = previous->font = next->font = font;
    for (int i = 0; i < body->ChildCount(); i++) {
        VirtCtrl* child = body->ChildAt(i);
        if (i < len(rows)) {
            auto* row = static_cast<HomeFeatureRow*>(child);
            if (row->text->font != font) {
                row->text->font = font;
                row->text->layoutDx = -1;
            }
            row->text->SetColor(kColRichText, ThemeWindowTextColor());
        } else {
            static_cast<HomeFeatureButton*>(child)->font = font;
        }
    }
}

int HomeFeatureRow::MinIntrinsicHeight(int width) {
    int inset = UiScalePx(32);
    return text->MinIntrinsicHeight(std::max(1, width - inset)) + UiScalePx(16);
}

void HomeFeatureRow::SetBounds(Rect r) {
    VirtCtrl::SetBounds(r);
    int pad = UiScalePx(8);
    int inset = UiScalePx(42);
    text->SetBounds({r.x + inset, r.y + pad, std::max(1, r.dx - inset - pad), r.dy - pad * 2});
}

void HomeFeatureRow::Paint(VirtPaintCtx& ctx) {
    Rect r = ctx.bounds;
    ctx.gfx->FillRoundedRect(r, UiScalePx(10), ThemeControlBackgroundColor(), ThemeEdgeColor());
    int sz = UiScalePx(22);
    Pixmap* pm = GetCachedPixmapForSvg(Str(icon), sz, sz, ThemeWindowTextColor(), ThemeControlBackgroundColor());
    if (pm) {
        ctx.gfx->DrawPixmap(pm, {r.x + UiScalePx(12), r.y + UiScalePx(14), sz, sz});
    }
}

int HomeFeatureBody::MinIntrinsicHeight(int width) {
    int dy = UiScalePx(8);
    for (VirtCtrl* child : children) {
        dy += child->MinIntrinsicHeight(width) + UiScalePx(8);
    }
    return dy;
}

void HomeFeatureBody::SetBounds(Rect r) {
    VirtCtrl::SetBounds(r);
    int y = r.y + UiScalePx(8);
    for (VirtCtrl* child : children) {
        int dy = child->MinIntrinsicHeight(r.dx);
        child->SetBounds({r.x, y, r.dx, dy});
        y += dy + UiScalePx(8);
    }
}

HomeFeatureScroll::HomeFeatureScroll() {
    flags |= vwfFocusable;
    onKeyDown = MkMethod1<HomeFeatureScroll, VirtKeyEvent*, &HomeFeatureScroll::Key>(this);
    SetTooltip(Tr("Scroll to read all features. Use arrow keys, Page Up or Page Down; press Escape to close."));
}

void HomeFeatureScroll::Key(VirtKeyEvent* ev) {
    switch (ev->vkey) {
        case VK_ESCAPE: {
            auto* features = static_cast<HomeFeaturesCtrl*>(parent);
            if (root) {
                root->SetFocus(features->toggle);
            }
            features->toggle->Click();
            ev->didHandle = true;
            return;
        }
        case VK_DOWN:
            ScrollBy(lineDy);
            break;
        case VK_UP:
            ScrollBy(-lineDy);
            break;
        case VK_NEXT:
            ScrollPage(1);
            break;
        case VK_PRIOR:
            ScrollPage(-1);
            break;
        case VK_HOME:
            ScrollTo(0);
            break;
        case VK_END:
            ScrollTo(MaxScrollY());
            break;
        default:
            return;
    }
    ev->didHandle = true;
}

void HomeFeatureScroll::Paint(VirtPaintCtx& ctx) {
    ctx.gfx->FillRect(ctx.bounds, ThemeMainWindowBackgroundColor());
    if (HasFlag(vwfFocused)) {
        ctx.gfx->DrawRect(ctx.bounds, ThemeWindowTextColor());
    }
}

void HomeFeaturesCtrl::Sync(Rect r) {
    visibility = r.IsEmpty() ? Visibility::Collapse : Visibility::Visible;
    SetBounds(r);
    toggle->SetText(HomeFeaturesLabel(expanded));
    int headerDy = 0;
    int navDx = std::max(1, (r.dx - UiScalePx(8)) / 2);
    int navDy = std::max(previous->MinIntrinsicHeight(navDx), next->MinIntrinsicHeight(navDx));
    bool show = expanded && r.dy >= headerDy + UiScalePx(32);
    bool showNavigation = show && r.dy >= headerDy + navDy + UiScalePx(64);
    scroll->visibility = show ? Visibility::Visible : Visibility::Collapse;
    previous->visibility = next->visibility = showNavigation ? Visibility::Visible : Visibility::Collapse;
    if (!show) {
        return;
    }
    int gap = UiScalePx(8);
    int viewportDy = std::max(1, r.dy - headerDy - (showNavigation ? navDy + gap : 0) - gap);
    int contentDx = std::max(1, r.dx - UiScalePx(16));
    int contentDy = body->MinIntrinsicHeight(contentDx);
    scroll->lineDy = std::max(UiScalePx(20), PlatformFontLineHeight(toggle->font));
    scroll->SetContentDy(contentDy);
    scroll->SetBounds({r.x + UiScalePx(8), r.y + headerDy + gap, contentDx, viewportDy});
    if (root && root->focused) {
        for (VirtCtrl* child : body->children) {
            if (root->focused == child) {
                scroll->ScrollIntoView(child);
                break;
            }
        }
    }
    int halfDx = std::max(1, (r.dx - gap) / 2);
    previous->SetBounds({r.x, r.Bottom() - navDy, halfDx, navDy});
    next->SetBounds({r.x + halfDx + gap, r.Bottom() - navDy, halfDx, navDy});
    bool canScroll = contentDy > viewportDy;
    previous->SetIsEnabled(canScroll);
    next->SetIsEnabled(canScroll);
}

static void HomeSelectFromSearchReturnCol(MainWindow* win);
static void HomeScrollSelectionIntoView(MainWindow* win);
static void HomePageShowSelectionTooltip(MainWindow* win);

struct HomeSearchEdit : Edit {
    MainWindow* win = nullptr;

    void WndProc(ControlBase::WndProcEvent* ev) {
        if (ev->msg == WM_KEYDOWN && ev->wparam == VK_DOWN) {
            // down from the search box moves into the file list (issue #1136),
            // restoring the column we left from when going up
            if (win) {
                HomeSelectFromSearchReturnCol(win);
                HwndSetFocus(win->hwndCanvas);
                HomeScrollSelectionIntoView(win);
                HomePageRelayout(win);
                HwndInvalidate(win->hwndCanvas);
                HomePageShowSelectionTooltip(win);
            }
            ev->result = 0;
            ev->didHandle = true;
            return;
        }
        if (ev->msg == WM_KEYDOWN && ev->wparam == VK_ESCAPE) {
            SetText(StrL(""));
            if (win) {
                HwndSetFocus(win->hwndCanvas);
                win->RedrawAll(true);
            }
            ev->result = 0;
            ev->didHandle = true;
            return;
        }
        if (ev->msg == WM_MOUSEWHEEL) {
            // the home page scrolls, not the one-line edit
            ev->result = SendMessageW(GetParent(ev->hwnd), ev->msg, ev->wparam, ev->lparam);
            ev->didHandle = true;
            return;
        }
        Edit::WndProc(ev);
    }
};

// Cue banner when the search field is empty: "Search N files (Ctrl + F)".
static void UpdateHomeSearchCueBanner(MainWindow* win) {
    if (!win || !win->homeSearch) {
        return;
    }
    // Tr returns Str; pass .s into type-safe fmt for the format string.
    TempStr cue = str::DupTemp(Tr("Search documents by name..."));
    EditSetCueText(win->homeSearch, cue);
}

static void HomeSearchTextChanged(MainWindow* win) {
    win->homePageScrollY = 0;
    // the filter changed the list, so select its first entry (#1136)
    HomePageSelectFirst(win);
    HomePageRelayout(win);
    HwndInvalidate(win->hwndCanvas);
}

// the keyboard selection outline is hidden while the search box has the focus
static void HomeSearchFocusChanged(MainWindow* win) {
    HwndInvalidate(win->hwndCanvas);
}

static void PlaceHomeSearchEdit(MainWindow* win, const Rect& rcSearchBorder) {
    if (!win || !win->homeSearchLayout || rcSearchBorder.IsEmpty()) {
        return;
    }
    int searchEditDy =
        std::max(UiScalePx(kSearchEditDy), PlatformFontLineHeight(win->homeSearch->GetFont()) + UiScalePx(18));
    int inset = UiScalePx(30);
    Rect rcEdit = {rcSearchBorder.x + UiScalePx(78), rcSearchBorder.y + ((rcSearchBorder.dy - searchEditDy) / 2),
                   std::max(1, rcSearchBorder.dx - UiScalePx(78) - inset), searchEditDy};
    LayoutToSize(win->homeSearchLayout, rcEdit.Size());
    win->homeSearchLayout->SetBounds(rcEdit);
}

static void EnsureHomeSearchCreated(MainWindow* win) {
    if (win->homeSearch) {
        UpdateHomeSearchCueBanner(win);
        return;
    }
    HWND parent = win->hwndCanvas;
    PlatformFont* font = HomeSearchFont();

    Edit::CreateArgs args;
    args.parent = parent;
    args.font = font;
    args.noTheme = true;
    // the home page draws the box around it, so the edit has no border of its own
    auto* e = new HomeSearchEdit();
    e->win = win;
    e->Create(args);
    RoundControlUseCustomPaint(e->hwnd);
    // Edit::Create wired Edit::WndProc; re-route to HomeSearchEdit for Esc/Down/wheel
    e->onWndProc = MkMethod1<HomeSearchEdit, ControlBase::WndProcEvent*, &HomeSearchEdit::WndProc>(e);
    e->SetColors(ThemeWindowTextColor(), ThemeControlBackgroundColor());
    e->onTextChanged = MkFunc0(HomeSearchTextChanged, win);
    e->onFocus = MkFunc0(HomeSearchFocusChanged, win);
    e->onKillFocus = MkFunc0(HomeSearchFocusChanged, win);
    win->homeSearch = e;
    UpdateHomeSearchCueBanner(win);
    // add left/right padding so text doesn't overlap the border
    int margin = UiScalePx(9);
    EditSetMargins(e, margin, margin);
    // restore the query from before the edit control was destroyed
    // (e.g. by switching to a document tab and back)
    if (len(win->homeSearchQuery) > 0) {
        e->SetText(win->homeSearchQuery);
    }
    // the box is kSearchEditDy tall but the edit is only as tall as its text,
    // so a one-child HBox centers it in there instead of us doing that by hand
    auto* box = new HBox();
    box->alignCross = CrossAxisAlign::CrossCenter;
    box->AddChild(e, 1);
    win->homeSearchLayout = box;
    e->SetIsVisible(false);
}

void HomePageHideSearch(MainWindow* win) {
    if (win && win->homeSearch) {
        win->homeSearch->SetIsVisible(false);
    }
    if (HomeChromeCtrl* chrome = HomeChrome(win)) {
        chrome->zoomEditing = false;
        static_cast<HomeDepthButton*>(chrome->sizeLabel)->editActive = false;
        if (chrome->zoomEdit) chrome->zoomEdit->SetIsVisible(false);
    }
}

void HomePageDestroySearch(MainWindow* win) {
    if (!win || !win->homeSearch) {
        return;
    }
    TempStr query = win->homeSearch->GetTextTemp();
    str::ReplaceWithCopy(&win->homeSearchQuery, query);
    // destroying the edit's window pumps messages, and the canvas answers most
    // of them by calling us again (see WndProcCanvas), so drop our pointers
    // before deleting - otherwise the re-entered call deletes the tree twice
    ILayout* layout = win->homeSearchLayout;
    win->homeSearchLayout = nullptr;
    win->homeSearch = nullptr;
    // the layout owns the edit
    delete layout;
}

// after a theme change; the edit paints itself from these (see
// Edit::OnMessageReflect and the reflection in WndProcCanvas)
void HomePageUpdateSearchColors(MainWindow* win) {
    if (win->homeSearch) {
        win->homeSearch->SetColors(ThemeWindowTextColor(), ThemeControlBackgroundColor());
    }
}

// The search edit survives while the frame moves between monitors. Its HFONT
// and text margins do not follow WM_DPICHANGED automatically, and the page's
// cached rectangles were measured at the previous DPI.
void HomePageOnDpiChanged(MainWindow* win, int dpi) {
    HideHomeAboutHover(win);
    ClearHomeLayoutCache(win);
    if (!win || dpi <= 0) {
        return;
    }
    if (win->homeSearch) {
        int fontSize = HomeSearchFontPx(dpi);
        win->homeSearch->SetFont(GetUserGuiFont(GetAppFontFamily(), fontSize));
        int margin = UiScalePxForDpi(dpi, 9);
        EditSetMargins(win->homeSearch, margin, margin);
    }
    HomePageRelayout(win);
    if (win->hwndCanvas) {
        HwndInvalidate(win->hwndCanvas, true);
    }
}

void PickAnotherRandomPromotion() {
    PickAnotherRandomTip();
}

// --- scroll-friendly layout cache: full LayoutHomePage only when content/size/
// filter changes; pure scrollY changes just offset stored thumb rects ---
struct HomePageLayoutCache {
    bool valid = false;
    int dpi = 0;
    PlatformFont* font = nullptr;
    float uiScale = 0;
    Rect canvasRc;
    int scrollY = 0;
    int thumbnailSize = 100;
    int nFiles = 0;
    int nRecent = 0;
    int gridColumns = 1;
    bool listView = false;
    int learningDue = 0;
    bool fixedHeader = false;
    bool fullPageThumbnails = false;
    bool thumbnailPadding = true;
    bool sortByFreq = false;
    bool showTips = false;
    bool isRtl = false;
    int tipIdx = -1;
    bool tipIsPromo = false;
    Str filterText; // owned

    Rect rcThumbsArea;
    Rect rcSearchBorder;
    Rect rcIconOpen;
    Rect rcIconListView;
    Rect rcIconThumbnailView;
    Rect rcIconHeaderPin;
    Rect rcIconFullPage;
    Rect rcLogo;
    Rect rcFeatures;
    HomeActionsLayout actions;
    Rect rcZoomControls;
    Rect rcCompletedHeader;
    Rect rcCompletedEmpty;
    Rect rcTip;
    Rect rcFreqRead;
    Rect rcOpenDoc;
    int totalContentDy = 0;
    int thumbsVisibleDy = 0;
    Rect rcTipText;
    bool hasTip = false;
    Vec<ThumbnailLayout> thumbs;
    StrVec filterWords;
    Vec<u8> highlighted;
};

// The layout belongs to the window: its home chrome entries are laid out and
// painted from these rects, so a second window must not overwrite them.
static HomePageLayoutCache& HomeLayout(MainWindow* win) {
    if (!win->homeLayout) {
        win->homeLayout = new HomePageLayoutCache();
    }
    return *win->homeLayout;
}

static void ClearHomeLayoutCache(MainWindow* win) {
    if (!win || !win->homeLayout) {
        return;
    }
    auto& c = *win->homeLayout;
    c.valid = false;
    str::Free(c.filterText);
    c.filterText = {};
    VecReset(c.thumbs);
    c.filterWords.Reset();
    VecReset(c.highlighted);
    c.hasTip = false;
    c.nFiles = 0;
    c.scrollY = 0;
}

// The cache holds raw FileState* (ThumbnailLayout::fs) owned by gSettings.
// Reloading settings frees and rebuilds those, so the cache has to be dropped
// first or hover / selection reads freed memory (crash 8c34d7eda). It is
// rebuilt on the next HomePageRelayout.
// must be called before the FileState objects the cache points at are freed
void HomePageInvalidateLayoutCache() {
    FreeHomeFileIcons();
    for (MainWindow* win : gWindows) {
        ClearHomeLayoutCache(win);
    }
}

void HomePageFocusSearch(MainWindow* win) {
    win->homePageScrollY = 0;
    EnsureHomeSearchCreated(win);
    HomePageRelayout(win);
    if (win->homeSearch) {
        win->homeSearch->SetIsVisible(true);
        EditSetFocus(win->homeSearch);
    }
}

static void OffsetThumbnailLayouts(Vec<ThumbnailLayout>& thumbs, int dy) {
    if (dy == 0) {
        return;
    }
    for (ThumbnailLayout& t : thumbs) {
        t.rcPage.y += dy;
        t.rcFileNumber.y += dy;
        t.rcText.y += dy;
        t.rcListRow.y += dy;
        t.rcListThumb.y += dy;
        t.rcListFileName.y += dy;
        t.rcListPath.y += dy;
        t.rcListSize.y += dy;
        t.rcListProgress.y += dy;
        t.rcListRemove.y += dy;
        t.rcListPin.y += dy;
    }
}

static void OffsetHomeChrome(HomePageLayout& l, int dy) {
    l.rcSearchBorder.y += dy;
    l.rcIconOpen.y += dy;
    l.rcIconListView.y += dy;
    l.rcIconThumbnailView.y += dy;
    l.rcIconHeaderPin.y += dy;
    l.rcIconFullPage.y += dy;
    l.rcLogo.y += dy;
    l.rcFeatures.y += dy;
    l.actions.Offset(0, dy);
    l.rcZoomControls.y += dy;
    l.rcTip.y += dy;
    l.rcTipText.y += dy;
    Rect header = l.freqRead->lastBounds;
    header.y += dy;
    l.freqRead->SetBounds(header);
    Rect open = l.openDoc->lastBounds;
    open.y += dy;
    l.openDoc->SetBounds(open);
}

static void OffsetHomeCache(HomePageLayoutCache& c, int dy) {
    OffsetThumbnailLayouts(c.thumbs, dy);
    c.rcCompletedHeader.y += dy;
    c.rcCompletedEmpty.y += dy;
    if (c.fixedHeader) return;
    c.rcSearchBorder.y += dy;
    c.rcIconOpen.y += dy;
    c.rcIconListView.y += dy;
    c.rcIconThumbnailView.y += dy;
    c.rcIconHeaderPin.y += dy;
    c.rcIconFullPage.y += dy;
    c.rcLogo.y += dy;
    c.rcFeatures.y += dy;
    c.actions.Offset(0, dy);
    c.rcZoomControls.y += dy;
    c.rcTip.y += dy;
    c.rcTipText.y += dy;
    c.rcFreqRead.y += dy;
    c.rcOpenDoc.y += dy;
}

static TempStr HomeSearchQueryTemp(MainWindow* win) {
    if (win->homeSearch) {
        return win->homeSearch->GetTextTemp();
    }
    // edit is created after paint; keep filtering with the remembered query
    return win->homeSearchQuery;
}

static bool HomeLayoutCacheMatches(MainWindow* win, const Rect& rc, Str filterText) {
    auto& c = HomeLayout(win);
    if (!c.valid) {
        return false;
    }
    if (c.thumbnailSize != HomeThumbPercent()) {
        return false;
    }
    if (c.dpi != DpiGet()) {
        return false;
    }
    if (c.font != HomePageFont(14) || c.uiScale != GetUiScale()) {
        return false;
    }
    if (c.learningDue != VocabularyDueCount({})) return false;
    if (c.canvasRc != rc) {
        return false;
    }
    if (c.listView != HomePageIsListView()) {
        return false;
    }
    if (c.fixedHeader != gSettings->homePageFixedHeader ||
        c.fullPageThumbnails != gSettings->homePageFullPageThumbnails ||
        c.thumbnailPadding != gSettings->homePageThumbnailPadding) {
        return false;
    }
    if (c.sortByFreq != (gSettings && gSettings->homePageSortByFrequentlyRead)) {
        return false;
    }
    if (c.showTips != (gSettings && gSettings->showTips)) {
        return false;
    }
    if (c.isRtl != IsUIRtl()) {
        return false;
    }
    if (c.tipIdx != gSelectedTipIdx || c.tipIsPromo != gSelectedIsPromo) {
        return false;
    }
    if (!str::Eq(c.filterText, filterText)) {
        return false;
    }
    // pin/remove/reorder changes FileState pointers or order → invalidate
    // (nFiles alone is not enough: pin does not change count)
    return true;
}

// true if cached thumb FileState* sequence still matches the current file list
static bool HomeLayoutCacheFilesMatch(MainWindow* win, const Vec<FileState*>& files) {
    auto& c = HomeLayout(win);
    if (len(files) != c.nFiles || len(c.thumbs) != c.nFiles) {
        return false;
    }
    int recent = 0;
    for (int i = 0; i < c.nFiles; i++) {
        if (!files[i]->completed) recent++;
        if (c.thumbs[i].fs != files[i]) {
            return false;
        }
    }
    return recent == c.nRecent;
}

static int CollectHomePageFiles(MainWindow* win, Vec<FileState*>& fileStates, StrVec& filterWords) {
    Vec<FileState*> allFileStates;
    if (gSettings->homePageSortByFrequentlyRead) {
        FileHistoryGetFrequencyOrder(allFileStates);
    } else {
        FileHistoryGetRecentlyOpenedOrder(allFileStates);
    }

    TempStr searchQuery = HomeSearchQueryTemp(win);
    bool hasFilter = searchQuery && searchQuery.s[0];
    if (hasFilter) {
        SplitFilterToWords(searchQuery, filterWords);
    }
    Vec<FileState*> completed;
    int limit = limitValue(gSettings->homePageMaxRecentItems, 1, 200);
    for (int i = 0; i < len(allFileStates); i++) {
        FileState* fs = allFileStates[i];
        if (len(fs->filePath) == 0) {
            continue;
        }
        if (hasFilter) {
            TempStr baseName = path::GetBaseNameTemp(fs->filePath);
            if (!FilterMatches(baseName, filterWords)) {
                continue;
            }
        }
        auto& section = fs->completed ? completed : fileStates;
        if (!hasFilter && len(section) >= limit) continue;
        VecAppend(section, fs);
    }
    int recent = len(fileStates);
    for (FileState* fs : completed) VecAppend(fileStates, fs);
    return recent;
}

static void SaveHomeLayoutCache(const HomePageLayout& l, Str filterText, int scrollY) {
    auto& c = HomeLayout(l.win);
    c.valid = true;
    c.dpi = DpiGet();
    c.font = HomePageFont(14);
    c.uiScale = GetUiScale();
    c.canvasRc = l.rc;
    c.scrollY = scrollY;
    c.thumbnailSize = HomeThumbPercent();
    c.nFiles = len(l.thumbnails);
    c.nRecent = l.nRecent;
    c.gridColumns = l.gridColumns;
    c.learningDue = VocabularyDueCount({});
    c.listView = HomePageIsListView();
    c.fixedHeader = gSettings->homePageFixedHeader;
    c.fullPageThumbnails = gSettings->homePageFullPageThumbnails;
    c.thumbnailPadding = gSettings->homePageThumbnailPadding;
    c.sortByFreq = gSettings && gSettings->homePageSortByFrequentlyRead;
    c.showTips = gSettings && gSettings->showTips;
    c.isRtl = IsUIRtl();
    c.tipIdx = gSelectedTipIdx;
    c.tipIsPromo = gSelectedIsPromo;
    str::ReplaceWithCopy(&c.filterText, filterText);
    c.rcThumbsArea = l.rcThumbsArea;
    c.rcSearchBorder = l.rcSearchBorder;
    c.rcIconOpen = l.rcIconOpen;
    c.rcIconListView = l.rcIconListView;
    c.rcIconThumbnailView = l.rcIconThumbnailView;
    c.rcIconHeaderPin = l.rcIconHeaderPin;
    c.rcIconFullPage = l.rcIconFullPage;
    c.rcLogo = l.rcLogo;
    c.rcFeatures = l.rcFeatures;
    c.actions = l.actions;
    c.rcZoomControls = l.rcZoomControls;
    c.rcCompletedHeader = l.rcCompletedHeader;
    c.rcCompletedEmpty = l.rcCompletedEmpty;
    c.rcTip = l.rcTip;
    c.rcFreqRead = l.freqRead ? l.freqRead->lastBounds : Rect{};
    c.rcOpenDoc = l.openDoc ? l.openDoc->lastBounds : Rect{};
    c.totalContentDy = l.totalContentDy;
    c.thumbsVisibleDy = l.thumbsVisibleDy;
    c.rcTipText = l.rcTipText;
    c.hasTip = l.hasTip;
    c.thumbs = l.thumbnails;
    c.filterWords = l.filterWords;
    c.highlighted = l.highlighted;
}

// rebuild chrome VirtText + copy cached geometry into l (no full layout)
static void ApplyHomeLayoutCache(HomePageLayout& l, int scrollY) {
    auto* win = l.win;
    auto& c = HomeLayout(win);
    bool isRtl = IsUIRtl();

    // clamp scroll using cached content height
    int maxScrollY = std::max(0, c.totalContentDy - c.thumbsVisibleDy);
    if (scrollY > maxScrollY) {
        scrollY = maxScrollY;
        win->homePageScrollY = scrollY;
    }
    if (scrollY < 0) {
        scrollY = 0;
        win->homePageScrollY = 0;
    }

    int dy = c.scrollY - scrollY; // content moves opposite scroll direction
    OffsetHomeCache(c, dy);
    c.scrollY = scrollY;
    c.thumbnailSize = HomeThumbPercent();

    l.rcThumbsArea = c.rcThumbsArea;
    l.rcSearchBorder = c.rcSearchBorder;
    l.rcIconOpen = c.rcIconOpen;
    l.rcIconListView = c.rcIconListView;
    l.rcIconThumbnailView = c.rcIconThumbnailView;
    l.rcIconHeaderPin = c.rcIconHeaderPin;
    l.rcIconFullPage = c.rcIconFullPage;
    l.rcLogo = c.rcLogo;
    l.rcFeatures = c.rcFeatures;
    l.actions = c.actions;
    l.rcZoomControls = c.rcZoomControls;
    l.rcCompletedHeader = c.rcCompletedHeader;
    l.rcCompletedEmpty = c.rcCompletedEmpty;
    l.nRecent = c.nRecent;
    l.gridColumns = c.gridColumns;
    l.rcTip = c.rcTip;
    l.totalContentDy = c.totalContentDy;
    l.thumbsVisibleDy = c.thumbsVisibleDy;
    l.rcTipText = c.rcTipText;
    l.hasTip = c.hasTip;
    l.thumbnails = c.thumbs;
    l.filterWords = c.filterWords;
    l.highlighted = c.highlighted;
    PlatformFont* hdrFont = HomeBoldFont(21);
    PlatformFont* fontText = HomePageFont(kHomeDocumentTextSize);

    Str txt = Tr("Recent Documents");
    if (gSettings->homePageSortByFrequentlyRead) {
        txt = Tr("Frequently Read");
    }
    HomeChromeCtrl* chrome = EnsureHomeChrome(win);
    VirtText* hdr = chrome->hdr;
    hdr->SetText(txt);
    hdr->font = hdrFont;
    hdr->isRtl = isRtl;
    hdr->SetBounds(c.rcFreqRead);
    l.freqRead = hdr;

    TempStr openTxt = str::DupTemp(Tr("Explore Files"));
    str::RemoveCharsInPlace(openTxt, StrL("&"));
    VirtText* openDoc = chrome->openDoc->text;
    openDoc->SetText(openTxt);
    openDoc->font = HomePageFont(kHomeActionTextSize);
    openDoc->isRtl = isRtl;
    openDoc->withUnderline = false;
    openDoc->SetBounds(c.rcOpenDoc);
    l.openDoc = openDoc;
}

static void LayoutHomePage(HomePageLayout& l) {
    EnsureTipsParsed();
    GetThumbnailMode();

    Vec<FileState*> allFileStates;
    if (gSettings->homePageSortByFrequentlyRead) {
        FileHistoryGetFrequencyOrder(allFileStates);
    } else {
        FileHistoryGetRecentlyOpenedOrder(allFileStates);
    }
    auto rc = l.rc;
    auto* win = l.win;

    Vec<FileState*> fileStates;
    l.nRecent = CollectHomePageFiles(win, fileStates, l.filterWords);

    bool isRtl = IsUIRtl();
    PlatformFont* fontText = HomePageFont(kHomeDocumentTextSize);

    // --- Pre-compute thumbnail grid x offset so header can align with it ---
    // use unfiltered count so layout stays stable when search filters results
    int nFilesForLayout = len(allFileStates);
    int colsForLayout = (rc.dx - kThumbsMarginLeft - kThumbsMarginRight + kThumbsSpaceBetweenX) /
                        (HomeThumbDx() + kThumbsSpaceBetweenX);
    int thumbsColsForLayout = std::max(colsForLayout, 1);
    l.gridColumns = thumbsColsForLayout;
    int thumbsStartX = rc.x + kThumbsMarginLeft +
                       ((rc.dx - (thumbsColsForLayout * HomeThumbDx()) -
                         ((thumbsColsForLayout - 1) * kThumbsSpaceBetweenX) - kThumbsMarginLeft - kThumbsMarginRight) /
                        2);
    if (thumbsStartX < UiScalePx(kInnerPadding)) {
        thumbsStartX = UiScalePx(kInnerPadding);
    } else if (nFilesForLayout == 0) {
        thumbsStartX = kThumbsMarginLeft;
    }
    int thumbsContentWidth = (thumbsColsForLayout * HomeThumbDx()) + ((thumbsColsForLayout - 1) * kThumbsSpaceBetweenX);

    // --- Step 1: two header rows: [palette] SumatraPDF [help] on top, then
    // [open link] [search edit] [view icons] ---
    Rect rcIconView(0, 0, 0, 0);
    rcIconView.dx = rcIconView.dy = HomeZoomControlDy();

    HomeChromeCtrl* chrome = EnsureHomeChrome(win);
    VirtText* hdr = chrome->hdr;
    hdr->SetText(gSettings->homePageSortByFrequentlyRead ? Tr("Frequently Read") : Tr("Recent Documents"));
    hdr->font = HomeBoldFont(21);
    hdr->isRtl = isRtl;
    l.freqRead = hdr;

    int searchThumbsGap = UiScalePx(kSearchThumbnailsGapY);
    int borderDy = std::max(UiScalePx(60), PlatformFontLineHeight(HomeSearchFont()) + UiScalePx(18));

    // [command palette] SumatraPDF [keyboard shortcuts], centered like the old
    // title. The HBox in logoRow sizes the three virt controls.
    SetHomeTitleFont(chrome, rc);
    Size logoRowSize = chrome->logoRow->GetIdealSize();
    int logoY = rc.y + UiScalePx(16);
    int logoX = rc.x + ((rc.dx - logoRowSize.dx) / 2);
    if (logoX < UiScalePx(kInnerPadding)) {
        logoX = UiScalePx(kInnerPadding);
    }
    l.rcLogo = {logoX, logoY, logoRowSize.dx, logoRowSize.dy};

    Rect subtitle = HomeSubtitleBox(rc, l.rcLogo);
    int hdrY = (subtitle.IsEmpty() ? l.rcLogo.Bottom() : subtitle.Bottom()) + UiScalePx(12);
    int iconGap = UiScalePx(4);
    int rowDy = std::max({rcIconView.dy, borderDy, HomeSearchButtonDx()});
    // every row item (link, search box, view icons) is centered on the row's
    // vertical centerline
    int centerY = hdrY + (rowDy / 2);
    int viewIconsDx = (4 * rcIconView.dx) + 3 * iconGap;

    /* "Open..." link at the left edge */
    Rect rcIconOpen(0, 0, 0, 0);
    rcIconOpen.dx = rcIconOpen.dy = HomePageIconSize();

    TempStr openTxt = str::DupTemp(Tr("Explore Files"));
    str::RemoveCharsInPlace(openTxt, StrL("&"));
    VirtText* openDoc = chrome->openDoc->text;
    openDoc->SetText(openTxt);
    openDoc->font = HomePageFont(kHomeActionTextSize);
    openDoc->isRtl = isRtl;
    openDoc->withUnderline = false;
    Size txtSize = openDoc->GetIdealSize(true);
    txtSize.dx = std::min(txtSize.dx, std::max(1, rc.dx - UiScalePx(84) - rcIconOpen.dx));
    openDoc->ellipsis = true;
    Rect rcOpenDoc;

    /* view-mode icons at the right edge */
    l.rcIconThumbnailView = {thumbsStartX + thumbsContentWidth - viewIconsDx, centerY - (rcIconView.dy / 2),
                             rcIconView.dx, rcIconView.dy};
    l.rcIconListView = {l.rcIconThumbnailView.x + rcIconView.dx + iconGap, l.rcIconThumbnailView.y, rcIconView.dx,
                        rcIconView.dy};

    // Center the search field independently of the action and view controls.
    int sideSpace = 2 * (HomeSearchButtonDx() + UiScalePx(8));
    int borderDx = std::min(UiScalePx(975), rc.dx - UiScalePx(48) - sideSpace);
    borderDx = std::max(1, borderDx);
    int borderX = rc.x + ((rc.dx - borderDx) / 2);
    l.rcSearchBorder = {borderX, hdrY + ((rowDy - borderDy) / 2), borderDx, borderDy};

    chrome->features->UpdateFonts();
    int actionsWidth = std::max(1, rc.dx - UiScalePx(48));
    l.actions = MeasureHomeActions(actionsWidth, chrome);
    int actionY = hdrY + rowDy + UiScalePx(16);
    l.actions.Offset(rc.x + (rc.dx - actionsWidth) / 2, actionY);
    Rect open = l.actions.items[0];
    rcIconOpen.x = open.x + UiScalePx(kHomeButtonPadX);
    rcIconOpen.y = open.y + (open.dy - rcIconOpen.dy) / 2;
    rcOpenDoc = {rcIconOpen.Right() + UiScalePx(8), open.y + (open.dy - txtSize.dy) / 2,
                 std::max(1, open.dx - rcIconOpen.dx - UiScalePx(8 + 2 * kHomeButtonPadX)), txtSize.dy};
    int sectionY = actionY + l.actions.height + UiScalePx(16);
    int sectionX = std::min(thumbsStartX, UiScalePx(24));
    int sectionWidth = std::max(1, rc.dx - sectionX * 2);
    bool showZoom = !HomePageIsListView();
    int zoomDx = HomeZoomControlDy() * 2 + HomeZoomValueDx(chrome, sectionWidth) + UiScalePx(8);
    bool inlineZoom = showZoom && sectionWidth >= hdr->GetIdealSize().dx + viewIconsDx + zoomDx + UiScalePx(24);
    int sizeControlsDx = inlineZoom ? zoomDx + UiScalePx(12) : 0;
    int headerDy = std::max(UiScalePx(28), PlatformFontLineHeight(hdr->font));
    int sectionDy = inlineZoom ? std::max(headerDy, HomeZoomControlDy()) : headerDy;
    l.rcIconThumbnailView.x = rc.x + sectionX + sectionWidth - viewIconsDx;
    l.rcIconListView.x = l.rcIconThumbnailView.Right() + iconGap;
    int sectionIconY = sectionY + (sectionDy - rcIconView.dy) / 2;
    l.rcIconHeaderPin = {l.rcIconListView.Right() + iconGap, sectionIconY, rcIconView.dx, rcIconView.dy};
    l.rcIconFullPage = {l.rcIconHeaderPin.Right() + iconGap, sectionIconY, rcIconView.dx, rcIconView.dy};
    hdr->ellipsis = true;
    hdr->SetBounds({rc.x + sectionX, sectionY + (sectionDy - headerDy) / 2,
                    std::max(1, sectionWidth - viewIconsDx - sizeControlsDx - UiScalePx(8)), headerDy});
    l.rcIconThumbnailView.y = sectionIconY;
    l.rcIconListView.y = sectionIconY;
    if (showZoom) {
        l.rcZoomControls = {
            inlineZoom ? l.rcIconThumbnailView.x - zoomDx - UiScalePx(12) : rc.x + sectionX,
            inlineZoom ? sectionY + (sectionDy - HomeZoomControlDy()) / 2 : sectionY + sectionDy + UiScalePx(8), zoomDx,
            HomeZoomControlDy()};
    }

    if (isRtl) {
        auto mirrorX = [&rc](Rect& r) { r.x = rc.dx - r.x - r.dx; };
        mirrorX(l.rcLogo);
        mirrorX(l.rcIconThumbnailView);
        mirrorX(l.rcIconListView);
        mirrorX(l.rcIconHeaderPin);
        mirrorX(l.rcIconFullPage);
        mirrorX(l.rcZoomControls);
        for (Rect& action : l.actions.items) mirrorX(action);
        mirrorX(rcIconOpen);
        mirrorX(rcOpenDoc);
        mirrorX(l.rcSearchBorder);
        Rect header = hdr->lastBounds;
        mirrorX(header);
        hdr->SetBounds(header);
    }
    l.rcIconOpen = rcIconOpen;
    openDoc->SetBounds(rcOpenDoc);
    l.openDoc = openDoc;

    int headerBottomY = std::max(sectionY + sectionDy, l.rcZoomControls.Bottom()) + searchThumbsGap;

    // --- Step 2: calculate tip area at the bottom (before thumbnails) ---
    int tipHeight = 0;
    PlatformFont* fontTip = HomePageFont(16);
    HomeTipCtrl* tipCtrl = EnsureHomeChrome(l.win)->tip;
    tipCtrl->SetTipLine(SelectedTipLine(), fontTip);
    VirtRichText* tip = chrome->features->expanded ? nullptr : tipCtrl->rich;
    if (tip) {
        int tipPadding = UiScalePx(8);
        tipHeight = tip->MinIntrinsicHeight(thumbsContentWidth) + (2 * tipPadding);
        if (gSettings->homePageFixedHeader &&
            headerBottomY + tipHeight + HomeThumbDy() + HomeTextLineDy() * 2 + UiScalePx(32) > rc.dy) {
            tip = nullptr;
            tipHeight = 0;
        }
    }

    // --- Step 3: middle area for thumbnails/list ---
    // content starts directly after headerBottomY (which includes kSearchThumbnailsGapY)
    int thumbsTopY = headerBottomY;
    int thumbsBottomY = rc.dy - tipHeight - kThumbsMiddleMargin;
    int availableDy = std::max(0, thumbsBottomY - thumbsTopY);
    chrome->features->UpdateFonts();
    int featureHeaderDy = 0;
    int featureNavDy = chrome->features->previous->MinIntrinsicHeight(std::max(1, rc.dx / 2 - UiScalePx(28)));
    int featureDy = std::min(availableDy, featureHeaderDy);
    if (chrome->features->expanded) {
        int expandedMinDy = featureHeaderDy + UiScalePx(32);
        int reserveDy = std::min(availableDy / 4, HomeThumbDy() + UiScalePx(40));
        if (availableDy - reserveDy >= expandedMinDy) {
            featureDy = std::min(availableDy - reserveDy,
                                 std::max(UiScalePx(360), featureHeaderDy + featureNavDy + UiScalePx(64)));
        }
        if (!gSettings->homePageFixedHeader) {
            featureDy = std::max(UiScalePx(360), featureHeaderDy + featureNavDy + UiScalePx(64));
        }
    }
    int featureMargin = UiScalePx(24);
    l.rcFeatures = {rc.x + featureMargin, thumbsTopY, std::max(1, rc.dx - featureMargin * 2), featureDy};
    if (featureDy > 0) thumbsTopY += featureDy + UiScalePx(8);
    int thumbsVisibleDy = std::max(0, thumbsBottomY - thumbsTopY);

    l.rcThumbsArea = gSettings->homePageFixedHeader ? Rect{0, thumbsTopY, rc.dx, thumbsVisibleDy} : rc;

    int nFiles = len(fileStates);
    bool showList = HomePageIsListView();
    // Leave room above the first row so RoundRect / selection outline top edges
    // aren't clipped by rcThumbsArea (they extend a few px upward).
    int thumbsContentPadTop = showList ? UiScalePx(2) : UiScalePx(12);
    auto sectionHeight = [&](int count) {
        if (!count) return 0;
        if (showList) return count * kHomeListRowDy;
        int rows = (count + thumbsColsForLayout - 1) / thumbsColsForLayout;
        return rows * (HomeThumbDy() + kThumbsSpaceBetweenY) - kThumbsSpaceBetweenY + kThumbsCaptionGapY +
               kThumbsCaptionDy;
    };
    int recentDy = sectionHeight(l.nRecent);
    int completedCount = nFiles - l.nRecent;
    int completedDy = completedCount ? sectionHeight(completedCount) : HomeTextLineDy() + UiScalePx(8);
    int completedHeaderDy = std::max(UiScalePx(28), PlatformFontLineHeight(HomeBoldFont(21)));
    int completedOffset = recentDy + UiScalePx(24) + completedHeaderDy + searchThumbsGap;
    int thumbsContentDy = thumbsContentPadTop + completedOffset + completedDy;

    int totalContentDy = thumbsContentDy;
    if (!gSettings->homePageFixedHeader) {
        totalContentDy = thumbsTopY - rc.y + thumbsContentDy + kThumbsMiddleMargin + tipHeight;
        thumbsVisibleDy = rc.dy;
    }
    int scrollY = std::max(0, win->homePageScrollY);
    int maxScrollY = std::max(0, totalContentDy - thumbsVisibleDy);
    if (scrollY > maxScrollY) {
        scrollY = maxScrollY;
    }
    win->homePageScrollY = scrollY;
    l.totalContentDy = totalContentDy;
    l.thumbsVisibleDy = thumbsVisibleDy;

    Point ptOff(thumbsStartX, thumbsTopY + thumbsContentPadTop - scrollY);
    l.rcCompletedHeader = {rc.x + sectionX, ptOff.y + recentDy + UiScalePx(24), sectionWidth, completedHeaderDy};
    if (!completedCount) {
        l.rcCompletedEmpty = {thumbsStartX, ptOff.y + completedOffset, thumbsContentWidth, completedDy};
    }

    if (showList) {
        int listX = thumbsStartX;
        if (isRtl) {
            listX = rc.dx - thumbsStartX - thumbsContentWidth;
        }
        int listIconDx = l.rcIconListView.dx;
        int listIconGap = UiScalePx(6);
        // fixed size column — never call file::GetSize during layout (disk/network I/O)
        int listSizeDx = UiScalePx(56);
        bool showProgress = gSettings && gSettings->showHomePageReadingProgress;
        int listProgressDx = showProgress ? UiScalePx(56) : 0;
        int listProgressGap = listProgressDx > 0 ? listIconGap : 0;
        // one-row margin so a quick scroll still has measured name/path splits ready
        int listPrefetchY = kHomeListRowDy;
        for (int row = 0; row < nFiles; row++) {
            ThumbnailLayout& thumb = *VecAppendBlanks(l.thumbnails, 1);
            thumb.fileSize = kSizeNotFetched;
            FileState* fs = fileStates[row];
            thumb.fs = fs;
            thumb.displayNumber = (row < l.nRecent ? row : row - l.nRecent) + 1;
            int rowY = row < l.nRecent ? row * kHomeListRowDy : completedOffset + (row - l.nRecent) * kHomeListRowDy;
            Rect rcRow(listX, ptOff.y + rowY, thumbsContentWidth, kHomeListRowDy);
            thumb.rcListRow = rcRow;
            bool onScreen = IsHomeThumbOnScreen(rcRow, l.rcThumbsArea, listPrefetchY);

            Size numberSize = HomeFileNumberSize(thumb.displayNumber);
            thumb.rcFileNumber = {rcRow.x, rcRow.y + (rcRow.dy - numberSize.dy) / 2, numberSize.dx, numberSize.dy};
            Rect rcThumb(rcRow.x + numberSize.dx + kHomeListRowGapDx, rcRow.y + ((rcRow.dy - kHomeListThumbDy) / 2),
                         kHomeListThumbDx, kHomeListThumbDy);
            Rect rcPin(rcRow.x + rcRow.dx - listIconDx, rcRow.y + ((rcRow.dy - listIconDx) / 2), listIconDx,
                       listIconDx);
            Rect rcRemove(rcPin.x - listIconGap - listIconDx, rcPin.y, listIconDx, listIconDx);
            Rect rcSize(rcRemove.x - listIconGap - listSizeDx, rcRow.y, listSizeDx, rcRow.dy);
            Rect rcProgress(rcSize.x - listProgressGap - listProgressDx, rcRow.y, listProgressDx, rcRow.dy);
            Rect rcFileName(rcThumb.x + rcThumb.dx + kHomeListRowGapDx, rcRow.y,
                            rcProgress.x - (rcThumb.x + rcThumb.dx + kHomeListRowGapDx) - kHomeListRowGapDx, rcRow.dy);
            if (isRtl) {
                thumb.rcFileNumber.x = rcRow.Right() - numberSize.dx;
                rcThumb.x = thumb.rcFileNumber.x - kHomeListRowGapDx - rcThumb.dx;
                rcPin.x = rcRow.x;
                rcRemove.x = rcPin.x + listIconDx + listIconGap;
                rcSize.x = rcRemove.x + listIconDx + listIconGap;
                rcProgress.x = rcSize.x + rcSize.dx + listProgressGap;
                rcFileName.x = rcProgress.x + rcProgress.dx + kHomeListRowGapDx;
                rcFileName.dx = rcThumb.x - rcFileName.x - kHomeListRowGapDx;
            }
            rcFileName.dx = std::max(rcFileName.dx, 0);
            // rcFileName is the whole name+path span; MeasureHomeListRowText()
            // splits it when the row is first painted. Doing it here would
            // measure text for every history entry on every layout, and doing it
            // only for rows that happen to be on screen *now* left rows scrolled
            // in later without their directory (#5870 follow-up).
            thumb.rcListThumb = rcThumb;
            thumb.rcListPin = rcPin;
            thumb.rcListRemove = rcRemove;
            thumb.rcListSize = rcSize;
            thumb.rcListProgress = rcProgress;
            thumb.rcListFileName = rcFileName;
            // already-cached in-memory thumb size only (no LoadThumbnail / disk)
            if (onScreen && fs->thumbnail) {
                thumb.szThumb = Size(fs->thumbnail->width, fs->thumbnail->height);
            }
        }
    } else {
        int thumbPrefetchY = HomeThumbDy() + kThumbsSpaceBetweenY;
        for (int idx = 0; idx < nFiles; idx++) {
            int sectionIdx = idx < l.nRecent ? idx : idx - l.nRecent;
            int row = sectionIdx / thumbsColsForLayout;
            int col = sectionIdx % thumbsColsForLayout;
            int groupOffset = idx < l.nRecent ? 0 : completedOffset;
            ThumbnailLayout& thumb = *VecAppendBlanks(l.thumbnails, 1);
            thumb.fileSize = kSizeNotFetched;
            FileState* fs = fileStates[idx];
            thumb.fs = fs;
            thumb.displayNumber = sectionIdx + 1;

            Rect rcPage(ptOff.x + (col * (HomeThumbDx() + kThumbsSpaceBetweenX)),
                        ptOff.y + groupOffset + (row * (HomeThumbDy() + kThumbsSpaceBetweenY)), HomeThumbDx(),
                        HomeThumbDy());
            if (isRtl) {
                rcPage.x = rc.dx - rcPage.x - rcPage.dx;
            }
            Rect rcCard = rcPage;
            bool onScreen = IsHomeThumbOnScreen(rcPage, l.rcThumbsArea, thumbPrefetchY);
            // only use already-resident thumbnails for aspect adjust — never LoadThumbnail
            // during layout (disk I/O dominated scroll/paint CPU)
            if (onScreen && fs->thumbnail) {
                Size szThumb(fs->thumbnail->width, fs->thumbnail->height);
                if (GetThumbnailMode() == ThumbnailMode::Cropped &&
                    (szThumb.dx != HomeThumbDx() || szThumb.dy != HomeThumbDy())) {
                    rcPage.dy = szThumb.dy * HomeThumbDx() / szThumb.dx;
                    rcPage.y += HomeThumbDy() - rcPage.dy;
                }
                thumb.szThumb = szThumb;
            }
            thumb.rcPage = rcPage;
            Size numberSize = HomeFileNumberSize(thumb.displayNumber);
            int margin = UiScalePx(4);
            thumb.rcFileNumber = {isRtl ? rcPage.Right() - numberSize.dx - margin : rcPage.x + margin,
                                  rcPage.y + margin, numberSize.dx, numberSize.dy};
            int iconSpace = HomeFileIconDy() + UiScalePx(4);
            Rect rcText(rcCard.x + iconSpace, rcCard.Bottom() + kThumbsCaptionGapY, rcCard.dx - iconSpace,
                        kThumbsCaptionDy);
            if (isRtl) {
                rcText.x -= iconSpace;
            }
            thumb.rcText = rcText;
        }
    }

    // layout tip at the bottom
    if (tip) {
        Rect rcClient = HwndClientRect(win->hwndCanvas);
        int tipPadding = UiScalePx(8);

        int tipY = gSettings->homePageFixedHeader
                       ? rcClient.dy - tipHeight
                       : std::max(rcClient.dy - tipHeight, thumbsTopY + thumbsContentDy + kThumbsMiddleMargin);
        // background spans full window width
        l.rcTip = {0, tipY, rcClient.dx, tipHeight};
        l.hasTip = true;

        // text area aligned with thumbnails
        int tipStartX = thumbsStartX;
        int tipStartY = tipY + tipPadding;
        l.rcTipText = {tipStartX, tipStartY, thumbsContentWidth, tip->MinIntrinsicHeight(thumbsContentWidth)};
    }
    if (!gSettings->homePageFixedHeader) OffsetHomeChrome(l, -scrollY);
}

#if IS_DEBUG
static bool HomeSurfaceCaptures();
static void UpdateHomeOverlayScrollbar(MainWindow* win);

bool HomePage_UnitTestsCompactHeader() {
    Settings* saved = gSettings;
    Vec<FileState*>* savedHistory = FileHistoryStates();
    int savedDpiX = dpiX;
    int savedDpiY = dpiY;
    gSettings = NewSettings({});
    FileHistorySetStates(gSettings->fileStates);
    if (!ThemeGetCount()) CreateThemeCommands();
    DpiSet(96, 96);
    bool ok = true;
    auto check = [&](bool passed, int line) {
        if (!passed) logf("Home compact header check failed at %d\n", line);
        ok &= passed;
    };
    {
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        win.hwndCanvas = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1100, 800, nullptr, nullptr,
                                         GetModuleHandle(nullptr), nullptr);
        check(win.hwndCanvas && !IsWindowVisible(win.hwndCanvas), __LINE__);
        EnsureHomeSearchCreated(&win);
        check(win.homeSearch && !win.homeSearch->createdWithFrame && !win.homeSearch->createdWithBorder &&
                  !win.homeSearch->createdWithBottomBorder,
              __LINE__);
        check(!(GetWindowLongPtrW(win.homeSearch->hwnd, GWL_STYLE) & WS_BORDER), __LINE__);
        check(!(GetWindowLongPtrW(win.homeSearch->hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE), __LINE__);
        Tooltip::CreateArgs tipArgs;
        tipArgs.parent = win.hwndCanvas;
        tipArgs.font = GetAppFont();
        tipArgs.isRtl = IsUIRtl();
        win.infotip = new Tooltip();
        check(win.infotip->Create(tipArgs) && !IsWindowVisible(win.infotip->hwnd), __LINE__);
        for (Size size : {Size{1100, 800}, Size{760, 600}, Size{420, 700}}) {
            HomePageLayout layout;
            layout.win = &win;
            layout.rc = {0, 0, size.dx, size.dy};
            LayoutHomePage(layout);
            auto* logo = HomeChrome(&win)->logo;
            check(logo->font->GetStyle() == PlatformFontStyle::Bold, __LINE__);
            check(layout.rcLogo.x >= 0 && layout.rcLogo.Right() <= size.dx, __LINE__);
            check(layout.rcLogo.y <= UiScalePx(24), __LINE__);
            check(layout.rcSearchBorder.y >= layout.rcLogo.Bottom(), __LINE__);
            check(layout.freqRead->lastBounds.Right() + UiScalePx(8) <= layout.rcIconThumbnailView.x, __LINE__);
            Rect subtitle = HomeSubtitleBox(layout.rc, layout.rcLogo);
            check(layout.rcSearchBorder.y >= subtitle.Bottom(), __LINE__);
            check(HomePageFont(21)->GetStyle() == PlatformFontStyle::Regular, __LINE__);
            HomeActionsLayout actions = MeasureHomeActions(std::max(1, size.dx - UiScalePx(48)), HomeChrome(&win));
            for (Rect item : actions.items) {
                check(item.x >= 0 && item.Right() <= size.dx - UiScalePx(48) && item.dy > 0, __LINE__);
            }
            check(!layout.rcZoomControls.IsEmpty() && !layout.rcCompletedHeader.IsEmpty(), __LINE__);
            check(layout.rcZoomControls.dy == layout.rcIconThumbnailView.dy, __LINE__);
            check(layout.rcZoomControls.dx < UiScalePx(166), __LINE__);
            SaveHomeLayoutCache(layout, {}, 0);
            HomePageSyncChrome(layout);
            HomeChromeCtrl* chrome = HomeChrome(&win);
            Rect palette = chrome->paletteBtn->BoundsInWindow();
            Rect shortcuts = chrome->helpBtn->BoundsInWindow();
            Rect left = IsUIRtl() ? shortcuts : palette;
            Rect right = IsUIRtl() ? palette : shortcuts;
            check(chrome->paletteBtn->HasFlag(vwfFocusable) && chrome->helpBtn->HasFlag(vwfFocusable), __LINE__);
            check(left.x >= 0 && right.Right() <= size.dx, __LINE__);
            check(left.Right() + UiScalePx(8) == layout.rcSearchBorder.x, __LINE__);
            check(layout.rcSearchBorder.Right() + UiScalePx(8) == right.x, __LINE__);
            check(layout.rcSearchBorder.dy >= UiScalePx(60) && left.dx >= UiScalePx(64), __LINE__);
            check(chrome->openDoc->text->font->GetStyle() == PlatformFontStyle::Regular &&
                      chrome->resumeBtn->font->GetStyle() == PlatformFontStyle::Regular,
                  __LINE__);
        }
        HomeChromeCtrl* chrome = HomeChrome(&win);
        Str populatedSummary = StrL("Learning Hub: 3886 due for review");
        HomeActionsLayout wideActions = MeasureHomeActions(UiScalePx(1600), chrome, populatedSummary);
        int padX = UiScalePx(kHomeButtonPadX);
        check(wideActions.items[0].y == wideActions.items[1].y && wideActions.items[0].y == wideActions.items[2].y &&
                  wideActions.items[3].y == wideActions.items[4].y && wideActions.items[3].y == wideActions.items[5].y,
              __LINE__);
        check(
            wideActions.items[3].dx == HomeLabelSize(HomePageFont(kHomeActionTextSize), populatedSummary).dx + 2 * padX,
            __LINE__);
        HomeActionsLayout narrowActions = MeasureHomeActions(UiScalePx(260), chrome, populatedSummary);
        check(narrowActions.height > wideActions.height, __LINE__);
        for (Rect item : narrowActions.items) check(item.x >= 0 && item.Right() <= UiScalePx(260), __LINE__);
        check(HomeSearchFontPx(96) == MulDiv(UiFontSizePxForDpi(96, kHomeSearchTextSize), 4, 5), __LINE__);
        int zoom = 0;
        check(ParseHomeZoom(StrL("175%"), zoom) && zoom == 175, __LINE__);
        check(ParseHomeZoom(StrL(" 120 % "), zoom) && zoom == 120, __LINE__);
        check(ParseHomeZoom(StrL("99999999999999999%"), zoom) && zoom == kHomeZoomMax, __LINE__);
        check(ParseHomeZoom(StrL("-100%"), zoom) && zoom == kHomeZoomMin, __LINE__);
        check(!ParseHomeZoom(StrL("120.5%"), zoom) && !ParseHomeZoom(StrL("abc"), zoom), __LINE__);
        check(!ParseHomeZoom(StrL("%"), zoom) && !ParseHomeZoom({}, zoom), __LINE__);
        for (int i = 0; i < 40; i++) {
            FileHistoryAppend(NewFileState(fmt("C:\\Reading\\Lecture-%02d.pdf", i)));
        }
        gSettings->homePageMaxRecentItems = 30;
        str::ReplaceWithCopy(&gSettings->homePageViewMode, StrL("list"));
        HomePageLayout recent;
        recent.win = &win;
        recent.rc = {0, 0, 1100, 800};
        LayoutHomePage(recent);
        check(len(recent.thumbnails) == 30 && recent.totalContentDy > recent.thumbsVisibleDy, __LINE__);
        SaveHomeLayoutCache(recent, {}, 0);
        UpdateHomeOverlayScrollbar(&win);
        check(win.overlayScrollV && win.overlayScrollV->nMax == recent.totalContentDy - 1, __LINE__);
        win.homePageScrollY = INT_MAX;
        HomePageLayout bottom;
        bottom.win = &win;
        bottom.rc = recent.rc;
        LayoutHomePage(bottom);
        check(win.homePageScrollY == bottom.totalContentDy - bottom.thumbsVisibleDy, __LINE__);
        check(bottom.thumbnails[29].rcListRow.Bottom() <= bottom.rcThumbsArea.Bottom(), __LINE__);
        check(bottom.rcLogo.y == recent.rcLogo.y - win.homePageScrollY, __LINE__);
        for (bool fixed : {false, true}) {
            gSettings->homePageFixedHeader = fixed;
            win.homePageScrollY = 0;
            HomePageLayout start;
            start.win = &win;
            start.rc = recent.rc;
            LayoutHomePage(start);
            SaveHomeLayoutCache(start, {}, 0);
            int scroll = std::min(UiScalePx(100), start.totalContentDy - start.thumbsVisibleDy);
            check(scroll > 0, __LINE__);
            HomePageLayout cached;
            cached.win = &win;
            cached.rc = start.rc;
            ApplyHomeLayoutCache(cached, scroll);
            check(cached.rcLogo.y == start.rcLogo.y - (fixed ? 0 : scroll), __LINE__);
            check(cached.thumbnails[0].rcListRow.y == start.thumbnails[0].rcListRow.y - scroll, __LINE__);
            win.homePageScrollY = scroll;
            HomePageLayout fresh;
            fresh.win = &win;
            fresh.rc = start.rc;
            LayoutHomePage(fresh);
            check(fresh.rcLogo == cached.rcLogo && fresh.rcSearchBorder == cached.rcSearchBorder, __LINE__);
            check(fresh.rcIconHeaderPin == cached.rcIconHeaderPin && fresh.rcIconFullPage == cached.rcIconFullPage,
                  __LINE__);
            check(fresh.rcFeatures == cached.rcFeatures && fresh.rcTip == cached.rcTip, __LINE__);
            check(fresh.thumbnails[0].rcListRow == cached.thumbnails[0].rcListRow, __LINE__);
            check(HomeChrome(&win)->headerPin->HasFlag(vwfFocusable), __LINE__);
        }
        check(FileThumbnails_UnitTests(), __LINE__);
        gSettings->homePageFixedHeader = false;
        str::ReplaceWithCopy(&gSettings->homePageViewMode, StrL("thumbnails"));
        win.homePageScrollY = 0;
        gSettings->homePageFullPageThumbnails = true;
        HomePageLayout fullPage;
        fullPage.win = &win;
        fullPage.rc = recent.rc;
        LayoutHomePage(fullPage);
        check(fullPage.thumbnails[0].rcPage.dy > fullPage.thumbnails[0].rcPage.dx, __LINE__);
        check(fullPage.thumbnails[0].rcText.y >= fullPage.thumbnails[0].rcPage.Bottom(), __LINE__);
        FileState* completedA = fullPage.thumbnails[0].fs;
        FileState* completedB = fullPage.thumbnails[1].fs;
        completedA->completed = completedB->completed = true;
        HomePageLayout grouped;
        grouped.win = &win;
        grouped.rc = recent.rc;
        LayoutHomePage(grouped);
        check(grouped.nRecent == 30 && len(grouped.thumbnails) == 32, __LINE__);
        check(grouped.thumbnails[0].displayNumber == 1 && grouped.thumbnails[29].displayNumber == 30, __LINE__);
        check(grouped.thumbnails[30].displayNumber == 1 && grouped.thumbnails[31].displayNumber == 2, __LINE__);
        check(grouped.thumbnails[0].rcPage.Intersect(grouped.thumbnails[0].rcFileNumber) ==
                  grouped.thumbnails[0].rcFileNumber,
              __LINE__);
        check(grouped.thumbnails[30].fs == completedA && grouped.thumbnails[31].fs == completedB, __LINE__);
        check(grouped.thumbnails[29].rcText.Bottom() < grouped.rcCompletedHeader.y, __LINE__);
        check(grouped.rcCompletedHeader.Bottom() < grouped.thumbnails[30].rcPage.y, __LINE__);
        SaveHomeLayoutCache(grouped, {}, 0);
        check(HomeGridMoveRow(&win, 29, 1) == 31, __LINE__);
        check(HomeGridMoveRow(&win, 31, -1) == 29, __LINE__);
        completedA->completed = completedB->completed = false;
        Vec<FileState*> restored;
        StrVec filter;
        CollectHomePageFiles(&win, restored, filter);
        check(!HomeLayoutCacheFilesMatch(&win, restored), __LINE__);
        SaveHomeLayoutCache(fullPage, {}, 0);
        gSettings->homePageFullPageThumbnails = false;
        check(!HomeLayoutCacheMatches(&win, fullPage.rc, {}), __LINE__);
        for (bool full : {false, true}) {
            gSettings->homePageFullPageThumbnails = full;
            gSettings->homePageThumbnailPadding = true;
            win.homePageScrollY = 0;
            HomePageLayout padded;
            padded.win = &win;
            padded.rc = recent.rc;
            LayoutHomePage(padded);
            SaveHomeLayoutCache(padded, {}, 0);
            gSettings->homePageThumbnailPadding = false;
            check(!HomeLayoutCacheMatches(&win, padded.rc, {}), __LINE__);
            HomePageLayout compact;
            compact.win = &win;
            compact.rc = recent.rc;
            LayoutHomePage(compact);
            check(compact.gridColumns >= padded.gridColumns, __LINE__);
            check(len(compact.thumbnails) == len(padded.thumbnails) && compact.nRecent == padded.nRecent, __LINE__);
            check(compact.thumbnails[0].rcPage.Size() == padded.thumbnails[0].rcPage.Size(), __LINE__);
            check(compact.thumbnails[0].rcText.y >= compact.thumbnails[0].rcPage.Bottom(), __LINE__);
            check(compact.thumbnails[0].rcText.Bottom() < compact.thumbnails[compact.gridColumns].rcPage.y, __LINE__);
        }
        gSettings->homePageThumbnailPadding = true;
        gSettings->homePageFullPageThumbnails = false;
        str::ReplaceWithCopy(&gSettings->homePageViewMode, StrL("list"));
        // Thumbnail completion clears the layout and schedules paint. Dispatch
        // a selection key and resolve Enter/Delete's path before any paint.
        TempStr selectedPath = str::DupTemp(recent.thumbnails[1].fs->filePath);
        win.homePageScrollY = 0;
        win.homePageSelIdx = 0;
        ClearHomeLayoutCache(&win);
        HomePageMoveSelection(&win, 0, 1);
        check(HomeLayout(&win).valid && win.homePageSelIdx == 1, __LINE__);
        ClearHomeLayoutCache(&win);
        check(str::Eq(HomePageSelectedFilePathTemp(&win), selectedPath), __LINE__);
        check(HomeLayout(&win).valid, __LINE__);
        delete win.infotip;
        win.infotip = nullptr;
        HWND canvas = win.hwndCanvas;
        HomePageDestroySearch(&win);
        HomePageDestroyChrome(&win);
        win.hwndCanvas = nullptr;
        DestroyWindow(canvas);
    }
    ok &= HomeSurfaceCaptures();
    DeleteSettings(gSettings);
    gSettings = saved;
    FileHistorySetStates(savedHistory);
    dpiX = savedDpiX;
    dpiY = savedDpiY;
    RefreshUiFonts();
    return ok;
}
#endif

static void GetFileStateIcon(FileState* fs) {
    if (fs->himl) {
        return;
    }
    SHFILEINFO sfi{};
    sfi.iIcon = -1;
    uint flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
    WCHAR* filePathW = CWStrTemp(fs->filePath);
    fs->himl = (HIMAGELIST)SHGetFileInfoW(filePathW, 0, &sfi, sizeof(sfi), flags);
    fs->iconIdx = sfi.iIcon;
}

struct HomeFileIcon {
    HomeFileIcon* next = nullptr;
    HIMAGELIST imageList = nullptr;
    int iconIdx = -1;
    int targetDy = 0;
    Pixmap* pixmap = nullptr;
};

static HomeFileIcon* gFileIcons = nullptr;

static void FreeHomeFileIcons() {
    for (HomeFileIcon* icon = gFileIcons; icon; icon = icon->next) {
        FreePixmap(icon->pixmap);
    }
    ListDelete(gFileIcons);
    gFileIcons = nullptr;
}

// Shell image lists are Windows drawing objects. Convert each distinct icon to
// a Pixmap once so home-page painting stays entirely within Gfx.
static Pixmap* GetFileStateIconPixmap(FileState* fs, int targetDy) {
    GetFileStateIcon(fs);
    if (!fs->himl || fs->iconIdx < 0) {
        return nullptr;
    }
    for (HomeFileIcon* icon = gFileIcons; icon; icon = icon->next) {
        if (icon->imageList == fs->himl && icon->iconIdx == fs->iconIdx && icon->targetDy == targetDy) {
            return icon->pixmap;
        }
    }

    HIMAGELIST imageList = fs->himl;
    int index = fs->iconIdx;
    if (targetDy > GetSystemMetrics(SM_CXSMICON)) {
        SHFILEINFO info{};
        HIMAGELIST large = (HIMAGELIST)SHGetFileInfoW(CWStrTemp(fs->filePath), 0, &info, sizeof(info),
                                                      SHGFI_SYSICONINDEX | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
        if (large && info.iIcon >= 0) {
            imageList = large;
            index = info.iIcon;
        }
    }
    HICON hicon = ImageList_GetIcon(imageList, index, ILD_TRANSPARENT);
    Pixmap* pixmap = nullptr;
    if (hicon) {
        pixmap = PixmapFromHICON(hicon);
        DestroyIcon(hicon);
    }
    auto* icon = new HomeFileIcon();
    icon->imageList = fs->himl;
    icon->iconIdx = fs->iconIdx;
    icon->targetDy = targetDy;
    icon->pixmap = pixmap;
    ListInsertFront(&gFileIcons, icon);
    return pixmap;
}

// --- Close (✕) button for Frequently Read thumbnails (issue #283, #5745) ---
//
// Drawn onto the home-page canvas (over the top-right corner of the thumbnail
// under the mouse) rather than as a separate top-level window. The separate
// window could be left behind, drawing stray crosses over a document (#5745).
// Styled like the tab close button (gray X on a white circle; red circle +
// white X on hover). It is a HomeCloseBtnCtrl in the chrome tree, shown on the
// entry the mouse is on.

static void DrawHomeViewButton(Gfx* gfx, Pixmap* icon, Rect r, bool selected, bool hovered) {
    if (selected || hovered) {
        Color bg = selected ? ThemeControlBackgroundColor() : ThemeMainWindowBackgroundColor();
        if (hovered) {
            bg = AccentColor(bg, 20);
        }
        gfx->FillRect(r, bg);
        if (selected) {
            gfx->DrawRect(r, AccentColor(bg, 40));
        }
    }
    if (icon) {
        gfx->DrawPixmap(icon, {r.x, r.y, icon->width, icon->height});
    }
}

static Rect FitRectInRect(Size src, Rect dst) {
    if (src.dx <= 0 || src.dy <= 0 || dst.dx <= 0 || dst.dy <= 0) {
        return dst;
    }
    int dx = dst.dx;
    int dy = src.dy * dx / src.dx;
    if (dy > dst.dy) {
        dy = dst.dy;
        dx = src.dx * dy / src.dy;
    }
    Rect r(dst.x + ((dst.dx - dx) / 2), dst.y + ((dst.dy - dy) / 2), dx, dy);
    return r;
}

static TempStr FileSizeForHomeListTemp(i64 size) {
    if (size < 0) {
        return str::DupTemp(StrL(""));
    }
    return str::FormatSizeShortTemp(size, nullptr);
}

// light blue outline marking the keyboard-selected entry (issue #1136).
// A fixed color: it has to read as "selected" against both the light and the
// dark page background
constexpr Color kHomeSelectionColor = MkRgb(0x4c, 0xa6, 0xff);

static void DrawHomeItemEdge(Gfx* gfx, const Rect& r, bool selected) {
    float width = 0.8f * DpiGet() / 72.0f * GetUiScale();
    gfx->FillRoundedRect(r, UiScalePx(10), kColorTransparent,
                         selected ? kHomeSelectionColor : ThemeWindowTextDisabledColor(), width);
}

// Give the file name the width it needs and put the directory path in what's
// left, right-aligned (mirrored for RTL). Done on first paint of a row, not
// during layout: measuring every history entry made layout (and so scrolling)
// slow, and measuring only the rows visible at layout time meant rows scrolled
// into view later never got a directory.
static void MeasureHomeListRowText(Gfx* gfx, ThumbnailLayout& thumb, PlatformFont* font, bool isRtl) {
    if (thumb.listTextMeasured) {
        return;
    }
    thumb.listTextMeasured = true;

    Rect rcFileName = thumb.rcListFileName;
    TempStr fileName = path::GetBaseNameTemp(thumb.fs->filePath);
    int nameDx = gfx->MeasureText(fileName, font).dx + UiScalePx(4);
    int minPathDx = UiScalePx(80);
    if (nameDx + kHomeListRowGapDx + minPathDx > rcFileName.dx) {
        // no room for a path, the name gets the whole span
        return;
    }
    int pathDx = rcFileName.dx - nameDx - kHomeListRowGapDx;
    if (isRtl) {
        thumb.rcListPath = Rect(rcFileName.x, rcFileName.y, pathDx, rcFileName.dy);
        rcFileName.x = rcFileName.x + rcFileName.dx - nameDx;
    } else {
        thumb.rcListPath = Rect(rcFileName.x + nameDx + kHomeListRowGapDx, rcFileName.y, pathDx, rcFileName.dy);
    }
    rcFileName.dx = nameDx;
    thumb.rcListFileName = rcFileName;
}

// True when keyboard focus is in the home search box (hide list selection then).
static bool HomeSearchHasFocus(MainWindow* win) {
    return win && win->homeSearch && GetFocus() == win->homeSearch->hwnd;
}

static void DrawHomeFileNumber(Gfx* gfx, const ThumbnailLayout& thumb) {
    gfx->FillRects(&thumb.rcFileNumber, 1, kColBlack, 160);
    gfx->DrawText(fmt("%d", thumb.displayNumber), thumb.rcFileNumber, gfxTextCenter | gfxTextVCenter, HomePageFont(11),
                  kColWhite);
}

static void DrawHomeListRow(Gfx* gfx, ThumbnailLayout& thumb, const StrVec& filterWords, Vec<u8>& highlighted,
                            PlatformFont* fontText, Color backgroundColor, bool isRtl, bool isSelected) {
    FileState* fs = thumb.fs;
    Rect row = thumb.rcListRow;
    gfx->FillRoundedRect(row, UiScalePx(10), ThemeControlBackgroundColor());
    DrawHomeItemEdge(gfx, HomeSelectionOutlineRect(thumb), isSelected);
    backgroundColor = ThemeControlBackgroundColor();
    MeasureHomeListRowText(gfx, thumb, fontText, isRtl);
    DrawHomeFileNumber(gfx, thumb);

    if (gShowListSeparatorLine) {
        Color lineCol = AccentColor(ThemeMainWindowBackgroundColor(), 30);
        gfx->DrawLine(Rect(row.x, row.y + row.dy - 1, row.dx, 0), lineCol);
    }

    // LoadThumbnail only hits disk the first time; result stays on fs->thumbnail
    RequestHomeThumbnail(fs);
    Pixmap* thumbImg = LoadThumbnail(fs);
    Rect thumbBox = thumb.rcListThumb;
    if (thumbImg) {
        Size szThumb(thumbImg->width, thumbImg->height);
        Rect thumbDst = FitRectInRect(szThumb, thumbBox);
        gfx->DrawPixmap(thumbImg, thumbDst);
        thumb.szThumb = szThumb;
    }
    Str path = fs->filePath;
    TempStr fileName = path::GetBaseNameTemp(path);
    u32 nameFmt = gfxTextEllipsis | gfxTextVCenter | (isRtl ? gfxTextRight : gfxTextLeft);
    DrawMaybeHighlightedText(gfx, thumb.rcListFileName, fileName, filterWords, highlighted, backgroundColor, isRtl,
                             false, nameFmt, fontText, ThemeWindowTextColor());

    // directory path, right-aligned and muted, in the space the file name doesn't need.
    // Must use DrawTextW: dirPath is UTF-8; DrawTextA treated it as the system ANSI
    // code page and mangled non-ASCII path characters (#5824).
    if (!thumb.rcListPath.IsEmpty()) {
        TempStr dirPath = path::GetDirTemp(path);
        u32 pathFmt = gfxTextVCenter | gfxTextPathEllipsis | (isRtl ? gfxTextLeft : gfxTextRight);
        Rect pathRect = thumb.rcListPath;
        gfx->DrawText(dirPath, pathRect, pathFmt, fontText, ThemeWindowTextDisabledColor());
    }

    // file::GetSize once per row, then cache on ThumbnailLayout (scroll reuses
    // it). kSizeNotFetched = not tried; kSizeFetchFail = GetSize failed; >= 0
    // is a real size (including empty files).
    if (thumb.fileSize == kSizeNotFetched) {
        i64 sz = file::GetSize(path);
        thumb.fileSize = (sz < 0) ? kSizeFetchFail : sz;
    }
    TempStr fileSize = FileSizeForHomeListTemp(thumb.fileSize);
    u32 sizeFmt = gfxTextVCenter | gfxTextEllipsis | (isRtl ? gfxTextLeft : gfxTextRight);
    Rect sizeRect = thumb.rcListSize;
    gfx->DrawText(fileSize, sizeRect, sizeFmt, fontText, ThemeWindowTextColor());

    if (!thumb.rcListProgress.IsEmpty()) {
        TempStr progress = FormatFileStateProgressTemp(fs);
        if (len(progress) > 0) {
            u32 progFmt = gfxTextVCenter | gfxTextEllipsis | (isRtl ? gfxTextLeft : gfxTextRight);
            gfx->DrawText(progress, thumb.rcListProgress, progFmt, fontText, ThemeWindowTextColor());
        }
    }

    if (fs->isPinned) {
        gfx->FillRect(thumb.rcListPin, ThemeControlBackgroundColor());
    }
    {
        int pinDx = thumb.rcListPin.dx > 0 ? thumb.rcListPin.dx : UiScalePx(16);
        int pinDy = thumb.rcListPin.dy > 0 ? thumb.rcListPin.dy : pinDx;
        Pixmap* pin = GetCachedPixmapForSvg(Str(GetPinIconSvg()), pinDx, pinDy);
        if (pin) {
            gfx->DrawPixmap(pin, thumb.rcListPin);
        }
    }
}

// one thumbnail: the page image with a rounded outline, the caption (with
// search-filter highlights) and the file-type icon
static void DrawHomeThumbnail(Gfx* gfx, ThumbnailLayout& thumb, const StrVec& filterWords, Vec<u8>& highlighted,
                              PlatformFont* fontText, Color backgroundColor, bool isRtl, bool isSelected) {
    FileState* fs = thumb.fs;
    const Rect& page = thumb.rcPage;
    bool padded = gSettings->homePageThumbnailPadding;
    if (padded) {
        Rect card = page.Union(thumb.rcText);
        card.Inflate(UiScalePx(10), UiScalePx(10));
        Rect shadow = card;
        shadow.y += MulDiv(UiScalePx(2), 3, 4);
        gfx->FillRoundedRect(shadow, UiScalePx(16), ThemeEdgeColor());
        gfx->FillRoundedRect(card, UiScalePx(16), ThemeControlBackgroundColor());
        backgroundColor = ThemeControlBackgroundColor();
    }
    // disk load only first time; stays on fs->thumbnail afterwards
    RequestHomeThumbnail(fs);
    Pixmap* thumbImg = LoadThumbnail(fs);
    if (thumbImg) {
        thumb.szThumb = Size(thumbImg->width, thumbImg->height);
        gfx->PushClip(page);
        // note: we used to invert bitmaps in dark theme but that doesn't
        // make sense for thumbnails
        Rect dst = GetThumbnailMode() == ThumbnailMode::FullPage ? FitRectInRect(thumb.szThumb, page) : page;
        gfx->DrawPixmap(thumbImg, dst);
        gfx->PopClip();
    }
    if (!isSelected) {
        DrawHomeItemEdge(gfx, HomeSelectionOutlineRect(thumb), false);
    }

    gfx->PushClip(page);
    DrawHomeFileNumber(gfx, thumb);
    gfx->PopClip();

    if (fs->isPinned) {
        int margin = UiScalePx(4);
        int padding = UiScalePx(4);
        int badgeSize = gfx->MeasureText(StrL("1"), HomePageFont(11)).dy + 2 * UiScalePx(2);
        int x = isRtl ? page.x + margin : page.Right() - badgeSize - margin;
        Rect badge{x, page.y + margin, badgeSize, badgeSize};
        int iconSize = std::max(1, badgeSize - 2 * padding);
        Pixmap* pin = GetCachedPixmapForSvg(Str(GetPinIconSvg()), iconSize, iconSize, kColWhite);
        if (pin) {
            gfx->PushClip(page);
            gfx->FillRects(&badge, 1, MkRgb(0, 0, 0), 160);
            gfx->DrawPixmap(pin, {badge.x + padding, badge.y + padding, iconSize, iconSize});
            gfx->PopClip();
        }
    }

    if (gSettings && gSettings->showHomePageReadingProgress) {
        TempStr progress = FormatFileStateProgressTemp(fs);
        if (len(progress) > 0) {
            PlatformFont* fontProg = HomePageFont(11);
            Size sz = gfx->MeasureText(progress, fontProg);
            int padX = UiScalePx(5);
            int padY = UiScalePx(2);
            int dx = sz.dx + (2 * padX);
            int dy = sz.dy + (2 * padY);
            int margin = UiScalePx(4);
            int x = isRtl ? page.x + margin : page.x + page.dx - dx - margin;
            int y = page.y + page.dy - dy - margin;
            Rect badge(x, y, dx, dy);
            gfx->FillRects(&badge, 1, MkRgb(0, 0, 0), 160);
            gfx->DrawText(progress, badge, gfxTextCenter | gfxTextVCenter, fontProg, kColWhite);
        }
    }

    const Rect& rect = thumb.rcText;
    Str path = fs->filePath;
    TempStr fileName = path::GetBaseNameTemp(path);
    u32 fmt = gfxTextEllipsis | gfxTextVCenter | (isRtl ? gfxTextRight : gfxTextLeft);
    DrawMaybeHighlightedText(gfx, rect, fileName, filterWords, highlighted, backgroundColor, isRtl, false, fmt,
                             fontText, ThemeWindowTextColor());

    int iconDy = HomeFileIconDy();
    Pixmap* icon = GetFileStateIconPixmap(fs, iconDy);
    if (icon) {
        gfx->DrawPixmap(icon, HomeFileIconBounds(rect, {icon->width, icon->height}, iconDy, UiScalePx(4), isRtl));
    }
}

// a white circle with either a centered SVG or a black glyph ("?"). Drawn with
// GDI+ so the disc edge is smooth; nothing is painted outside it, so the page
// background shows through.
static void DrawHomeCircleButton(Gfx* gfx, Rect r, Pixmap* icon, Str glyph, Color background) {
    gfx->FillEllipse(r, background);
    if (icon) {
        int x = r.x + ((r.dx - icon->width) / 2);
        int y = r.y + ((r.dy - icon->height) / 2);
        gfx->DrawPixmap(icon, {x, y, icon->width, icon->height});
        return;
    }
    PlatformFont* font = HomePageFont(14);
    gfx->DrawText(glyph, r, gfxTextCenter | gfxTextVCenter, font, ThemeWindowTextColor());
}

// Slack so the first/last row's rounded outline isn't cut by rcThumbsArea.
// The search field and tip band stay outside this clip (issue #5978).
static Rect HomeOutlinePaintClip(const Rect& thumbsArea, const Rect& searchBorder, const Rect& tip, bool hasTip) {
    if (thumbsArea.IsEmpty()) {
        return {};
    }
    Rect clip = thumbsArea;
    clip.Inflate(0, UiScalePx(8));
    if (!searchBorder.IsEmpty() && clip.y < searchBorder.Bottom()) {
        int d = searchBorder.Bottom() - clip.y;
        clip.y += d;
        clip.dy -= d;
    }
    if (hasTip && !tip.IsEmpty() && clip.y + clip.dy > tip.y) {
        clip.dy = tip.y - clip.y;
    }
    if (clip.dx <= 0 || clip.dy <= 0) {
        return {};
    }
    return clip;
}

static TempStr RectCsvTemp(const Rect& r) {
    return fmt("%d,%d,%d,%d", r.x, r.y, r.dx, r.dy);
}

// The home page's keyboard state: which entry the arrows have selected, how
// many entries are currently shown (the search box filters them) and whether
// the search box has the focus. A test driving the home page with keys waits on
// this instead of sleeping after each key, which is what made tests/issue-1136
// flaky: a key posted while focus was still moving went to the wrong window.
// search / outline / outlineFull are canvas rects so a test can check that the
// hover outline does not paint over the search field (issue #5978).
TempStr HomeSelectionResultTemp(int* exitCodeOut) {
    auto finish = [&](int code, TempStr s) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return s;
    };
    MainWindow* win = len(gWindows) > 0 ? gWindows[0] : nullptr;
    if (!win) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-window")));
    }
    auto& c = HomeLayout(win);
    if (!c.valid) {
        return finish(2, str::DupTemp(StrL("NOTREADY no-layout")));
    }
    int sel = win->homePageSelIdx;
    Str path;
    if (sel >= 0 && sel < len(c.thumbs) && c.thumbs[sel].fs) {
        path = c.thumbs[sel].fs->filePath;
    }
    int searchFocus = HomeSearchHasFocus(win) ? 1 : 0;
    // the search box is created while the home page lays out; until it exists
    // Up from the first row has nowhere to move the focus to
    int searchBox = win->homeSearch ? 1 : 0;
    Rect search = c.rcSearchBorder;
    Rect outlineFull;
    Rect outline;
    bool showSel = !HomePageIsListView() && !searchFocus && sel >= 0 && sel < len(c.thumbs);
    if (showSel) {
        outlineFull = HomeSelectionOutlineRect(c.thumbs[sel]);
        outline = outlineFull.Intersect(HomeOutlinePaintClip(c.rcThumbsArea, c.rcSearchBorder, c.rcTip, c.hasTip));
    }
    // caption rect of the last thumbnail: with the band scrolled to the bottom
    // it must fit inside the thumbs area (issue #6234)
    Rect lastCaption;
    if (!HomePageIsListView() && len(c.thumbs) > 0) {
        lastCaption = c.thumbs[len(c.thumbs) - 1].rcText;
    }
    // the tip shown (promo or tip, index) and its band, for picking another one
    Rect tipRect = c.hasTip ? c.rcTip : Rect{};
    return finish(0, fmt("OK sel=%d entries=%d searchFocus=%d searchBox=%d search=%s outline=%s outlineFull=%s path=%s "
                         "listView=%d listIcon=%s thumbsArea=%s lastCaption=%s tip=%d,%d tipRect=%s",
                         sel, len(c.thumbs), searchFocus, searchBox, RectCsvTemp(search), RectCsvTemp(outline),
                         RectCsvTemp(outlineFull), path, HomePageIsListView() ? 1 : 0, RectCsvTemp(c.rcIconListView),
                         RectCsvTemp(c.rcThumbsArea), RectCsvTemp(lastCaption), gSelectedIsPromo ? 1 : 0,
                         gSelectedTipIdx, RectCsvTemp(tipRect)));
}

TempStr HomeOptionsResultTemp(int* exitCodeOut) {
    MainWindow* win = len(gWindows) > 0 ? gWindows[0] : nullptr;
    if (!win || !HomeLayout(win).valid) {
        if (exitCodeOut) *exitCodeOut = 2;
        return str::DupTemp(StrL("NOTREADY no-home-layout"));
    }
    auto& c = HomeLayout(win);
    Rect page;
    Size pixels;
    if (len(c.thumbs) > 0) {
        page = c.thumbs[0].rcPage;
        Pixmap* bitmap = c.thumbs[0].fs->thumbnail;
        if (bitmap) pixels = {bitmap->width, bitmap->height};
    }
    if (exitCodeOut) *exitCodeOut = 0;
    return fmt(
        "OK fixedHeader=%d fullPage=%d scrollY=%d maxScrollY=%d logo=%s search=%s pin=%s pageIcon=%s "
        "firstPage=%s thumbPixels=%d,%d",
        c.fixedHeader ? 1 : 0, c.fullPageThumbnails ? 1 : 0, c.scrollY,
        std::max(0, c.totalContentDy - c.thumbsVisibleDy), RectCsvTemp(c.rcLogo), RectCsvTemp(c.rcSearchBorder),
        RectCsvTemp(c.rcIconHeaderPin), RectCsvTemp(c.rcIconFullPage), RectCsvTemp(page), pixels.dx, pixels.dy);
}

static TempStr HomeLibraryResultTemp(int* exitCodeOut) {
    MainWindow* win = len(gWindows) > 0 ? gWindows[0] : nullptr;
    if (win && !HomeLayout(win).valid) HomePageRelayout(win);
    if (!win || !HomeLayout(win).valid) {
        if (exitCodeOut) *exitCodeOut = 2;
        return str::DupTemp(StrL("NOTREADY no-home-layout"));
    }
    auto& c = HomeLayout(win);
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (exitCodeOut) *exitCodeOut = 0;
    str::Builder out;
    out.Append(
        fmt("OK recent=%d completed=%d zoom=%d editing=%d zoomBox=%s minus=%s plus=%s completedHeader=%s "
            "completedEmpty=%s",
            c.nRecent, len(c.thumbs) - c.nRecent, HomeThumbPercent(), chrome->zoomEditing ? 1 : 0,
            RectCsvTemp(chrome->sizeLabel->BoundsInWindow()), RectCsvTemp(chrome->smallerBtn->BoundsInWindow()),
            RectCsvTemp(chrome->largerBtn->BoundsInWindow()), RectCsvTemp(c.rcCompletedHeader),
            RectCsvTemp(c.rcCompletedEmpty)));
    out.Append(
        fmt(" learning=%s dictionary=%s features=%s subtitle=%s subtitleBold=%d zoomBold=%d "
            "grid=%s page=%s caption=%s",
            RectCsvTemp(chrome->learning->BoundsInWindow()), RectCsvTemp(chrome->dictionary->BoundsInWindow()),
            RectCsvTemp(chrome->features->toggle->BoundsInWindow()), RectCsvTemp(HomeSubtitleBox(c.canvasRc, c.rcLogo)),
            HomePageFont(21)->GetStyle() == PlatformFontStyle::Bold ? 1 : 0,
            chrome->smallerBtn->font->GetStyle() == PlatformFontStyle::Bold ? 1 : 0, RectCsvTemp(c.rcIconThumbnailView),
            RectCsvTemp(len(c.thumbs) ? c.thumbs[0].rcPage : Rect{}),
            RectCsvTemp(len(c.thumbs) ? c.thumbs[0].rcText : Rect{})));
    out.Append(fmt(" padding=%d fullPage=%d searchBorder=%d palette=%s shortcuts=%s search=%s zoomIcons=%d",
                   gSettings->homePageThumbnailPadding ? 1 : 0, gSettings->homePageFullPageThumbnails ? 1 : 0,
                   win->homeSearch && win->homeSearch->HasBorder() ? 1 : 0,
                   RectCsvTemp(chrome->paletteBtn->BoundsInWindow()), RectCsvTemp(chrome->helpBtn->BoundsInWindow()),
                   RectCsvTemp(c.rcSearchBorder),
                   static_cast<HomeDepthButton*>(chrome->smallerBtn)->icon == gIconZoomOut &&
                           static_cast<HomeDepthButton*>(chrome->largerBtn)->icon == gIconZoomIn
                       ? 1
                       : 0));
    Size summary = HomeLabelSize(HomePageFont(kHomeActionTextSize), HomeLearningSummaryTemp());
    int wrappedDy = HomeLabelSize(HomePageFont(kHomeActionTextSize), HomeLearningSummaryTemp(),
                                  std::max(1, chrome->learning->lastBounds.dx - UiScalePx(2 * kHomeButtonPadX)))
                        .dy;
    out.Append(
        fmt(" logo=%s badge=%d searchTextPx=%d exploreBold=%d resumeBold=%d learningLines=%d due=%d "
            "shadowOffset=%d",
            RectCsvTemp(c.rcLogo), chrome->logo->BadgeSize(), HomeSearchFontPx(DpiGet()),
            chrome->openDoc->text->font->GetStyle() == PlatformFontStyle::Bold ? 1 : 0,
            chrome->resumeBtn->font->GetStyle() == PlatformFontStyle::Bold ? 1 : 0,
            (wrappedDy + std::max(1, summary.dy) - 1) / std::max(1, summary.dy), VocabularyDueCount({}),
            MulDiv(UiScalePx(3), 3, 4)));
    out.Append(fmt(" zoomBorder=%d zoomRadius=%d zoomEditor=%s",
                   chrome->zoomEdit && chrome->zoomEdit->HasBorder() ? 1 : 0, chrome->sizeLabel->cornerRadius,
                   RectCsvTemp(chrome->zoomEdit && chrome->zoomEditing ? chrome->zoomEdit->lastBounds : Rect{})));
    HomeActionsLayout naturalActions = MeasureHomeActions(UiScalePx(100000), chrome);
    int padX = UiScalePx(kHomeButtonPadX);
    out.Append(
        fmt("\nhomeActions explore=%s resume=%s word=%s expanded=%d searchBaseTextPx=%d actionPadX=%d "
            "actionTextPx=%d featureShow=%d featureHide=%d contentWidths=%d,%d,%d,%d,%d,%d",
            RectCsvTemp(chrome->openDoc->BoundsInWindow()), RectCsvTemp(chrome->resumeBtn->BoundsInWindow()),
            RectCsvTemp(chrome->wordOfDay->BoundsInWindow()), chrome->features->expanded ? 1 : 0,
            UiFontSizePx(kHomeSearchTextSize), UiScalePx(kHomeButtonPadX), UiFontSizePx(kHomeActionTextSize),
            str::Eq(chrome->features->toggle->s, HomeFeaturesLabel(false)) ? 1 : 0,
            str::Eq(chrome->features->toggle->s, HomeFeaturesLabel(true)) ? 1 : 0,
            naturalActions.items[0].dx - 2 * padX, naturalActions.items[1].dx - 2 * padX,
            naturalActions.items[2].dx - 2 * padX, naturalActions.items[3].dx - 2 * padX,
            naturalActions.items[4].dx - 2 * padX, naturalActions.items[5].dx - 2 * padX));
    for (int i = 0; i < len(c.thumbs); i++) {
        const auto& thumb = c.thumbs[i];
        out.Append(fmt("\nfileNumber idx=%d number=%d completed=%d rect=%s item=%s preview=%s name=%s file=%s", i,
                       thumb.displayNumber, i >= c.nRecent ? 1 : 0, RectCsvTemp(thumb.rcFileNumber),
                       RectCsvTemp(c.listView ? thumb.rcListRow : thumb.rcPage),
                       RectCsvTemp(c.listView ? thumb.rcListThumb : thumb.rcPage),
                       RectCsvTemp(c.listView ? thumb.rcListFileName : thumb.rcText),
                       path::GetBaseNameTemp(thumb.fs->filePath)));
    }
    return ToStrTemp(out);
}

// Dispatch the real Home input handler and snapshot its effect in one UI turn.
// A cross-process sent key followed by a queued snapshot can otherwise sample
// focus after an unrelated desktop activation instead of the key's effect.
TempStr HomeInputResultTemp(Str action, int value, int* exitCodeOut) {
    auto fail = [&](Str reason) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = 1;
        }
        return str::DupTemp(reason);
    };
    MainWindow* win = len(gWindows) > 0 ? gWindows[0] : nullptr;
    if (str::EqI(action, StrL("complete-selected")) && win && win->IsCurrentTabAbout()) {
        Str path = str::Dup(HomePageSelectedFilePathTemp(win));
        defer {
            str::Free(path);
        };
        if (!len(path)) return fail(StrL("ERROR no selected document"));
        HomePageSetCompleted(win, path, value != 0);
        return HomeSelectionResultTemp(exitCodeOut);
    }
    if (!gForTesting || !win || !win->IsCurrentTabAbout() || !win->hwndCanvas) {
        return fail(StrL("ERROR home input requires a testing Home window"));
    }
    if (str::EqI(action, StrL("library-state"))) return HomeLibraryResultTemp(exitCodeOut);
    if (str::EqI(action, StrL("zoom-open"))) {
        HomeChromeCtrl* chrome = EnsureHomeChrome(win);
        win->homeRoot->SetFocus(chrome->sizeLabel);
        chrome->sizeLabel->Click();
        return HomeLibraryResultTemp(exitCodeOut);
    }
    if (str::EqI(action, StrL("zoom-key")) || str::EqI(action, StrL("zoom-char"))) {
        HomeChromeCtrl* chrome = EnsureHomeChrome(win);
        if (!chrome->zoomEditing || !chrome->zoomEdit) return fail(StrL("ERROR no preview-size editor"));
        SendMessageW(chrome->zoomEdit->hwnd, str::EqI(action, StrL("zoom-key")) ? WM_KEYDOWN : WM_CHAR, value, 0);
        return HomeLibraryResultTemp(exitCodeOut);
    }
    if (str::EqI(action, StrL("zoom-plus")) || str::EqI(action, StrL("zoom-minus"))) {
        HomeChromeCtrl* chrome = EnsureHomeChrome(win);
        (str::EqI(action, StrL("zoom-plus")) ? chrome->largerBtn : chrome->smallerBtn)->Click();
        return HomeLibraryResultTemp(exitCodeOut);
    }
    if (str::EqI(action, StrL("page-options"))) return HomeOptionsResultTemp(exitCodeOut);
    if (str::EqI(action, StrL("pin-header")) || str::EqI(action, StrL("full-page"))) {
        HomeChromeCtrl* chrome = EnsureHomeChrome(win);
        HomeOptionIconCtrl* button = str::EqI(action, StrL("pin-header")) ? chrome->headerPin : chrome->fullPage;
        VirtKeyEvent key;
        key.target = button;
        key.vkey = VK_SPACE;
        button->OnKeyDown(&key);
        if (!key.didHandle) return fail(StrL("ERROR Home option ignored Space"));
        return HomeOptionsResultTemp(exitCodeOut);
    }
    if (str::EqI(action, StrL("page-scroll"))) {
        HomePageOnVScroll(win, MAKEWPARAM(value, 0));
        return HomeOptionsResultTemp(exitCodeOut);
    }
    if (str::EqI(action, StrL("canvas-key"))) {
        SendMessageW(win->hwndCanvas, WM_KEYDOWN, (WPARAM)value, 0);
    } else if (str::EqI(action, StrL("search-key")) || str::EqI(action, StrL("search-char"))) {
        if (!win->homeSearch || !win->homeSearch->hwnd) {
            return fail(StrL("ERROR no Home search edit"));
        }
        UINT msg = str::EqI(action, StrL("search-key")) ? WM_KEYDOWN : WM_CHAR;
        SendMessageW(win->homeSearch->hwnd, msg, (WPARAM)value, 0);
    } else if (str::EqI(action, StrL("find-search"))) {
        SendMessageW(win->hwndFrame, WM_COMMAND, CmdFindFirst, 0);
    } else {
        return fail(StrL("ERROR unsupported Home input action"));
    }
    return HomeSelectionResultTemp(exitCodeOut);
}

// What the home page list drew for each row: the path, the size text as drawn,
// and the size column's rect. Reads the layout cache, so it needs a paint to
// have happened; NOTREADY until then. A test can't sample the size text from
// the pixels instead: the column's position depends on the window width, the
// DPI and the theme, so a fixed sample band lands on the path (identical for
// two renders of the same file) on any other machine. Used by tests/issue-5870.ts.
TempStr HomeListRowsResultTemp(int* exitCodeOut) {
    str::Builder out;
    auto finish = [&](int code) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return ToStrTemp(out);
    };

    MainWindow* win = len(gWindows) > 0 ? gWindows[0] : nullptr;
    if (!win) {
        out.Append(StrL("NOTREADY no-window\n"));
        return finish(2);
    }
    auto& c = HomeLayout(win);
    if (!c.valid) {
        out.Append(StrL("NOTREADY no-layout\n"));
        return finish(2);
    }
    if (!c.listView) {
        out.Append(StrL("ERROR not-list-view\n"));
        return finish(1);
    }
    out.Append(fmt("OK rows=%d\n", len(c.thumbs)));
    for (int i = 0; i < len(c.thumbs); i++) {
        ThumbnailLayout& t = c.thumbs[i];
        Rect r = t.rcListSize;
        Str path = t.fs ? t.fs->filePath : Str{};
        // fileSize is fetched when a row is first drawn, so an off-screen row
        // still reads kSizeNotFetched and its size text is empty
        TempStr progress;
        if (gSettings && gSettings->showHomePageReadingProgress) {
            progress = FormatFileStateProgressTemp(t.fs);
        }
        if (str::IsNull(progress)) {
            progress = StrL("");
        }
        out.Append(fmt("row=%d size='%s' sizeRect=%d,%d,%d,%d progress='%s' path=%s\n", i,
                       FileSizeForHomeListTemp(t.fileSize), r.x, r.y, r.dx, r.dy, progress, path));
    }
    return finish(0);
}

//--- home page chrome VirtCtrls

HomeViewIconCtrl::HomeViewIconCtrl() {
    cursor = CursorId::Hand;
}

void HomeViewIconCtrl::Paint(VirtPaintCtx& ctx) {
    bool selected = (listView == HomePageIsListView());
    DrawHomeViewButton(ctx.gfx, pixmap, ctx.bounds, selected, HasFlag(vwfHovered));
}

static void DrawHomeBookIcon(Gfx* gfx, Rect bounds, bool fullPage) {
    int size = std::max(1, std::min(bounds.dx, bounds.dy));
    Point origin{bounds.x + (bounds.dx - size) / 2, bounds.y + (bounds.dy - size) / 2};
    auto point = [&](int x, int y) { return Point{origin.x + MulDiv(x, size, 24), origin.y + MulDiv(y, size, 24)}; };
    float stroke = std::max(1.0f, (float)size / 16.0f);
    Color color = ThemeWindowTextColor();
    Point outline[] = {point(3, 4), point(12, 6), point(21, 4), point(21, 19), point(12, 21), point(3, 19)};
    for (int i = 0; i < dimof(outline); i++) {
        gfx->DrawLineAA(outline[i], outline[(i + 1) % dimof(outline)], color, stroke);
    }
    gfx->DrawLineAA(point(12, 6), point(12, 21), color, stroke);
    int rows = fullPage ? 4 : 2;
    for (int row = 0; row < rows; row++) {
        int y = 8 + row * 3;
        gfx->DrawLineAA(point(5, y), point(10, y + 1), color, stroke);
        gfx->DrawLineAA(point(14, y + 1), point(19, y), color, stroke);
    }
    if (!fullPage) {
        for (int col = 0; col < 4; col++) {
            int x = 4 + col * 4;
            gfx->DrawLineAA(point(x, 14), point(x + 2, 14), color, stroke);
        }
    }
}

void HomeOptionIconCtrl::Paint(VirtPaintCtx& ctx) {
    bool selected = fixedHeader ? gSettings->homePageFixedHeader : gSettings->homePageFullPageThumbnails;
    DrawHomeViewButton(ctx.gfx, fixedHeader ? pixmap : nullptr, ctx.bounds, selected, HasFlag(vwfHovered));
    if (!fixedHeader) DrawHomeBookIcon(ctx.gfx, ctx.bounds, selected);
    if (HasFlag(vwfFocused)) ctx.gfx->DrawRect(ctx.bounds, ThemeWindowTextColor());
}

HomeOpenDocCtrl::HomeOpenDocCtrl() {
    cursor = CursorId::Hand;
}

void HomeOpenDocCtrl::Paint(VirtPaintCtx& ctx) {
    if (!pixmap) {
        return;
    }
    Rect r = {ctx.bounds.x + rcIconLocal.x, ctx.bounds.y + rcIconLocal.y, pixmap->width, pixmap->height};
    DrawHomeBottomShadow(ctx.gfx, ctx.bounds, UiScalePx(12));
    Color bg = HasFlag(vwfHovered) ? AccentColor(ThemeBrandColor(), 10) : ThemeBrandColor();
    ctx.gfx->FillRoundedRect(ctx.bounds, UiScalePx(12), bg, ThemeEdgeColor(), HomeEdgeWidth());
    ctx.gfx->DrawPixmap(pixmap, r);
    if (HasFlag(vwfFocused)) ctx.gfx->DrawRect(ctx.bounds, ThemeBrandTextColor());
}

HomeCircleBtnCtrl::HomeCircleBtnCtrl() : VirtButton(StrL("")) {
    cursor = CursorId::Hand;
}

Size HomeCircleBtnCtrl::GetIdealSize() {
    int d = HomeSearchButtonDx();
    return {d, d};
}

void HomeLearningCtrl::Paint(VirtPaintCtx& ctx) {
    Gfx* gfx = ctx.gfx;
    Rect r = ctx.bounds;
    DrawHomeBottomShadow(gfx, r, UiScalePx(12));
    Color bg = HasFlag(vwfHovered) ? AccentColor(ThemeBrandColor(), 10) : ThemeBrandColor();
    gfx->FillRoundedRect(r, UiScalePx(12), bg, ThemeEdgeColor(), HomeEdgeWidth());
    int pad = UiScalePx(kHomeButtonPadX);
    r.SubLR(pad, pad);
    r.SubTB(UiScalePx(kHomeButtonPadY), UiScalePx(kHomeButtonPadY));
    gfx->DrawText(HomeLearningSummaryTemp(), r, gfxTextCenter | gfxTextWrap, HomePageFont(kHomeActionTextSize),
                  kColBlack);
    if (HasFlag(vwfFocused)) gfx->DrawRect(ctx.bounds, kColBlack);
}

static void DrawHomeBottomShadow(Gfx* gfx, Rect bounds, int radius) {
    bounds.y += MulDiv(UiScalePx(3), 3, 4);
    gfx->FillRoundedRect(bounds, radius, ThemeEdgeColor());
}

void HomeDepthButton::Paint(VirtPaintCtx& ctx) {
    DrawHomeBottomShadow(ctx.gfx, ctx.bounds, cornerRadius);
    Color bg = GetColor(HasFlag(vwfEnabled) && HasFlag(vwfHovered) ? kColBtnBgHover : kColBtnBg);
    bool focused = HasFlag(vwfFocused) || editActive;
    Color edge = focused ? EnsureContrast(ThemeBrandColor(), bg) : GetColor(kColBtnBorder);
    ctx.gfx->FillRoundedRect(ctx.bounds, cornerRadius, bg, edge, focused ? UiScalePx(1) : HomeEdgeWidth());
    if (!icon) {
        VirtPaintCtx text = ctx;
        text.content.SubTB(textPadding.top, textPadding.bottom);
        text.content.SubLR(textPadding.left, textPadding.right);
        PaintText(text, TextColor(bg));
    } else {
        int size = std::max(1, std::min(ctx.bounds.dx, ctx.bounds.dy) - UiScalePx(4));
        Pixmap* pixmap = GetCachedPixmapForSvg(Str(icon), size, size, TextColor(bg), bg);
        if (pixmap)
            ctx.gfx->DrawPixmap(pixmap, {ctx.bounds.x + (ctx.bounds.dx - size) / 2,
                                         ctx.bounds.y + (ctx.bounds.dy - size) / 2, size, size});
    }
}

void HomeCircleBtnCtrl::Paint(VirtPaintCtx& ctx) {
    Color background = HasFlag(vwfHovered) ? ThemeHotBackgroundColor() : ThemeControlBackgroundColor();
    if (!keyboard) {
        DrawHomeCircleButton(ctx.gfx, ctx.bounds, pixmap, glyph, background);
    } else {
        ctx.gfx->FillEllipse(ctx.bounds, background);
        int width = UiScalePx(40), height = UiScalePx(28);
        Rect frame{ctx.bounds.x + (ctx.bounds.dx - width) / 2, ctx.bounds.y + (ctx.bounds.dy - height) / 2, width,
                   height};
        Color color = EnsureContrast(ThemeWindowTextColor(), background);
        ctx.gfx->FillRoundedRect(frame, UiScalePx(6), kColorTransparent, color, (float)UiScalePx(2));
        int key = std::max(1, UiScalePx(4));
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 4; col++) {
                Rect cell{frame.x + UiScalePx(6 + col * 8), frame.y + UiScalePx(6 + row * 8), key, key};
                ctx.gfx->FillRoundedRect(cell, std::max(1, key / 2), color);
            }
        }
        ctx.gfx->FillRoundedRect({frame.x + UiScalePx(12), frame.Bottom() - UiScalePx(6), UiScalePx(16), key},
                                 std::max(1, key / 2), color);
    }
    if (HasFlag(vwfFocused)) ctx.gfx->DrawFocusRect(ctx.bounds);
}

HomeLogoRow::HomeLogoRow() {
    flags |= vwfNoHitTest;
    box = new HBox();
    box->alignMain = MainAxisAlign::MainStart;
    box->alignCross = CrossAxisAlign::CrossCenter;
}

HomeLogoRow::~HomeLogoRow() {
    // the buttons and logo are VirtCtrl children; don't let HBox delete them
    if (box) {
        VecClear(box->children);
        delete box;
        box = nullptr;
    }
}

void HomeLogoRow::AddItem(VirtCtrl* c) {
    AddChild(c);
    box->AddChild(c);
}

Size HomeLogoRow::GetIdealSize() {
    box->gap = UiScalePx(10);
    return {box->MinIntrinsicWidth(0), box->MinIntrinsicHeight(0)};
}

void HomeLogoRow::SetBounds(Rect r) {
    VirtCtrl::SetBounds(r);
    box->gap = UiScalePx(10);
    box->Layout(Tight(r.Size()));
    box->SetBounds(r);
}

HomeSearchBorderCtrl::HomeSearchBorderCtrl() {
    SetFlag(vwfNoHitTest, true);
}

// border and fill around the search edit; the edit HWND sits inside, so only
// the 1px frame and the padding around it are actually visible
void HomeSearchBorderCtrl::Paint(VirtPaintCtx& ctx) {
    Color bgCol = ThemeControlBackgroundColor();
    DrawHomeBottomShadow(ctx.gfx, ctx.bounds, UiScalePx(36));
    ctx.gfx->FillRoundedRect(ctx.bounds, UiScalePx(36), bgCol,
                             HomeSearchHasFocus(win) ? EnsureContrast(ThemeBrandColor(), bgCol) : kColorTransparent);
    int sz = UiScalePx(30);
    Pixmap* icon = GetCachedPixmapForSvg(Str(gIconSearch), sz, sz, ThemeWindowTextDisabledColor(), bgCol);
    ctx.gfx->DrawPixmap(icon, {ctx.bounds.x + UiScalePx(36), ctx.bounds.y + (ctx.bounds.dy - sz) / 2, sz, sz});
}

//--- tip links

HomeTipCtrl::~HomeTipCtrl() {
    str::Free(richFor);
}

// re-parses when the markup changes; the parse is what the band draws
void HomeTipCtrl::SetTipLine(Str line, PlatformFont* font) {
    if (rich && str::Eq(richFor, line)) {
        rich->font = font;
        rich->hwndForCmds = hwndForCmds;
        return;
    }
    if (rich) {
        RemoveChild(rich, true);
        rich = nullptr;
    }
    str::ReplaceWithCopy(&richFor, line);
    if (len(line) == 0) {
        return;
    }
    rich = ParseTip(line);
    rich->font = font;
    rich->hwndForCmds = hwndForCmds;
    AddChild(rich);
}

// the band's background; the markup (VirtRichText child) paints over it
void HomeTipCtrl::Paint(VirtPaintCtx& ctx) {
    ctx.gfx->FillRect(ctx.bounds, ThemeControlBackgroundColor());
}

// rcTip is the whole band (its background), rcText where the markup goes
void HomeTipCtrl::Sync(const Rect& rcTip, const Rect& rcText) {
    if (!rich || rcTip.IsEmpty()) {
        visibility = Visibility::Collapse;
        return;
    }
    visibility = Visibility::Visible;
    SetBounds(rcTip);
    rich->SetBounds(rcText);
}

//--- file entries

static Rect HomeEntryRect(const ThumbnailLayout& t);

// Pinned thumbnails reserve the trailing corner for their pin badge.
static Rect HomeCloseBtnRectForThumb(const Rect& thumb, const FileState* fs) {
    int sz = UiScalePx(18);
    int margin = UiScalePx(5);
    bool onLeft = IsUIRtl();
    if (fs && fs->isPinned) onLeft = !onLeft;
    int bx = onLeft ? (thumb.x + margin) : (thumb.x + thumb.dx - sz - margin);
    int by = thumb.y + margin;
    return {bx, by, sz, sz};
}

// the ✕ of a thumbnail / list row: forget the file it belongs to
static void HomeForgetEntryClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* entry = (HomeEntryCtrl*)ev->target->parent;
    TempStr path = str::DupTemp(entry->filePath);
    if (len(path) > 0) {
        ForgetFileFromFrequentlyRead(win, path);
    }
}

static void HomePinEntryClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* entry = (HomeEntryCtrl*)ev->target->parent;
    TempStr path = str::DupTemp(entry->filePath);
    if (len(path) == 0) {
        return;
    }
    FileState* fs = FileHistoryFindByPath(path);
    if (!fs) {
        return;
    }
    fs->isPinned = !fs->isPinned;
    ScheduleSaveSettings();
    win->DeleteToolTip();
    HomePageRelayout(win);
    win->RedrawAll(true);
}

static void HomeEntryOpenClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* entry = (HomeEntryCtrl*)ev->target;
    if (len(entry->filePath) == 0) {
        return;
    }
    LoadArgs args(entry->filePath, win);
    // ctrl forces always opening
    args.activateExisting = !ev->isCtrl;
    args.activateExistingInWindow = true;
    StartLoadDocument(&args);
}

static void HomeViewModeClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* btn = (HomeViewIconCtrl*)ev->target;
    if (btn->listView == HomePageIsListView()) {
        return;
    }
    SetHomePageListView(btn->listView);
    if (btn->listView) {
        win->DeleteToolTip();
    }
    win->homePageScrollY = 0;
    ScheduleSaveSettings();
    HomePageRelayout(win);
    win->RedrawAll(true);
}

static void HomeOptionClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* btn = static_cast<HomeOptionIconCtrl*>(ev->target);
    bool& setting = btn->fixedHeader ? gSettings->homePageFixedHeader : gSettings->homePageFullPageThumbnails;
    setting = !setting;
    ScheduleSaveSettings();
    HomePageInvalidateLayoutCache();
    for (MainWindow* w : gWindows) {
        w->homePageScrollY = 0;
        if (!w->IsDocLoaded()) {
            w->DeleteToolTip();
            HomePageRelayout(w);
            w->RedrawAll(true);
        }
    }
}

static void HomeOptionFocused(MainWindow* win, VirtFocusEvent* ev) {
    if (!ev->gotFocus || gSettings->homePageFixedHeader) return;
    Rect r = ev->w->BoundsInWindow();
    Rect canvas = HwndClientRect(win->hwndCanvas);
    int dy = r.y < canvas.y ? r.y - canvas.y : std::max(0, r.Bottom() - canvas.Bottom());
    if (!dy) return;
    win->homePageScrollY = std::max(0, win->homePageScrollY + dy);
    HomePageRelayout(win);
    HwndInvalidate(win->hwndCanvas);
}

static void SetHomeThumbSize(MainWindow* win, int value) {
    value = ClampI(value, kHomeZoomMin, kHomeZoomMax);
    if (value == HomeThumbPercent()) {
        return;
    }
    struct Anchor {
        MainWindow* win;
        Str path;
        int top;
    };
    Vec<Anchor> anchors;
    for (MainWindow* w : gWindows) {
        if (w->IsDocLoaded()) continue;
        auto& layout = HomeLayout(w);
        Anchor anchor{w, {}, 0};
        for (const ThumbnailLayout& entry : layout.thumbs) {
            Rect r = HomeEntryRect(entry);
            if (!r.Intersect(layout.rcThumbsArea).IsEmpty()) {
                anchor.path = entry.fs->filePath;
                anchor.top = r.y;
                break;
            }
        }
        VecAppend(anchors, anchor);
    }
    gSettings->homePageThumbnailSize = value;
    ScheduleSaveSettings();
    HomePageInvalidateLayoutCache();
    for (const Anchor& anchor : anchors) {
        MainWindow* w = anchor.win;
        HomePageRelayout(w);
        for (const ThumbnailLayout& entry : HomeLayout(w).thumbs) {
            if (str::Eq(entry.fs->filePath, anchor.path)) {
                w->homePageScrollY = std::max(0, w->homePageScrollY + HomeEntryRect(entry).y - anchor.top);
                HomePageRelayout(w);
                break;
            }
        }
        w->RedrawAll(true);
    }
}

static bool ParseHomeZoom(Str text, int& value) {
    TempStr input = str::DupTemp(text);
    str::TrimWSInPlace(input, str::TrimOpt::Both);
    int end = len(input);
    if (end > 0 && input.s[end - 1] == '%') end--;
    while (end > 0 && (input.s[end - 1] == ' ' || input.s[end - 1] == '\t')) end--;
    int start = 0;
    bool negative = end > 0 && input.s[0] == '-';
    if (end > 0 && (negative || input.s[0] == '+')) start++;
    if (start == end) return false;
    int parsed = 0;
    for (int i = start; i < end; i++) {
        char c = input.s[i];
        if (c < '0' || c > '9') return false;
        parsed = std::min(kHomeZoomMax + 1, parsed * 10 + c - '0');
    }
    value = ClampI(negative ? -parsed : parsed, kHomeZoomMin, kHomeZoomMax);
    return true;
}

static void CancelHomeZoom(MainWindow* win) {
    EndHomeZoom(win, HomeZoomFinish::Cancel);
}

static Rect HomeZoomEditBounds(Rect field) {
    field.SubLR(UiScalePx(4), UiScalePx(4));
    field.SubTB(UiScalePx(3), UiScalePx(3));
    return field;
}

static void ResizeHomeZoom(MainWindow* win) {
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (!chrome || !chrome->zoomEditing) return;
    ClearHomeLayoutCache(win);
    HomePageRelayout(win);
    HwndInvalidate(win->hwndCanvas);
}

static void EndHomeZoom(MainWindow* win, HomeZoomFinish finish) {
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (!chrome || !chrome->zoomEditing) return;
    int value = HomeThumbPercent();
    if (finish == HomeZoomFinish::Apply && !ParseHomeZoom(chrome->zoomEdit->GetTextTemp(), value)) {
        Rect bounds = chrome->sizeLabel->BoundsInWindow();
        win->ShowToolTip(Tr("Enter a whole number from 75% to 250%."), bounds);
        return;
    }
    chrome->zoomEditing = false;
    static_cast<HomeDepthButton*>(chrome->sizeLabel)->editActive = false;
    chrome->zoomEdit->SetIsVisible(false);
    ClearHomeLayoutCache(win);
    if (finish == HomeZoomFinish::Apply) SetHomeThumbSize(win, value);
    if (!HomeLayout(win).valid) HomePageRelayout(win);
    if (win->homeRoot) win->homeRoot->SetFocus(chrome->sizeLabel);
    HwndSetFocus(win->hwndCanvas);
    HwndInvalidate(win->hwndCanvas);
}

static void StartHomeZoom(MainWindow* win, VirtMouseEvent*) {
    HomeChromeCtrl* chrome = EnsureHomeChrome(win);
    if (!chrome->zoomEdit) {
        auto* edit = new HomeZoomEdit();
        edit->win = win;
        Edit::CreateArgs args;
        args.parent = win->hwndCanvas;
        args.font = HomePageFont(kHomeActionTextSize);
        args.noTheme = true;
        args.marginLeft = args.marginRight = UiScalePx(2);
        edit->Create(args);
        RoundControlUseCustomPaint(edit->hwnd);
        SetWindowLongPtrW(edit->hwnd, GWL_STYLE, (GetWindowLongPtrW(edit->hwnd, GWL_STYLE) & ~ES_RIGHT) | ES_CENTER);
        edit->onWndProc = MkMethod1<HomeZoomEdit, ControlBase::WndProcEvent*, &HomeZoomEdit::WndProc>(edit);
        edit->onKillFocus = MkFunc0(CancelHomeZoom, win);
        edit->onTextChanged = MkFunc0(ResizeHomeZoom, win);
        EditSetCueText(edit, Tr("Preview size (75% to 250%)"));
        SendMessageW(edit->hwnd, EM_LIMITTEXT, 16, 0);
        chrome->zoomEdit = edit;
    }
    chrome->zoomEdit->SetColors(ThemeWindowTextColor(), ThemeControlBackgroundColor());
    chrome->zoomEdit->SetText(fmt("%d%%", HomeThumbPercent()));
    Rect bounds = HomeZoomEditBounds(chrome->sizeLabel->BoundsInWindow());
    chrome->zoomEdit->SetBounds(bounds);
    chrome->zoomEditing = true;
    static_cast<HomeDepthButton*>(chrome->sizeLabel)->editActive = true;
    chrome->zoomEdit->SetIsVisible(true);
    EditSetFocus(chrome->zoomEdit);
    SendMessageW(chrome->zoomEdit->hwnd, EM_SETSEL, 0, -1);
    HwndInvalidate(win->hwndCanvas);
}

static void HomeThumbSizeClicked(MainWindow* win, VirtMouseEvent* ev) {
    SetHomeThumbSize(win, HomeThumbPercent() + (ev->target->id * 25));
}

static void HomeOpenDocClicked(MainWindow* win, VirtMouseEvent*) {
    HwndSendCommand(win->hwndFrame, CmdOpenFile);
}

static void HomeFeaturesClicked(MainWindow* win, VirtMouseEvent*) {
    auto* features = HomeChrome(win)->features;
    features->expanded = !features->expanded;
    HomePageInvalidateLayoutCache();
    HomePageRelayout(win);
    HwndInvalidate(win->hwndCanvas);
}

static void HomeFeatureAction(MainWindow* win, VirtMouseEvent* ev) {
    int cmd = ev->target->id;
    if (!cmd) {
        SumatraLaunchBrowser(StrL("https://github.com/abelokoj/sumatrapdf-enhanced/releases"));
        return;
    }
    if ((cmd == CmdInkPen || cmd == CmdExportStudyNotes) && !win->ctrl) {
        HwndSendCommand(win->hwndFrame, CmdOpenFile);
    }
    if ((cmd != CmdInkPen && cmd != CmdExportStudyNotes) || win->ctrl) {
        HwndSendCommand(win->hwndFrame, cmd);
    }
}

static void HomeFeaturePage(MainWindow* win, VirtMouseEvent* ev) {
    HomeChrome(win)->features->scroll->ScrollPage(ev->target->id);
}

static void HomeFeatureFocus(HomeFeaturesCtrl* features, VirtFocusEvent* ev) {
    if (ev->gotFocus) {
        features->scroll->ScrollIntoView(ev->w);
    }
}

static void CreateHomeFeatures(MainWindow* win, HomeChromeCtrl* chrome) {
    auto* features = new HomeFeaturesCtrl();
    chrome->features = features;
    chrome->AddChild(features);
    features->toggle = new HomeFeatureButton(HomeFeaturesLabel(false), HomePageFont(14));
    features->toggle->onClick = MkFunc1(HomeFeaturesClicked, win);
    features->toggle->SetTooltip(Tr("Expand or collapse the feature overview"));
    chrome->AddChild(features->toggle);
    features->scroll = new HomeFeatureScroll();
    features->AddChild(features->scroll);
    features->body = new HomeFeatureBody();
    features->scroll->AddChild(features->body);
    struct Feature {
        const char* icon;
        const char* text;
    };
    Feature inventory[] = {
        {gIconHomeThumbnails,
         "**Enhanced: a personal reading library.** Search recent documents, switch between grid and list views, "
         "resize previews with Ctrl + wheel and resume your last document."},
        {gIconCommandPalette,
         "**Enhanced: your interface, your way.** Pretty-style themes, a green app identity, bundled Manrope, "
         "Pretendard Std and Public Sans fonts, adjustable interface text and icons, and minimum tab width."},
        {gIconDictionary,
         "**Enhanced: offline dictionary.** Look up a selected word with Shift + D or Dictionary in the selection "
         "popup. WordNet definitions work offline; save useful words to your vocabulary."},
        {gIconAnnotInk,
         "**Enhanced: handwriting tools.** Pen, fountain pen, brush and pencil, adjustable thickness, colors and "
         "favorite presets. Saved PDF ink can be edited again after reopening the document."},
        {gIconAnnotLine,
         "**Enhanced: temporary laser ink.** Solid, hollow and dot modes with selectable color, width and "
         "disappearance time. "
         "Laser marks stay out of the saved PDF."},
        {gIconLearning,
         "**Enhanced: vocabulary practice.** A learning hub, saved words, flashcards, meaning quizzes, study decks, "
         "practice games and spaced review. Replayable guides help you get started; lettered answers wrap and feedback "
         "follows your theme."},
        {gIconSearch,
         "**Enhanced: reference previews.** Hover supported internal PDF links to preview their destination. The "
         "compact zoom picker includes 25% to 600% presets and a custom percentage."},
        {gIconStudyExport,
         "**Enhanced: study exports and annotation fonts.** Export highlights and notes to Markdown, text, Typst, "
         "HTML, Word, JSON or CSV. Free Text supports bundled and installed fonts with PDF embedding."},
        {gIconFileOpen,
         "**Enhanced: PDF bookmark editing.** Edit titles, pages and hierarchy with an optional cpdf tool downloaded "
         "on demand. Save a separate PDF copy; cpdf is not bundled."},
        {gIconFileOpen,
         "**Retained from upstream SumatraPDF.** Fast document reading, bookmarks, search, printing, multiple document "
         "formats and PDF annotations. These foundations remain part of the reader."},
    };
    for (const auto& entry : inventory) {
        auto* row = new HomeFeatureRow();
        row->icon = entry.icon;
        row->text = ParseTip(Str(entry.text));
        row->text->font = HomePageFont(14);
        row->AddChild(row->text);
        features->body->AddChild(row);
        VecAppend(features->rows, row);
    }
    struct Action {
        Str label;
        int command;
    };
    Action actions[] = {
        {Tr("Open dictionary"), CmdDictionaryLookup},      {Tr("Practice vocabulary"), CmdVocabularyHome},
        {Tr("Appearance settings"), CmdOptions},           {Tr("Export notes: open a PDF"), CmdExportStudyNotes},
        {Tr("Pen tools: open a PDF to write"), CmdInkPen}, {Tr("What's new: release history"), 0}};
    for (const auto& action : actions) {
        auto* button = new HomeFeatureButton(action.label, HomePageFont(14));
        button->id = action.command;
        button->onClick = MkFunc1(HomeFeatureAction, win);
        button->onFocusChanged = MkFunc1(HomeFeatureFocus, features);
        features->body->AddChild(button);
    }
    features->previous = new HomeFeatureButton(Tr("Previous features"), HomePageFont(13));
    features->next = new HomeFeatureButton(Tr("More features"), HomePageFont(13));
    features->previous->id = -1;
    features->next->id = 1;
    features->previous->onClick = features->next->onClick = MkFunc1(HomeFeaturePage, win);
    features->AddChild(features->previous);
    features->AddChild(features->next);
}

static void HomeLearningClicked(MainWindow* win, VirtMouseEvent*) {
    HwndSendCommand(win->hwndFrame, CmdVocabularyHome);
}

static void HomeWordOfDayClicked(MainWindow* win, VirtMouseEvent*) {
    VocabularyWord* word = VocabularyWordOfDay();
    if (word) {
        ShowDictionaryDialog(win, word->word, word->context, word->sourcePath, word->page);
    } else {
        HwndSendCommand(win->hwndFrame, CmdVocabularyHome);
    }
}

static void HomeDictionaryClicked(MainWindow* win, VirtMouseEvent*) {
    HwndSendCommand(win->hwndFrame, CmdDictionaryLookup);
}

static void HomeResumeClicked(MainWindow* win, VirtMouseEvent*) {
    Vec<FileState*> files;
    FileHistoryGetRecentlyOpenedOrder(files);
    for (FileState* fs : files) {
        if (len(fs->filePath) == 0) {
            continue;
        }
        LoadArgs args(fs->filePath, win);
        args.activateExistingInWindow = true;
        StartLoadDocument(&args);
        return;
    }
}

static void HomeHelpClicked(MainWindow* win, VirtMouseEvent*) {
    HwndSendCommand(win->hwndFrame, CmdToggleKeyboardHelp);
}

static void HomePaletteClicked(MainWindow* win, VirtMouseEvent*) {
    HwndSendCommand(win->hwndFrame, CmdCommandPalette);
}

static void HomeTipBandDoubleClicked(MainWindow* win, VirtMouseEvent* ev) {
    ev->didHandle = true;
    PickAnotherRandomPromotion();
    // painting reuses the layout, which holds the parsed tip
    HomePageRelayout(win);
    HwndInvalidate(win->hwndCanvas);
}

HomeListIconCtrl::HomeListIconCtrl() {
    cursor = CursorId::Hand;
    onGetTooltip = MkMethod1<HomeListIconCtrl, VirtTooltipEvent*, &HomeListIconCtrl::OnGetTooltip>(this);
}

void HomeListIconCtrl::OnGetTooltip(VirtTooltipEvent* ev) {
    auto* entry = (HomeEntryCtrl*)parent;
    FileState* fs = FileHistoryFindByPath(entry->filePath);
    bool pinned = fs && fs->isPinned;
    ev->tip = str::DupTemp(pinned ? Tr("Unpin") : Tr("Pin"));
}

HomeEntryCtrl::~HomeEntryCtrl() {
    str::Free(filePath);
}

HomeEntryCtrl::HomeEntryCtrl() {
    cursor = CursorId::Hand;
}

// paints this entry's list row or thumbnail, from our window's layout entry at
// our index (HomePageSyncChrome created us from it)
void HomeEntryCtrl::Paint(VirtPaintCtx& ctx) {
    auto* entries = (HomeEntriesCtrl*)parent;
    if (!entries || !entries->win || !entries->filterWords || !entries->highlighted) {
        return;
    }
    MainWindow* win = entries->win;
    auto& cache = HomeLayout(win);
    if (idx < 0 || idx >= len(cache.thumbs)) {
        return;
    }
    ThumbnailLayout* t = &cache.thumbs[idx];
    Gfx* gfx = ctx.gfx;
    bool isRtl = IsUIRtl();
    PlatformFont* fontText = HomePageFont(kHomeDocumentTextSize);
    Color backgroundColor = ThemeMainWindowBackgroundColor();
    // no selection chrome while typing in the search box
    bool isSelected = (idx == win->homePageSelIdx) && !HomeSearchHasFocus(win);
    // ctx.clip is the entries band (vwfClipChildren on the parent): an entry
    // scrolled partially out must not paint over the header / tip band
    gfx->PushClip(ctx.clip);
    if (HomePageIsListView()) {
        DrawHomeListRow(gfx, *t, *entries->filterWords, *entries->highlighted, fontText, backgroundColor, isRtl,
                        isSelected);
    } else {
        DrawHomeThumbnail(gfx, *t, *entries->filterWords, *entries->highlighted, fontText, backgroundColor, isRtl,
                          isSelected);
    }
    gfx->PopClip();
}

HomeEntryCtrl* HomeEntriesCtrl::EntryAt(int idx) {
    return (HomeEntryCtrl*)ChildAt(idx);
}

// the wnd the mouse is on may be one of an entry's buttons
HomeEntryCtrl* HomeEntriesCtrl::EntryForCtrl(VirtCtrl* w) {
    while (w && w != this) {
        if (w->parent == this) {
            return (HomeEntryCtrl*)w;
        }
        w = w->parent;
    }
    return nullptr;
}

void HomeEntriesCtrl::SetEntryCount(int n) {
    while (ChildCount() > n) {
        RemoveChild(children[ChildCount() - 1], true);
    }
    while (ChildCount() < n) {
        auto* e = new HomeEntryCtrl();
        e->idx = ChildCount();
        e->onClick = MkFunc1(HomeEntryOpenClicked, win);

        e->closeBtn = new VirtCloseButton();
        // it sits on the thumbnail, so it needs the circle behind it
        e->closeBtn->withCircle = true;
        e->closeBtn->onClick = MkFunc1(HomeForgetEntryClicked, win);
        e->closeBtn->visibility = Visibility::Collapse;
        e->AddChild(e->closeBtn);

        e->removeBtn = new VirtCloseButton();
        e->removeBtn->SetTooltip(Tr("Remove from Frequently Read"));
        e->removeBtn->onClick = MkFunc1(HomeForgetEntryClicked, win);
        e->removeBtn->visibility = Visibility::Collapse;
        e->AddChild(e->removeBtn);

        e->pinBtn = new HomeListIconCtrl();
        e->pinBtn->onClick = MkFunc1(HomePinEntryClicked, win);
        e->pinBtn->visibility = Visibility::Collapse;
        e->AddChild(e->pinBtn);

        AddChild(e);
    }
    if (activeIdx >= n) {
        activeIdx = -1;
    }
}

// the ✕ shows on the entry the mouse is on, and only in thumbnail view
void HomeEntriesCtrl::UpdateCloseBtnVisibility() {
    bool canShow = CanAccessDisk() && !HomePageIsListView();
    int n = ChildCount();
    for (int i = 0; i < n; i++) {
        HomeEntryCtrl* e = EntryAt(i);
        bool show = canShow && (i == activeIdx);
        e->closeBtn->visibility = show ? Visibility::Visible : Visibility::Collapse;
    }
}

void HomeEntriesCtrl::SetActiveEntry(int idx) {
    if (idx == activeIdx) {
        return;
    }
    activeIdx = idx;
    UpdateCloseBtnVisibility();
    if (idx >= 0 && idx != win->homePageSelIdx) {
        win->homePageSelIdx = idx;
    }
    HwndInvalidate(win->hwndCanvas);
    if (idx >= 0) {
        // the tip is anchored to the active entry, never to the cursor
        HomePageShowSelectionTooltip(win);
    }
}

// mouse events bubble up to us from the entry (or one of its buttons) that was
// hit, so this is where the active entry is tracked
HomeEntriesCtrl::HomeEntriesCtrl() {
    keyboardOnlyForTest =
        gForTesting && str::Eq(GetEnvVariableTemp(StrL("SUMATRA_TEST_HOME_KEYBOARD_ONLY")), StrL("1"));
    onMouseMove = MkMethod1<HomeEntriesCtrl, VirtMouseEvent*, &HomeEntriesCtrl::OnMouseMove>(this);
}

void HomeEntriesCtrl::OnMouseMove(VirtMouseEvent* ev) {
    // Keyboard-only fixtures must not move or compete with the user's cursor.
    // Normal Home interaction and the hover regressions retain mouse selection.
    if (keyboardOnlyForTest) {
        return;
    }
    // keyboard nav invalidates the canvas and Windows may re-send WM_MOUSEMOVE
    // with the same coordinates: ignore those so the selection doesn't snap
    // back under a stationary cursor
    if (ev->ptWindow == lastHoverPt) {
        return;
    }
    lastHoverPt = ev->ptWindow;
    HomeEntryCtrl* e = EntryForCtrl(ev->hit);
    SetActiveEntry(e ? e->idx : -1);
    return;
}

// TOOLTIPS_CLASS default for TTDT_INITIAL
static int TooltipInitialDelayMs() {
    int ms = (int)GetDoubleClickTime();
    return ms > 0 ? ms : 500;
}

constexpr UINT_PTR kHomeAboutHoverTimerID = 100;
constexpr UINT_PTR kHomeAboutHoverHideTimerID = 102;
// long enough to cross from the title onto the popup. The cursor is read when
// the timer fires, not when the leave message was queued.
constexpr UINT kAboutHoverHideDelayMs = 200;

static HomeChromeCtrl* HomeChrome(MainWindow* win) {
    if (!win || !win->homeRoot) {
        return nullptr;
    }
    if (!IsVirtCtrlOfKind(win->homeRoot->owned, kindHomeChromeCtrl)) {
        return nullptr;
    }
    return (HomeChromeCtrl*)win->homeRoot->owned;
}

static Rect HomeLogoScreenRect(HomeChromeCtrl* chrome) {
    if (!chrome || !chrome->logo) {
        return {};
    }
    HWND hwnd = chrome->GetHwnd();
    if (!hwnd) {
        return {};
    }
    Rect logo = chrome->logo->BoundsInWindow();
    Point origin = HwndClientToScreen(hwnd, Point());
    return {origin.x + logo.x, origin.y + logo.y, logo.dx, logo.dy};
}

static bool CursorOverHomeLogo(HomeChromeCtrl* chrome) {
    return HomeLogoScreenRect(chrome).Contains(GetCursorPosition());
}

static bool CursorOverAboutHover(HomeChromeCtrl* chrome) {
    return chrome && chrome->aboutHover && chrome->aboutHover->IsVisible() &&
           chrome->aboutHover->ScreenRect().Contains(GetCursorPosition());
}

static void CancelHomeAboutHoverTimer(MainWindow* win) {
    if (win && win->hwndCanvas) {
        KillTimer(win->hwndCanvas, kHomeAboutHoverTimerID);
        KillTimer(win->hwndCanvas, kHomeAboutHoverHideTimerID);
    }
}

static void HideHomeAboutHover(MainWindow* win) {
    CancelHomeAboutHoverTimer(win);
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (chrome && chrome->aboutHover) {
        chrome->aboutHover->Show(false);
    }
}

static bool CursorOverAboutOrLogo(MainWindow* win) {
    HomeChromeCtrl* chrome = HomeChrome(win);
    return CursorOverHomeLogo(chrome) || CursorOverAboutHover(chrome);
}

static void CALLBACK HomeAboutHoverHideTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    KillTimer(hwnd, id);
    MainWindow* win = FindMainWindowByHwnd(hwnd);
    if (!win || !IsMainWindowValidAndNotClosing(win)) {
        return;
    }
    if (CursorOverAboutOrLogo(win)) {
        return;
    }
    HideHomeAboutHover(win);
}

static void ScheduleHideHomeAboutHover(MainWindow* win) {
    if (!win || !win->hwndCanvas || CursorOverAboutOrLogo(win)) {
        return;
    }
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (!chrome || !chrome->aboutHover || !chrome->aboutHover->IsVisible()) {
        return;
    }
    SetTimer(win->hwndCanvas, kHomeAboutHoverHideTimerID, kAboutHoverHideDelayMs, HomeAboutHoverHideTimerProc);
}

static void OnHomeAboutHoverLeave(MainWindow* win) {
    ScheduleHideHomeAboutHover(win);
}

static void CALLBACK HomeAboutHoverTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    KillTimer(hwnd, id);
    MainWindow* win = FindMainWindowByHwnd(hwnd);
    if (!win || !IsMainWindowValidAndNotClosing(win)) {
        return;
    }
    ShowHomeAboutHover(win);
}

static void OnHomeLogoEnter(MainWindow* win) {
    if (!win || !win->hwndCanvas || !win->IsCurrentTabAbout()) {
        return;
    }
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (chrome && chrome->aboutHover && chrome->aboutHover->IsVisible()) {
        return;
    }
    CancelHomeAboutHoverTimer(win);
    SetTimer(win->hwndCanvas, kHomeAboutHoverTimerID, (UINT)TooltipInitialDelayMs(), HomeAboutHoverTimerProc);
}

static void OnHomeLogoLeave(MainWindow* win) {
    ScheduleHideHomeAboutHover(win);
}

// chrome-less About box under the home-page logo; links stay clickable
static void ShowHomeAboutHover(MainWindow* win) {
    if (!win || !IsMainWindowValidAndNotClosing(win) || !win->IsCurrentTabAbout()) {
        return;
    }
    HomeChromeCtrl* chrome = HomeChrome(win);
    if (!chrome || !chrome->logo || !CursorOverHomeLogo(chrome)) {
        return;
    }

    if (!chrome->aboutHover) {
        VirtHost::CreateArgs args;
        args.parent = win->hwndFrame;
        args.className = WStrL(L"SUMATRA_ABOUT_HOVER");
        args.isPopup = true;
        args.noActivate = true;
        args.visible = false;
        args.bgColor = ThemeMainWindowBackgroundColor();
        args.userData = win;
        chrome->aboutHover = VirtHost::Create(args);
        if (!chrome->aboutHover) {
            return;
        }
        chrome->aboutHover->onMouseLeave = MkFunc0(OnHomeAboutHoverLeave, win);
        ULONG_PTR cls = GetClassLongPtrW(chrome->aboutHover->native, GCL_STYLE);
        SetClassLongPtrW(chrome->aboutHover->native, GCL_STYLE, (LONG_PTR)(cls | CS_DROPSHADOW));
    }

    VirtHost* host = chrome->aboutHover;
    host->bgColor = ThemeMainWindowBackgroundColor();
    DpiSetFromHwnd(host->native);

    Rect measureRc{0, 0, 2000, 2000};
    AboutCtrl* about = UpdateAboutLayout(&host->vroot, host->native, measureRc, true);
    Size box = about->aboutRect.Size();
    if (box.IsEmpty()) {
        return;
    }

    Rect logoScreen = HomeLogoScreenRect(chrome);
    Rect pos{logoScreen.x + ((logoScreen.dx - box.dx) / 2), logoScreen.Bottom(), box.dx, box.dy};
    pos = ShiftRectToWorkArea(pos, win->hwndCanvas, true);
    host->SetPos(pos, true);
    UpdateAboutLayout(&host->vroot, host->native, {0, 0, box.dx, box.dy}, true);
    host->Invalidate();
}

HomeChromeCtrl::~HomeChromeCtrl() {
    HWND hwnd = GetHwnd();
    if (hwnd) {
        KillTimer(hwnd, kHomeAboutHoverTimerID);
        KillTimer(hwnd, kHomeAboutHoverHideTimerID);
    }
    delete aboutHover;
    aboutHover = nullptr;
    zoomEditing = false;
    delete zoomEdit;
    zoomEdit = nullptr;
}

// e.g. "Command Palette (Ctrl + K)"
static TempStr AppendCmdAccel(Str base, int cmd) {
    TempStr accel = AppendAccelKeyToMenuStringTemp({}, cmd);
    if (len(accel) == 0) {
        return base;
    }
    return str::JoinTemp(base, fmt(" (%s)", Str(accel.s + 1, len(accel) - 1))); // +1 skips the leading \t
}

// created once per window so that hover / pressed state survives the repaints
// that scrolling and filtering cause
static HomeChromeCtrl* EnsureHomeChrome(MainWindow* win) {
    // the canvas root holds either the home page's chrome or the About page's
    // controls, depending on which one is showing
    if (win->homeRoot && IsVirtCtrlOfKind(win->homeRoot->owned, kindHomeChromeCtrl)) {
        return (HomeChromeCtrl*)win->homeRoot->owned;
    }
    HWND hwnd = win->hwndCanvas;
    if (!win->homeRoot) {
        win->homeRoot = new VirtRoot(hwnd);
    }

    auto* chrome = new HomeChromeCtrl();
    chrome->kind = kindHomeChromeCtrl;
    chrome->flags |= vwfNoHitTest;

    // first, so that the rest of the chrome hit-tests and paints on top of the
    // entries. Below everything else: the tip band sits at the bottom of the page
    chrome->tip = new HomeTipCtrl();
    chrome->tip->hwndForCmds = win->hwndFrame;
    chrome->tip->onDoubleClick = MkFunc1(HomeTipBandDoubleClicked, win);
    chrome->AddChild(chrome->tip);

    chrome->searchBorder = new HomeSearchBorderCtrl();
    chrome->searchBorder->win = win;
    chrome->AddChild(chrome->searchBorder);

    chrome->entries = new HomeEntriesCtrl();
    chrome->entries->win = win;
    // hit-testable so that moving into the gaps between thumbnails still
    // reaches OnMouseMove() and clears the active entry
    chrome->entries->flags |= vwfClipChildren;
    chrome->AddChild(chrome->entries);

    chrome->thumbView = new HomeViewIconCtrl();
    chrome->thumbView->listView = false;
    chrome->thumbView->SetTooltip(Tr("Show as thumbnails"));
    chrome->thumbView->onClick = MkFunc1(HomeViewModeClicked, win);
    chrome->AddChild(chrome->thumbView);

    chrome->listView = new HomeViewIconCtrl();
    chrome->listView->listView = true;
    chrome->listView->SetTooltip(Tr("Show as list"));
    chrome->listView->onClick = MkFunc1(HomeViewModeClicked, win);
    chrome->AddChild(chrome->listView);

    chrome->headerPin = new HomeOptionIconCtrl();
    chrome->headerPin->onClick = MkFunc1(HomeOptionClicked, win);
    chrome->headerPin->onFocusChanged = MkFunc1(HomeOptionFocused, win);
    chrome->AddChild(chrome->headerPin);
    chrome->fullPage = new HomeOptionIconCtrl();
    chrome->fullPage->fixedHeader = false;
    chrome->fullPage->onClick = MkFunc1(HomeOptionClicked, win);
    chrome->fullPage->onFocusChanged = MkFunc1(HomeOptionFocused, win);
    chrome->AddChild(chrome->fullPage);

    chrome->hdr = new VirtText(StrL(""));
    chrome->hdr->padding = {0, 0, 0, UiScalePx(34)};
    chrome->AddChild(chrome->hdr);
    chrome->completedHdr = new VirtText(Tr("Completed Documents"), HomeBoldFont(21));
    chrome->completedEmpty = new VirtText(Tr("No completed documents"), HomePageFont(14));
    chrome->completedSection = new VirtCtrl();
    chrome->completedSection->flags |= vwfNoHitTest | vwfClipChildren;
    chrome->completedSection->AddChild(chrome->completedHdr);
    chrome->completedSection->AddChild(chrome->completedEmpty);
    chrome->AddChild(chrome->completedSection);

    // The title opens About on hover; search actions remain directly beside the field.
    chrome->logoRow = new HomeLogoRow();
    chrome->paletteBtn = new HomeCircleBtnCtrl();
    chrome->paletteBtn->SetText(Tr("Command Palette"));
    chrome->paletteBtn->SetTooltip(AppendCmdAccel(Tr("Command Palette"), CmdCommandPalette));
    chrome->paletteBtn->onClick = MkFunc1(HomePaletteClicked, win);
    chrome->logo = new SumatraLogo();
    chrome->logo->homeIdentity = true;
    chrome->logo->SetFlag(vwfNoHitTest, false);
    chrome->logo->onMouseEnter = MkFunc0(OnHomeLogoEnter, win);
    chrome->logo->onMouseLeave = MkFunc0(OnHomeLogoLeave, win);
    chrome->helpBtn = new HomeCircleBtnCtrl();
    chrome->helpBtn->SetText(Tr("Keyboard Shortcuts"));
    chrome->helpBtn->keyboard = true;
    chrome->helpBtn->SetTooltip(AppendCmdAccel(Tr("Keyboard Shortcuts"), CmdToggleKeyboardHelp));
    chrome->helpBtn->onClick = MkFunc1(HomeHelpClicked, win);
    chrome->logoRow->AddItem(chrome->logo);
    chrome->AddChild(chrome->logoRow);
    chrome->AddChild(chrome->paletteBtn);
    chrome->AddChild(chrome->helpBtn);

    chrome->openDoc = new HomeOpenDocCtrl();
    chrome->openDoc->text = new VirtText(StrL(""));
    chrome->openDoc->text->withUnderline = false;
    chrome->openDoc->AddChild(chrome->openDoc->text);
    chrome->openDoc->onClick = MkFunc1(HomeOpenDocClicked, win);
    chrome->AddChild(chrome->openDoc);
    chrome->resumeBtn = new HomeFeatureButton(Tr("Resume Last"), HomePageFont(kHomeActionTextSize));
    chrome->resumeBtn->cornerRadius = UiScalePx(12);
    chrome->resumeBtn->onClick = MkFunc1(HomeResumeClicked, win);
    chrome->AddChild(chrome->resumeBtn);
    chrome->wordOfDay = new HomeFeatureButton(Tr("Word of the Day"), HomePageFont(kHomeActionTextSize));
    chrome->wordOfDay->onClick = MkFunc1(HomeWordOfDayClicked, win);
    chrome->wordOfDay->SetTooltip(Tr("Look up today's word"));
    chrome->AddChild(chrome->wordOfDay);
    CreateHomeFeatures(win, chrome);
    chrome->learning = new HomeLearningCtrl();
    chrome->learning->onClick = MkFunc1(HomeLearningClicked, win);
    chrome->learning->SetTooltip(Tr("Your saved words, study decks, flashcards and practice games"));
    chrome->AddChild(chrome->learning);
    chrome->dictionary = new HomeFeatureButton(Tr("Dictionary"), HomePageFont(14));
    chrome->dictionary->onClick = MkFunc1(HomeDictionaryClicked, win);
    chrome->dictionary->SetTooltip(Tr("Look up meanings offline (Shift + D)"));
    chrome->AddChild(chrome->dictionary);

    auto* smaller = new HomeDepthButton(Tr("Smaller previews"), HomePageFont(14));
    auto* larger = new HomeDepthButton(Tr("Larger previews"), HomePageFont(14));
    smaller->icon = gIconZoomOut;
    larger->icon = gIconZoomIn;
    chrome->smallerBtn = smaller;
    chrome->largerBtn = larger;
    chrome->sizeLabel = new HomeDepthButton(StrL(""), HomePageFont(14));
    for (VirtButton* button : {chrome->smallerBtn, chrome->sizeLabel, chrome->largerBtn}) {
        button->textPadding = {UiScalePx(2), UiScalePx(2), UiScalePx(2), UiScalePx(2)};
        button->cornerRadius = UiScalePx(8);
    }
    chrome->sizeLabel->SetTooltip(Tr("Edit preview size (75% to 250%). Enter applies; Escape cancels."));
    chrome->sizeLabel->onClick = MkFunc1(StartHomeZoom, win);
    chrome->sizeLabel->onFocusChanged = MkFunc1(HomeOptionFocused, win);
    chrome->smallerBtn->id = -1;
    chrome->largerBtn->id = 1;
    chrome->smallerBtn->SetTooltip(Tr("Smaller previews (Ctrl + wheel)"));
    chrome->largerBtn->SetTooltip(Tr("Larger previews (Ctrl + wheel)"));
    chrome->smallerBtn->onClick = MkFunc1(HomeThumbSizeClicked, win);
    chrome->largerBtn->onClick = MkFunc1(HomeThumbSizeClicked, win);
    chrome->AddChild(chrome->smallerBtn);
    chrome->AddChild(chrome->sizeLabel);
    chrome->AddChild(chrome->largerBtn);
    win->homeRoot->SetChild(chrome);
    return chrome;
}

void HomePageDestroyChrome(MainWindow* win) {
    ClearHomeLayoutCache(win);
    delete win->homeLayout;
    win->homeLayout = nullptr;
    CancelHomeAboutHoverTimer(win);
    delete win->homeRoot;
    win->homeRoot = nullptr;
}

// gives the home page's virtual controls first shot at the canvas messages.
// Returns true when the event was consumed and the caller should stop
bool HomePageOnCanvasMessage(MainWindow* win, UINT msg, WPARAM wp, LPARAM lp, LRESULT& res) {
    VirtRoot* root = win->homeRoot;
    if (!root || !root->owned) {
        return false;
    }
    // Hover feedback (highlight, ✕ button, tooltips) must stay quiet while
    // another window is in front. The mouse still moves over the home page when
    // e.g. the command palette or the theme window is up, and putting a tooltip
    // over them steals activation: the palette closes on kill-focus and the
    // theme window ends up behind the main window. Clicks are exempt: clicking
    // a background window is meant to activate it.
    bool isHoverMsg = (msg == WM_MOUSEMOVE) || (msg == WM_SETCURSOR);
    if (isHoverMsg && GetForegroundWindow() != win->hwndFrame) {
        // thumbnail hover / tooltips stay quiet so they don't steal activation
        // from e.g. the command palette. The About dropdown is WS_EX_NOACTIVATE,
        // so hovering the logo still shows it.
        if (msg == WM_MOUSEMOVE) {
            Point pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ILayout* el = ElementFromPoint(root, pt, nullptr);
            VirtCtrl* w = el ? el->AsVirtCtrl() : nullptr;
            if (w && IsVirtCtrlOfKind(w, kindSumatraLogo)) {
                LRESULT ignored = 0;
                root->OnMessage(msg, wp, lp, ignored);
            } else {
                ScheduleHideHomeAboutHover(win);
            }
        }
        return false;
    }
    if (msg != WM_SETCURSOR) {
        bool didHandle = root->OnMessage(msg, wp, lp, res);
        // moving outside the entries band (or off the canvas) drops the active
        // entry, so the ✕ button goes away
        if (msg == WM_MOUSEMOVE && !root->hovered) {
            HomeEntriesCtrl* entries = HomeEntries(win);
            if (entries) {
                entries->SetActiveEntry(-1);
            }
        }
        // not on the title: the popup closes once the cursor has settled off it
        if (msg == WM_MOUSEMOVE && !IsVirtCtrlOfKind(root->hovered, kindSumatraLogo)) {
            ScheduleHideHomeAboutHover(win);
        }
        return didHandle;
    }
    Point pt = HwndGetCursorPos(win->hwndCanvas);
    Point ptLocal{0, 0};
    ILayout* el = ElementFromPoint(root, pt, &ptLocal);
    VirtCtrl* w = el ? el->AsVirtCtrl() : nullptr;
    if (!w || !w->OnSetCursor(ptLocal)) {
        return false;
    }
    // no tooltip of its own means "leave the tooltip alone", not "hide it": a
    // thumbnail entry's tip is put up by HomePageShowSelectionTooltip() and would
    // otherwise be torn down by the WM_SETCURSOR that follows the hover
    TempStr tip = w->GetTooltipTemp(ptLocal);
    if (tip && *tip.s) {
        Rect r = w->BoundsInWindow();
        win->ShowToolTip(tip, r);
    } else if (HomePageIsListView()) {
        HomeEntriesCtrl* entries = HomeEntries(win);
        if (entries && entries->EntryForCtrl(w)) {
            win->DeleteToolTip();
        }
    }
    res = TRUE;
    return true;
}

// feeds the geometry LayoutHomePage() computed into the persistent chrome tree
static void HomePageSyncChrome(HomePageLayout& l) {
    MainWindow* win = l.win;
    HomeChromeCtrl* chrome = EnsureHomeChrome(win);
    VirtRoot* root = win->homeRoot;
    // the chrome positions its children itself, so don't let the root re-layout
    root->bounds = l.rc;
    root->needsLayout = false;
    chrome->SetBounds(l.rc);

    chrome->tip->Sync(l.rcTip, l.rcTipText);
    chrome->features->UpdateFonts();
    chrome->features->Sync(l.rcFeatures);

    chrome->searchBorder->visibility = l.rcSearchBorder.IsEmpty() ? Visibility::Collapse : Visibility::Visible;
    chrome->searchBorder->SetBounds(l.rcSearchBorder);

    // file entries: clipped to the thumbnails band, like the static links were
    HomeEntriesCtrl* entries = chrome->entries;
    entries->SetBounds(l.rcThumbsArea);
    auto& cache = HomeLayout(win);
    entries->filterWords = &cache.filterWords;
    entries->highlighted = &cache.highlighted;
    int nEntries = len(cache.thumbs);
    entries->SetEntryCount(nEntries);
    bool listView = HomePageIsListView();
    for (int i = 0; i < nEntries; i++) {
        ThumbnailLayout& t = cache.thumbs[i];
        HomeEntryCtrl* e = entries->EntryAt(i);
        e->idx = i;
        Str path = t.fs ? t.fs->filePath : Str{};
        if (!str::Eq(e->filePath, path)) {
            str::ReplaceWithCopy(&e->filePath, path);
        }
        Rect rc = HomeEntryRect(t);
        e->visibility = rc.IsEmpty() ? Visibility::Collapse : Visibility::Visible;
        e->SetBounds(rc);
        if (listView) {
            e->removeBtn->visibility = Visibility::Visible;
            e->removeBtn->SetBounds(t.rcListRemove);
            e->pinBtn->visibility = Visibility::Visible;
            e->pinBtn->SetBounds(t.rcListPin);
        } else {
            e->removeBtn->visibility = Visibility::Collapse;
            e->pinBtn->visibility = Visibility::Collapse;
            // relative to the on-screen part of the entry, so the ✕ stays
            // visible on a thumbnail scrolled half-way out of the band
            e->closeBtn->SetBounds(HomeCloseBtnRectForThumb(rc.Intersect(l.rcThumbsArea), t.fs));
        }
    }
    entries->UpdateCloseBtnVisibility();

    Size iconSize = l.rcIconThumbnailView.Size();
    chrome->thumbView->pixmap = GetCachedPixmapForSvg(Str(gIconHomeThumbnails), iconSize.dx, iconSize.dy);
    chrome->thumbView->SetBounds(l.rcIconThumbnailView);
    chrome->listView->pixmap = GetCachedPixmapForSvg(Str(gIconHomeList), iconSize.dx, iconSize.dy);
    chrome->listView->SetBounds(l.rcIconListView);
    chrome->headerPin->pixmap = GetCachedPixmapForSvg(Str(GetPinIconSvg()), iconSize.dx, iconSize.dy);
    chrome->headerPin->SetBounds(l.rcIconHeaderPin);
    Str pinLabel =
        gSettings->homePageFixedHeader ? Tr("Unpin header (header is fixed)") : Tr("Pin header (whole page scrolls)");
    chrome->headerPin->SetText(pinLabel);
    chrome->headerPin->SetTooltip(pinLabel);
    chrome->fullPage->SetBounds(l.rcIconFullPage);
    Str pageLabel = gSettings->homePageFullPageThumbnails ? Tr("Show cropped previews (whole-page previews are on)")
                                                          : Tr("Show whole-page previews (cropped previews are on)");
    chrome->fullPage->SetText(pageLabel);
    chrome->fullPage->SetTooltip(pageLabel);

    // re-apply: the bounds were set before the parent was positioned
    chrome->hdr->SetBounds(l.freqRead->lastBounds);
    chrome->completedSection->SetBounds(l.rcThumbsArea);
    chrome->completedHdr->SetText(Tr("Completed Documents"));
    chrome->completedHdr->font = HomeBoldFont(21);
    chrome->completedHdr->isRtl = IsUIRtl();
    chrome->completedHdr->SetBounds(l.rcCompletedHeader);
    chrome->completedEmpty->SetText(Tr("No completed documents"));
    chrome->completedEmpty->font = HomePageFont(14);
    chrome->completedEmpty->isRtl = IsUIRtl();
    chrome->completedEmpty->visibility = l.rcCompletedEmpty.IsEmpty() ? Visibility::Collapse : Visibility::Visible;
    chrome->completedEmpty->SetBounds(l.rcCompletedEmpty);

    // font also set here so the cached-layout path (ApplyHomeLayoutCache)
    // repaints the logo without a full relayout
    SetHomeTitleFont(chrome, l.rc);
    int sideDx = HomeSearchButtonDx();
    int iconSz = std::max(UiScalePx(16), MulDiv(sideDx, 2, 3));
    chrome->paletteBtn->pixmap = GetCachedPixmapForSvg(Str(gIconCommandPalette), iconSz, iconSz, ThemeWindowTextColor(),
                                                       ThemeControlBackgroundColor());
    int sideY = l.rcSearchBorder.y + (l.rcSearchBorder.dy - sideDx) / 2;
    Rect palette{l.rcSearchBorder.x - UiScalePx(8) - sideDx, sideY, sideDx, sideDx};
    Rect shortcuts{l.rcSearchBorder.Right() + UiScalePx(8), sideY, sideDx, sideDx};
    chrome->paletteBtn->SetBounds(IsUIRtl() ? shortcuts : palette);
    chrome->helpBtn->SetBounds(IsUIRtl() ? palette : shortcuts);
    chrome->paletteBtn->visibility = chrome->helpBtn->visibility = Visibility::Visible;
    chrome->logoRow->SetBounds(l.rcLogo);
    Rect sizeAnchor = l.rcZoomControls;
    int buttonDx = sizeAnchor.dy;
    int gap = UiScalePx(4);
    int valueDx = std::max(1, sizeAnchor.dx - 2 * buttonDx - 2 * gap);
    int smallerX = sizeAnchor.x;
    int valueX = smallerX + buttonDx + gap;
    int largerX = valueX + valueDx + gap;
    if (IsUIRtl()) {
        smallerX = sizeAnchor.Right() - buttonDx;
        valueX = smallerX - gap - valueDx;
        largerX = sizeAnchor.x;
    }
    Visibility sizeVisibility = sizeAnchor.IsEmpty() ? Visibility::Collapse : Visibility::Visible;
    chrome->smallerBtn->visibility = sizeVisibility;
    chrome->largerBtn->visibility = sizeVisibility;
    chrome->sizeLabel->visibility = sizeVisibility;
    chrome->smallerBtn->font = chrome->largerBtn->font = HomePageFont(kHomeActionTextSize);
    for (VirtButton* button : {chrome->smallerBtn, chrome->sizeLabel, chrome->largerBtn}) {
        button->cornerRadius = UiScalePx(8);
        button->SetColor(kColBtnBg, ThemeControlBackgroundColor());
        button->SetColor(kColBtnBgHover, ThemeHotBackgroundColor());
        button->SetColor(kColBtnBorder, ThemeEdgeColor());
        button->SetColor(kColBtnText, ThemeWindowTextColor());
        button->SetColor(kColBtnTextDisabled, ThemeWindowTextDisabledColor());
    }
    chrome->smallerBtn->SetBounds({smallerX, sizeAnchor.y, buttonDx, sizeAnchor.dy});
    chrome->sizeLabel->SetText(fmt("%d%%", HomeThumbPercent()));
    chrome->sizeLabel->align = VirtTextAlign::Center;
    chrome->sizeLabel->font = HomePageFont(kHomeActionTextSize);
    chrome->sizeLabel->SetBounds({valueX, sizeAnchor.y, valueDx, sizeAnchor.dy});
    chrome->largerBtn->SetBounds({largerX, sizeAnchor.y, buttonDx, sizeAnchor.dy});
    chrome->smallerBtn->SetIsEnabled(HomeThumbPercent() > kHomeZoomMin);
    chrome->largerBtn->SetIsEnabled(HomeThumbPercent() < kHomeZoomMax);
    if (chrome->zoomEdit && chrome->zoomEditing) {
        Rect edit = HomeZoomEditBounds(chrome->sizeLabel->BoundsInWindow());
        chrome->zoomEdit->SetFont(HomePageFont(kHomeActionTextSize));
        chrome->zoomEdit->SetColors(ThemeWindowTextColor(), ThemeControlBackgroundColor());
        chrome->zoomEdit->SetBounds(edit);
        if (sizeAnchor.IsEmpty() || edit.Intersect(l.rc).IsEmpty()) {
            chrome->zoomEditing = false;
            static_cast<HomeDepthButton*>(chrome->sizeLabel)->editActive = false;
            chrome->zoomEdit->SetIsVisible(false);
        }
    }
    if (chrome->aboutHover && chrome->aboutHover->IsVisible()) {
        Size sz = chrome->aboutHover->ScreenRect().Size();
        Rect logoScreen = HomeLogoScreenRect(chrome);
        Rect want{logoScreen.x + ((logoScreen.dx - sz.dx) / 2), logoScreen.Bottom(), sz.dx, sz.dy};
        want = ShiftRectToWorkArea(want, l.win->hwndCanvas, true);
        if (want != chrome->aboutHover->ScreenRect()) {
            chrome->aboutHover->SetPos(want, true);
        }
    }

    HomeOpenDocCtrl* od = chrome->openDoc;
    Rect rcOpen = l.actions.items[0];
    od->pixmap = GetCachedPixmapForSvg(Str(gIconFileOpen), l.rcIconOpen.dx, l.rcIconOpen.dy, ThemeBrandTextColor(),
                                       ThemeBrandColor());
    od->SetBounds(rcOpen);
    chrome->resumeBtn->font = chrome->wordOfDay->font = chrome->dictionary->font = HomePageFont(kHomeActionTextSize);
    chrome->resumeBtn->visibility = chrome->wordOfDay->visibility = chrome->dictionary->visibility =
        Visibility::Visible;
    chrome->resumeBtn->SetBounds(l.actions.items[1]);
    chrome->wordOfDay->SetBounds(l.actions.items[2]);
    Vec<FileState*> recent;
    FileHistoryGetRecentlyOpenedOrder(recent);
    chrome->resumeBtn->SetIsEnabled(len(recent) > 0);
    chrome->learning->SetBounds(l.actions.items[3]);
    chrome->learning->SetText(HomeLearningSummaryTemp());
    chrome->learning->SetTooltip(HomeLearningSummaryTemp());
    chrome->dictionary->SetBounds(l.actions.items[4]);
    chrome->features->toggle->visibility = Visibility::Visible;
    chrome->features->toggle->SetBounds(l.actions.items[5]);
    od->rcIconLocal = {l.rcIconOpen.x - rcOpen.x, l.rcIconOpen.y - rcOpen.y, l.rcIconOpen.dx, l.rcIconOpen.dy};
    od->text->SetColor(kColText, ThemeBrandTextColor());
    od->text->SetBounds(l.openDoc->lastBounds);
}

static void DrawHomePageLayout(HomePageLayout& l) {
    Gfx* gfx = l.gfx;
    auto* win = l.win;

    gfx->FillRect(l.rc, ThemeMainWindowBackgroundColor());
    if (!ThemeUsesHighContrastColors()) {
        int radius = std::max(l.rc.dx / 2, UiScalePx(400));
        for (int step = 80; step > 0; step--) {
            int d = radius * step / 80;
            gfx->FillEllipse({l.rc.x + l.rc.dx / 4 - d / 2, l.rc.dy / 3 - d / 2, d, d}, ThemeBrandColor(), 1);
            gfx->FillEllipse({l.rc.x + l.rc.dx * 3 / 4 - d / 2, l.rc.dy - d / 2, d, d}, ThemeBrandColor(), 1);
        }
    }
    Rect subtitle = HomeSubtitleBox(l.rc, l.rcLogo);
    if (!subtitle.IsEmpty()) {
        gfx->DrawText(HomeSubtitle(), subtitle, gfxTextCenter | gfxTextWrap, HomePageFont(21), HomeSubtitleColor());
    }
    Rect section = HomeLayout(win).rcFreqRead;
    int clockSz = UiScalePx(22);
    const char* clockSvg =
        "<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round'><circle "
        "cx='12' cy='12' r='9'/><path d='M12 7v5l4 2'/></svg>";
    Pixmap* clock = GetCachedPixmapForSvg(Str(clockSvg), clockSz, clockSz, ThemeWindowTextColor(),
                                          ThemeMainWindowBackgroundColor());
    gfx->DrawPixmap(clock, {section.x, section.y + (section.dy - clockSz) / 2, clockSz, clockSz});
    int separatorY = std::max(section.Bottom(), HomeLayout(win).rcZoomControls.Bottom()) + UiScalePx(8);
    gfx->FillRect({section.x, separatorY, l.rc.dx - section.x * 2, 1}, ThemeEdgeColor());
    if (!l.rcThumbsArea.IsEmpty()) {
        gfx->PushClip(l.rcThumbsArea);
        gfx->FillRect({l.rcCompletedHeader.x, l.rcCompletedHeader.y - UiScalePx(12), l.rcCompletedHeader.dx, 1},
                      ThemeEdgeColor());
        gfx->PopClip();
    }

    int nThumbs = len(l.thumbnails);
    // keep the keyboard selection inside the (possibly filtered) list
    if (win->homePageSelIdx >= nThumbs) {
        win->homePageSelIdx = nThumbs - 1;
    }

    // the chrome tree paints everything else: search border, file entries
    // (thumbnails / list rows), tip band, header (palette / logo / help),
    // view buttons, and "Open a document..."
    win->homeRoot->Paint(gfx, l.rc);

    // thumbnails selection outline: after the entries so it sits on top of the
    // thumbnail, clipped so it cannot paint over the search field or the tip
    // band (issue #5978). A little slack above/below the thumbs band keeps the
    // first row's top curve from being cut off.
    int selIdx = win->homePageSelIdx;
    bool showSel = !HomePageIsListView() && !HomeSearchHasFocus(win) && selIdx >= 0 && selIdx < nThumbs;
    if (showSel) {
        ThumbnailLayout& t = l.thumbnails[selIdx];
        if (IsHomeThumbOnScreen(t.rcPage.Union(t.rcText), l.rcThumbsArea)) {
            Rect clip = HomeOutlinePaintClip(l.rcThumbsArea, l.rcSearchBorder, l.rcTip, l.hasTip);
            if (!clip.IsEmpty()) {
                gfx->PushClip(clip);
                DrawHomeItemEdge(gfx, HomeSelectionOutlineRect(t), true);
                gfx->PopClip();
            }
        }
        // the edit HWND is on top of the canvas; this is the canvas-drawn
        // border/fill around it. Repaint so a 1px antialiased leak cannot
        // show through the field.
        HomeChromeCtrl* chrome = HomeChrome(win);
        if (chrome && chrome->searchBorder) {
            chrome->searchBorder->PaintStandalone(gfx);
        }
    }
}

#if IS_DEBUG
static bool CaptureHomeSurface(MainWindow* win, Str path) {
    if (IsWindowVisible(win->hwndCanvas)) return false;
    Rect client = HwndClientRect(win->hwndCanvas);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), client.dx, -client.dy, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        DeleteObject(bitmap);
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    HomePageLayout layout;
    layout.win = win;
    layout.rc = client;
    LayoutHomePage(layout);
    SaveHomeLayoutCache(layout, {}, 0);
    HomePageSyncChrome(layout);
    win->homeSearch->SetIsVisible(true);
    PlaceHomeSearchEdit(win, layout.rcSearchBorder);
    {
        Gfx* gfx = GfxCreate(dc);
        layout.gfx = gfx;
        DrawHomePageLayout(layout);
        delete gfx;
    }
    HWND edit = win->homeSearch->hwnd;
    RECT bounds;
    GetWindowRect(edit, &bounds);
    MapWindowPoints(nullptr, win->hwndCanvas, (POINT*)&bounds, 2);
    if (bounds.left < layout.rcSearchBorder.x || bounds.top < layout.rcSearchBorder.y ||
        bounds.right > layout.rcSearchBorder.Right() || bounds.bottom > layout.rcSearchBorder.Bottom() ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        return false;
    int saved = SaveDC(dc);
    SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
    IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
    SendMessageW(edit, WM_PRINT, (WPARAM)dc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
    RestoreDC(dc, saved);
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + client.dx * client.dy * 4;
    str::Builder bytes;
    bytes.Append(Str((const char*)&header, sizeof(header)));
    bytes.Append(Str((const char*)&info.bmiHeader, sizeof(info.bmiHeader)));
    bytes.Append(Str((const char*)pixels, client.dx * client.dy * 4));
    bool ok = file::WriteFile(path, ToStrTemp(bytes));
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return ok;
}

static bool HomeSurfaceCaptures() {
    WCHAR folder[1024]{};
    DWORD length = GetEnvironmentVariableW(L"SUMATRA_HOME_SNAPSHOTS", folder, dimof(folder));
    if (!length || length >= dimof(folder)) return true;
    Str output = ToUtf8Temp(folder);
    if (!dir::CreateAll(output)) return false;
    RenderCache* originalCache = gRenderCache;
    if (!originalCache) gRenderCache = new RenderCache();
    int originalTheme = ThemeGetCurrentIndex();
    int originalFont = gSettings->uIFontSize;
    int originalScale = gSettings->interfaceScale;
    int originalPinStyle = gSettings->pinIconStyle;
    Str originalView = str::Dup(gSettings->homePageViewMode);
    str::ReplaceWithCopy(&gSettings->homePageViewMode, StrL("thumbnails"));
    defer {
        gSettings->uIFontSize = originalFont;
        gSettings->interfaceScale = originalScale;
        gSettings->pinIconStyle = originalPinStyle;
        str::ReplaceWithCopy(&gSettings->homePageViewMode, originalView);
        str::Free(originalView);
        SetThemeByIndex(originalTheme);
        RefreshUiFonts();
        if (!originalCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    for (Str name : {StrL("Lecture notes.pdf"), StrL("Research methods.pdf"), StrL("Reading list.pdf")}) {
        auto* state = NewFileState(path::JoinTemp(output, name));
        str::ReplaceWithCopy(&state->pageNo, StrL("3"));
        state->isPinned = str::Eq(name, StrL("Lecture notes.pdf"));
        FileHistoryAppend(state);
    }
    bool ok = true;
    for (int variant = 0; variant < 4; variant++) {
        gSettings->pinIconStyle = (int)(variant == 1   ? PinIconStyle::Solid
                                        : variant == 2 ? PinIconStyle::Round
                                                       : PinIconStyle::Soft);
        gSettings->uIFontSize = variant == 3 ? 28 : 0;
        gSettings->interfaceScale = variant == 3 ? 150 : 100;
        SetTheme(variant == 1 ? StrL("Modern Green Dark") : StrL("Sumatra Light"));
        RefreshUiFonts();
        int width = variant == 2 ? 420 : variant == 3 ? 1500 : 1100;
        int height = variant == 2 ? 700 : variant == 3 ? 1400 : 800;
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        win.hwndCanvas = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, width, height, nullptr, nullptr,
                                         GetModuleHandle(nullptr), nullptr);
        EnsureHomeSearchCreated(&win);
        ok &= win.hwndCanvas && CaptureHomeSurface(&win, path::JoinTemp(output, fmt("home-%d.bmp", variant)));
        HWND canvas = win.hwndCanvas;
        HomePageDestroySearch(&win);
        HomePageDestroyChrome(&win);
        win.hwndCanvas = nullptr;
        DestroyWindow(canvas);
    }
    return ok;
}
#endif

static bool HomePageShouldShow(MainWindow* win) {
    if (!win || !win->IsCurrentTabAbout()) {
        return false;
    }
    if (!HasPermission(Perm::SavePreferences | Perm::DiskAccess)) {
        return false;
    }
    return gSettings && SettingsRememberOpenedFiles() && gSettings->showStartPage;
}

static void UpdateHomeOverlayScrollbar(MainWindow* win) {
    auto& c = HomeLayout(win);
    bool show = c.valid && !ScrollbarsAreHidden() && c.totalContentDy > c.thumbsVisibleDy;
    if (show) {
        if (!win->overlayScrollV) {
            win->overlayScrollV =
                OverlayScrollbarCreate(win->hwndCanvas, OverlayScrollbar::Type::Vert, OverlayScrollbar::Mode::Thick);
        }
        OverlayScrollbarSetMode(win->overlayScrollV, OverlayScrollbar::Mode::Thick);
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        si.nMin = 0;
        si.nMax = c.totalContentDy - 1;
        si.nPage = c.thumbsVisibleDy;
        si.nPos = win->homePageScrollY;
        OverlayScrollbarSetInfo(win->overlayScrollV, &si, HwndIsVisible(win->hwndCanvas));
    }
    OverlayScrollbarShow(win->overlayScrollV, show && HwndIsVisible(win->hwndCanvas));
}

void HomePageCreate(MainWindow* win) {
    if (!win || !win->hwndCanvas) {
        return;
    }
    EnsureHomeChrome(win);
    EnsureHomeSearchCreated(win);
}

void HomePageRelayout(MainWindow* win) {
    if (!win || !win->hwndCanvas) {
        return;
    }
    if (!HomePageShouldShow(win)) {
        HomePageHideSearch(win);
        return;
    }
    RemoveAppScrollbar(win->hwndCanvas);
    EnsureHomeChrome(win);
    EnsureHomeSearchCreated(win);

    HomePageLayout l;
    l.rc = HwndClientRect(win->hwndCanvas);
    l.win = win;
    if (l.rc.IsEmpty()) {
        HomePageHideSearch(win);
        return;
    }

    TempStr filterText = HomeSearchQueryTemp(win);
    bool usedCache = false;
    if (HomeLayoutCacheMatches(win, l.rc, filterText)) {
        Vec<FileState*> files;
        StrVec filterWords;
        CollectHomePageFiles(win, files, filterWords);
        if (HomeLayoutCacheFilesMatch(win, files)) {
            ApplyHomeLayoutCache(l, win->homePageScrollY);
            usedCache = true;
        }
    }
    if (!usedCache) {
        LayoutHomePage(l);
        SaveHomeLayoutCache(l, filterText, win->homePageScrollY);
    }
    HomePageSyncChrome(l);
    PlaceHomeSearchEdit(win, l.rcSearchBorder);
    if (win->homeSearch) win->homeSearch->SetIsVisible(!l.rcSearchBorder.Intersect(l.rc).IsEmpty());
    UpdateHomeSearchCueBanner(win);
    UpdateHomeOverlayScrollbar(win);
}

void DrawHomePage(MainWindow* win, Gfx* gfx) {
    if (!HomeLayout(win).valid || !win->homeRoot) {
        HomePageRelayout(win);
    }
    if (!HomeLayout(win).valid || !win->homeRoot) {
        return;
    }

    auto& c = HomeLayout(win);
    HomePageLayout l;
    l.win = win;
    l.gfx = gfx;
    l.rc = c.canvasRc;
    l.rcLogo = c.rcLogo;
    l.rcFeatures = c.rcFeatures;
    l.actions = c.actions;
    l.rcCompletedHeader = c.rcCompletedHeader;
    l.rcCompletedEmpty = c.rcCompletedEmpty;
    l.rcThumbsArea = c.rcThumbsArea;
    l.rcSearchBorder = c.rcSearchBorder;
    l.rcTip = c.rcTip;
    l.hasTip = c.hasTip;
    l.thumbnails = c.thumbs;
    DrawHomePageLayout(l);
}

// --- keyboard navigation of the file list (issue #1136) ---

// Thumbnail completion can invalidate the cache before WM_PAINT runs. A Home
// key arriving first still needs the current entries instead of dropping input.
static void RefreshHomeSelectionLayout(MainWindow* win) {
    if (HomePageShouldShow(win) && !HomeLayout(win).valid) {
        HomePageRelayout(win);
    }
}

// Selection works off the layout cache, filled by HomePageRelayout.
static int HomeSelectableCount(MainWindow* win) {
    RefreshHomeSelectionLayout(win);
    auto& c = HomeLayout(win);
    return c.valid ? len(c.thumbs) : 0;
}

// bounding box of an entry, in window coordinates for the current scroll
static Rect HomeEntryRect(const ThumbnailLayout& t) {
    if (HomePageIsListView()) {
        return t.rcListRow;
    }
    return t.rcPage.Union(t.rcText);
}

// how many thumbnails fit in a grid row: the run of entries sharing the y of
// the first one
static int HomeGridColumnCount(MainWindow* win) {
    auto& c = HomeLayout(win);
    return std::max(1, c.gridColumns);
}

static int HomeGridMoveRow(MainWindow* win, int idx, int direction) {
    auto& c = HomeLayout(win);
    Rect current = c.thumbs[idx].rcText;
    int nearestY = direction < 0 ? INT_MIN : INT_MAX;
    for (const ThumbnailLayout& t : c.thumbs) {
        int y = t.rcText.y;
        if (direction < 0 && y < current.y) nearestY = std::max(nearestY, y);
        if (direction > 0 && y > current.y) nearestY = std::min(nearestY, y);
    }
    if (nearestY == INT_MIN) return -1;
    if (nearestY == INT_MAX) return len(c.thumbs) - 1;
    int target = idx;
    int distance = INT_MAX;
    for (int i = 0; i < len(c.thumbs); i++) {
        Rect r = c.thumbs[i].rcText;
        if (r.y != nearestY) continue;
        int dx = abs(r.x - current.x);
        if (dx < distance) {
            target = i;
            distance = dx;
        }
    }
    return target;
}

// Select the first-row entry at the column remembered when leaving for search.
static void HomeSelectFromSearchReturnCol(MainWindow* win) {
    int n = HomeSelectableCount(win);
    if (n <= 0) {
        win->homePageSelIdx = 0;
        return;
    }
    if (HomePageIsListView()) {
        win->homePageSelIdx = 0;
        return;
    }
    int nCols = HomeGridColumnCount(win);
    nCols = std::max(nCols, 1);
    int col = win->homePageSearchReturnCol;
    col = std::max(col, 0);
    if (col >= nCols) {
        col = nCols - 1;
    }
    // first row only has min(nCols, n) entries
    int firstRowN = n < nCols ? n : nCols;
    if (col >= firstRowN) {
        col = firstRowN - 1;
    }
    win->homePageSelIdx = col;
}

// Keep layout-cache thumb rects in sync with homePageScrollY (without a full
// paint) so keyboard tooltips can use up-to-date geometry after scroll.
static void HomeSyncLayoutCacheScroll(MainWindow* win) {
    auto& c = HomeLayout(win);
    if (!c.valid || !win) {
        return;
    }
    int scrollY = win->homePageScrollY;
    int maxScrollY = std::max(0, c.totalContentDy - c.thumbsVisibleDy);
    if (scrollY > maxScrollY) {
        scrollY = maxScrollY;
        win->homePageScrollY = scrollY;
    }
    if (scrollY < 0) {
        scrollY = 0;
        win->homePageScrollY = 0;
    }
    int dy = c.scrollY - scrollY;
    OffsetHomeCache(c, dy);
    c.scrollY = scrollY;
    c.thumbnailSize = HomeThumbPercent();
}

// scroll so the selected entry is fully visible
static void HomeScrollSelectionIntoView(MainWindow* win) {
    auto& c = HomeLayout(win);
    int idx = win->homePageSelIdx;
    if (!c.valid || idx < 0 || idx >= len(c.thumbs)) {
        return;
    }
    // rects must match current scroll before we measure visibility
    HomeSyncLayoutCacheScroll(win);
    Rect r = HomeEntryRect(c.thumbs[idx]);
    const Rect& area = c.rcThumbsArea;
    if (r.IsEmpty() || area.IsEmpty()) {
        return;
    }
    // scrollY grows as the content moves up
    int dy = 0;
    if (r.y < area.y) {
        dy = r.y - area.y;
    } else if (r.y + r.dy > area.y + area.dy) {
        dy = (r.y + r.dy) - (area.y + area.dy);
    }
    if (dy == 0) {
        return;
    }
    int newScrollY = win->homePageScrollY + dy;
    newScrollY = std::max(newScrollY, 0);
    win->homePageScrollY = newScrollY; // layout clamps against content height
    HomeSyncLayoutCacheScroll(win);
}

// Selection outline rect — must match DrawHomePageLayout / DrawHomeListRow.
static Rect HomeSelectionOutlineRect(const ThumbnailLayout& t) {
    if (HomePageIsListView()) {
        // list: outline is the row, 1px shorter (separator line), with 0.5rem
        // of breathing room on the left and right
        int pad = UiScalePx(8);
        return {t.rcListRow.x - pad, t.rcListRow.y, t.rcListRow.dx + (2 * pad), t.rcListRow.dy - 1};
    }
    // thumbnails: page ∪ name, inflated by the same amounts as paint
    Rect sel = t.rcPage.Union(t.rcText);
    sel.Inflate(UiScalePx(4), UiScalePx(3));
    return sel;
}

// Show/update the infotip for the keyboard-selected home thumbnail. Placed just
// below the blue selection outline, left-aligned with the outline's left edge;
// shifted left if it would extend past the right edge of the last outline in
// that row.
static void HomePageShowSelectionTooltip(MainWindow* win) {
    if (!win || HomePageIsListView() || HomeSearchHasFocus(win)) {
        if (win) {
            win->DeleteToolTip();
        }
        return;
    }
    // never put a tip up from a window that isn't in front: track-mode tips are
    // topmost popups and showing one steals activation, which pulls the frame
    // over the command palette / Change Theme window
    if (GetForegroundWindow() != win->hwndFrame) {
        return;
    }
    auto& c = HomeLayout(win);
    int idx = win->homePageSelIdx;
    if (!c.valid || idx < 0 || idx >= len(c.thumbs)) {
        win->DeleteToolTip();
        return;
    }
    HomeSyncLayoutCacheScroll(win);
    ThumbnailLayout& t = c.thumbs[idx];
    FileState* fs = t.fs;
    if (!fs || len(fs->filePath) == 0) {
        win->DeleteToolTip();
        return;
    }

    // Same text as hover: path + size (size looked up only when shown)
    TempStr tip = str::DupTemp(fs->filePath);
    i64 size = file::GetSize(fs->filePath);
    if (size >= 0) {
        tip = fmt("%s  %s", tip, str::FormatSizeShortTemp(size, nullptr));
    }

    HWND hwnd = win->hwndCanvas;
    Rect outline = HomeSelectionOutlineRect(t);
    // a little below the outline so the tip clears the blue border
    int tipClientX = outline.x;
    int tipClientY = outline.y + outline.dy + UiScalePx(4);

    int rightEdgeClient = outline.x + outline.dx;
    if (!HomePageIsListView()) {
        int n = len(c.thumbs);
        int lastInRow = idx;
        while (lastInRow + 1 < n && c.thumbs[lastInRow + 1].rcText.y == t.rcText.y) lastInRow++;
        Rect lastOutline = HomeSelectionOutlineRect(c.thumbs[lastInRow]);
        rightEdgeClient = lastOutline.x + lastOutline.dx;
    }

    POINT tl{tipClientX, tipClientY};
    POINT tr{rightEdgeClient, tipClientY};
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &tr);
    win->ShowToolTipAt(tip, outline, Point(tl.x, tl.y), false, tr.x);
}

// select the first entry, e.g. after the filter changed the list
void HomePageSelectFirst(MainWindow* win) {
    win->homePageSelIdx = 0;
    win->homePageSearchReturnCol = 0;
}

void HomePageRefreshFiles(MainWindow* active, Str selectedPath) {
    struct Selection {
        MainWindow* win;
        Str path;
    };
    Vec<Selection> selected;
    for (MainWindow* w : gWindows) {
        if (w->IsDocLoaded()) continue;
        Str path = w == active ? selectedPath : HomePageSelectedFilePathTemp(w);
        VecAppend(selected, Selection{w, str::Dup(path)});
    }
    HomePageInvalidateLayoutCache();
    for (Selection& selection : selected) {
        MainWindow* w = selection.win;
        HomePageRelayout(w);
        auto& c = HomeLayout(w);
        for (int i = 0; i < len(c.thumbs); i++) {
            if (!str::Eq(c.thumbs[i].fs->filePath, selection.path)) continue;
            w->homePageSelIdx = i;
            HomeScrollSelectionIntoView(w);
            HomePageRelayout(w);
            break;
        }
        str::Free(selection.path);
        w->DeleteToolTip();
        HwndInvalidate(w->hwndCanvas);
    }
}

void HomePageSetCompleted(MainWindow* win, Str path, bool completed) {
    if (!IsMainWindowValidAndNotClosing(win) || !HasPermission(Perm::SavePreferences)) return;
    FileState* fs = FileHistoryFindByPath(path);
    if (!fs || fs->completed == completed) return;
    fs->completed = completed;
    ScheduleSaveSettings();
    HomePageRefreshFiles(win, path);
}

// hide keyboard-selection tip on deactivate; restore it when the frame is active
void HomePageOnWindowActivate(MainWindow* win, bool active) {
    if (!win) {
        return;
    }
    if (!active || IsIconic(win->hwndFrame)) {
        // Also when the frame is iconic: activate can fire while minimized and
        // ClientToScreen then pins the tip at the top-left of the desktop (#5928).
        win->DeleteToolTip();
        // a click in the no-activate About popup can report WA_INACTIVE while
        // the frame is still the foreground window; don't close it then
        HWND fg = GetForegroundWindow();
        HomeChromeCtrl* chrome = HomeChrome(win);
        bool stillUs = fg == win->hwndFrame || (chrome && chrome->aboutHover && fg == chrome->aboutHover->native);
        if (!stillUs) {
            HideHomeAboutHover(win);
        }
        return;
    }
    // only restore the selection tip (positioned at the active entry, not cursor)
    if (win->IsCurrentTabAbout()) {
        HomePageShowSelectionTooltip(win);
    }
}

// the entries wnd of the chrome tree, if the home page is showing
static HomeEntriesCtrl* HomeEntries(MainWindow* win) {
    HomeChromeCtrl* chrome = HomeChrome(win);
    return chrome ? chrome->entries : nullptr;
}

// mouse left the canvas (or the page scrolled): drop the active entry so the
// close button goes away
void HomePageClearActiveEntry(MainWindow* win) {
    HomeEntriesCtrl* entries = HomeEntries(win);
    if (!entries) {
        return;
    }
    if (win->homeRoot) {
        win->homeRoot->ClearHover();
    }
    entries->SetActiveEntry(-1);
}

// File of the entry at (x,y), empty if there is no entry there. Replaces the
// old "look the click up in win->staticLinks" - entries are VirtCtrls now
Str HomePageFilePathAtTemp(MainWindow* win, int x, int y) {
    HomeEntriesCtrl* entries = HomeEntries(win);
    if (!entries) {
        return {};
    }
    Point ptLocal{0, 0};
    ILayout* el = ElementFromPoint(win->homeRoot, {x, y}, &ptLocal);
    VirtCtrl* w = el ? el->AsVirtCtrl() : nullptr;
    HomeEntryCtrl* e = entries->EntryForCtrl(w);
    if (!e) {
        return {};
    }
    return str::DupTemp(e->filePath);
}

// Mouse over a file entry: update homePageSelIdx and, in thumbnail view, show
// the tip at that entry (not at the cursor).
bool HomePageOnHover(MainWindow* win, int x, int y) {
    HomeEntriesCtrl* entries = HomeEntries(win);
    if (!entries) {
        return false;
    }
    Point ptLocal{0, 0};
    ILayout* el = ElementFromPoint(win->homeRoot, {x, y}, &ptLocal);
    VirtCtrl* w = el ? el->AsVirtCtrl() : nullptr;
    HomeEntryCtrl* e = entries->EntryForCtrl(w);
    if (!e) {
        return false;
    }
    entries->SetActiveEntry(e->idx);
    return true;
}

// file of the keyboard-selected entry, empty if there's no selection
Str HomePageSelectedFilePathTemp(MainWindow* win) {
    RefreshHomeSelectionLayout(win);
    auto& c = HomeLayout(win);
    int idx = win->homePageSelIdx;
    if (!c.valid || idx < 0 || idx >= len(c.thumbs)) {
        return {};
    }
    FileState* fs = c.thumbs[idx].fs;
    if (!fs) {
        return {};
    }
    return str::DupTemp(fs->filePath);
}

// keyboard navigation of the file list (issue #1136). dCol/dRow are in grid
// steps; in list view only dRow matters. Moving up past the first row puts
// focus in the search box
void HomePageMoveSelection(MainWindow* win, int dCol, int dRow) {
    int n = HomeSelectableCount(win);
    if (n == 0) {
        win->DeleteToolTip();
        return;
    }
    int idx = win->homePageSelIdx;
    if (idx < 0 || idx >= n) {
        // nothing selected yet: any arrow key selects the first entry
        win->homePageSelIdx = 0;
        HomeScrollSelectionIntoView(win);
        HwndInvalidate(win->hwndCanvas);
        HomePageShowSelectionTooltip(win);
        return;
    }

    int nCols = HomePageIsListView() ? 1 : HomeGridColumnCount(win);
    int delta;
    if (HomePageIsListView()) {
        // one entry per row; left/right have nothing to move along
        delta = dRow;
    } else {
        delta = dCol + (dRow * nCols);
    }
    if (delta == 0) {
        return;
    }
    int newIdx = idx + delta;
    if (!HomePageIsListView() && dRow) {
        newIdx = HomeGridMoveRow(win, idx, dRow);
        if (newIdx >= 0) newIdx += dCol;
    }
    if (newIdx < 0) {
        // above the first row: hand focus to the search box, remember column
        if (dRow < 0 && win->homeSearch) {
            int groupIdx = idx < HomeLayout(win).nRecent ? idx : idx - HomeLayout(win).nRecent;
            win->homePageSearchReturnCol = HomePageIsListView() ? 0 : (groupIdx % nCols);
            win->DeleteToolTip();
            HomePageFocusSearch(win);
            HwndInvalidate(win->hwndCanvas); // drop selection outline while typing
            return;
        }
        newIdx = 0;
    }
    if (newIdx >= n) {
        newIdx = n - 1;
    }
    if (newIdx == idx) {
        return;
    }
    win->homePageSelIdx = newIdx;
    HomeScrollSelectionIntoView(win);
    HomePageRelayout(win);
    HwndInvalidate(win->hwndCanvas);
    HomePageShowSelectionTooltip(win);
}

void HomePageOnVScroll(MainWindow* win, WPARAM wp) {
    USHORT msg = LOWORD(wp);
    int lineDy = HomePageIsListView() ? kHomeListRowDy : HomeThumbDy() + kThumbsSpaceBetweenY;
    int pageDy = lineDy * 3;

    int newScrollY = win->homePageScrollY;
    switch (msg) {
        case SB_LINEUP:
            newScrollY -= lineDy;
            break;
        case SB_LINEDOWN:
            newScrollY += lineDy;
            break;
        case SB_PAGEUP:
            newScrollY -= pageDy;
            break;
        case SB_PAGEDOWN:
            newScrollY += pageDy;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            int pos = (int)(short)HIWORD(wp);
            // overlay scrollbar sends full position in HIWORD for THUMBTRACK
            if (win->overlayScrollV) {
                pos = win->overlayScrollV->nTrackPos;
            }
            newScrollY = pos;
            break;
        }
        case SB_TOP:
            newScrollY = 0;
            break;
        case SB_BOTTOM:
            newScrollY = INT_MAX; // will be clamped by layout
            break;
    }
    newScrollY = std::max(newScrollY, 0);
    if (newScrollY != win->homePageScrollY) {
        win->homePageScrollY = newScrollY;
        HomePageRelayout(win);
        HwndInvalidate(win->hwndCanvas);
    }
}

void HomePageOnMouseWheel(MainWindow* win, int delta, bool isCtrl) {
    if (isCtrl && !HomePageIsListView()) {
        SetHomeThumbSize(win, HomeThumbPercent() + (delta > 0 ? 25 : -25));
        return;
    }
    int thumbsRowDy = HomePageIsListView() ? kHomeListRowDy : HomeThumbDy() + kThumbsSpaceBetweenY;

    int scrollBy = thumbsRowDy / 3;
    if (delta > 0) {
        scrollBy = -scrollBy;
    }
    int newScrollY = win->homePageScrollY + scrollBy;
    newScrollY = std::max(newScrollY, 0);
    if (newScrollY != win->homePageScrollY) {
        win->homePageScrollY = newScrollY;
        HomePageRelayout(win);
        HwndInvalidate(win->hwndCanvas);
    }
}
