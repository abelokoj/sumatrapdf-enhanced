/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "gui/Dpi.h"
#include "base/File.h"
#include "base/Pixmap.h"
#include "base/GdiPlusUtil.h"
#include "base/UITask.h"
#include "base/Win.h"

#include "gui/UIModels.h"

#include "Accelerators.h"
#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "base/GuessFileType.h"
#include "EngineAll.h"
#include "DisplayMode.h"
#include "DisplayModel.h"
#include "ProgressUpdateUI.h"
#include "TextSelection.h"
#include "TextSearch.h"
#include "SumatraPDF.h"
#include "SumatraConfig.h"
#include "MainWindow.h"
#include "Annotation.h"
#include "AnnotPlacement.h"
#include "Notifications.h"
#include "Canvas.h"
#include "SelectionToolbar.h"
#include "WindowTab.h"
#include "resource.h"
#include "Commands.h"
#include "AppTools.h"
#include "CommandAvailability.h"
#include "Menu.h"
#include "SearchAndDDE.h"
#include "AnnotEditToolbar.h"
#include "AnnotFilterToolbar.h"
#include "Tabs.h"
#include "PagePosition.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/PlatformWindow.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"
#include "SidebarPanel.h"
#include "gui/VirtHost.h"
#include "gui/win/TabsCtrl.h"
#include "gui/win/ScrollableMenu.h"
#include "FindBar.h"
#include "SumatraDialogs.h"
#include "Translations.h"
#include "SvgIcons.h"
#include "EnhancedIcons.h"
#include "Theme.h"
#include "DarkMode.h"
#include "ReadAloud.h"
#include "RenderCache.h"
#include "Toolbar.h"

// https://docs.microsoft.com/en-us/windows/win32/controls/toolbar-control-reference

constexpr int kButtonSpacingX = 6;

// distance between label and edit field
constexpr int kTextPaddingRight = 6;

struct ToolbarButtonInfo {
    const char* icon = nullptr; // gIcon*, or null for a separator / page box / text
    int cmdId = 0;
    Str toolTip;
    Str svgIcon; // custom SVG from settings
    bool isText = false;
};

static void CollectPaletteControls(ILayout*, Vec<VirtCtrl*>&);
static int PinnedToolCommand(Str);
static int OriginalCommandId(int);
static int PinnedToolIndex(MainWindow*, int, Color = kColorUnset);
static void TogglePinnedTool(MainWindow*, int, Color = kColorUnset);
static ILayout* NewPaletteHeader(MainWindow*, Str, int, ILayout* = nullptr);
static bool ShowPinToolMenu(MainWindow*, int);
static const char* kToolSettingsIcon =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round"><path d="M3 6h4m4 0h10M7 3v6M3 12h10m4 0h4M17 9v6M3 18h5m4 0h9M8 15v6"/></svg>)";

static const char* kTextSelectToolIcon =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 4h2c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H8m8-16h-2c-1.1 0-2 .9-2 2m4 14h-2c-1.1 0-2-.9-2-2M9 12h6" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>)";
static const char* kHandToolIcon =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 12V6a2 2 0 0 1 4 0v5-7a2 2 0 0 1 4 0v7-5a2 2 0 0 1 4 0v8c0 5-3 8-7 8-2 0-4-1-5-3l-4-5a2 2 0 0 1 3-3l1 1Z" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>)";
static const char* kHandQuillIcon =
    R"quill(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1254 1254"><path fill="currentColor" fill-rule="evenodd" d="M464 1180h-43l-1 -2h-7h-1l-12 -2h-1h-4l-1 -2h-8l-4 -2l-7 -2l-39 -12h-3l-1 -2l-11 -2h-1h-4l-1 -2h-12l-1 -2h-8h-1h-26h-1h-10l-1 2h-12l-1 2h-4h-1h-3l-1 2l-14 2l-15 6l-18 8l-2 -2v-2l10 -23v-2l2 -5v-2l4 -5l4 -15l2 -3l14 -43l18 -18l18 -23l10 -13l-2 -2v-4l6 -8l-10 -4l-2 -3l-1 -1h-2l-3 -5v-8l2 -6v-6v-1v-4l2 -1l2 -19l4 -12v-2v-8l-2 -1v-12v-1v-21v-1v-6l2 -1l2 -10l2 -3l4 -13l2 -3v-2l4 -10v-2l2 -3v-2l2 -3v-2l8 -16l2 -25l2 -1v-3l2 -6l4 -11l8 -10l7 -8l10 -6l34 -18l2 -2l11 -4l8 -6h2l7 -4h2l7 -4l6 -2l2 -1l-2 -4v-3l18 31l1 17v-20h1v3l2 1v10h0v-13l-2 -6l-2 -2l-4 -7l-6 -8v-2l-2 -2l-6 -10l-2 -9v-4l-2 -1l1 -18l5 18l8 13l10 11l6 12l2 10v-9v-1v-3l-2 -1v-3l-2 -6l-6 -12l-12 -15l-6 -11v-3l-2 -5v-4v-1v-24v-1v-4l2 -3l2 16l2 3h0v-10l-2 -1v-21l2 -1v-7l3 -5l1 1v1v6l-2 1l2 21v1v5l2 1l2 11l6 17l8 13l2 2l2 5l2 2v4l2 3l2 7v1v4l2 1v5v1v6h2v-21l-2 -1v-5v-1v-3l-2 -1v-3l-2 -6l-14 -29v-2l-4 -8v-6l-2 -4l-2 -9v-1v-6l-2 -1v-18l2 -1v-6v-1v-5l2 -1v-7l5 -9l1 26h0l2 -21v-1l2 -12v-1l2 -7l2 -3l2 -8l6 -13v5v1v5l-2 1v8v1v10v1v6h2v-15v-1v-6l2 -1v-5v-1l2 -7l6 -18l2 -5v-2l2 -4l4 -3l6 -13l2 -3l4 -8l9 -14l-1 8l-2 1v4v1v5l-2 1v9v1v12v-6l2 -1v-16l2 -1v-4v-1v-3l2 -4v-3l4 -9l4 -10l8 -9l6 -11l6 -7l4 -7l4 -6l3 -2l-9 26v3l-6 15v4l-2 6v5v1l-2 19v1v17v1v9l2 6v5l-2 -7l-2 -11v-1v-7l-2 -1v-25l2 -1v-7v-1v-3l-2 1v6v1v5l-2 6v9l-2 1v19l2 1v18l2 1v4l2 5l-1 3l-3 -5l-2 -11v-1v-4l-2 -1v-4v-1l-2 -25l2 -3v-7h-2v12l-2 1v32l2 1v12l4 9l-1 5l-3 -7v-7l-2 -4l-2 -10v-1l-2 -31v9l-2 1v28l2 1v8v1l2 10l2 8v3l4 9l4 10l2 6v3l2 1v3v1l2 10v1v14l2 1l-2 1l1 9l1 -5v-8l2 -1l-1 -38l1 3v5l2 6v32l2 -7v-5l2 -1v-18l2 -1v-26l-2 -1v-7v-1v-5l2 7v6l2 1v6v1v8l2 1v14l-2 1v13l2 -7v-9l2 -1v-9v-1v-21v-1v-8l-2 -1v-7v-1v-6l2 7v1l2 9v1v4l2 1l1 31l1 -4v-1v-9l2 -1v-6l-2 -1v-18v-1l-2 -15v-1v-5l-2 -1v-5v-1v-5l-2 -1l1 -7l1 3l2 14l4 9v4v1l3 21v-30l1 4v5l2 1v24v1l-2 16v1v6l2 -4v-4v-1v-5l2 -1v-5v-1v-9l2 -1v-19l-2 -1v-16l-2 -1v-4v-1v-4l-2 -1l-2 -15l-4 -12l-2 -12v-1v-32l-2 3v9l-2 1v5v1l-2 33l-2 -4v-33l2 -1v-14l2 -5v-3l-4 10v4v1v4l-2 1v4l-2 6v7v1v29v1v5l2 1l-1 6l-1 -2l-2 -13l-2 -1v-8v-1v-29l2 -1v-10l2 -1v-8l2 -4l2 -7l8 -22l10 -12l5 -5l4 -2h1l-2 2l-8 16v3v1v3l-4 9v11l-2 1v4h2v-7l2 -1v-4v-1v-3l4 -12l10 -22l8 -10l11 -10h1l-2 2l-2 3l1 1l1 -2l12 -16l18 -22v2l-10 23l-2 3l1 2l9 -19l4 -6l12 -16l12 -11l2 -1l-8 13l-6 16v2l-2 3l-2 10l-6 14l1 3l5 -16l2 -3v-2l10 -20l4 -5l4 -6l14 -17l6 -8l6 -7l6 -8l7 -6l1 2l-4 7v2l-2 3l-6 18v2l2 -7l8 -17l14 -20l7 -6h1l-4 5l-4 8l1 3l7 -10l10 -10l8 -6l6 -2v1l-3 3l-7 8l-8 10l-8 18l-4 12v3l-2 1v4l-2 6v5v1v9l-2 1v3l2 1l-2 1v3l2 1v10v1v7l2 1v13l2 1v21l-2 1v16l-2 1v3v1v3l-2 6l-2 3v1l6 -11l2 -7l4 -7l2 -6v-3l2 -1v-3v-1v-3l2 -1v-10l2 -1v-11v-1v-24l2 -1v-8v-1v-6l2 -1v-4l2 -5v-4l2 -7v-3l14 -30l6 -9l12 -15l21 -18l18 -12l34 -18h1l-10 9l1 1l14 -12l8 -6l14 -6l1 1l-14 14l-4 6l1 1l16 -18l13 -10l17 -8l9 -2h2l-30 28l-2 6l-10 13l-9 19l-1 -2l2 -3v-3l2 -2l4 -7l4 -5l4 -9l6 -10v-1l-14 14l-12 19l-8 14l-4 14l-1 1l1 -6l8 -21l20 -27l2 -4l6 -6h-1h-1l-12 11l-12 16l-4 8)quill"
    R"quill(l-2 2l-8 14v2l-4 8v6l-2 4l-2 8v1l-2 8v1v5l-2 1v5v1v5l-2 1v9l-6 14l1 2l3 -6v-2l6 -11v-3l4 -12v-8l2 -1l2 -15l3 -4l1 2l-2 4v4l-2 5v5l-2 6l-2 11l-4 12l1 3l5 -10l6 -18v-3l2 -4v-6h2v4l-2 1v10l-2 1v7l-4 7l1 3l5 -9l4 -12v-3l2 -3v-4v-1l2 -10l2 -2v1v1v5l-2 6v5l-2 1l-2 11l-2 4l1 2l7 -14l7 -24l1 4l-2 6v5l-4 11v3l-4 6l1 1l5 -7v-2l4 -8v-3l1 -2l1 5l-2 1v3h2v-2l4 -7l2 -11l2 -3l2 -8l2 -3l2 -7l2 -4v8l-2 3v3l-4 10v3l-2 6l-4 7l-2 3l1 1l5 -6l6 -14v-2l2 -3v-2l2 -3l4 -10l4 -11l2 -1h0l-2 3l-4 15v3l-2 1v3l-2 9l-6 10l1 2l3 -3l6 -12v-5l4 -9v-4l4 -12l2 -1h0v1v5l-2 5l-2 11l-2 4v4l-2 4v2l2 -4l4 -9l4 -9v-2l4 -8l6 -15l1 -1h1l-4 10v3l-6 18l-4 6l1 3l5 -9l4 -12l4 -7v-2l6 -16h2v2l-8 24l-6 11v3v2l4 -6l9 -16l1 2l-2 3v3l4 -5l10 -22l8 -9l2 -6l15 -16l-1 3l-6 8l-8 14l-6 8l-6 10v2l-4 6l1 1l7 -10l4 -9l6 -7v2l-2 2l-6 15v2l-4 5h1l3 -2l22 -36l18 -20v2l-4 3l-16 24l-2 7l-8 10l1 2l3 -4l2 -4l5 -4h1v-2l4 -3v-3l10 -15l12 -15l7 -6l4 -2l11 -10l4 -1l-7 7l-8 6l-11 12l-8 12l-6 8l-2 5l-2 2l1 1l19 -24l22 -20h2l-22 25l1 3l9 -12h2l33 -32l2 -2l1 1l-12 13l-14 16l1 2l5 -4l11 -12h1l15 -14l4 -2h1l-14 13v1l-6 7v1l10 -10l12 -10l7 -4l7 -4l6 -4l4 -4h-1l-8 4l-3 2l-2 -1l3 -3l7 -3l-2 -1l-11 6l-7 4h-1l1 -1l-2 -1l-3 4h-3h-2l8 -6l3 -2l5 -3l-2 -1l-19 10l-3 2h-2l-3 2l-2 2l1 -4l6 -4l3 -2v-2l-12 6l-12 8l-6 6h-1h0l7 -8l9 -7l-1 -1l-16 10l-12 10h0l15 -14h-2l-14 8l-15 14h-1l2 -3l10 -8l-2 -1l-3 4l-4 2l-6 6h-1l-13 14l-1 -2l4 -4l10 -10l20 -16h-2h-2l-2 2l-7 4l-3 2l-3 2l-11 8l-10 10l-14 18l-7 14l-1 -3l8 -15l8 -14l12 -13l-2 -1l-18 18l-10 14v-3l12 -17l10 -9l-2 -1l-22 21l-14 21v-2l6 -11l18 -25h-1l-6 4l-18 20l1 -3l14 -17h-1l-17 14v1l-8 8l-12 17l-6 13l-2 7l-4 5l-5 19l-1 -4l2 -1l4 -19l2 -4v-6l4 -5l2 -6l16 -22l20 -19l9 -6l12 -6l8 -2l3 -2v1l2 1l2 -2l8 -4l12 -4h3l1 -2l2 1l-7 5l-3 2h2l5 -4l9 -4l2 -2l13 -4h3l9 -4h4l9 -2h5l5 -2l6 -2h1h7l6 -4h10l1 -2h8l5 -2l14 -2l1 -2h3h1h3l1 -2h3h1h3l9 -4l23 -10l9 -6l19 -18h1l-1 8l-2 3l-2 7l-12 21l-16 24l-4 4v4l-6 9l-6 11l-2 2l-6 14l-12 17l-17 18l-6 4l-4 2l-3 2h-3h-1h-10l-1 2h-4l-6 2l-7 6h-3l-1 -2l-9 2l-1 -2h-4l9 -2h0l-3 -1l3 -1h8l3 -2l-3 -2h-32l-1 2h-12l-1 2h-3l-8 2h36l2 1l-1 1h-25l-1 2h-4l-7 2h29h0h-17l-1 2h-7h-1l-5 1l20 -1l1 2h9h1h5l4 2l14 4l2 1v3l2 4l4 4l6 2l2 -2h5l8 -4l7 -6v2l-14 26l-16 24l-6 6v2l-4 6l-4 5l-8 7v3l-2 3l-16 19l-1 1l-13 12h-1l-4 6l-14 10l-13 4l-16 2l-1 -2h-5l-5 -2h-4l-5 -2h-7h-1h-13h-1h-4l-8 4l-4 4h-3h-7l-1 -2h-7l-1 2h-20l-3 -2v-2l-1 2h-4l-5 2h-7l-7 4h-3l-3 2l-10 2l-10 6h2l10 -4h7l4 -2l9 -2h1h19l2 1l-2 1h-16l-1 2h-5l-4 2h-3l-4 2h-6l-3 2l-8 2l-4 3l2 1l1 -2h7l5 -2h5l1 -2h5h1h13l1 -2l28 4l4 2h3l13 6l6 8l11 8l15 6h15l1 -2h4l6 -2l10 -4l11 -8v1l-2 6l-18 24l-4 6l-14 12l-2 6l-10 12l-10 10v1l-20 18l-19 14l-19 10h-2l-3 2l-9 4h-3l-5 2h-5h-1h-8l-1 2h-14l-1 -2h-8h-1h-5l-1 -2h-13l-1 -2h-9h-1h-22h-1h-9l-1 2h-5l-5 2h-4l-21 6v2h3l18 -6l10 -2h1h3l-2 2l-11 2l-4 2h-3l-12 4l-3 2l-11 2l-2 2h2l8 -2h3l5 -2h4l5 -2h9l1 -2h15h1h10l1 2h4l5 2l16 2h1l9 2h1h7l1 2h16l1 -2h13l4 -2h3l9 -4l10 -4h2l-14 12l-5 4l-4 4l-6 2l-1 2v1l-2 3h-1l-14 12l-20 14l-15 8h-2l-12 6l-12 4l-7 2l-4 2h-3l-5 2h-4h-1h-4l-1 2h-4h-1h-4l-1 2h-5h-1l-16 2l-1 )quill"
    R"quill(2h-8l-8 4h-3l-6 2l-9 3l4 1l1 -2h6h2h1l-2 2h-8l-15 6h-3l-14 6h0h2l8 -2h8l4 -2l8 -1l-2 1h-3l-7 4h-3l-12 4l-22 10h0h2l3 -2l12 -4h7l1 -2h6l6 -2l4 1l-13 5l-10 2l-9 4h-2l-3 2h-2l-13 6l-4 4h1l16 -8l12 -4l13 -2l9 -2h4l1 -2l15 -2l4 -2h3l8 -4l8 -2l6 -4h-2l-11 4l-3 -1l8 -3l3 -2h6l6 -4h3l6 -3l-2 -1l-4 2l-20 4v-2l15 -2l7 -3l-8 -1l-1 2h-9v-2l19 -2l5 -2h7l9 -4h2l3 -2h2l5 -4h3h1l-40 28l-18 8l-18 6h-3l-1 2h-3l-9 2l-9 2l-12 4h-3l-29 12l-2 2l-8 4l-3 3l2 1l18 -10l15 -4l16 -4l7 -2l10 -2l3 -2l11 -4l17 -8h2l21 -12v1l-5 5l-12 12l-8 6h-3l-4 6l-8 4h11l1 2h4l12 4l16 8l14 8l7 4l2 2l23 12l5 4l3 2l47 30l3 4l20 12l46 34l6 4l15 4l10 2h1h8l1 2h22l5 -8l8 -10l13 -8h8l1 -2h12l1 2h5l14 4h3l5 2h5l1 2h14l1 -2h5l4 -2h6l11 -10l8 -2h17l1 -2h10l9 -4h4h1l9 -2h1h6l1 -2h7h1l23 -2h1h8l6 -2l15 -2h1l21 -2h1l26 -2l1 -2h12l1 2l-6 3l3 1l1 1l-3 1l-1 2l-4 1l4 1v2h-4l-1 -2l-9 1l9 3l7 4l4 3l-2 3l-7 -6h-4l-5 -4l-6 -2h-3h-1h-2l1 2h6l8 4l7 7l2 2l-1 1l-11 -4h-4l8 4l10 11v1l-15 -12l-4 -2l-1 1l4 3l8 10l6 7l-1 1l-19 -16l-3 -2l-3 -2h-4l13 12l11 12l8 11l4 7l-1 2l-8 -8l-6 -6h-2h-1l12 19l2 4v1l-20 -20l-2 1l6 7l6 7l10 17h0l-4 -3l-12 -15v2l10 16v2l2 5v2l4 5l1 6l-1 -1v-2l-10 -13v-3l-14 -18l-6 -7l-2 2l12 16l4 8l6 7l2 6v2l4 7l6 15v8l2 1l-1 8l-9 -20l-8 -10v-3h-2l2 9l8 14l2 9v3l4 7v3l2 5v5v1v5l2 1l-1 5h-1v-3l-4 -8l-2 -7l-6 -9v-6l-2 -3l-2 -7l-6 -9v-2l-8 -12l-4 -6l-10 -14l-10 -10h-2l20 24v3l12 16l8 17v2l2 3v5l2 3l6 18v3l2 4v3l2 5v4l2 1v6v1v15l2 1l-2 2l-6 -23l-2 -3l-2 -8l-2 -3l-6 -18l-4 -7v-2l-4 -7l-2 -4l-2 -2l-7 -15l-1 2l12 22l2 7l6 13l2 8l2 3l2 10l4 11v4l2 4v4l2 5v4l2 6v5v1v8l2 1l-1 18l-15 -48v4l2 4v3v1v3l2 5l2 7v1l2 14l2 1v5v1v5l2 1v7v1v6l2 1v12l-2 -1l-2 -4v-6l-4 -5v-6l-2 -2v3v1l2 8l2 5v4v1v3l4 9v11l2 4v5h-1l-2 -4l-1 1l4 10l-2 1l-3 -2l-1 2l2 3v2l4 9h-1l-5 -4l-6 -6l-2 -5l-3 -3l-1 2l2 2l6 11l8 10v2l2 1v1l-2 1h2l2 4h-57h-1h-6h-1h-41h-1l-24 -2l-1 2h-9h-1h-7l-1 2h-7l-12 -4l-7 -6l-6 -8l-22 -2h-1h-5l-9 -4h-6h-1h-15h-1h-5l-1 2h-8l-9 6h-2l-9 4h-12l-8 -4l-8 -11l-6 -12l-4 -12v-3l-2 -4l-4 -18h-15l-1 -2h-29h-1h-7l-1 2h-8l-27 12h-3l-7 2h-3l-5 2h-4l-1 2h-4h-1l-17 2h-1h-35l-1 -2h-7h-1h-5l-1 -2h-5l-6 -2h-4h-1l-15 -4l-4 -2h-3l-9 -2l-3 -2l-5 -2l-18 -8h-2l-14 -6h-2l-13 -8h-3l-6 -2l-12 -6l-6 -2h-3l-13 -4h-6l-5 -2h-1l6 8l6 13v3l2 1v18l-4 12l-6 8l-6 7l-9 4h-3l-1 2h-13l-1 -2h-5l-5 -2h-4l-4 -2h-3l-27 -10l-9 10l-9 4h-3l-1 2h-12l-6 -4l-10 -8l-20 -20l-15 -10l-13 -10l-11 -13l-9 -17l-1 4l-6 6v5l-2 4l-4 3v3l-2 5l-4 2l-6 2l-2 3l-6 10v2l-2 3v2l-2 3v2l-4 9v3l-2 9v4l-2 1v4l-2 3l-10 4l-12 8l-23 20l-19 20l-18 22v1l2 1l21 -12h2l3 -2l18 -6h3l4 -2h4h1h4l1 -2h4h1l17 -2h1h27h1h8l1 2h6h1l10 2h1l11 2l7 4h3l44 14h3l9 4h4h1h4l1 2h12l1 2h10h1h28h1h10l1 -2h6h1h5l1 -2l14 -2l12 -4l3 1l-25 9h-3l-9 4h-4h-1h-4l-1 2h-14ZM1098 75l4 -4v-1l-6 5ZM1094 79l2 -3h0l-4 3ZM360 872l6 -2h3l5 -3v-2l6 -10l8 -14l1 -1h1l2 -1l18 -37l8 -10l10 -20l2 -2l14 -26l4 -7l14 -26l6 -7l2 -5l8 -12l2 -5l2 -2l6 -13l8 -11l10 -16l6 -8v-3l4 -5l2 -3l4 -6l2 -3l2 -3l2 -3l18 -27l2 -3l4 -6l8 -10l4 -7l2 -3l20 -28l32 -42l8 -9l2 -5l26 -30l28 -30v-1l38 -39v-1l2 -1l2 -4l50 -48h1l13 -14h1l39 -36l50 -40l2 -2l1 2l3 -2l)quill"
    R"quill(5 -6l13 -10l8 -6l3 -2h2l9 -8l12 -8l19 -14l46 -30l62 -36l21 -14l4 -2l4 -4h-1l-28 18l-5 2l-14 10l-9 4l-2 2l-7 2l-2 2l-9 4l-5 4l-19 10l-10 8l-13 6l-8 6l-3 2l-5 4h-3l-44 30l-7 6l-16 12l-4 4l-4 2l-51 40l-39 36h-1l-46 44h-1l-72 72v1l-10 11l-16 18l-8 8l-42 53l-52 71l-8 13l-4 3l-14 22l-2 3l-10 15l-2 3l-10 14l-4 8l-6 8l-18 31l-6 10l-4 7l-8 12l-2 5l-4 7l-2 5l-2 2l-2 5l-2 2l-2 5l-6 7l-6 14l-2 2l-10 18l-6 11l-4 7l-2 2l-4 9l-2 2l-2 7l-6 9l-4 8v2l6 -10l1 -2l1 1l-14 25l-4 7v1ZM1092 88l6 -5v-1l-8 6ZM1016 113l15 -11l4 -2l1 1l-3 3l-3 3v1l16 -12h2l6 -2h0l-4 4h1l8 -6h3l12 -8h-2l-4 2l-9 6l-1 -2l6 -4h-1l-4 2h-2l-4 4l-14 6h-1l4 -4l6 -3l-2 -1l-2 2l-2 2l-9 4l-11 8l-8 7ZM1088 91l2 -2v-1l-4 3ZM1026 96l2 -1l-3 -1l-1 2ZM1066 112l7 -2l7 -5l-1 -1l-15 8ZM1028 110l2 -2h-1l-3 2ZM1028 136l12 -2h1h3l9 -4l14 -6l5 -5l-1 -1l-10 6l-8 2l-9 4l-6 2l-14 4ZM1014 147l11 -1h1l11 -2l6 -2l5 -2h2l8 -5l-2 -1l-6 4h-3l-9 2h-4l-1 2h-4l-4 2h-4l-11 3ZM990 163l8 -1h1h15l1 -2h5l4 -2h5l18 -8l7 -4h-3l-5 2l-12 4l-13 2l-1 2h-5l-6 2h-4h-1h-3l-1 2h-3h-1h-3l-7 3ZM840 158l2 -1l-1 -1l-1 1v1ZM855 170l1 -3l2 -2v-2l3 -3h1l-2 2l-2 5ZM980 173l33 -3h1h3l11 -4l6 -2l8 -4h-2l-3 2h-6l-3 2h-3l-1 2h-19l-1 2l-17 2h-1l-8 2l-4 1ZM968 180h12l1 -2h32h1l7 -2l3 -2h6l5 -4h2l7 -4h-2l-5 2l-9 4h-4h-1l-13 2h-1h-11l-1 2h-18l-1 2h-5h-1h-4l-4 2ZM1000 189l8 -1h1h5l1 -2h7l14 -8h-1l-3 2l-9 2h-4l-1 2h-13h-2h-1l-6 -1l7 -1h0h-8h-1h-13l-1 2h-14l-1 2h-5h-1l-7 2h28h1ZM844 186v-2l3 -6l1 1ZM910 181l2 -1l-1 -2l-1 2v1ZM756 306l8 -25l6 -25v6v1l-2 11v1v4l-2 1v4v1l-2 7l-2 7v3l6 -16l2 -6v-3l2 -1v-3v-1v-3l3 -7l1 2l-2 1v12l-2 1v4v1l-2 7l-2 7v1l6 -9l4 -15l2 -7v-1v-4l2 -6v-6l2 -1v-7l2 -6v-5v-1l2 -9l6 -18l10 -22v-1l-8 12l-10 19l-7 19l-1 -2l2 -1v-7l4 -12l4 -8v-2l4 -5l-1 -1l-7 9l-10 22l-6 21l-2 10l-2 4l-2 9v1v6l-2 1v6v1v5l-2 1v4v1l-2 7l-2 4v2ZM738 324l6 -15v-3l4 -9v-4v-1v-4l2 -1v-5l1 -2l1 6l-2 1v8v1l-2 11l-2 5l1 2l1 -2l2 -6l2 -3l2 -9v-3l2 -4v-3l2 -1v-10l2 -6v-5l2 -6v-5l2 -1l4 -19l2 -4v-3l2 -3l2 -10l10 -18l6 -10v-1l-5 4l-1 -2v1l-18 24l-6 11v2l-4 4l-4 15l-2 3l-2 6v3l-2 1v9l-2 1v32l-2 1v13l-4 12v2ZM792 191l4 -5l-1 -2l-3 6v1ZM766 200v-1v-1l-2 1v1ZM764 203v-1v-2l-2 2v1ZM978 206h-11l-1 -2l5 -2h9v2ZM678 385l6 -11v-2l4 -8l5 -22l1 4l-2 1v5v1v5l-2 1v7l-2 4v2l2 -4v-2l2 -3l4 -9l4 -22v9v1v6l-2 1v9l-4 8h1l1 -1v-2l2 -3l6 -18l2 -13l2 -1v-5v-1v-4l2 -4v7v1v7l-2 1v13l-2 6v3h0l4 -12l2 -7v-1v-4l2 -1v-3l2 -5v-5v-1v-6l2 -3v6v1v8l-2 1v6v1l-2 16l-2 4v3l2 -5l6 -18l4 -21h2l-2 15v1l-2 9l-2 4v3h2l4 -15l2 -7v-1v-4l2 -1v-9l2 -1v-7l1 -3l1 12l-2 1v8v1v5l-2 1v8l-4 10v2l4 -8v-2l2 -3l4 -9v-3l2 -5v-4v-1l2 -13v-1v-7l2 -1v-7v-1v-10l2 -1v-6v-1l2 -10v-1l2 -7l4 -12l8 -16l2 -4v-1l-6 7l-6 14l-2 2v2l-4 7l-6 15v3l-1 2l-1 -2l2 -1v-4v-1l2 -7l2 -9l2 -3l2 -8l4 -7v-2l-6 11l-2 2l-4 10l-2 7l-4 5l-2 9v3l-2 4l-2 7v1l-2 10l-1 1l1 -13v-1v-5l2 -1v-4l2 -5v-3l4 -12l2 -3v-2l6 -11l-1 -2l-5 9l-10 18v2l-4 9v3l-4 14l-2 10v1v7l-2 5v-10v-1v-10l2 -1v-5v-1l2 -9l4 -15l6 -9l-1 -3l-11 24l-4 9v5v1l-2 13l-3 8l-1 -5l2 -1v-6v-1l2 -10v-1v-8l4 -7l-1 -3l-3 5l-2 6v3v1v3l-4 8v8l-2 1l-2 19l-2 -1v-3l2 -1v-10v-1v-6l2 -1l-1 -3l-3 7v3v1v4l-2 6v5l)quill"
    R"quill(-2 1v7v1v10l-2 1v16l-2 1v4v1l-2 13l-2 4v3l-2 6l-6 10v1ZM757 248l-1 -4l2 -4v-3l4 -12l8 -15v2l-6 15l-2 6ZM984 223l-7 -3l-11 -3l9 -1l1 2h4l3 2l1 -1l-4 -3l-9 -4h-13l-1 -2h-8l-1 2h-8h-1l-11 2h-1h-3l-1 2h-3h-1h-3l-8 3l5 1l1 -2h12l1 -2h17l1 2h9h1h5l6 2l11 2l4 2ZM978 236h8l1 -2h4l3 -1l-4 -1l-1 2h-7l-1 -2h-6h-1l-11 -2v-2l2 2l23 -1l-12 -3h-1l-9 -2h-1h-6l-1 -2h-30l-1 2h-6h-1h-5l-1 2h-4l-4 1l4 1l1 -2h9h1h22h1l6 1l-23 1h-1h-8l-1 2l-17 2h40h1l12 2l9 2ZM852 226v-1v-1l-2 1v1ZM958 250l16 -2h1h3l1 -2h3l6 -2l8 -3l-3 -1l-5 2h-5l-1 2h-18l-1 -2h-9h-1h-6l-1 -2h-4l1 -2h18h1l-2 -2h-4h-1l-25 -2l-1 2h-15h-1h-7l-1 2h-10l-1 2l-8 1l5 1l1 -2h7h1h27h1h3v2h-27l-1 2l-17 2h-1l-7 2h4h1h6l1 -2h11h1h12h1h21l4 2l-14 1l2 1ZM840 256l4 -3l-1 -1l-5 4ZM944 265l17 -3l21 -8h-2h-1h-4l-6 2h-17h-1l-17 -2h-1h-5l-1 -2h-30l-1 2h-6h-1h-4l-1 2h-4h-1l-13 3l6 1l6 -2l14 -2h1h22h1h7l3 2h-10h-1h-11h-1h-12l-1 2h-6h-1h-4l-1 2h-4h-1h-4l-3 2h4h1h6l1 -2h8h1h11h1h10l1 2h5h1h6l1 2ZM840 279l7 -1l1 -2h4h1l17 -2h1h21l1 2h6h1h5l1 2h30l1 -2h5h1h4l1 -2h7l7 -4h3l6 -2l12 -6l7 -5l-1 -1l-9 6l-18 6h-6h-1h-20l-6 -2l-1 2h-9l-1 -2h-30l-1 2h-7h-1h-5l-1 2h-9l-1 2l-11 2l-4 2h-3l-9 3ZM834 261l4 -3h-1l-5 3ZM826 269l4 -4l-1 -1l-5 5ZM816 277l6 -6v-1h-1l-7 7ZM796 317l11 -3h5l1 -2h5h1h5l1 -2h12h1h21l1 2h6h1h1h22h1h12l1 -2h12l14 -5l-4 -1l-1 2h-6h-1h-18l-2 -1l17 -1l5 -2h10l4 -2h3l1 -2h-2h-3h-1h-1h-1h-1h-16h-1l-15 -2l-3 -1l30 -1h1l12 -2l17 -5l-4 -1l-3 2l-10 2h-1h-8l-1 2l-5 -1l2 -1h6l5 -2h5l1 -2h5l4 -2h9l17 -9l-3 -1l-3 2l-9 4h-7l-1 2h-4h-1h-12l-1 2l-17 -2h-1h-8l-1 -2h-13h-1h-24l-1 2l-17 2l-5 2l-11 2l-9 2l-3 2l-5 2l-5 2h3l3 -2h3l9 -2h4l1 -2h15l1 -2h17l1 2h7h1h6l1 2h4h-34l-1 2h-12l-1 2h-4h-1l-11 2l-9 4l-3 2l-8 2l-2 1l2 1l7 -4h8l5 -2h8l1 -2h26l1 2h6h-17l-1 2h-9h-1l-12 2h-1l-15 4l-3 2l-8 2l-7 3ZM806 287l6 -7h0h-1l-7 7ZM636 429l4 -15l2 -10v-1v-7l2 -1v-5l2 -4v-9l1 -5l1 15l-2 1v17v1v4l-2 6v4l6 -20l2 -8v-1v-5l2 -1v-8v-1v-15h0v4l2 6v14v1v9l-2 6v3l2 -5v-3l2 -5v-4l2 -5l3 -28l1 28l-2 1v12l-4 8l1 3l5 -13v-3l4 -8v-9l2 -1v-22l-2 -1v-8l-2 -9l-2 -11v-1l-1 -4l-2 20l-1 -4v-7l-2 -1v-14l2 -1l-1 -7l-1 3h0v-9l2 -1l-1 -5l-1 4l-2 10v1v4l-2 1v15l-2 1v10v1v11h-2v-6v-1v-3v-1v-24l2 -1v-7v-1v-5l2 -1v-4l2 -6l-1 -4l-5 14v3l-2 5v10l-2 1v8v1v8l-2 1l-1 19l-1 -6v-1v-13v-1l2 -18v-1l2 -10v-1v-3l-2 6l-4 15v1v4l-2 6v6l-2 1v9v1v22l2 4v32v1v9l-2 1v4l-2 5v2ZM794 298l6 -7v-1h-1l-7 8ZM936 304l11 -4l15 -8h-2l-6 2h-3l-3 2l-7 2l-5 4l-6 2ZM588 486l2 -6l2 -8l2 -8v-1v-4l2 -1l2 -31h2v18l-2 1v19l-2 1v5v1v4l2 -7l2 -13l4 -9l2 -30h2v14l-2 1v15v1v7l-2 1l1 4l1 -3v-5l2 -1v-4l2 -5v-4v-1l2 -18v-1v-11h2v32l-2 1v6v1v6l-2 1l1 4l3 -8v-3v-1v-3l2 -1v-3v-1v-7l2 -1v-6v-1v-6l2 -1v-9l1 -5l1 14l-2 1v14v1v8h0v-3l2 -1v-7l2 -5v-7l2 -1v-5v-1l2 -28v4l2 1v22l-2 1v18l-2 1l-2 13l-2 3l1 1l3 -5l4 -9l2 -13v-1v-5l2 -1v-17l2 -1v-22l-2 -1v-7v-1l-2 -26l2 -1v-13v-1v-6l2 -1v-7v-1v-4l2 -5v-4l-6 16v3l-2 9v4l-2 1v9l-2 1v13l-2 2v-5v-1v-8l2 -1v-6v-1v-7l2 -1v-9l4 -9l-1 -2l-5 10v3l-2 4v3l-2 4v3l-4 12v11l-2 1v5v1v11l-2 4v-23v-1v-7l2 -1v-10l4 -9v-3h-2l-4 15v3l-2 4l-2 7v1v4l-2 5l-2 10l-2 6v9l-1 2l-1 -3l2 -1l-2 -1v-3l2 -2l-2 -1l2 -1v-8)quill"
    R"quill(v-1v-9l2 -1v-4l2 -5v-4v-1v-4h0l-2 6l-2 4v3l-2 4v3l-2 5v5v1v4l-2 1l-2 31v-29v-1v-7l2 -1v-5l2 -6l-1 -3l-3 6v3l-2 9v5l-2 1v5v1v6l-2 1v32l2 1v25v1l-2 25v1l-2 8v1v3ZM782 309l6 -6l-1 -1h-1l-6 7ZM924 318h5l3 -2l8 -2l10 -6l6 -4h-1l-6 2l-15 6h-3l-4 2h-4l-6 2h-5h-1l-13 1l3 1ZM760 330l14 -15v-1h-1l-15 16ZM778 333l10 -3l7 -2l5 -2h4h1h5l1 -2h33l1 2l2 -2h8l1 2h9h1h27h1l15 -2l3 -1l-20 -3h-1l-8 -2h-1h-4l-1 -2h-6h-1l-24 -2l-1 2h-10h-1h-7l-1 2l-14 2l-4 2h-3l-4 2h-3l-6 2l-16 7ZM849 322l-17 -1l5 -1h12ZM914 331l7 -1l13 -6h-2l-11 4h-3l-5 2h-5l-2 1ZM752 359l15 -5l8 -2l5 -2h10l1 -2h7v2l-4 2h3h1h10l1 -2h19l1 -2h5l2 -1l-22 -2l1 -1h6l6 -4h27l1 2l2 -2h18h1h8l4 -2h5l9 -4h6l-4 -2h-15h-1l-19 -2l-6 -2h-7l-1 -2h-25l-1 2h-17l-1 2h-5l-8 2l-10 2l-3 2l-15 6l-8 4h2l3 -2l12 -4h3l4 -2h4h1l20 -2h1h18l-8 2l-20 2l-1 2h-4h-1l-8 2l-4 2h-3l-12 4l-3 2h-2l-14 7ZM886 338h-23l-1 -2h-10h34ZM548 531l2 -7v-1v-6l2 -1v-17l-2 -1l2 -2v-24l-2 -1v-13v-1v-4v-1l2 2l3 30l1 -1v-8l2 -1l-2 -3h2v8v1v3v15v1v12l-2 1v5v1v4l2 -3v-4l2 -4v-4v-1v-4l2 -6v-5l2 -1v-33l2 -1l-2 -1l2 -3v44v1l-2 12v1v6l2 -12l2 -1v-3v-1l2 -9v-1v-7l2 -1v-7v-1v-18l2 -1v-10l-2 -1l1 -19l1 1v5l2 1v10v1v9l2 1v7l-2 1v15v1v9l-2 1l1 8l1 -4v-1v-5l2 -1v-12l2 -1v-7v-1l2 -20h0v33v1v7l-2 1v9l-2 1l1 4l3 -8v-3v-1v-3l2 -1v-3v-1l2 -12v-1v-8l2 -1v-36l-2 -1v-9v-1v-34v-1v-9l2 -1v-6v-1v-5l-2 4v4v1v4l-2 1l-2 25v1v26h-2v-3v-1v-40l-2 3v13l-2 1v29h-2v-17v-1v-23l2 -1v-10l2 -3v-4l2 -1v-7v-1v-4l-2 4v4v1l-2 11l-2 4v4l-2 5l-2 9v1v6l-2 1v15v1v5v1l-2 -2v-9v-1v-24l2 -1v-12l4 -12v-4l2 -4v-5l-2 6l-2 5l-6 17v-2l2 -4v-4l4 -15v-3l4 -6l-1 -2l-5 10v2l-4 8l-8 24v4l-2 1v4v1v4l-2 6v6l-2 1v13v1v6v1l-1 2l-1 -6v-1v-20v-1v-7l2 -1v-5l2 -5v-9l4 -11v-2l-4 8l-4 9v3l-2 7l-2 8v1v5l-2 1v13l-2 1v27v1v8l4 8v4v1v4l2 6v5l2 1v14l-2 1l1 14l1 -3v-7l2 -2v1v3v15l-2 6v4ZM900 345l12 -3l8 -3l-3 -1l-4 2l-11 2h-1h-5l-1 2h-11l1 2ZM558 348v-1v-1l-2 1v1ZM740 349l2 -3h0l-4 2l1 2ZM728 362v-1v-1l-2 2ZM706 384l18 -18l-1 -2l-19 19v1ZM616 523l7 -3l18 -6l8 -2l5 -2h4h1h4l1 -2h14l1 -2h20l1 2h8h1h8l1 2h9h1h16h1l17 -2h1h4l1 -2h3l4 -2h6l3 -2h-18l-1 2h-12l-1 -2h-6l22 -2h1h4l9 -4h4l15 -4l17 -9l-3 -1l-3 2l-6 2h-3l-1 2h-3h-1h-3l-1 2h-13l-1 2h-21l-1 -2h-6h-1l-14 -2l-3 -1l31 1l1 -2h9h1h5l1 -2h5h1l8 -2l18 -6l16 -8l6 -4h-1l-15 6l-6 2h-3l-1 2h-3h-1h-7l-1 2l-16 -1l14 -1l9 -4h3l12 -4l3 -2h5l6 -4h-2l-10 4h-11l-1 2h-28l-1 -2h-5l28 -2h1h5l1 -2h4h1l7 -2l18 -6l17 -9l-1 -1l-7 4l-12 4h-3l-5 2h-6l-1 2h-8h-1h-25l-1 -2h-8h-1l-5 -1l13 -1l1 2h13l1 -2h10h1h5l1 -2h4l5 -2h4l4 -2h3l13 -5l-3 -1l-8 4h-4l-6 2h-14v-2h10l1 -2h7l13 -5l-4 -1l-1 2h-11l-1 2h-16l-1 -2h-11l-5 -1l18 -1v-2h15h1h5l1 -2l11 -2l12 -4l13 -6h-2h-1h-3l-4 2h-4l-5 2h-5l-1 2h-13h-1h-2h-4l-3 -2v-2h-3h-1l-11 -2h0h32h1l13 -2l11 -3l-5 -1l-1 2h-10h-1h-17v-1l1 -1h1h3h1h11l1 -2h6l-1 -2h-15h-1l-11 -2h-1h-5l-4 -2h6h1l19 1l-8 -3l-7 -2h-1h-4l-5 -2l-10 -2h-1h-8v-2h19l1 2h10l-10 -4h-3l-8 -4h-5l-2 -1l3 -1l1 2h7l15 4h2l-6 -4l-6 -4l-10 -2h-1h-4l-1 -2l-14 -1l7 -1h1h2h6l-3 -2h-4h-1h-23h-1h-5l-5 2h-4l-4 2h-3l-9 4l-3 2l-8 2l-8 4h-2l-3 1l3 1l17 -6l7 -2h1h4l1 -2h6h1l17 -2l1 2h10h-10l-1 2h-10h-1h-5l-5 2h-4l-1 2h-)quill"
    R"quill(7l-18 6l-3 2h-2l-5 4h-4v2l18 -6h3l9 -4h4h1h7l1 -2h25l1 2h6l5 1l-8 1l-1 -2h-13l-1 2h-8h-1l-11 2h-1l-11 2l-4 2h-3l-9 4l-10 4h-2l-6 4h0h2l3 -2l12 -4h7l4 -2h4l6 -2h8l1 -2h22h1h11l1 2h7h1l5 1l-28 -1l-1 2h-9h-1l-12 2h-1l-15 4l-18 6l-3 2h-2l-19 8l-22 14l-3 3l2 1l2 -2l13 -8h2l3 -2h2l3 -2h5l9 -4h3l12 -4l9 -2h1l11 -2h1h9l1 -2h27l1 2h8h1h7l1 2h7h1l5 1l-49 -1l-1 2h-8h-1h-6l-6 2h-5l-4 2h35h1h7l1 2h5h1l4 1l-8 1l-1 -2h-23h-1l-25 2h-1h-4l-6 2h-5l-4 2h5h1h6l1 -2l3 1l-2 1l-19 4l-15 6l-3 2h-5l-11 6l-1 1v1l20 -8h3l4 -2l8 -2l5 -2h4h1h5l1 -2h28h1h11l1 2h7h1l4 1l-13 1l-1 -2h-12l-1 2h-14h-1h-7l-1 2h-10l-9 4h-4l-4 2h-3l-6 2l-8 4h-2l-18 8l-12 9l1 1l12 -6h2l3 -2h2l3 -2l12 -4l8 -2l9 -2l12 -2h1h10l1 -2h29l1 2h9h1h8l1 2h7h-44l-1 2h-9h-1l-12 2h-1h-4l-1 2h-4h-1l-11 2l-3 2h11l1 -2h13h1h8l-13 2h-1h-6l-1 2h-5h-1h-5l-1 2h-4h-1h-3l-4 2l-6 2l-23 10l-17 10l-7 6h2l2 -2l12 -6h2l5 -2h2l5 -4h5l8 -4h3l1 -2h7l5 -2l9 -2l9 -2l18 -2h1h12l-4 2h-9h-1h-6l-6 2h-5l-1 2h-4l-4 2l-23 4v2h3l1 -2h17l1 -2l31 1v1h-23l-1 2h-7h-1h-4l-1 2h-5h-1l-7 2l-15 4l-12 6h-2l-19 11l2 1l7 -4l10 -4l17 -8l6 -2h4l1 -2h5l5 -2h8h1h22h1h13l1 2h5h-40l-1 2h-9l-9 2l-8 2l-4 2l-7 1l7 1l1 -2h15l1 -2l28 1l-27 1l-1 2h-7h-1h-5l-1 2h-4h-1l-7 2l-9 4h-2l-3 2h-2l-5 2h-2l-11 7ZM738 379l2 -1h-3l-3 1ZM558 410v-10v-1l2 -11l1 -2l1 5l-2 1v14ZM692 399l8 -9v-2l-10 10v1ZM538 549l2 -6v-1v-4l2 -1v-5v-1l2 -24l-2 -1v-6l-2 -4v-3l-2 -1v-5v-1v-5l-2 -1l1 -4l1 2v4l2 1v3l2 5v4l1 3l1 -2v-14v-1v-6l-2 -1v-10l-2 -5l-2 -13v-1v-31v-1v-7l2 -1v-10l2 -1l-1 -2l-3 6v10l-2 6v5l-2 1v7v1v14l-2 1l2 2l-1 13l-1 -5v-7l-2 -1v-16l2 -1v-15l2 -1l-2 -1l2 -1v-8l4 -9l-1 -3l-3 6v7l-2 4l-2 9v1l-2 12v1l-2 19l2 1v17l2 1v4v1l2 11l4 9v3v1l2 10v1v7l2 1v19l-2 1v8v1v4ZM682 410l6 -7v-1l-8 7v1ZM743 414v-2h6h1h8l1 -2l9 1l-1 1h-11l-1 2h-11ZM792 414l-22 -2l1 -2l3 2h15h3ZM672 421l6 -7v-2l-8 8v1ZM662 431l6 -6v-1l-6 6v1ZM854 433l2 -1h-3l-3 1ZM656 438l4 -3l-1 -1l-3 3v1ZM646 451l8 -10v-1l-10 10v1ZM567 450l-1 -5l2 -1v3ZM592 519l32 -41l20 -25v-1l-6 5l-22 29l-4 5l-4 4l-14 20l-2 3v1ZM474 636v-29l-2 -6v-6v-1v-3l-2 -1v-3l-2 -6l1 -1l1 2v2l2 3l2 6l2 7l2 9v1l1 26l1 -3v-7l2 -1v-27v-1l-2 -14v-1v-4l-2 -5v-3l-2 -1l1 -1l3 4l4 12v8l2 1v6v1v6h2v-16l-2 -1v-14l-2 -1v-4l-2 -5l1 -3l1 2l2 6v4l4 8v14l2 1l-1 19l1 -5v-9l2 -1v-17l-2 -1v-15l-2 -5l-2 -11l-12 -33l-2 -8l-1 -3l-1 4l2 1v6v1v5l2 5l2 11l2 1l-1 4l-1 -3l-2 -6l-6 -18v-13l-2 -2v19l2 1v8l2 5v3h-2l-2 -6v-3v-1v-3l-2 -4v-5l-2 -1v-11v-1l-1 -11l-1 31l2 1v12l4 8v7l2 1l-1 2l-1 -2l-8 -21l-2 -21v17v1v7l2 1v5v1v6l2 1v4v1v3l4 8v5l-2 -2v-2l-4 -8v-3l-2 -3v-3l-2 -9v-4l-2 -1v-7v17v1v6l2 1v4v1l2 11l10 27l4 9v5h-2v1l2 5v6l2 6v6ZM584 530l8 -9l-1 -1l-7 9v1ZM544 624l15 -8h2l5 -2l12 -4h3l4 -2l7 -2l8 -2h5l1 -2h4h1l19 -4l5 -2h4l5 -2h4l4 -2h4l4 -1h-16l5 -1h5l1 -2l15 -2l1 -2h4l7 -2h-4h-1h-9l-1 2h-11v-2h6h1h5l1 -2h5h1l5 -1l-11 -1l-1 2h-13h-1h-10l2 -2h8h1h6l1 -2h6h1l23 -4l10 -3l-32 -1h-1h-9l-1 -2h-9h0l3 -2h30l1 -2h14l1 -2h4h1h4l1 -2l13 -2l18 -7l-2 -1l-8 4h-5l-6 2l-26 2v-2l9 -1l-2 -1l-17 -2l-1 -1h22l-7 -1h-1l-20 -2h0l1 -2h4l1 2h15h1h19h1h5l1 -2h9l1 -2h3l11 -3l-20 1h-1h-27l-2 -1l18 -1l-2 -2h-5h-1l-8)quill"
    R"quill( -2h-1h-5l-1 -2l-26 2h-1h-8l4 -2l22 -1l-3 -1h-33h-1l-10 2l-24 8l-19 10l-14 10h1l4 -2l24 -12h3l4 -2h3l9 -2h4l1 -2h6h1h31h1l4 1l-23 1l-1 2h-11l-1 2h-4l-12 4h-3l-3 2l-6 1l5 1h0l-3 2h-6l-26 12l-2 2h1l6 -2l10 -4l3 -2h3l15 -4h5l6 -2h6l1 -2h6h1h11l-4 2h-12l-1 2l-11 2h-1h-3l-1 2h-3h-1h-3l-9 4l-3 2h-5l-27 14l-10 8h1l4 -2l17 -8l5 -2l3 -2l12 -4h3l9 -2l11 -2h1l19 -2l1 2h-6l-6 2h-5l-1 2l-11 2l-12 4l-3 2h-5l-5 4h-2h0h3l12 -4h10l1 -2l2 1l-5 3h-3l-10 4h-3l-18 6l-5 2l-5 2l-6 2l-1 2h0l9 -2l3 -2h3l14 -6h8l9 -4l4 1l-6 3h-3l-9 2l-11 6l-8 2l-17 8h3l6 -2l9 -4h3l4 -2h3h1l9 -2h1h5l1 -2h6v2h-4l-5 2l-11 2l-16 6h-2l-3 2h-2l-14 8l-10 6ZM386 863l2 -2l10 -19l2 -2l8 -17l2 -2l10 -20l2 -2l8 -17l2 -2l4 -9l2 -2l12 -24l4 -7l4 -7l2 -2l2 -7l2 -2l4 -7l4 -7l4 -7l4 -7l10 -19l10 -15l12 -21l4 -5l4 -8l14 -20l2 -3l24 -36l2 -3l14 -20l6 -7l2 -3l-1 -2l-3 4l-2 4l-14 17l-10 16l-2 3l-14 21l-2 3l-20 31l-4 8l-4 5l-2 5l-8 11l-2 5l-10 15l-8 16l-14 21l-14 27l-2 2l-2 7l-8 11l-4 9l-6 11l-4 9l-2 2l-30 59l-2 2l-4 8v1ZM470 655v-4v-1v-19v-1l-2 -14v-1v-3l2 -1l-2 -1v-7l-2 -4v-3l-4 -12l-4 -8l-6 -15v-8l-2 -1v-5v-1v-10l-2 -2v11v1l2 17v1v5l2 1v8l4 8l-1 2l-7 -20v-4l-2 -1v-9l-2 -1v-9l-2 2v3l2 1v13v1l2 11v1v3l2 1v3v1v3l2 6l14 30v3l4 12v17ZM446 693v-19v-1v-6l-2 -1v-5l-2 -5l1 -2l3 5v3l2 9l2 17l2 -16l-2 -1v-16l-4 -12v-1l2 4l6 15v17h2v-20l-2 -1v-7v-1l-2 -11l-4 -8v-2l4 5l4 15v3l2 1v6h0v-10v-1l-2 -11l-4 -12v-3v-1v-2l4 6l6 18l1 14l1 -15l-2 -1v-6v-1v-4l-2 -1v-3l-2 -4v-6l-4 -9l-2 -2v1v3v2l-2 -5l-2 -5l-2 -5l-2 -5l-4 -9l-4 -17v8v1v5l2 1l2 14v1l2 7l2 5v3v1v2l-2 -4v-2l-4 -9l-2 -7l-2 -9l-3 -19l-1 16l2 1v18l2 1v4l4 12v7l4 7l-1 2l-5 -11l-6 -15l-3 -16l-1 9l2 1v6l2 6v6v1l2 11l2 4v3l4 7l-1 2l-9 -20v-3l-2 -4l-2 -13v7v1v4l2 1l2 11l2 3v6l4 6l8 27l2 9v1l2 13ZM708 567l8 -1l6 -3l-3 -1l-5 2l-11 2l-3 1ZM434 716v-15v-1l-2 -9v-1v2h-2l-6 -14l-4 -6l-6 -5l-6 -14l-4 -10l-1 -1l-1 1l2 1v3l2 4v3l2 6l4 7l4 7l2 3l10 14l4 15l2 10ZM500 694l24 -12l8 -2l11 -4h3l15 -6l3 -2l15 -4l19 -10h-2l-10 4l-9 4h-3l-4 2h-10l-5 2h-4l-5 2h-3l-15 6l-3 2h-2l-14 6l-11 8h0h2l9 -6l13 -4v2l-12 4l-2 2l-10 6ZM526 671l9 -3l15 -4l4 -2h6l3 -2l7 -2l3 -2h3h-4h-1h-4l-5 2l-11 2l-16 6l-5 2l-6 3ZM436 688l-4 -13l-6 -9v1l6 10v2l2 5l2 4ZM576 675l6 -3l18 -3l-2 -1h-3h-6l-15 7ZM792 1030l13 -10l25 -26v-1l6 -6v-2l2 -3h2l14 -14l10 -7v-1l12 -14l12 -16h1l1 1l-4 6l-10 17l-22 22h2l4 -2l14 -10l21 -24h1v1l-14 21l-8 8v1l-4 4l1 1l11 -8l12 -12l6 -9l4 -8l2 -2v-2l8 -15v-2l2 -3l4 -12v-12h2v-3v-6l-2 -4v10l-2 2l-2 7l-2 5l-2 6l-4 6v2l-6 13l2 1v2h-2l-4 4h-2h0l12 -23v-5l2 -3l6 -15v-5l2 -5l-1 -16l1 1l2 3v5l1 1h1v-6l-6 -10l-2 -2v-10l-2 -3l-4 -7l-6 -4l-2 -4l-18 -12l-16 -6l-8 -2h-1h-5l8 2l5 4l6 2l4 2h2l17 12l4 5l-2 1l-4 -4l-13 -10l-15 -8l-18 -6h-3h-1h-4l8 4h2l20 10l12 8l10 11l-1 1l-4 -4l-4 -2l-13 -10l-9 -4l-9 -6h-2l-16 -8h-2l8 6l8 4l3 2l15 10l10 10l2 3l-1 1l-5 -6l-12 -8l-8 -4h-4l-9 -2l-9 -6h-2l-6 -2h-3l-12 -4h-4l-12 -6l-14 -2l-4 -2l-8 -2h-1h-6l-2 -2h6l6 2h5l4 2h4l8 2h3l13 6h7l8 4l2 -1l-19 -9l-37 -12l-31 -6h0h17l1 2h12l12 4h3l21 8l5 2l5 2l12 6l12 6l8 4h1l-17 -12l-30 -14l-7 -2l-8 -4l-9 -2l-9 -4h-4l-6 -4h-5h-1h-4l-6 -2h-6l)quill"
    R"quill(-1 -2h-9h0l1 -2h4l1 2h10h1h5l1 2h9l5 2h4l21 8l12 6l2 -1l-10 -7l-17 -8l-18 -6h-9l-10 -4h7l5 2h4h1h3l4 2h3l9 4l6 2h2l-8 -4h-2l-10 -6l-9 -2h-3l-4 -2l-11 -2l-1 -2h-4l-6 -2h-7h-1h-12l-2 -2h0h15h1h7l1 2h5h1h5l5 2h4l11 4h4l4 2h1l-9 -6h-1l2 2h-1l-9 -4h-3l-3 -2h-3l-27 -8l-23 -4h0h19l9 4h8l8 4l6 2l3 2l12 4l2 -1l-16 -9l-8 -2l-7 -4h-5l-9 -4l-7 -2l-8 -2h-5l-1 -2h-12l1 -2h11l1 2h13l30 10l5 4l1 -1l-20 -11l-15 -6l-11 -2h-1l-12 -2h-1l-14 -1l7 -1h1h12h1h5l1 2h4h1l7 2l9 4l2 -1l-8 -5h-2l-9 -4l-11 -2l-9 -2l-25 -2l4 -2h18l1 2h5h1h4l4 2h3l3 2h5l-9 -4l-9 -4l-13 -2h-1h-4l-1 -2h-19l7 -2h14l12 4h3l3 2l1 -1l-2 -3l-6 -2l-9 -4h-11l-1 -2h-14l-1 2h-6v-2h7l1 -2h8h1h13l-8 -4l-2 -2l-12 -4h-19l-4 2h-3l8 -4h13l1 -1l-2 -1l-19 -2l-6 2h-3l10 -4l16 1l-4 -3l-5 -2h-13l-1 2h-4l-6 2l-2 2h-2l-6 4l-1 -1l2 -3l8 -4h6l3 -2l11 -2l-4 -2h-10l-9 4l-5 4l-11 6l-8 6h-1l11 -10l7 -4h-3l-13 6l-13 12h-1v-2l10 -10h-2l-12 9v3l-8 8v-2l6 -7v-1l-3 2l-10 10l-2 -2l-6 6h-4l-4 4l-9 4h4l11 -4h1l-7 6l-17 8l-9 2l-3 2l-8 2l-5 4h-4l-1 2h0l27 -8l10 -6l1 1l-12 9l-3 2l-4 2l-3 2h2l3 -2h5l-2 2l-10 6v1v9l-2 -1v-5l-6 4v7l-1 1l-1 -6l-6 4v13v1v5l-2 1v3l-2 6h2l4 -8l4 -14l1 -1l1 6l-2 1v4v1l-2 6h1l1 -1l4 -6l4 -18l2 -1v8v1v5l-2 1v4l-2 6l1 1l1 -2l8 -16l2 -10l2 1l-2 9l-6 18h1l1 -1l8 -16l2 -6l4 -9v-4h2v3l-2 6l-2 10l-6 16l1 1l1 -2l2 -5l2 -2l2 -5l4 -7v-3l2 -6l2 -2v8l-6 17v3l8 -15l4 -15h1l1 4l-2 6v4l-4 5v5l-8 14v2l6 -8v-4l4 -4v-6l2 -2l2 -10l2 -3l2 -7l2 -4v-8l1 -2l1 5l-2 1v10v1v9l4 -10l2 -12h2l-2 1v9l-2 4v10l-6 15l-2 5v5l-2 3v5h0l2 -6l2 -2v-5l2 -1v-6l2 -4l2 -1v9l-2 1v4l-2 3v4v2v1l2 -2v-2l2 -4v-2l4 -5v-9l2 -4v10l-2 5v2h2v-4l4 -7v-3l2 -4v-3l2 -4l-2 -2l4 -7v-3h1l-1 9l-2 1v24l2 1v5v-15l2 -1l2 -19l2 -3v9v1v8l-2 1l1 11l1 -5l2 -13v-1l4 -15l4 -12l3 -6l1 2l-2 3l-2 6l-2 4v4v1v5l-2 1l1 8l3 -8v-3l4 -12l2 -5v-2l3 -2h1l-4 10v4l4 -8l1 -2l1 1l-4 6v4l-6 10l1 3l3 -5l2 -3v1l-6 12l-2 9v3l-2 1l1 2l5 -11l3 -3l1 1l-12 26v1l2 -1l5 -11l1 3l-14 27v3l-4 7l1 2l7 -15l2 -1v2l-8 16l-2 3l-2 7h0l16 -24v2l-8 17v2h2l10 -19v-2l2 -6l4 -4l-2 10l-8 17v1l2 -2l8 -15v-2l6 -15v-2l10 -20l2 -7l5 -7l1 2h0l2 -2l-2 -2l2 -3l8 -15v1l-8 17v4l-2 6l-2 8l-2 6v2l4 -8l2 -3l-2 -2l4 -6l4 -5v-2l4 -8l3 -2l1 1l-4 4l-4 7l-2 2l-2 6l-2 2l2 1l-2 6l-12 25v2l6 -8l4 -10l6 -6v1l-2 2l-6 12v2l-4 6l-10 15h2l8 -9l18 -25l-2 7l-2 2l-4 7l-2 3l-4 9h0l2 -2l2 -4l8 -9l11 -17l1 2l-2 2l-2 2l-6 12l-6 8l-2 5l-8 9h1l9 -9l8 -11l4 -2l4 -6l2 -3l4 -5v2v2l-4 4l-4 6l-4 5l-6 8l-8 9v1v1l5 -6h3l7 -6l10 -10l-1 3l-6 7h-1l-3 4l-8 4l-4 4h3l7 -4l5 -2l14 -10l1 1l-3 3l-17 11l9 1l4 -4l9 -2l16 -8h0l-3 4h-2l-15 8h-3l-3 1l11 1v2h-18h-1h-7l-1 2h-4h-1h-3l-9 4l-17 8l-4 2l1 2h-5v2h-5l-5 3l5 1h1l-2 2h-8h-1l-5 1l6 1v2h-7l-3 -2l-8 3l8 1h0h-7h-3h-1l-5 1l6 1h0l-18 1l4 1l2 1l-1 1l-10 -2h-1l-6 2h-10h4v2h-5l-6 -2h-7l3 2h0h-7l-1 -2h-5h-1h-1l2 2h-1h-1h-5l-1 -2h-19l-1 2h-4h-1l-7 2l-9 2l-3 2h-2v2l-3 -2l-9 4l-3 2h-4l-5 1l5 1h0h-10l-7 3l5 1h0l-9 2h-1h-7l-6 4l-8 2l-3 2l-6 2l-18 10l-11 9l-6 12l-2 6v1v15l2 9l4 7l4 3h2l-6 -7l-2 -5l4 7l6 6l-2 -1v1l1 1l4 2l3 2l8 4h-3v2h3l16 1l-1 1h-8v2h16l15 -6h3l5 -2l4 -2l4 -2l3 -4l3 -1v1l-8 6l-11 6h-3l-7 4l-9 1l8 1l15 -6l8 -4l7 -4)quill"
    R"quill(l10 -10l2 -3v-2l2 -2v-3h0l2 1v3l-2 2l-2 4l-12 14l-11 6l-15 4h5h1h4l6 -4h2l2 -2l7 -2l17 -18l4 -8v-4l2 -2l2 -5l1 -1l1 3l-4 4v5l-4 6l4 2l4 -4l-2 -3l2 1l2 -4v-4h2l2 -2l2 -10l2 1v5l-2 1v4l-2 3v5h-2l-4 8l3 1l1 -2l4 -7l4 -3v-4l-2 -2h1l2 2l2 -2l1 -2v-6l-2 -3l-4 -9l1 -4l1 3v3l2 3v3l5 6l3 -4v-9l-2 -1v-5v-1v-6l2 5v4l2 3v9l2 1v3l2 1v-3l-2 -5v-2l2 4l4 2v3l-2 1v3h2l1 -3l2 2l3 -3l-2 -5h2l2 -5l2 -3v6l-2 4v2l2 2l4 -7v-3l2 -9v-4l2 -1l-1 -14l1 1v5l2 1v20l-4 8l1 3l3 -7l2 -6v-3l2 -1v-5v-3h2v11l-2 1v4l-2 9l-4 5l1 1l7 -10l4 -13l1 -3l1 1l-2 12l-2 9l-4 4l1 2l5 -6l6 -13v-2l2 -3l1 -6l1 9l-2 1v3l-2 6l-4 7l1 2l9 -13l6 -10l1 -5l1 5l-2 1v3l-2 6v2l-6 8l1 1l3 -1l8 -10l8 -15v1v1v3l-2 6l-6 10l-2 2v3l4 -3l12 -16l4 -7h1l-1 6l-10 18h0l3 -2l9 -10l2 -4l4 -3l3 -7l1 2l-4 8v2l-6 8l-6 7l2 1l14 -14l4 -7l6 -11v2l-2 6l-6 12l-8 10l1 2l7 -7l15 -19l1 2l-6 12l-8 9l-2 3h0l5 -2l9 -10l10 -12h2l-2 3l-6 11l-8 9v1l4 -2l14 -13l6 -9h2l-4 9l-10 13l-6 5l1 1l8 -6l15 -14l10 -16l2 1l-10 16l-4 7l-14 13l2 1l10 -6l14 -14l8 -13l4 -6l1 -1l1 3l-10 17l-4 5l-18 16l3 1l10 -6l13 -13l14 -18l7 -15l1 4l-4 4v5l-8 14l-12 13l-11 10l-4 2l-3 2h1l9 -2l8 -6l16 -15l10 -17l4 -7l2 -1v3l-6 13l-10 14l-7 8l-18 12l-9 4h-3l-3 2h4l15 -4l3 -2h2l2 -2l5 -2l1 -2l8 -7l-2 -2l2 -2l6 -7l12 -16l6 -14l6 -18v4v1v3l-2 3v5l-2 3v3l-2 3l-2 5l-8 16l-14 16h1l9 -7l14 -18l10 -19v-6l4 -5v-11v11v1v5l-2 4v3l-8 18l-8 10l1 2l9 -11l10 -16v-2l2 -3v-3l4 -9v-8l1 -2l1 14l-6 18v2l2 -4l6 -10l2 -7l1 -1l1 2l-2 1v3l-2 9l-12 22v1l4 -5l6 -8l12 -21v3l-10 24l-8 11l-22 20l-4 2l-4 4l-9 4l-2 2l-11 4h5l9 -2l3 -2h2l17 -12l20 -20l8 -12l2 -3l6 -10l6 -14l2 -1v3l-2 6v2l-10 19l-10 14l-23 22l-7 4l-10 5l3 1l15 -8l9 -6l13 -11l4 -5l10 -12v-4l4 -3l8 -17l2 -2l4 -10l3 -4l1 1l-2 3l-2 6l-6 13l-18 30l-16 15l-14 10h2l7 -2l15 -10l18 -16v2l-22 19l2 1l9 -6l5 -2l2 -3l6 -6l14 -21l4 -7l4 -6v-4l2 -2l2 -6v-3l2 -3l2 -7h1l1 1l-2 1v4l-2 4v3l-2 6l-2 5l-4 10l-6 6l-6 12l-19 20l-7 6l-22 10l-9 4h-3h-1h-4l-6 2h-8l-3 2h18h1h6l1 -2h4h1l11 -2l23 -10l2 -2l7 -4l11 -8l1 2l-4 4l-15 10l-14 6h-2l-3 2l-15 6h-9l-6 2h-17h-1l-10 -1l5 3h5h1h28l27 -8h0l-1 2l-6 2l-6 2h-3l-2 2v2l11 -2h1h3l1 -2h3l6 -2l16 -6l16 -10l8 -8l3 -2v1l-3 5l-19 14l-17 8h-2l-8 4h-3l-5 2h-6l-1 2h-4l-1 -2h-17l-1 2l-2 -2l-13 1l5 1h7l1 2h34l1 -2h5h1l7 -2l9 -2l5 -4h2l18 -10l15 -12l12 -12l10 -13l2 -1v1l-6 7l-8 12l-14 14l-13 10l-16 10h-2l-3 2h-2l-3 2l-9 2l-8 2h10h1l8 -2l12 -4l20 -10l16 -12l21 -21l10 -12l6 -12l3 -5l-5 12l-2 2l-4 9l-6 10l-22 23l-13 10l-17 8h-2l-8 4l-6 2l-4 2h-14l-1 2l-1 -2l-18 1l16 1h1h19l1 -2h4l8 -2h3l16 -8l2 -2l11 -6l14 -10l17 -18l17 -22l1 1l-8 14l-12 15l-10 12l-16 12l-12 8h-2l-7 4h-2l-3 2l-9 2h-3l-4 2h-5l-1 2h-9h-1l-6 1l16 1l1 -2h5h1l9 -2l4 -2h3l6 -2l9 -4h2l17 -12l13 -10l14 -15l10 -13l4 -7l3 -3l1 1l-8 14l-6 9l-4 5l-4 6l-21 19l-12 6l-2 2l-13 6l-9 2l-7 2h-1h-5l-1 2h-4l-5 1l6 1l1 -2h12l1 -2h7l12 -4l4 -4l9 -4l19 -14l29 -30l12 -16l2 -6v-2l3 -4l1 2l-4 6v2l-8 16l-12 18l-12 12h0l8 -6l14 -13l8 -11l10 -13l8 -17v1l-4 11v2l-14 23l-14 18l-2 1l-2 3l-6 4l1 1l4 -2l17 -16l10 -12l6 -10l9 -12l1 2l-10 18l-12 17l-12 12l1 1l7 -6l18 -19l6 -11l4 -3l9 -19h1v2l-2 4v2l-4 7)quill"
    R"quill(v2l-4 4l-2 7l-14 18l-6 7l-1 3l7 -6l16 -19l13 -21l1 1l-10 19l-4 3l-2 6l-12 16v1l8 -8l16 -21v-3l10 -15l4 -6v-2l2 -3v3l-12 25l-4 6v2l-10 14l-4 4l1 2l13 -14l4 -5l2 -5l6 -7l4 -6l4 -5h1l1 1l-8 14l-10 16l1 1l3 -3v-1l10 -11l12 -15l6 -13l3 -1l-1 3l-6 9l-2 7l-16 22l-6 7v2l2 -2l10 -10l12 -13l4 -8l4 -4l6 -12l2 -1v1l-4 8l-4 9l-12 19l-10 12l-12 11l-13 10l-5 7l1 1l3 -4l15 -12l13 -12v-1l14 -15l17 -22l1 3l-4 6l-10 15l-6 9l-38 37l-16 19l-14 13ZM430 684v-2l-2 -3l-3 -7l-1 2l2 6l2 4ZM401 692l-1 -4v-2l2 2ZM496 705l17 -5l12 -8l11 -3l-4 -1l-1 2h-3h-1h-3l-1 2h-3l-6 2l-16 8l-4 3ZM650 694h-4l-6 -2l-20 1l7 -3h10h1h4l6 2l2 2ZM484 716l7 -4h6l9 -4l3 -2h5l12 -6h-3l-16 6h-3l-9 2l-13 8ZM406 711l-2 -3l1 -2l-5 2l1 2h2l2 2ZM264 987l4 -4l2 -7l2 -2l1 2h1v-4l1 -2l1 3l-2 6v4h0l6 -13l1 -10l1 11l-2 6v2h0l4 -12v-5l2 -1l-1 -9l3 5v9l-2 1v4v2v1l2 -2v-6l2 -1v-13l-2 -1l1 -13l1 4l2 19h2v-8l-2 -1v-7v-1v-12v-1v-6l2 -1v-6h2v2l6 -12l2 -3l2 -4l8 -7l-1 -2h-3l2 -2l4 2l2 -2l-2 -2l-3 -2l-1 -1l2 -1l3 2l4 2l1 -1l-8 -7h-2v-2h3l8 6l1 -1l-2 -5l-6 -4h2l8 4v-1l-6 -7h-1l-1 -1v-1l6 2l6 8h2l-2 -3l-7 -7l-11 -6h4l12 6v-3l-6 -5l-9 -4l-11 -1l10 -1l1 2h4v-2l-7 -2h5h1h4l7 4h1l-3 -4h-2l-2 -2l-7 -2h3l9 4l4 3v2l4 3l1 2l1 -1l-2 -2l-8 -10l2 -1l10 10l3 8l1 -3l-2 -1v-4l-2 -6l-8 -8h-3v-2l10 6l5 4l4 9h0v-5l-4 -9l-4 -4l-8 -7h2l4 2l10 9l2 3v4h2v-3l-4 -8v-2l-4 -4l1 -1l1 -1l-2 -4l-6 -5h3l7 7l6 15v-3l-2 -4v-4l-2 -6l-2 -3v-4l-4 -5l2 1l2 2l2 3l4 7v2l3 4l1 -2l-4 -9v-2l-2 -3v-4l4 6l4 8v-3l-2 -3v-5l-4 -6l-8 -8v-1h1l15 15l2 10h2v-4l-2 -6l-4 -3v-4l-6 -9l2 1l4 3l6 11v-2v-3l-6 -12l-4 -5v-2l-4 -4h0l12 13l5 9l1 -3l-6 -12l-8 -10l1 -1l7 6l6 7l2 7l2 8h0v-5v-1v-4l-2 -3v-2l-6 -11l-8 -8h2l10 10l4 8v-2v-3l-4 -8l-6 -7l-10 -8h2h2l8 8l5 6l1 -2l-6 -8l-5 -6l-7 -4l-4 -2l-2 -1l2 -1l6 4h2l5 4l1 -2l-8 -8h0l5 2l10 10l1 -1l-2 -2l-6 -7l1 -2l9 11l6 13v-2l-2 -9l-6 -8v-3l4 5l6 8l4 11v5l1 1l1 -5l-2 -1v-5l-6 -16l1 -1l1 2l6 10l3 10l1 -11l-2 -1v-4l-2 -6v-2l-4 -4h1l3 3l4 5v2l2 6l2 8v-18l-4 -10h2l4 8l2 10l2 2v-6v-1v-7l-4 -9l1 -1l3 3l2 4l1 11l1 -4v-7v-1v-5l-2 -5l2 1l2 6v3l2 2v-1v-2v-6l-2 -4v-3h2v2l2 6v3h2l-2 -10l-2 -6l-4 -9h0l4 4l4 8v-3v-1v-3l-4 -5v-2l-2 -2v-2l-4 -6h1l8 6l1 -2l-6 -6l-6 -4l-1 -2l-6 2l-23 12l-2 2h2h2h0l-1 2h-3l-1 -2l-2 2l-5 2l1 2l3 -2v2h-7l-5 3l3 1h0h-7l-6 4h6v2h-10l-2 1l4 1v2l-7 -2l-6 2l-5 4h11l1 1l-8 1l-6 2l-2 2h-3l-5 5l1 1l8 -4h3l-8 4l-10 7l1 1l5 -2h3l9 -4h2l-2 2l-8 2l-6 2l-8 9l1 1l6 -6l7 -4v1l-10 9l-4 7l-2 3h1l9 -10l7 -4l1 1l-3 3l-11 10v1l1 1l7 -6l8 -4v1l-14 11l-8 13l-2 2l1 1l7 -8l7 -6l1 1v1l-10 9l-8 9v2v2l9 -10l3 -2v1v1h-1l-13 15l-2 4v1l8 -8l3 -2h1l-16 18v2v2l9 -10h3l-16 15v2v3l5 -6l5 -4h0l-1 2h-1l-6 7l-4 4l-2 2l1 1l4 -4l4 -2l1 1l-8 8l-6 8l-2 4h2l6 -7l5 -4l1 1l-14 12l-2 6v5l4 -9l6 -5h2l-8 8l-2 5l-2 3l-2 4h2l4 -7l6 -5h0l-8 12l-2 7h0l2 -3l4 -7l7 -7h1v1l-8 11l-2 6h0l2 -4l7 -6l1 1l-8 9l-2 6h2l7 -8h1l-6 6l-2 6h0l4 -4l3 -2h1l-6 8v2l4 -4l2 1l-6 6l-2 17l-1 2l-1 -9l2 -1v-3l-2 -1l-4 5v3h4l-2 2l-4 8v7l-2 4v4l-2 5l-2 9l-2 9h2v-3l6 -7v2v3l-6 9l2 4l4 -5v-2l1 -1h1l-2 5l-2 5v1ZM480 725l23 -11l3 -2h-1l-3 2l-7 2l-12 6l-5 3ZM570 718v-1l2 -3v1ZM572 724v-2l5 -6l1 1ZM553 746)quill"
    R"quill(l-1 -5l2 -1v-3l1 -1l1 2ZM462 748l10 -6h7l3 -2h-5l-9 2l-8 6ZM519 746l-1 -1l2 -3v3ZM495 750h-1v-6l2 2ZM537 756l-1 -4l2 -4v-3l2 -1v7l-2 1v3ZM588 758h-2l6 -10l3 -4h1ZM513 750l-1 -1v-3l2 1ZM482 749l2 -1h-2l-2 1ZM592 769l4 -10l-1 -3l-5 11v2ZM1160 766l2 -1l-3 -3l-15 -4h-4l9 2l11 6ZM1148 767v-1l-8 -4l-6 -2h-3l-1 -2l-4 1l2 1h3l11 6ZM1138 770l-2 -2h-2l-4 -4h-2l-3 -2h-6l-1 -2h-4h-1h-1v2h3l9 2l12 6ZM573 794l-1 -1l2 -6l4 -7l2 -5l4 -10l4 -5v3l-6 11v2l-6 11v2ZM1124 771l-10 -5h-2l-6 -4h-4h-1h-5l22 8l2 2ZM731 766l-21 -3l6 -1l1 2h10h3l2 2ZM1110 774v-2h-3l-2 -2l-6 -2l-5 -4h-6l-1 -2l-3 1l1 1l11 4l13 6ZM460 792l6 -15v-3l2 -5v-5l-3 2l-1 -1l-2 14v1l-2 9v1v2ZM603 770l-1 -1l3 -5l1 3ZM1100 776l-8 -6l-6 -2l-9 -4l-5 1l11 3l5 4h2l8 4ZM1086 776l-13 -8h-6l-1 -2l-6 1l7 1l11 6h2l5 2ZM1076 779l-13 -7l-6 -2h-3l-1 -2h-3h-1h-3l2 2h3l9 2l3 2h2l9 6ZM575 778l-1 -2l5 -8l1 2l-2 2ZM454 793l6 -15l-1 -8l-3 3v3v1v5l-2 1v4l-2 6ZM1062 780l-15 -8l-13 -2l1 2h3l9 2l12 6ZM625 776l-1 -3l3 -1l1 1ZM1144 774l-3 -2h-1l2 2ZM1050 784l-2 -2l-9 -6h-2l-9 -4l-4 1l11 3l12 6l2 2ZM1156 795l-2 -2l2 -1h2l-1 -2l-8 -4l-13 -10l-5 -2l-3 1l2 3l6 2l19 16ZM536 792l4 -10l1 -6h-1l-4 8v2v4v2ZM634 780l-2 -1l3 -3l1 1ZM902 1095l4 -1l6 -6l12 -20l4 -6v-1l-2 1v-2l10 -22l-1 -2l-3 4l-2 2h0l8 -15v-5l2 -3v-3l2 -1l-2 -4l8 -20l-1 -1l-3 4h0l4 -15l2 -8v-1v-7l2 -1v-6v-1v-6l2 -1v-7v-1l2 -25v-1l1 -6l1 19l-2 1v15v1v10l-2 1v8v1v7l-2 1v6v1v5l-2 1l1 3l1 -2l2 -6l2 -7v-4l2 -4v-5l2 -6v-5l2 -1v-6v-1v-6l2 -1v-4v8v1v9l-2 1v8v1v6l-2 1v7v1v5l-2 4v2v2v6l-2 1l-2 19l-6 14l1 4l1 -2l2 -5l2 -3l4 -15v-3l4 -11l2 -8l2 -4v-9l2 -4v-4l2 -1v-4l1 -3l1 5l-2 1v5v1v5l-2 1v5v1l-4 19v1l-2 7v1v3l-4 9l-2 3l-2 7l-4 8l-2 6l-2 2l-10 17v2l-8 17h1l3 -3l12 -17v-2l4 -7v-2l6 -9l4 -12l2 -5l10 -30l4 -14l2 -4v-3l2 -6l1 -2l-1 6l-2 5v4l-2 4l-2 16l-6 18v7l-14 27l-6 9v5l-6 10l-2 2l1 1l11 -11l6 -9v-2l2 -5l2 -7l8 -15l2 -6v-4l2 -5l2 -6v-3l2 -5v-7l2 -1h0v2v9l-2 1l1 6l7 -19l2 -11l2 -1v-4v-1v-4l2 -1v-5l2 -6v-5v-1v-6l2 -1v-5v8v1l-2 15v1l-2 10l-2 5v9l-2 4v4l-2 4v3l-8 15v6l-4 6l-12 24l-2 6l-6 11l-4 5l2 1l12 -12l2 -5l2 -3v5l-2 3v4l-4 5v1l6 -5l2 -4l2 -3l2 -5l10 -24l4 -9v-3l2 -6v-7l-2 4l-2 5l-1 1l-3 3v7l-4 9v2l-9 13l-1 -3l6 -10l2 -7l2 -5l2 -5l4 -10l2 -1l6 -9v-2l2 -3v-3l2 -4l4 -9v-3h1l-1 6l-2 3v3l-2 5v3l-2 4l-2 13v1v5l-2 4v4h0l4 -7v4l-6 13l-2 2v2l-8 11l-2 9l-6 10l-4 5h0l8 -5l12 -16v-4l6 -11v-2l4 -8l2 -10l2 -3v-3l2 -7v-4l4 -7v-4l4 -12v-5l2 -9v-5l2 -1v-4v-1v-4l2 -5v5v1v6l-2 1v7v1v4l-2 1v4l-2 6v6v1v3l-4 9v5l-2 9v3l6 -11l1 -1h1l4 -6v-6l4 -13v-4l4 -12v-3l-2 5l-2 1h0l4 -10l2 -20l2 -4l2 5v4l-4 7v8l2 3l-2 7v8l-2 3v3l-2 1v5v1v4l-2 1v4v1l-2 7l-6 16v2l-2 2l-4 9v3l-4 8v2l-2 6l-4 5l-4 10l-6 6l1 1l5 -3l10 -15v-2l6 -10v-7l4 -9l2 -8l10 -21v-5l4 -10v-7l2 -1v-5l2 -4v-11l2 -1l-2 -8l4 -9l-1 -10l-3 6l-1 2l-1 -3l2 -1v-3l2 -2v-6l2 -1v-4v-1v-8v-1v-10v-1v-7l-2 -1l2 -1v-10l-2 -1v-11v-1v-7v5l2 1v6v1v8l2 1v12l2 4v6l-2 1v9l2 1v5l-2 1v16v1l-2 14v1l-2 15v1v5l-2 1l2 6l-2 4l-2 9v1v3l-2 1v3l-2 9v3l2 -4l2 -4v3l-6 13l-6 18l1 4l5 -7v-4l4 -4v-2l4 -7v-4l4 -6v-4l2 -5v-10l2 -6v-6l2 -1v-10l2 -1v-7v-1l2 -18v-1l2 -45l-2 -1v-23l-2 -1v-12l-2 -1)quill"
    R"quill(v-5v-1v-4l-2 -5l-2 -8l-4 -10v-6l-4 -8v-2l-8 -14l-4 -4h0l2 2l2 -1l-4 -6l-7 -7l-5 -4h-9l-7 3l6 1l2 2h-1h-3h-1h-8l-1 1l6 1v2h-7l-1 -2h-6l-2 2l3 2h3l2 1l-1 1h-4l-5 -2h-12l1 2h2h2v2h-4l-6 -4h-5h0l2 3v1l-14 -8h-3l-6 -4l-9 -2l-11 -2l-1 2h-7l-8 4l-9 9l-4 6l2 1l3 -4l6 -4h1h2v2l2 -1v-3l8 -2h3l4 2l7 6l6 11l6 18v8l2 1v8l2 1v5v1l2 14v1v8l2 1v13v1v42v1v15l-2 1v21l-2 1v7v1l-2 19l-2 1l-2 14l-2 5v4v1l-4 19l-2 4v3l-4 12l-10 20l-8 10h-3h-3v-2h2v-2l-3 -6l-1 1l2 3l2 3l-2 1l-2 -3v-3v-7l-2 -1v-3l-2 -7v15l2 5h-2v-2v-4l-2 -4v-3l-2 -4l-2 -7h0v14l2 1l-2 5l4 10l2 3l8 7ZM1090 778l-2 -2l-2 1l2 1ZM446 796l2 -3l2 -6v-4l2 -1l-2 -4l-2 13l-2 4v1ZM1152 803l-12 -13l-9 -6l-11 -6h-4l20 14l12 10l2 2ZM572 786l2 -6v2ZM1142 808v-2l-19 -18l-14 -8l-3 1l3 1l14 10l18 16ZM534 786v-2v-2l-2 2v2ZM572 813l14 -29l-1 -2l-7 12v5l-6 12v2ZM1132 811l-1 -1l-19 -18l-13 -8l-3 -2l-2 1l1 1l7 4l12 8l16 16ZM625 794l1 -5l5 -5l1 1ZM638 790h0l4 -6h1l-1 2ZM1056 798l-12 -10l-9 -4h-3l16 10l4 4h2ZM1054 786l-2 -2l-2 1l2 1ZM498 794l4 -5v-2v-1l-6 7l1 1ZM1086 787l-2 -1h-2l2 2ZM530 800l2 -4v-5v-1v-4l-2 4v8l-2 1v1ZM648 792v-2l3 -2h1ZM1026 836l-4 -15l-6 -12v-2l-8 -12l-2 -3l-4 -4h1h1l8 8l6 13v2l4 8l2 6l2 7ZM1124 815v-1l-11 -10l-5 -4l-4 -4l-4 -2l-6 -4l-3 -2l-1 1l4 3l6 6l4 2l10 8l5 4l4 4ZM300 793l2 -2v-1l-2 2v1ZM955 858l-1 -2l-2 -12v-1v-4l-2 -1v-4v-1l-2 -11l-4 -9l-2 -3v-2l-6 -12l-4 -5l1 -1l9 12l2 4l2 5v3l4 7v7l2 1v4v1v4l2 1v16l2 1ZM442 795v-2v-3l-2 3v2ZM657 794l-1 -2l2 -2v2ZM1013 816l-7 -13l-4 -6l-6 -6v-1h1l7 7l4 6l4 10ZM1118 821v-1l-16 -14l-12 -8l-9 -6l-3 -2l-2 1l15 11l5 2l17 14l4 4ZM522 802l4 -8l-1 -2l-3 6v4ZM634 796l-2 -1l4 -3h0ZM884 797l4 -5h0l-4 2v2v1ZM470 795v-1v-2l-2 2v1ZM642 798v-2l3 -2h1ZM998 806l-2 -4l-8 -7v-1l8 7l2 3ZM536 800l2 -4v-2l-2 5v1ZM960 836l-8 -23l-8 -15l-2 -3v-1l4 4l8 12v2l2 3l2 9l2 8v1ZM1068 820v-1l-13 -13l-11 -10l-3 -2l-1 1l8 7l19 18ZM462 800l2 -3l-1 -1l-3 3v1ZM653 798h-1l3 -2h1ZM664 800v-1l3 -3l1 1ZM560 800v-2l1 -2l1 2ZM750 798l-6 -1l6 -1ZM454 802l2 -3v-1l-2 3v1ZM985 802l-5 -4h1l3 2ZM635 804l-1 -1l5 -5l1 2ZM985 820l-5 -11l-10 -10l1 -1l9 8l4 9l2 3ZM965 820l-7 -13l-4 -5v-2l6 7l4 8ZM1066 806l-7 -6h-1l6 6ZM640 816l-2 -1l2 -1l13 -6l8 -6h1l-10 8ZM968 806l-2 -3v-1l2 3ZM436 812l4 -3l2 -7l-2 1v3l-4 5v1ZM444 810l4 -6l-1 -2l-5 7v1ZM450 808l4 -4l-1 -2l-5 6ZM659 816l-3 -1l10 -5l3 -4h3l-3 4h-3ZM1070 836l-8 -13l-17 -17l-1 1l10 9l14 18l2 2ZM1106 823l-4 -5l-12 -12l-2 1l11 9l6 8ZM773 810h-5h-1h-4l-5 -2h6h1h5l3 2ZM1080 821v-1l-12 -12h-2l14 14ZM918 843l-6 -9l-8 -8h-1l-3 -4h2l4 2l4 4l2 -1l-12 -9l-9 -6h-2l-6 -2h-3l-1 -2l-11 1l7 1l21 12l4 2l18 20ZM912 820l-7 -6l-10 -4h-3l-1 -2l-5 1l8 3l14 6l2 2ZM1086 816l-6 -6h-2l7 6ZM786 814l-8 -2l-2 -1l5 -1l5 2ZM428 816l2 -2v-2l-2 3v1ZM673 816l-3 -1l5 -3l1 1ZM683 816h-3l4 -2v1ZM864 848l-13 -10l-20 -10l-7 -2l-5 -4h-5l-3 -2h-2l-9 -5l3 -1l3 2h3l3 2h3l6 2l3 2h2l3 2h2l3 2l5 2l9 4l8 5l8 8ZM1164 855l-8 -17l-4 -4l-4 -5l-8 -9v-1l-2 -1l2 5l8 11l2 4l12 16l2 2ZM368 825v-3l-2 -2v2v3ZM805 826l-10 -4l-3 -2l12 4l2 1ZM1092 823l-1 -3l-1 1l2 3ZM1084 826l-3 -4l-1 1l4 3ZM1072 825l-2 -3v2l1 2ZM839 854l-11 -6l-2 )quill"
    R"quill(-2l-24 -12h-2l-5 -4h-2l-3 -2h-2l-3 -2l-7 -2h1l9 4h3l6 2l15 6h2l17 10l9 7ZM1072 851l-8 -13l-6 -8l-6 -6v1l14 17l4 7l2 3ZM851 850l-14 -10l-24 -12h-2l-1 -1l2 -1l3 2l3 2h2l16 6l10 8l6 5ZM1154 856v-3l-10 -14l-10 -11v1l10 14l4 7l6 6ZM548 834v-1v-1l-2 1l1 1ZM340 836l-2 -1l1 -1l1 1ZM828 858l-8 -4h-2l-2 -2l-9 -4l-8 -6h-2l-3 -2l-14 -4h3h1l7 2l3 2l12 4l16 8l3 2l3 3ZM904 842l-4 -3l1 -1l3 4ZM1080 880l-6 -15l-4 -5l-6 -11l-9 -11l-1 2l2 2l8 12l4 8l8 12l2 5l2 1ZM1124 845l-5 -7l-1 1l6 7ZM1108 849l-1 -3l-1 2l2 2ZM911 850l-3 -1l1 -1h1ZM1128 850l-1 -2l-1 1l2 1ZM366 856l4 -5l-1 -1l-3 5v1ZM922 858l-4 -8v2v3l2 3ZM1138 914l-8 -16v-2l-2 -3v-2l-6 -12l-2 -2l-4 -8v-2l-2 -2l2 -1l-2 -4l-5 -10l-1 2l6 9v7l6 12l2 2v2l10 19v2l4 8l1 1ZM812 860l-6 -2l-10 -6h2l5 2h2l2 2l3 2l2 2ZM1074 854l-1 -2l-1 1l2 1ZM1132 854l-2 -2v1l1 1ZM911 862h-1l-2 -2v-3l-2 -3l1 -2l1 1v2l2 3ZM1098 897l-2 -7l-10 -19l-2 -2l-6 -12l-3 -3l3 7l2 2l8 16l2 2v4l6 12ZM1104 857l-1 -3l-1 2l2 2ZM1134 857l-2 -3v2l1 2ZM364 861l2 -3l-1 -2l-3 4v1ZM1108 861l-3 -3l-1 1l3 3ZM1124 864l-2 -2v1l1 1ZM655 868l-1 -1v-5l2 3ZM1027 934l-1 -8l2 -1v-12l-2 -1l2 -1v-19l-2 -1v-6v-1v-13l-2 -1v-8l2 2v11l2 1v7v1v9l2 1v22v1v5v1l-2 8ZM1080 898v-2l-10 -22l-8 -12v2l4 8l2 2l4 9l2 2l6 13ZM681 872l-1 -7l2 -1v3v1ZM910 882v-4v-1l1 -13l1 3v5v1v8ZM1018 884l-2 -1v-16l-2 -1l2 -2l2 5v7v1ZM1160 943l-6 -20l-6 -13l-2 -5l-6 -11v-4l-2 -2l-4 -7l-10 -17v2l4 8l4 7l6 10v2l4 7v2l4 7v2l8 17l4 13v2ZM406 868h-2l3 -2l1 1ZM469 890l-1 -1l-6 -13l-2 -2v-3l-4 -4v-1l8 10l4 6l2 6ZM668 870h-2l1 -4l1 1ZM332 870l-2 -2v1v1ZM1118 917l-2 -6l-4 -7v-2l-8 -17v-2l-2 -2l-7 -13l-1 2l12 22v2l2 2v4l8 16l2 2ZM315 872h-2l-1 -1l1 -1l3 2ZM477 892h-1l-2 -2v-4l-4 -9l-4 -5l1 -2l5 6l4 8ZM456 880l-4 -6l1 -2l3 4v3ZM427 892l-1 -3v-2v-7l4 -8v2l-2 4v5v1ZM619 880l-1 -4l2 -4v6ZM436 888l-2 -1v-9l2 -4ZM481 880l-3 -5v-1l4 5ZM629 884h-1v-1l2 -3v-3l1 -3l1 2l-2 1v4ZM678 882v-2l2 -6v5ZM924 878l-1 -4l-1 4ZM1130 922l-4 -11v-2l-6 -9v-2l-12 -24v2l4 7l2 7l8 15v2l4 7v2l2 6ZM605 888l-1 -3l2 -1v-4l2 -4v2v1l-2 7ZM666 878v-2h1l1 1ZM450 888l-2 -3v-7l2 4ZM328 881l-1 -3l-1 2l2 2ZM592 890v-2l3 -8l-1 6ZM669 884l-1 -2l2 -2v2ZM1088 936l-6 -11l-6 -18l-13 -27l-1 3l2 3l12 24l2 7l2 5l2 5l4 8l1 1ZM617 888l-1 -1v-1l2 -4v3ZM680 944l-2 -1l8 -6v-1l10 -10l8 -12l12 -20v-2l4 -10v3v1v3l-8 14v3l-4 7l-12 18l-6 5ZM355 968h-12h-1h-4l-7 -4l-6 -4l-7 -8l-6 -11v-5h2v5l6 10l8 7h4l-5 -2l-1 -1l1 -1h4h1h5l1 -2l11 -2l9 -4h6l6 -4v-2l-4 2h-4l-5 2h-3l2 -2l14 -4l-2 -2l-6 2l-4 -1l8 -3l2 -1l-2 -1l1 -2l5 10l6 6l3 2h2l1 -13l-2 -1v-5l-2 -5v-4l-2 -5v-3l-4 -13l-3 -1l-3 3l-4 11h-2l2 -3l-1 -1h-2h-4l-1 2l-2 -1l10 -3v-2l-5 2h-7l1 -2l10 -2l1 -1v-1l-2 2h-5h-1h-7l-1 2l-4 -1l8 -3h4l2 -1l-3 -1l-1 2h-4l-6 2l-4 -1l18 -5h-4h-1h-4l-5 2h-4l-14 6l-8 7l-4 7v-1l4 -9l10 -8l15 -8h3l4 -2h4l2 -2h8l1 -2h8v2h-10l-1 2h5l4 2h2l5 2l4 6l6 13l2 13v1v14l-2 7v-19v-1v-6l-2 -6v-4l-4 -11l-4 -4l-5 -5l-1 2l6 8l6 15v3v1l2 10v1v17l-2 5l-6 6l-6 4h-2l-3 2l-11 2ZM577 898l-1 -3l5 -9l-1 8ZM1018 896v-10l2 4ZM381 888h-5h5ZM624 894v-2l2 -4v3ZM891 926l-1 -3l8 -16l4 -9v-7l2 -3v5v1l-2 10l-2 3l-2 5l-2 2l-2 7ZM387 900l)quill"
    R"quill(-6 -6l-6 -4h-7h8l5 2l5 4l2 3ZM438 900l-2 -4l1 -6l1 4v1ZM602 896h-1l-1 -1l4 -5v2ZM589 898l-1 -3l3 -3l1 2ZM870 904v-4l3 -8l1 3l-2 4v3ZM855 912h-1v-1l2 -5l2 -8l3 -4l1 2l-4 6v6ZM882 896v-1l1 -1l1 1ZM890 900v-4l1 -2l1 2ZM705 934l-1 -1l6 -5l2 -4l8 -11l2 -3l4 -7l2 -6l3 -3l-3 11l-4 4l-2 5l-4 6ZM307 898l-1 -1l1 -1ZM477 902v-6l1 1v3ZM565 900l-1 -2l2 -2v2ZM455 904v-8l1 5ZM844 910h-2l6 -14v4ZM1116 991l-2 -10l-2 -3v-6l-4 -6v-3l-2 -3v-3l-2 -3v-5l-4 -5l-2 -10l-8 -15l-4 -12l-2 -6l-3 -3l-1 3l8 13v2l6 12v5l2 3l2 5l4 9v3l2 3l4 16l2 3l4 14l2 2ZM464 906v-5l1 -1l1 4ZM483 902v-2l1 1ZM680 914l2 -4l2 -3l5 -7l1 2l-2 2ZM270 905v-2v-3l-2 3v2ZM390 904l-2 -2l1 -2l1 2ZM573 906h-1l3 -6l1 3ZM879 906l-1 -2l3 -4l-1 4ZM562 906v-1l2 -3v1ZM834 908v-3l2 -3v1ZM887 908h-1v-1l3 -5l1 2ZM1084 955v-4l-4 -9l-2 -3l-2 -11l-12 -26v3l2 3v3l2 3l4 15l2 3l6 20l2 2ZM867 912h-1v-1l3 -7l1 3ZM1023 926l-1 -5l2 -1v-5v-1l1 -10l1 11l-2 1v6ZM814 922h-2l10 -16v1l-2 2v2l-2 2l-2 6v2ZM875 914l-1 -1l4 -5v2ZM392 912l-2 -2h1h1ZM719 934l-1 -1l8 -8l12 -15v1l-6 10ZM832 914h-2l3 -4l1 3ZM884 975l18 -15l12 -17l8 -22v-6l2 -1l-1 -4l-3 7l-2 3v2l-4 7v3l-4 4l-4 9l-6 11l-6 8l-12 11ZM879 926l-1 -2l4 -7v-2l3 -3l1 3l-4 5v4ZM677 920l-1 -2l3 -2h1ZM849 924l-1 -1l2 -3l3 -4l1 1ZM452 922l-2 -1l2 -3ZM1138 991l-2 -14l-4 -10v-3l-6 -15l-2 -5l-4 -9l-9 -17l-1 2l8 17l2 8l4 5l2 8l4 8l6 20l2 3v2ZM390 952v-1l2 -3v-7l2 -1v-22l2 5v16l-2 1v7ZM440 922h-2l1 -4l1 1ZM830 924h-1l-1 -2l3 -4l1 2ZM1104 927l-1 -5l-1 3l2 2ZM1122 928v-2l-2 -4v2v3l2 1ZM580 955l4 -1h4l9 -4h2l5 -4h2l13 -10l9 -9v-3h0l-11 12l-7 4l-9 4l-4 2l-13 6l-14 3ZM634 929l2 -1l-1 -2l-1 2v1ZM1124 982v-3l-2 -7v-3l-6 -15l-11 -26l-1 2l2 1v6l2 3l6 16l2 3l2 8l2 5l4 10ZM444 935l2 -4l-4 -3l-2 4l4 4ZM458 936l4 -4l-1 -2l-3 3v3ZM1076 964l-2 -10l-4 -12l-2 -3l-2 -8l-1 -1l-1 3l2 3l2 3l8 25ZM1152 998v-4l-4 -11v-3l-6 -15v-3l-4 -9l-2 -11l-7 -12l1 5l2 3l12 36v3l2 3l4 12v6ZM268 940v-4l3 -6l1 4ZM582 959l11 -1l9 -4l14 -6l9 -8l5 -4l2 -5v-1l-13 12l-10 6l-17 8h-3l-5 2h-5l-3 1ZM410 934h-2l1 -2h1ZM440 941l2 -3l-4 -4l-2 6l2 2ZM450 938l2 -3l-4 -1v3l1 1ZM995 938h-1l1 -4ZM510 946l12 -6l4 -4h-2l-3 2l-13 8ZM536 942l3 -2l5 -4h-2l-6 4l-4 2ZM1164 999v-4l-2 -4v-3l-4 -9v-3l-2 -4v-3l-6 -18l-2 -3v-5l-4 -7v2v1v3l16 45v3l2 9ZM488 948l5 -2l7 -8v-1v-1l-14 12ZM394 956h-2l4 -4l6 -14v4l-4 10ZM409 942l-1 -1l1 -3l1 1ZM476 952l5 -4l11 -10h-3l-9 10l-6 4ZM498 947l8 -3l6 -6h-2l-4 4l-6 2l-4 3ZM520 945l4 -1l8 -6h-2l-2 2l-12 5ZM456 942v-1v-1l-2 1v1ZM446 943l2 -1l-1 -2h-2l-1 2l1 2ZM446 964l9 -4l4 -4l9 -11l2 -3h-2l-4 6l-11 10l-1 -1l6 -6l2 -2l-1 -1l-15 16ZM466 955l10 -9l2 -3l-1 -1l-5 5l-8 8ZM382 946l-2 -2l1 -2l1 2ZM542 944l2 -1l-3 -1l-5 2ZM548 947l3 -1l7 -3l-3 -1l-3 2l-8 3ZM432 951l2 -3l4 -2l-2 -2h-2l-2 1v3l-2 2l1 2ZM902 971l8 -10l6 -9l4 -6l-1 -2l-5 9l-14 18ZM554 950h6l5 -2h4l5 -3l-10 1l-3 2h-7l-2 2ZM440 950l2 -1l1 -3l-3 1v2v1ZM340 958v-2h4l8 -4h8l8 -4h-3h-1h-4l-12 4h-4l-3 2h-1l-4 2h-4v2l1 -2l3 2l1 -2l2 2ZM1070 967l-4 -14l-2 -5v3l2 6l2 3v5l2 2ZM327 952h-3v-1l3 -1ZM450 953l2 -1l-1 -2l-1 2v1ZM430 954l2 -1l-2 -1h-1l-1 2ZM382 978l9 -2l4 -2l7 2l1 -2h3l3)quill"
    R"quill( -2l5 2l1 -2h6l7 -4l4 -4l2 -5l-1 -1l-2 2l-6 6l-11 6v-1l1 -1l5 -4l8 -7l-1 -1h-2l-8 8h-3l-5 4l-10 4h-1l2 -2l10 -4l10 -11l-2 -1l-9 10l-10 6l-1 -1l6 -4l10 -12v-1l-2 -2l-2 2v3l-2 1l-7 8l-4 2l-3 4l-11 4h-2l-1 2ZM436 956l2 -2h0l-4 1l1 1ZM430 969l10 -5l10 -9v-1h-1l-3 2l-9 8l-9 5ZM1108 999l-2 -9v-3l-10 -27l-3 -6l-1 2l2 1v3l6 18l4 11l2 8l2 2ZM352 956l4 -1l-4 -1l-4 2ZM440 959l4 -4l-1 -1l-5 5ZM360 957l4 -1h-4l-4 1ZM338 962h5l5 -2h5l3 -2h-4h-1l-11 2h-1h-3l-2 2ZM1088 965l-2 -6l-2 -1v1l2 6ZM434 963l4 -3h-1l-5 3ZM346 964h5l1 -2l8 -1l-5 -1l-5 2h-5l-1 2ZM828 963l2 -1l-1 -2l-1 2v1ZM1072 996l-2 -9v-3l-9 -22l-1 3l2 3l2 6l6 20l2 2ZM294 1009l-1 -1l3 -1l-2 -3l4 -1l-2 -2l1 -1h3v-1l-2 -2l1 -1h1h2v-2l2 -1l-2 -2l2 -1l2 -3l4 -5v-4l2 -2v-1v-2l-5 -5l-3 4l-2 2v-1l2 -5l2 -2l-2 -2l-14 22h1h1l2 1l-5 3l-5 2h6v2l-2 1l2 1l-3 2h-5l-2 2h3v2h-5l5 2l9 6ZM999 976l-1 -3l3 -7l1 5l-2 1v3ZM828 971l4 -4v-1l-6 5ZM1033 978l-1 -2l5 -6h1ZM1096 996v-3l-2 -7v-3l-4 -12l-2 -1v3l2 6v3l2 3l4 11ZM822 976l4 -3l-1 -1l-5 4ZM702 1003l13 -3l7 -2h3l6 -2l16 -10l13 -10l-3 -2l-3 4l-3 2l-9 6l-12 6l-32 11ZM272 981v-5v5ZM818 979l2 -2v-1l-4 2ZM708 1023l9 -1h1h5l1 -2h4l4 -2h3l22 -10l21 -14l18 -16h-2l-22 16l-23 12h-1l18 -12l18 -15l-1 -1l-3 4l-18 12l-16 8l-12 4h-4l-5 2h-6h-1l-21 2h-1h-24l7 2h3l1 2l37 1l-4 1h-6l-2 2l-3 2h-17l-1 -2h-11l-1 -2h-4h-1l-7 -2l-9 -2l-3 -2h-3l7 4h5l5 4l6 2h3h1l16 2l1 2ZM714 1007l8 -3h3l9 -2l3 -2h2l3 -2h2l16 -10l8 -7l-1 -1l-6 4l-31 16l-24 7ZM270 986l2 -2l-1 -2l-3 3v1ZM979 988h-1l2 -6v2ZM1068 1006l-6 -18l-3 -6l1 5l2 3l4 14v2ZM1013 988l-1 -2l2 -2v1ZM320 989l2 -3l-1 -2l-3 4v1ZM430 999l-6 -7l-7 -4l-6 -2h-3h-1h-3v2h3l6 2h2l11 6l3 4ZM808 1022l9 -6l6 -4l31 -25l-3 -1l-1 2l-1 -2l-2 2l-18 12l-23 22ZM826 1018l12 -6l3 -4l11 -6l5 -6l8 -4l5 -5l-2 -1l-7 4l-14 10l-9 4l-14 14ZM844 1014l18 -10l4 -4l7 -4l9 -8h-2l-8 4l-3 4l-7 2l-9 6l-11 9l1 1ZM316 994l2 -3v-1l-2 3v1ZM418 1070l2 -3v-7l2 -1v-14l-2 -4v-3l-4 -5v-1l4 3l4 10v4l2 1v10l-2 1v3l-2 3l1 1l3 -5v-3l2 -1v-12l-2 -1v-5l-2 -5v-2l-4 -5l-4 -4l1 -1l9 8l2 5l2 4l1 1h1v-6l-2 -4l-2 -2l-2 -3l-8 -8l2 -1l6 4l6 7l1 1h1l-2 -5l-12 -11h2l10 6l6 8l2 8v-3l-2 -9l-7 -6l-3 -4l-4 -2l-6 -5l1 -1l11 6l6 8v-2v-2l-5 -6l-7 -6l-12 -6h4h3l2 2l5 2l7 6l1 -1l-12 -11l-9 -4l-6 -2h-19h-1l-6 2l-13 6l-6 5l1 1l5 -4l8 -4l3 -2l7 1l-10 3l-12 8l-2 -2l-2 4v2h0l3 -2l10 -6h3h4l-11 6l-5 5l1 1l6 -6h7v2l-6 4h1l2 -2h4l2 2h0l-7 1l6 1l9 6l15 14l4 8l2 18l2 -2v-4v-1v-3v-1v-3l2 3v3v1v11l2 -6v-13l-2 -4l1 -1l1 2l4 7v12l-4 6v1ZM772 1024l8 -6l26 -27l-1 -1l-3 2l-10 12l-5 2l-9 10l-5 2l-3 6ZM878 1010l15 -12l5 -7l-1 -1l-16 14l-4 2l-3 4ZM1034 994v-2l1 -2l1 2ZM506 1066l2 -7v-1v-3v-8l-2 -1v-3l-2 -4l-2 -3h0l6 6l2 4v3l2 1l-1 14l1 -2v-7l2 -3l-2 -1v-6v-3l-4 -6l-6 -6v-2l7 4l7 10v5v1v3v9l2 -3v-12l-2 -1v-3l-2 -4l-2 -5l-7 -6l-5 -3l4 -1l8 6h2l-2 -2l-4 -4l-12 -7l4 -1l3 2l9 4l4 4v-1l-6 -7l-6 -4h-4l-4 -4l8 2l5 4l1 -1l-5 -5l-5 -2h-5h-1h-4l-4 -2h6l5 2h4h1v-1l-3 -3l-8 -2l-13 -1l5 -1h1h2h5l6 2l8 6l2 2l1 -1l-10 -9l-11 -4l-8 -2h-1h-15h-1h-3v2h19l8 2h-6h-1h-5h-1h-13l-3 2h0h12h1l6 1l-14 1l-8 4l-2 1l1 1l8 -4h12l3 2l-16 2l-6 5l1 1l3 -2h3l1 -2h13l3 2h-1)quill"
    R"quill(3l-3 2l-4 2l-2 1l1 1l5 -2h6l-6 2l-3 2v2l6 -2h5l1 -2l5 1l-2 1l-7 2l-7 4h0h2l4 -2l6 1l-3 1h-3l-6 4h8h1h5h0l-4 1l10 3l11 6l5 6l4 9v5h2l1 4l1 -6l-2 -1v-5l-2 -5l1 -1l3 5l2 6v1v5v1v4ZM946 1021l8 -23v-4v-1l-1 -1l-1 4l-4 8v7l-4 7v3ZM312 1002l4 -6l-1 -2l-5 6v2ZM362 998l4 -3l-1 -1l-2 2l-3 2ZM858 1014l3 -2l6 -2l3 -4l4 -2l8 -8v-1v-1l-3 2l-9 8h-2l-12 10ZM890 1009l4 -3l12 -9l-2 -1l-3 4l-15 9ZM1064 1021l-7 -23l-1 6l4 8l2 7l2 2ZM812 1002l2 -2l-1 -2l-3 3v1ZM1010 1006h-2l2 -1v-3l2 -4v2ZM356 1004l4 -3l-1 -1l-3 3v1ZM716 1043l10 -1l1 -2h3l4 -2h6l23 -12l9 -7l2 -4l14 -15h-2l-3 2l-16 14l-15 10l-5 2l-5 4l-4 2l-5 2l-6 2h-3l-1 2h-4h-1h-6l-1 2h-17l-1 -2l-15 1l2 1h10l1 2ZM762 1042l9 -4l8 -6l15 -17l10 -14v-1l-6 6l-2 2l-16 15l2 1l-2 3l-5 5l-15 10ZM574 1004l-1 -2l-1 1l1 1ZM802 1013l8 -8l-1 -1l-9 8v1ZM610 1021l-19 -17l-1 1v1l14 12l4 4ZM602 1007l-2 -3v1l1 3ZM620 1007l-1 -1h-1l1 2ZM630 1027l-11 -7l-15 -12h-2l7 8l4 2l3 2l12 8ZM674 1037l-7 -3h-3l-3 -2h-6l-3 -2l-10 -4l-20 -10l-10 -8h-2l7 6l21 12h2l8 4l18 6h3l1 2ZM662 1028l-10 -4h-2l-7 -4h-2l-11 -6l-8 -6h-2l11 8l11 6h2l3 2h2l9 4ZM743 1010l-3 -1l2 -1h2h0ZM208 1127l10 -18l2 -2l2 -5l2 -2l2 -5l4 -7l10 -18l14 -17l19 -27l1 3l-2 2l-8 12l-8 17l-4 13l-12 15l-12 20h0l2 -2l2 -4l6 -6v2l-6 8v2l16 -14l14 -9v-1v-2v-2l-2 5l-2 1h0l2 -6l2 -5v-2l6 -12v-3l-4 7v4l-2 1v-1l8 -17v-2l4 -7v-2l12 -24l2 -2v-3l-2 -2l-5 -2l1 3l-4 1l-2 1v2l-4 5v3l-8 10l-4 7l-8 7l-6 9l-8 8l-10 12l-2 5l-3 7l-1 -2l2 -1v-3l2 -9v-1l-2 2v3l-4 10v3l-4 15l-2 3l-4 12l-4 5v2ZM636 1010l-1 -2l-1 1l1 1ZM346 1019v-9v9ZM372 1011l2 -1h-2l-2 1ZM352 1022v-5l2 -4l-1 -3l-3 5v7ZM590 1012v-2l-2 1l2 1ZM696 1032l15 -2l13 -4h-5h-1h-9l-1 2h-20l-1 -2h-7h-1l-8 -2l-6 -2h-3l-3 -2l-12 -4l-9 -6l-2 1l3 3l5 2l2 2l15 6l15 6h3h1l12 2h1ZM735 1014l-3 -1l4 -1h2ZM596 1016l-4 -4l-2 1l4 3ZM716 1036l12 -2l13 -6h2l8 -6l11 -8h-2l-32 16l-8 2l-3 2l-15 2ZM1062 1041l-2 -11l-2 -3l-5 -13l-1 4l2 3l4 8l2 10l2 2ZM842 1027l4 -1l11 -4h2l6 -4l4 -2l1 -1l-3 -1l-12 6l-19 7ZM826 1026h4l8 -4h3l11 -6h-3l-8 4l-6 2h-3l-10 4ZM976 1080l8 -8l8 -12v-2l8 -14v-6l2 -3l2 -8l2 -3v-3l2 -4v-1l-4 4l-2 5l-2 2v2l-4 6l-2 9l-6 12l-4 4v5l-2 2l-2 6l-4 6v1ZM856 1028h6l3 -2l7 -2l12 -7l-3 -1l-5 4l-26 8ZM356 1024v-6v6ZM866 1030l14 -2l18 -9l-3 -1l-6 4l-18 6h-7l-2 2ZM820 1022l2 -2h-1l-3 2ZM370 1038l-2 -6l2 -4v-4l2 -1l-1 -3l-1 1l-4 4v9l2 4ZM406 1071l-2 -1l2 -30l-10 -10l-10 -6l-4 -2h-2l4 3l4 4l-1 1l-10 -8h-1l8 10h0l-3 -2l-4 -4l-1 2l2 2l8 10h0l-4 -2l-6 -6v1l4 7l8 7l-1 1l-2 -2l-9 -8v1l9 11h1h2l6 6h-1l-7 -6v1v1l6 5v2v1l-15 -16l-1 2l19 18l9 8ZM1014 1034l4 -9v-3l-2 7l-2 3v2ZM882 1031l11 -3l9 -4h-2l-7 2h-3l-7 4l-7 1ZM720 1049l15 -3l4 -2h3l8 -4h2l10 -4l8 -6l8 -5l-2 -1l-1 2h-3l-5 4l-11 4l-4 4h-2l-5 2l-12 4l-9 2h-1h-5l-1 2l-19 1ZM998 1089l8 -3l6 -7l4 -6v-2l4 -8v-3l2 -7v-3l2 -3v-2l2 -3l2 -9v-3l2 -1l-1 -3l-1 2v2l-8 14l-2 2l-6 11l-2 10l-8 13v2l-8 7ZM918 1086l8 -9l8 -14l2 -5l2 -3v-2l2 -3v-2l2 -3l4 -12v-5h-2v4l-2 2l-8 17l-6 9h1l1 2l-10 21l-2 2v1ZM1056 1048l-2 -6l-2 -6l-2 -7l-1 -1l-1 4l2 1v3l2 3l2 7v2ZM674 1032l-5 -2h-3l-1 -2l-3 1l9 3ZM518 1033l-3 -3l-1 1l3 3ZM894 1032l4 -1l-4 -1l-4 2Z)quill"
    R"quill(M438 1051v-10v-1v-4l-3 -4l1 8v1v10ZM498 1072l-4 -12v-16l-2 -4l-11 -4l-7 -2h-1h-5l7 4h0l-8 -2h-1l4 2v2h-3l-3 -2h-2l12 6h-3l-9 -2h0l1 2l6 2l5 2l4 2l-15 -4l-1 1l2 1l9 4h2l3 2l6 2l6 1l-2 1l-15 -4h-3l-7 -4l-1 1l2 1l6 4l8 2l3 2h8l1 2h-2h-1h-4l-11 -4h-3h0l17 6h3l2 2l1 1l-8 -3h-3l-10 -4h-1v2l3 2l9 2l10 2l2 2h-3h-1l-15 -4h-1h-2h0l15 6h4h1ZM522 1043l-2 -1v-7l-2 -1v2v1l2 6ZM454 1051v-13l-2 3v5v1v1v3ZM992 1084l6 -6l6 -7l4 -8v-3l2 -3l4 -12l-1 -5l-9 18v2l-4 6l-4 11l-6 7ZM376 1044l-1 -2l-1 1l2 1ZM432 1060l2 -6v-8l-2 -4v15l-2 1v2ZM1048 1049v-5l-2 -2v3l2 4ZM1048 1066l-4 -11v-2l-2 -1v5l2 3l2 5l1 1ZM1052 1057l-2 -5v3v2ZM270 1058v-2v-2l-2 2v2ZM399 1062l-1 -2l1 -6l1 2v1ZM878 1071l-2 -5l-2 -10v7v1l2 6l1 2ZM1013 1066l1 -3l3 -3h1ZM400 1066l-6 -5l1 -1l5 3v2ZM270 1082l2 -7l2 -9v-4l-2 7l-2 7v1v4l-2 1ZM890 1069v-5v5ZM1042 1072l-2 -8l-2 3l2 5ZM1080 1068l-2 -4v3l1 1ZM1104 1075v-3l-4 -4v1v2l2 3l2 2ZM1050 1074v-4h-2l2 4ZM1108 1102v-1l-2 -1l-16 -15l-9 -15l-1 2l4 6l8 12l8 8l4 4ZM1120 1073l-2 -3v2l1 2ZM894 1081l-2 -7v3l2 4ZM1122 1076l-2 -2v1l1 1ZM1038 1079v-2l-2 -1v1v2ZM1112 1086l-2 -4l-5 -6l-1 1l2 3l4 4l2 2ZM1070 1082l-1 -2l-1 1l2 1ZM1072 1085l-1 -3l-1 2l2 2ZM1152 1087l-2 -3v1l2 3ZM253 1088l1 -2l1 -2l1 1ZM1156 1102l-12 -8l-10 -10v1l4 6l8 7l6 4ZM1068 1092l-4 -6v1l2 3l2 2ZM1074 1088l-1 -2l-1 1l2 1ZM1142 1102l-9 -6l-9 -10v1l2 6l5 5l6 4h3ZM1124 1102l-8 -4l-11 -10h-1v1l9 9l9 4ZM1116 1090l-2 -2v1l1 1ZM1168 1103l-3 -3l-12 -12h-1l4 7l7 7l3 2ZM1090 1102l-4 -2l-11 -12l-1 1l2 4l7 7l5 2ZM1062 1095l-1 -1h-1l1 2ZM1078 1101l-8 -7h0l4 6l3 2ZM200 1145v-1v-2l-2 2l1 2Z"/></svg>)quill";
static const char* kLassoToolIcon =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 15c-2-1-3-3-2-6 1-4 5-6 10-5 5 1 8 4 6 8-2 3-7 5-11 4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-dasharray="3 2" stroke-linecap="round"/><path d="M9 14c-3-1-5 0-5 2 0 2 4 3 5 1 1-1 0-3-1-2-1 2 0 6 3 7" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>)";

static bool FitToolbarGroups(const Vec<int>& widths, int available, int gap, int overflowDx, int brandIdx,
                             Vec<bool>& visible) {
    VecReset(visible);
    int total = 0;
    int count = 0;
    for (int width : widths) {
        bool show = width > 0;
        VecAppend(visible, show);
        if (show) total += width + (count++ > 0 ? gap : 0);
    }
    if (total <= available) return false;

    auto remove = [&](int idx) {
        if (idx < 0 || !visible[idx]) return;
        visible[idx] = false;
        total -= widths[idx] + (count-- > 1 ? gap : 0);
    };
    remove(brandIdx);
    if (total <= available) return false;
    for (int i = len(widths) - 1; i >= 0 && total + overflowDx + (count > 0 ? gap : 0) > available; i--) remove(i);
    return true;
}

static bool FitScrolledToolbarGroups(const Vec<int>& widths, int available, int gap, int controlsDx, int brandIdx,
                                     int& first, Vec<bool>& visible, bool& before, bool& after) {
    before = after = false;
    if (!FitToolbarGroups(widths, available, gap, controlsDx, brandIdx, visible)) {
        first = 0;
        return false;
    }
    VecReset(visible);
    for (int i = 0; i < len(widths); i++) VecAppend(visible, false);
    first = std::clamp(first, 0, std::max(0, len(widths) - 1));
    int remaining = std::max(0, available - controlsDx - gap);
    bool showed = false;
    for (int i = 0; i < len(widths); i++) {
        if (i == brandIdx || widths[i] <= 0) continue;
        if (i < first) {
            before = true;
            continue;
        }
        int needed = widths[i] + (showed ? gap : 0);
        if (needed > remaining) {
            after = true;
            continue;
        }
        // Keep a contiguous run: arrows move through groups in displayed order.
        if (after) continue;
        visible[i] = true;
        remaining -= needed;
        showed = true;
    }
    return true;
}

static int LocationEditWidth(int preferred, int minimum, int cap) {
    return std::min(std::max(preferred, minimum), std::max(minimum, cap));
}

static int LocationEditCap(int available, int fixedDx, int reservedDx, int fieldCount) {
    int budget = std::max(0, available - fixedDx - reservedDx);
    return std::min(available / 4, budget / std::max(1, fieldCount));
}

static TempStr CompactZoomText(float zoom) {
    char text[64];
    for (int decimals = 0; decimals <= 9; decimals++) {
        snprintf(text, dimof(text), "%.*f", decimals, (double)zoom);
        if ((float)strtod(text, nullptr) == zoom) return fmt("%s%%", Str(text));
    }
    return fmt("%s%%", Str(text));
}

#if IS_DEBUG
static void ToolbarInteractionTests();
static void ToolbarPaletteTests();
static void ToolbarReorderTests();

void ToolbarReorder_UnitTests() {
    ToolbarReorderTests();
}

void ToolbarLayout_UnitTests() {
    Vec<int> widths;
    for (int width : {180, 120, 170, 100}) VecAppend(widths, width);
    Vec<bool> visible;
    utassert(!FitToolbarGroups(widths, 588, 6, 36, 0, visible));
    utassert(visible[0] && visible[1] && visible[2] && visible[3]);
    utassert(!FitToolbarGroups(widths, 402, 6, 36, 0, visible));
    utassert(!visible[0] && visible[1] && visible[2] && visible[3]);
    utassert(FitToolbarGroups(widths, 360, 6, 36, 0, visible));
    utassert(!visible[0] && visible[1] && visible[2] && !visible[3]);
    utassert(FitToolbarGroups(widths, 40, 6, 36, 0, visible));
    utassert(!visible[0] && !visible[1] && !visible[2] && !visible[3]);
    VecReset(widths);
    VecAppend(widths, 0);
    VecAppend(widths, 100);
    utassert(!FitToolbarGroups(widths, 100, 6, 36, -1, visible));
    utassert(!visible[0] && visible[1]);

    VecReset(widths);
    for (int width : {180, 120, 170, 100}) VecAppend(widths, width);
    int first = 0;
    bool before = false, after = false;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, 0, first, visible, before, after));
    utassert(!visible[0] && visible[1] && !visible[2] && !before && after);
    first = 0;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, -1, first, visible, before, after));
    utassert(visible[0] && !visible[1] && !before && after);
    first = 2;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, 0, first, visible, before, after));
    utassert(!visible[1] && visible[2] && !visible[3] && before && after);
    first = 3;
    utassert(FitScrolledToolbarGroups(widths, 300, 6, 108, 0, first, visible, before, after));
    utassert(visible[3] && before && !after);
    utassert(!FitScrolledToolbarGroups(widths, 600, 6, 108, 0, first, visible, before, after));
    utassert(first == 0 && !before && !after && visible[0] && visible[3]);

    // Empty and one-digit labels stay compact; longer text grows until capped.
    utassert(LocationEditWidth(0, 24, 100) == 24);
    utassert(LocationEditWidth(18, 24, 100) == 24);
    utassert(LocationEditWidth(46, 24, 100) == 46);
    utassert(LocationEditWidth(140, 24, 100) == 100);
    utassert(LocationEditWidth(140, 24, 12) == 24);
    utassert(LocationEditWidth(92, 48, 60) == 60);

    // Reserve the navigation group's labels/buttons and the overflow command.
    utassert(LocationEditCap(600, 80, 36, 1) == 150);
    utassert(LocationEditCap(200, 140, 36, 1) == 24);
    utassert(LocationEditCap(200, 140, 36, 2) == 12);
    utassert(LocationEditCap(100, 140, 36, 1) == 0);
    utassert(LocationEditCap(1200, 160, 72, 1) == 300);
    utassert(str::Eq(CompactZoomText(150.0f), StrL("150%")));
    utassert(str::Eq(CompactZoomText(150.125f), StrL("150.125%")));
    utassert(str::Eq(CompactZoomText(100.1f), StrL("100.1%")));
    ToolbarInteractionTests();
    ToolbarPaletteTests();
}
#endif

enum class ToolbarFieldKind {
    Page,
    Chapter,
    Zoom
};

static bool AnnotationToolbarRail(ToolbarVirt* tb) {
    return tb && tb->win && tb->win->annotationMode && tb->vertical;
}

static bool SingleToolbarColumn(ToolbarVirt* tb) {
    return AnnotationToolbarRail(tb) && tb->annotationColumns == 1;
}

static bool MultiToolbarColumns(ToolbarVirt* tb) {
    return AnnotationToolbarRail(tb) && tb->annotationColumns > 1;
}

static int ToolbarColumnWidth(ToolbarVirt* tb) {
    for (VirtCtrl* control : tb->items) {
        if (control && control->id == CmdOpenFile) {
            if (auto* button = AsVirtIconButton(control)) return button->MinIntrinsicWidth(0);
        }
    }
    return tb->rowDy;
}

static int ToolbarOuterPad();
static Color TbDisabledColor();

struct ToolbarLocationEdit : Edit {
    MainWindow* win = nullptr;
    ToolbarFieldKind fieldKind = ToolbarFieldKind::Page;
    int preferredDx = 0;
    int minimumDx = 0;
    int widthCap = Inf;
    Tooltip* detailsTip = nullptr;
    bool mouseInside = false;
    bool railPaint = false;
    bool originalFrame = false;
    LONG_PTR originalAlignment = 0;
    DWORD originalMargins = 0;
    Rect railBounds;

    ~ToolbarLocationEdit() override { delete detailsTip; }

    bool CompactVertical() const { return win->annotationMode && win->toolbarVirt && win->toolbarVirt->vertical; }

    bool InRail() const { return AnnotationToolbarRail(win->toolbarVirt); }

    void SetRailPaint(bool active) {
        if (railPaint == active || !hwnd) return;
        if (active) {
            originalFrame = createdWithFrame;
            originalAlignment = GetWindowLongPtrW(hwnd, GWL_STYLE) & (ES_CENTER | ES_RIGHT);
            originalMargins = (DWORD)SendMessageW(hwnd, EM_GETMARGINS, 0, 0);
        }
        railPaint = active;
        createdWithFrame = active ? false : originalFrame;
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE) & ~(ES_CENTER | ES_RIGHT);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style | (active ? ES_CENTER : originalAlignment));
        EditSetMargins(this, active ? UiScalePx(2) : LOWORD(originalMargins),
                       active ? UiScalePx(2) : HIWORD(originalMargins));
        RoundControlSetPaint(hwnd, active ? RoundedControlPaint::Custom : RoundedControlPaint::Native);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    int PreferredWidth() const { return LocationEditWidth(preferredDx, minimumDx, widthCap); }

    bool MeasureWidth() {
        int previous = PreferredWidth();
        int dpi = win->frameDpi > 0 ? win->frameDpi : DpiGet();
        int breathingRoom = UiScalePxForDpi(dpi, 8);
        minimumDx =
            GetPreferredWidth(fieldKind == ToolbarFieldKind::Zoom ? StrL("100%") : StrL("00"), 0, Inf) + breathingRoom;
        preferredDx = GetPreferredWidth(GetTextTemp(), 0, Inf) + breathingRoom;
        return previous != PreferredWidth();
    }

    void OnTextChanged() {
        int oldPreferred = preferredDx;
        bool changed = MeasureWidth();
        if (InRail()) return;
        if (CompactVertical() && oldPreferred != preferredDx) {
            ScheduleUiUpdate(win, kUiForceRelayout | kUiRelayout);
            return;
        }
        if (!changed) return;
        auto* tb = win->toolbarVirt;
        if (!tb || !tb->host->layout) return;
        tb->host->Relayout();
        tb->host->Invalidate(true);
    }

    Size GetIdealSize() override {
        Size size = Edit::GetIdealSize();
        if (InRail()) {
            size.dx = widthCap < Inf ? widthCap : ToolbarColumnWidth(win->toolbarVirt);
            return size;
        }
        if (fieldKind != ToolbarFieldKind::Zoom || CompactVertical()) size.dx = PreferredWidth();
        return size;
    }

    TempStr DetailsText() const {
        auto* ctrl = win->ctrl;
        if (!ctrl || fieldKind == ToolbarFieldKind::Zoom) return {};
        if (ShowChapterUi(ctrl)) {
            Location location = ctrl->CurrentLocation();
            return fmt("%s %d / %d\n%s %d / %d", Tr("Chapter:"), location.chapter, ctrl->ChapterCount(), Tr("Page:"),
                       location.page, ctrl->ChapterPageCount(location.chapter));
        }
        int physical = ctrl->CurrentPageNo();
        int physicalTotal = ctrl->PageCount();
        int logicalTotal = physicalTotal;
        if (auto* dm = ctrl->AsFixed()) logicalTotal = dm->LogicalPageCount();
        TempStr label = ctrl->GetPageLabeTemp(physical);
        if (ctrl->HasPageLabels() && logicalTotal > 0 && logicalTotal != physicalTotal)
            return fmt("%s %s / %d\n%s %d / %d", Tr("Page:"), label, logicalTotal, Tr("Physical page:"), physical,
                       physicalTotal);
        return fmt("%s %s / %d", Tr("Page:"), label, physicalTotal);
    }

    void HideDetails() {
        if (detailsTip) detailsTip->Delete();
        if (InRail() && win->toolbarVirt->host) win->toolbarVirt->host->Invalidate(true);
    }

    void ShowDetails() {
        if (InRail() && win->toolbarVirt->host) win->toolbarVirt->host->Invalidate(true);
        TempStr text = DetailsText();
        if (!CompactVertical() || !len(text) || !IsVisible()) return;
        if (!detailsTip) {
            detailsTip = new Tooltip();
            Tooltip::CreateArgs args;
            args.parent = hwnd;
            args.font = GetFont();
            args.isRtl = IsUIRtl();
            detailsTip->Create(args);
        }
        Rect rect = HwndWindowRect(hwnd);
        int gap = UiScalePx(4);
        bool right = ToolbarPositionForWindow(win) == kToolbarRight;
        Point position{right ? rect.x - gap : rect.Right() + gap, rect.y};
        detailsTip->SetSingleAt(text, HwndClientRect(hwnd), position, true, right ? position.x : 0);
    }

    LRESULT OnMessage(HWND native, UINT msg, WPARAM wp, LPARAM lp) override {
        if (msg == WM_NCPAINT && railPaint) {
            HDC dc = GetWindowDC(native);
            if (dc) {
                RECT strip{0, 0, HwndWindowRect(native).dx, ncCenterTop};
                if (HBRUSH brush = BackgroundBrush()) FillRect(dc, &strip, brush);
                ReleaseDC(native, dc);
            }
            return 0;
        }
        if (msg == WM_MOUSEMOVE && !mouseInside) {
            mouseInside = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, native, 0};
            TrackMouseEvent(&track);
            ShowDetails();
        } else if (msg == WM_MOUSELEAVE) {
            mouseInside = false;
            if (!IsFocused()) HideDetails();
        } else if (msg == WM_SHOWWINDOW && !wp) {
            mouseInside = false;
            HideDetails();
        }
        return Edit::OnMessage(native, msg, wp, lp);
    }

    void SetBounds(Rect bounds) override {
        railBounds = bounds;
        if (InRail() && fieldKind != ToolbarFieldKind::Page) {
            int inset = UiScalePx(2);
            bounds = {bounds.x + inset, bounds.y + inset, std::max(1, bounds.dx - 2 * inset),
                      std::max(1, bounds.dy - 2 * inset)};
        }
        bool resizing = bounds.dx != lastBounds.dx && IsFocused();
        int start = 0, end = 0;
        if (resizing) EditGetSelection(this, start, end);
        Edit::SetBounds(bounds);
        if (!resizing) return;
        int newStart = 0, newEnd = 0;
        EditGetSelection(this, newStart, newEnd);
        if (start != newStart || end != newEnd) EditSelectText(this, start, end);
        SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    }
};

static Kind kindToolbarPageBox = "toolbarPageBox";

struct ToolbarPageBox : ILayout {
    ToolbarVirt* tb;
    ToolbarLocationEdit* edit;
    VirtText* total;

    ToolbarPageBox(ToolbarVirt* toolbar, ToolbarLocationEdit* field) : tb(toolbar), edit(field) {
        kind = kindToolbarPageBox;
        total = new VirtText(Str{}, tb->platformFont);
        total->id = PageInfoId;
        total->align = VirtTextAlign::Center;
        total->ellipsis = true;
        total->SetColor(kColText, TbTextColor());
        total->SetVisibility(Visibility::Collapse);
    }

    ~ToolbarPageBox() override {
        delete edit;
        delete total;
    }

    bool Active() const { return AnnotationToolbarRail(tb); }
    bool Horizontal() const {
        return Active() && (tb->annotationColumns == 2 ||
                            (tb->annotationColumns == 3 && gSettings->annotationThreeColumnPageStyle != 1));
    }
    bool FullWidth() const {
        return Active() && (tb->annotationColumns == 2 ||
                            (tb->annotationColumns == 3 && gSettings->annotationThreeColumnPageStyle == 2));
    }
    int Style() const { return limitValue(gSettings->annotationPageNumberStyle, 1, 3); }
    int Gap() const { return Style() == 3 ? UiScalePx(3) : 1; }
    int RowHeight() const { return std::max(PlatformFontLineHeight(tb->platformFont) + UiScalePx(4), edit->idealDy); }

    void UpdateTotal() {
        DocController* ctrl = tb->win->ctrl;
        int count = ctrl ? ctrl->PageCount() : 0;
        if (ShowChapterUi(ctrl))
            count = ctrl->ChapterPageCount(ctrl->CurrentLocation().chapter);
        else if (ctrl && ctrl->HasPageLabels() && ctrl->AsFixed())
            count = ctrl->AsFixed()->LogicalPageCount();
        total->SetText(count > 0 ? fmt("%d", count) : Str{});
        total->SetColor(kColText, edit->IsEnabled() ? TbTextColor() : TbDisabledColor());
        total->SetTooltip(edit->DetailsText());
    }

    void SetVisibility(Visibility value) override {
        ILayout::SetVisibility(value);
        edit->SetVisibility(value);
        total->SetVisibility(Active() ? value : Visibility::Collapse);
    }

    Visibility GetVisibility() override { return edit->GetVisibility(); }
    int LayoutChildCount() override { return 2; }
    ILayout* LayoutChildAt(int index) override { return index == 0 ? (ILayout*)edit : total; }
    int MinIntrinsicWidth(int height) override {
        return Active() ? ToolbarColumnWidth(tb) * (FullWidth() ? tb->annotationColumns : 1)
                        : edit->MinIntrinsicWidth(height);
    }
    int MinIntrinsicHeight(int width) override {
        return Active() ? (Horizontal() ? RowHeight() : 2 * RowHeight() + Gap()) : edit->MinIntrinsicHeight(width);
    }

    Size Layout(Constraints bc) override {
        total->SetVisibility(Active() && edit->GetVisibility() == Visibility::Visible ? Visibility::Visible
                                                                                      : Visibility::Collapse);
        if (!Active()) return edit->Layout(bc);
        int width = bc.HasBoundedWidth() ? bc.max.dx : ToolbarColumnWidth(tb);
        int fieldWidth = Horizontal() ? width / 2 : width;
        edit->widthCap = fieldWidth;
        edit->Layout(Loose({fieldWidth, RowHeight()}));
        total->Layout(Loose({fieldWidth, RowHeight()}));
        return bc.Constrain({width, Horizontal() ? RowHeight() : 2 * RowHeight() + Gap()});
    }

    Rect TopBounds() const {
        return {lastBounds.x, lastBounds.y, Horizontal() ? lastBounds.dx / 2 : lastBounds.dx, RowHeight()};
    }
    Rect TotalBounds() const {
        if (Horizontal()) {
            int half = lastBounds.dx / 2;
            return {lastBounds.x + half, lastBounds.y, lastBounds.dx - half, RowHeight()};
        }
        return {lastBounds.x, lastBounds.y + RowHeight() + Gap(), lastBounds.dx, RowHeight()};
    }

    void SetBounds(Rect bounds) override {
        lastBounds = bounds;
        if (!Active()) {
            edit->SetBounds(bounds);
            return;
        }
        int inset = UiScalePx(2);
        Rect top = TopBounds();
        edit->SetBounds(
            {top.x + inset, top.y + inset, std::max(1, top.dx - 2 * inset), std::max(1, top.dy - 2 * inset)});
        total->SetBounds(TotalBounds());
    }

    void Paint(Gfx* gfx) const {
        if (!Active() || edit->GetVisibility() != Visibility::Visible || lastBounds.IsEmpty()) return;
        Color fill = ThemeWindowControlBackgroundColor();
        Color edge = edit->IsFocused() ? ThemeBrandColor() : ThemeEdgeColor();
        int radius = UiScalePx(6);
        if (Horizontal()) {
            gfx->FillRoundedRect(lastBounds, radius, fill, edge);
            gfx->FillRect({lastBounds.x + lastBounds.dx / 2, lastBounds.y + 1, 1, lastBounds.dy - 2}, ThemeEdgeColor());
            return;
        }
        if (Style() == 3) {
            gfx->FillRoundedRect(TopBounds(), radius, fill, edge);
            gfx->FillRoundedRect(TotalBounds(), radius, fill, ThemeEdgeColor());
            return;
        }
        gfx->FillRoundedRect(lastBounds, radius, fill, edge);
        int inset = Style() == 1 ? lastBounds.dx / 3 : 1;
        gfx->FillRect({lastBounds.x + inset, lastBounds.y + RowHeight(), lastBounds.dx - 2 * inset, 1},
                      ThemeEdgeColor());
    }
};

static Kind kindToolbarGroup = "toolbarGroup";

struct ToolbarGroup : Wrap {
    ToolbarVirt* tb;
    Vec<Rect> cells;

    explicit ToolbarGroup(ToolbarVirt* toolbar) : tb(toolbar) { kind = kindToolbarGroup; }

    bool NavigationColumn() const {
        if (!tb->win || !tb->win->annotationMode) return false;
        for (auto& child : children)
            if (child.layout == tb->win->pageEdit || child.layout == tb->win->chapterEdit ||
                child.layout == tb->zoomEdit || child.layout == tb->pageBox)
                return true;
        return false;
    }

    Size LayoutVertical(int width) {
        VecReset(cells);
        for (int i = 0; i < len(children); i++) VecAppend(cells, Rect{});
        bool navigation = NavigationColumn();
        int columns = navigation ? 1 : tb->annotationColumns;
        int cellDx = std::max(1, (width - (tb->annotationColumns - 1) * colGap) / tb->annotationColumns);
        int spacing = navigation ? UiScalePx(1) : std::max(rowGap, UiScalePx(2));
        int y = 0, column = 0, rowHeight = 0;
        Vec<int> rowItems;
        auto finishRow = [&]() {
            if (rowHeight == 0) return;
            for (int i : rowItems) cells[i].dy = rowHeight;
            y += rowHeight + spacing;
            VecReset(rowItems);
            column = rowHeight = 0;
        };
        Vec<int> order;
        int zoomIdx = -1;
        for (int i = 0; i < len(children); i++)
            if (children[i].layout == tb->zoomEdit) zoomIdx = i;
        for (int i = 0; i < len(children); i++) {
            if (i == zoomIdx) continue;
            auto* control = children[i].layout->AsVirtCtrl();
            if (zoomIdx >= 0 && control && control->id == CmdZoomIn) VecAppend(order, zoomIdx);
            VecAppend(order, i);
        }
        if (zoomIdx >= 0 && VecFind(order, zoomIdx) < 0) VecAppend(order, zoomIdx);
        for (int i : order) {
            auto& child = children[i];
            if (IsCollapsed(child.layout)) {
                child.size = {};
                continue;
            }
            bool span = !navigation && !AsVirtIconButton(child.layout->AsVirtCtrl());
            if (span && column) finishRow();
            int dx = span ? width : cellDx;
            child.size = child.layout->Layout(Loose({dx, Inf}));
            if (navigation || SingleToolbarColumn(tb)) child.size.dx = dx;
            int x = navigation ? (width - dx) / 2 : span ? 0 : column * (cellDx + colGap);
            if (rtl && !span) x = width - x - dx;
            cells[i] = {x, y, dx, child.size.dy};
            VecAppend(rowItems, i);
            rowHeight = std::max(rowHeight, child.size.dy);
            if (span || ++column == columns) finishRow();
        }
        finishRow();
        return {width, std::max(0, y - spacing)};
    }

    Size Layout(Constraints bc) override {
        if (tb->vertical) return bc.Constrain(LayoutVertical(bc.HasBoundedWidth() ? bc.max.dx : UiScalePx(80)));
        Constraints row = bc;
        row.max.dx = Inf;
        return bc.Constrain(Wrap::Layout(row));
    }

    void SetBounds(Rect bounds) override {
        if (!tb->vertical) {
            Wrap::SetBounds(bounds);
            return;
        }
        lastBounds = bounds;
        for (int i = 0; i < len(children) && i < len(cells); i++) {
            auto& child = children[i];
            if (IsCollapsed(child.layout) || cells[i].IsEmpty()) continue;
            Rect cell = cells[i];
            cell.x += bounds.x;
            cell.y += bounds.y;
            child.layout->SetBounds(SingleToolbarColumn(tb)
                                        ? cell
                                        : AlignInCell(cell, child.size, CrossAxisAlign::CrossCenter, alignCross));
        }
    }

    int MinIntrinsicHeight(int width) override {
        if (tb->vertical) return LayoutVertical(width).dy;
        return Wrap::MinIntrinsicHeight(Inf);
    }

    int MinIntrinsicWidth(int height) override {
        if (tb->vertical) return Wrap::MinIntrinsicWidth(height);
        int width = 0;
        int count = 0;
        for (auto& child : children) {
            if (IsCollapsed(child.layout)) continue;
            width += child.layout->MinIntrinsicWidth(height) + (count++ ? colGap : 0);
        }
        return width;
    }
};

static ToolbarGroup* AsToolbarGroup(ILayout* layout) {
    return layout && layout->GetKind() == kindToolbarGroup ? (ToolbarGroup*)layout : nullptr;
}

struct ToolbarLocationField {
    ToolbarLocationEdit* edit = nullptr;
    ToolbarGroup* group = nullptr;
};

struct ToolbarHiddenLayout {
    ILayout* layout = nullptr;
    Visibility visibility = Visibility::Visible;
};

enum class ToolbarLineKind {
    Main,
    Annotations
};

enum class ToolbarRailRole {
    Tool,
    Page,
    Chapter,
    Zoom,
    Pin
};

struct ToolbarRailCell {
    ILayout* layout = nullptr;
    Rect bounds;
    ToolbarRailRole role = ToolbarRailRole::Tool;
    int cmd = 0;
    int pinIndex = -1;
    int group = 0;
    int rowTop = 0;
};

static int ToolbarOuterPad();

// Hide complete groups, including native fields, while retaining document availability.
struct ToolbarLine : HBox {
    ToolbarVirt* tb = nullptr;
    Vec<ToolbarHiddenLayout> hidden;
    Vec<ToolbarLocationField> locationFields;
    int brandIdx = -1;
    int findGroupIdx = -1;
    ToolbarLineKind lineKind;
    VirtIconButton* previousButton = nullptr;
    VirtIconButton* nextButton = nullptr;
    int firstGroup = 0;
    int wheelRemainder = 0;
    int verticalScrollY = 0;
    int verticalMaxScroll = 0;
    int verticalViewportDy = 0;
    int railContentDy = 0;
    Vec<ToolbarRailCell> railCells;
    Vec<Rect> railGroups;

    int FixedCount() const { return lineKind == ToolbarLineKind::Main && tb->annotationModeButton ? 4 : 3; }
    bool canScrollBefore = false;
    bool canScrollAfter = false;

    explicit ToolbarLine(ToolbarVirt* toolbar, ToolbarLineKind kind = ToolbarLineKind::Main)
        : tb(toolbar), lineKind(kind) {
        alignCross = CrossAxisAlign::CrossCenter;
    }

    VirtIconButton* OverflowButton() const {
        return lineKind == ToolbarLineKind::Main ? tb->overflowButton : tb->annotationOverflowButton;
    }

    Vec<VirtCtrl*>& OverflowItems() const {
        return lineKind == ToolbarLineKind::Main ? tb->overflowItems : tb->annotationOverflowItems;
    }

    bool Scroll(int direction) {
        if ((direction < 0 && !canScrollBefore) || (direction > 0 && !canScrollAfter)) return false;
        if (tb->vertical) {
            int target = verticalScrollY + direction * (tb->rowDy + UiScalePx(2));
            if (AnnotationToolbarRail(tb) && len(railCells)) {
                target = direction > 0 ? verticalMaxScroll : 0;
                for (auto& cell : railCells) {
                    if (direction > 0 && cell.rowTop > verticalScrollY) target = std::min(target, cell.rowTop);
                    if (direction < 0 && cell.rowTop < verticalScrollY) target = std::max(target, cell.rowTop);
                }
            }
            verticalScrollY = limitValue(target, 0, verticalMaxScroll);
            tb->host->Relayout();
            tb->host->Invalidate(true);
            return true;
        }
        Restore();
        int next = firstGroup + direction;
        int count = len(children) - FixedCount();
        while (next >= 0 && next < count && (next == brandIdx || children[next].layout->MinIntrinsicWidth(0) == 0))
            next += direction;
        firstGroup = std::clamp(next, 0, std::max(0, count - 1));
        tb->host->Relayout();
        tb->host->Invalidate(true);
        return true;
    }

    bool Wheel(int delta) {
        if (!delta) return false;
        if ((delta < 0 && !canScrollBefore) || (delta > 0 && !canScrollAfter)) {
            wheelRemainder = 0;
            return false;
        }
        wheelRemainder += delta;
        bool moved = false;
        while (std::abs(wheelRemainder) >= WHEEL_DELTA) {
            int direction = wheelRemainder > 0 ? 1 : -1;
            wheelRemainder -= direction * WHEEL_DELTA;
            moved |= Scroll(direction);
            if ((direction < 0 && !canScrollBefore) || (direction > 0 && !canScrollAfter)) {
                wheelRemainder = 0;
                break;
            }
        }
        return moved;
    }

    void Restore() {
        for (int i = len(hidden) - 1; i >= 0; i--) hidden[i].layout->SetVisibility(hidden[i].visibility);
        VecReset(hidden);
        VecReset(OverflowItems());
    }

    void HideLayout(ILayout* item) {
        VecAppend(hidden, ToolbarHiddenLayout{item, item->GetVisibility()});
        if (VirtCtrl* ctrl = item->AsVirtCtrl()) {
            if (ctrl->id && ctrl->GetVisibility() == Visibility::Visible) VecAppend(OverflowItems(), ctrl);
        }
        for (int i = 0; i < item->LayoutChildCount(); i++) HideLayout(item->LayoutChildAt(i));
        item->SetVisibility(Visibility::Collapse);
    }

    int MinIntrinsicWidth(int) override { return OverflowButton() ? OverflowButton()->MinIntrinsicWidth(0) : 0; }

    int MinIntrinsicHeight(int) override {
        if (tb->vertical) return tb->rowDy;
        int height = tb->rowDy - 2 * ToolbarOuterPad();
        for (auto& child : children) {
            if (!IsCollapsed(child.layout)) height = std::max(height, child.layout->MinIntrinsicHeight(Inf));
        }
        return height;
    }

    void SizeLocationFields(int available, int overflowDx) {
        for (auto& field : locationFields) {
            if (IsCollapsed(field.edit)) continue;
            int fieldsDx = 0;
            int count = 0;
            for (auto& other : locationFields) {
                if (other.group != field.group || IsCollapsed(other.edit)) continue;
                fieldsDx += other.edit->PreferredWidth();
                count++;
            }
            int fixedDx = field.group->MinIntrinsicWidth(0) - fieldsDx;
            // Long labels share the remaining group budget, with room for other commands.
            field.edit->widthCap = LocationEditCap(available, fixedDx, overflowDx + gap, count);
        }
    }

    Size Layout(Constraints bc) override {
        Restore();
        if (AnnotationToolbarRail(tb) && lineKind == ToolbarLineKind::Main) return LayoutRail(bc);
        if (tb->vertical) return LayoutVertical(bc);
        if (lineKind == ToolbarLineKind::Main && tb->findSlot) {
            tb->findSlot->SetVisibility(tb->findExpanded ? Visibility::Visible : Visibility::Collapse);
            tb->findSlot->dx = tb->findExpanded ? tb->findPreferredWidth : 0;
        }
        if (lineKind == ToolbarLineKind::Main && tb->findButton && tb->findExpanded &&
            tb->findButton->GetVisibility() == Visibility::Visible) {
            VecAppend(hidden, ToolbarHiddenLayout{tb->findButton, Visibility::Visible});
            tb->findButton->SetVisibility(Visibility::Collapse);
        }
        auto* overflowButton = OverflowButton();
        overflowButton->SetVisibility(Visibility::Collapse);
        previousButton->SetVisibility(Visibility::Collapse);
        nextButton->SetVisibility(Visibility::Collapse);
        int available = bc.HasBoundedWidth() ? bc.max.dx : Inf;
        int pickerDx = overflowButton->MinIntrinsicWidth(0) + gap;
        if (tb->annotationModeButton && tb->annotationModeButton->IsVisible())
            pickerDx += tb->annotationModeButton->MinIntrinsicWidth(0) + gap;
        available = std::max(0, available - pickerDx);
        int overflowDx = previousButton->MinIntrinsicWidth(0) + nextButton->MinIntrinsicWidth(0) + gap;
        SizeLocationFields(available, overflowDx);
        Vec<int> widths;
        for (int i = 0; i < len(children) - FixedCount(); i++) {
            auto* child = children[i].layout;
            VecAppend(widths, IsCollapsed(child) ? 0 : child->MinIntrinsicWidth(0));
        }
        bool showScrollButtons = true;
        if (lineKind == ToolbarLineKind::Main && tb->findExpanded && findGroupIdx >= 0 && findGroupIdx < len(widths)) {
            int withoutFind = widths[findGroupIdx] - tb->findSlot->dx;
            // Give active search priority; Open stays available in the toolbar picker.
            if (withoutFind + tb->findMinWidth + overflowDx + gap > available) {
                ILayout* group = children[findGroupIdx].layout;
                for (int i = 0; i < group->LayoutChildCount(); i++) {
                    auto* item = group->LayoutChildAt(i)->AsVirtCtrl();
                    if (item && item->id == CmdOpenFile && item->IsVisible()) HideLayout(item);
                }
                withoutFind = group->MinIntrinsicWidth(0) - tb->findSlot->dx;
            }
            // Keep active search usable on narrow windows; the picker and wheel
            // still provide access to the other groups while the arrows are hidden.
            if (withoutFind + tb->findMinWidth + overflowDx + gap > available) {
                overflowDx = 0;
                showScrollButtons = false;
            }
            int searchDx = std::min(tb->findPreferredWidth,
                                    std::max(tb->findMinWidth, available - withoutFind - overflowDx - gap));
            tb->findSlot->dx = searchDx;
            widths[findGroupIdx] = withoutFind + searchDx;
        }
        Vec<bool> visible;
        int removableBrand = tb->findExpanded && findGroupIdx == brandIdx ? -1 : brandIdx;
        bool overflow = FitScrolledToolbarGroups(widths, available, gap, overflowDx, removableBrand, firstGroup,
                                                 visible, canScrollBefore, canScrollAfter);
        for (int i = 0; i < len(widths); i++) {
            if (widths[i] > 0 && !visible[i]) HideLayout(children[i].layout);
        }
        overflowButton->SetVisibility(Visibility::Visible);
        previousButton->SetVisibility(overflow && showScrollButtons ? Visibility::Visible : Visibility::Collapse);
        nextButton->SetVisibility(overflow && showScrollButtons ? Visibility::Visible : Visibility::Collapse);
        previousButton->SetIsEnabled(canScrollBefore);
        nextButton->SetIsEnabled(canScrollAfter);
        Size size = HBox::Layout(bc);
        size.dy = bc.ConstrainHeight(MinIntrinsicHeight(available));
        return size;
    }

    bool RailDrawingCommand(int command) const {
        command = OriginalCommandId(command);
        return (command >= CmdCreateAnnotFirst && command <= CmdCreateAnnotLast) || command == CmdToggleEditPDF ||
               command == CmdHandTool || command == CmdTextSelectTool || command == CmdAnnotationLasso ||
               command == CmdAnnotationShapes || command == CmdAnnotationHighlightBrush || command == CmdInkEraser ||
               command == CmdToggleLaserPointer;
    }

    void CollectRailCells(ILayout* layout, Vec<ToolbarRailCell>& cells) {
        if (!layout || layout->GetVisibility() != Visibility::Visible) return;
        if (layout == tb->pageBox) {
            VecAppend(cells, ToolbarRailCell{layout, {}, ToolbarRailRole::Page, PageInfoId});
            return;
        }
        if (layout == tb->zoomEdit || layout == tb->win->chapterEdit) {
            bool zoom = layout == tb->zoomEdit;
            VecAppend(cells, ToolbarRailCell{layout,
                                             {},
                                             zoom ? ToolbarRailRole::Zoom : ToolbarRailRole::Chapter,
                                             zoom ? CmdZoomCustom : PageInfoId});
            return;
        }
        if (VirtCtrl* control = layout->AsVirtCtrl()) {
            if (!control->id || control == tb->annotationModeButton || control == tb->pageLabel ||
                control == tb->pageLabel2 || control == tb->pageTotal || control == tb->chapterTotal ||
                VecFind(tb->pinnedItems, control) >= 0)
                return;
            VecAppend(cells, ToolbarRailCell{layout, {}, ToolbarRailRole::Tool, control->id});
            return;
        }
        for (int i = 0; i < layout->LayoutChildCount(); i++) CollectRailCells(layout->LayoutChildAt(i), cells);
    }

    void HideRailDecorations(ILayout* layout) {
        if (!layout || IsCollapsed(layout)) return;
        if (auto* control = layout->AsVirtCtrl()) {
            if (!control->id && control->HasFlag(vwfNoHitTest)) HideLayout(control);
            return;
        }
        if (layout == tb->pageBox) return;
        for (int i = 0; i < layout->LayoutChildCount(); i++) HideRailDecorations(layout->LayoutChildAt(i));
    }

    void PlaceRailRows(const Vec<ToolbarRailCell>& cells, int width, int& y, int group, bool fullWidth = false,
                       bool navigation = false) {
        if (!len(cells)) return;
        int columns = fullWidth ? 1 : tb->annotationColumns;
        int rows = (len(cells) + columns - 1) / columns;
        int baseCount = len(cells) / rows;
        int extra = len(cells) % rows;
        int next = 0;
        int spacing = UiScalePx(navigation ? 1 : 2);
        for (int row = 0; row < rows; row++) {
            int count = baseCount + (row < extra ? 1 : 0);
            int cellWidth = fullWidth ? width : width / tb->annotationColumns;
            int rowHeight = 0;
            for (int i = 0; i < count; i++) {
                auto& cell = cells[next + i];
                if (auto* edit = cell.layout->AsControl()) {
                    if (cell.role == ToolbarRailRole::Zoom || cell.role == ToolbarRailRole::Chapter)
                        ((ToolbarLocationEdit*)edit)->widthCap = cellWidth;
                }
                rowHeight = std::max(rowHeight, cell.layout->MinIntrinsicHeight(cellWidth));
            }
            for (int i = 0; i < count; i++) {
                ToolbarRailCell cell = cells[next++];
                int dy = cell.layout->MinIntrinsicHeight(cellWidth);
                int x = (width - count * cellWidth) / 2 + i * cellWidth;
                if (rtl) x = width - x - cellWidth;
                cell.bounds = {x, y + (rowHeight - dy) / 2, cellWidth, dy};
                cell.rowTop = y;
                cell.group = group;
                cell.layout->Layout(Tight(cell.bounds.Size()));
                VecAppend(railCells, cell);
            }
            y += rowHeight + spacing;
        }
    }

    void AddRailGroup(const Vec<ToolbarRailCell>& cells, int width, int& y) {
        if (!len(cells)) return;
        int start = y;
        int group = len(railGroups);
        int page = -1, zoom = -1;
        for (int i = 0; i < len(cells); i++) {
            if (cells[i].role == ToolbarRailRole::Page) page = i;
            if (cells[i].role == ToolbarRailRole::Zoom) zoom = i;
        }
        bool navigation = page >= 0 || zoom >= 0;
        bool widePage = page >= 0 && tb->pageBox && tb->pageBox->FullWidth();
        if ((tb->annotationColumns == 2 && navigation) || widePage) {
            Vec<ToolbarRailCell> icons;
            Vec<ToolbarRailCell> fields;
            for (auto& cell : cells) {
                if (cell.role == ToolbarRailRole::Tool || cell.role == ToolbarRailRole::Pin)
                    VecAppend(icons, cell);
                else
                    VecAppend(fields, cell);
            }
            if (zoom >= 0) PlaceRailRows(fields, width, y, group, true, true);
            PlaceRailRows(icons, width, y, group, false, true);
            if (page >= 0) PlaceRailRows(fields, width, y, group, true, true);
        } else {
            Vec<ToolbarRailCell> ordered;
            for (int i = 0; i < len(cells); i++) {
                if (i == zoom) continue;
                if (zoom >= 0 && OriginalCommandId(cells[i].cmd) == CmdZoomIn) VecAppend(ordered, cells[zoom]);
                VecAppend(ordered, cells[i]);
            }
            if (zoom >= 0) {
                bool inserted = false;
                for (auto& cell : ordered) inserted |= cell.role == ToolbarRailRole::Zoom;
                if (!inserted) VecAppend(ordered, cells[zoom]);
            }
            PlaceRailRows(ordered, width, y, group, false, navigation);
        }
        int spacing = UiScalePx(navigation ? 1 : 2);
        int height = std::max(0, y - start - spacing);
        VecAppend(railGroups, Rect{0, start, width, height});
        y = start + height + gap;
    }

    void AddPinnedRail(int width, int& y) {
        Vec<ToolbarRailCell> pins;
        auto* presets = gSettings->pinnedAnnotationTools;
        for (int i = 0; i < len(tb->pinnedItems); i++) {
            VirtCtrl* control = tb->pinnedItems[i];
            if (!control || !control->IsVisible()) continue;
            int command = presets && i < len(*presets) ? PinnedToolCommand((*presets)[i]->tool) : control->id;
            VecAppend(pins, ToolbarRailCell{control, {}, ToolbarRailRole::Pin, command, i});
        }
        AddRailGroup(pins, width, y);
    }

    void BuildNavigationRail(const Vec<ToolbarRailCell>& source, int width, int& y, Vec<ILayout*>& consumed,
                             Vec<ToolbarRailCell>& drawingPrefix) {
        auto take = [&](Vec<ToolbarRailCell>& target, int command, ToolbarRailRole role = ToolbarRailRole::Tool) {
            for (auto& cell : source) {
                if (cell.role != role || VecFind(consumed, cell.layout) >= 0) continue;
                if (role == ToolbarRailRole::Tool && OriginalCommandId(cell.cmd) != command) continue;
                VecAppend(target, cell);
                VecAppend(consumed, cell.layout);
                return;
            }
        };
        Vec<ToolbarRailCell> row;
        take(row, CmdOpenFile);
        take(row, CmdFindFirst);
        if (tb->annotationColumns == 3) take(row, CmdSinglePageView);
        AddRailGroup(row, width, y);
        VecReset(row);
        int start = y;
        int group = len(railGroups);
        bool fullPage = tb->pageBox && tb->pageBox->FullWidth();
        take(row, CmdGoToPrevPage);
        if (tb->annotationColumns == 3) {
            if (fullPage)
                take(row, CmdZoomFitWidthAndContinuous);
            else
                take(row, PageInfoId, ToolbarRailRole::Page);
        }
        take(row, CmdGoToNextPage);
        PlaceRailRows(row, width, y, group, false, true);
        VecReset(row);
        take(row, PageInfoId, ToolbarRailRole::Chapter);
        PlaceRailRows(row, width, y, group, true, true);
        VecReset(row);
        if (fullPage) {
            take(row, PageInfoId, ToolbarRailRole::Page);
            PlaceRailRows(row, width, y, group, true, true);
            VecReset(row);
        }
        if (tb->annotationColumns == 2) {
            take(row, CmdZoomCustom, ToolbarRailRole::Zoom);
            PlaceRailRows(row, width, y, group, true, true);
            VecReset(row);
        }
        take(row, CmdZoomOut);
        if (tb->annotationColumns == 3) take(row, CmdZoomCustom, ToolbarRailRole::Zoom);
        take(row, CmdZoomIn);
        PlaceRailRows(row, width, y, group, false, true);
        VecReset(row);
        if (tb->annotationColumns == 2) {
            take(row, CmdZoomFitWidthAndContinuous);
            take(row, CmdSinglePageView);
            PlaceRailRows(row, width, y, group, false, true);
            VecReset(row);
        }
        if (tb->annotationColumns == 3 && fullPage) {
            take(drawingPrefix, CmdRotateLeft);
            take(drawingPrefix, CmdRotateRight);
        } else {
            if (tb->annotationColumns == 3) take(row, CmdZoomFitWidthAndContinuous);
            take(row, CmdRotateLeft);
            take(row, CmdRotateRight);
            PlaceRailRows(row, width, y, group, false, true);
        }
        if (y > start) {
            int height = y - start - UiScalePx(1);
            VecAppend(railGroups, Rect{0, start, width, height});
            y = start + height + gap;
        }
    }

    void BuildRail(int width) {
        VecReset(railCells);
        VecReset(railGroups);
        if (tb->findSlot) tb->findSlot->SetVisibility(Visibility::Collapse);
        for (auto* label : {tb->pageLabel, tb->pageLabel2, tb->pageTotal, tb->chapterTotal})
            if (label && !IsCollapsed(label)) HideLayout(label);
        int count = len(children) - FixedCount();
        int lastDrawing = -1;
        Vec<ToolbarRailCell> source;
        for (int i = 0; i < count; i++) {
            ILayout* group = children[i].layout;
            if (i == brandIdx) {
                if (!IsCollapsed(group)) HideLayout(group);
                continue;
            }
            HideRailDecorations(group);
            Vec<ToolbarRailCell> cells;
            CollectRailCells(group, cells);
            for (auto& cell : cells) VecAppend(source, cell);
            for (auto& cell : cells) {
                if (RailDrawingCommand(cell.cmd) || VecFind(tb->annotationGroups, group) >= 0) lastDrawing = i;
            }
        }
        int y = 0;
        Vec<ILayout*> consumed;
        Vec<ToolbarRailCell> drawingPrefix;
        bool defaultOrder = str::IsEmptyOrWhiteSpace(gSettings->toolbarCustomLayout) && !len(gSettings->toolbarOrder);
        if (MultiToolbarColumns(tb) && defaultOrder) BuildNavigationRail(source, width, y, consumed, drawingPrefix);
        for (int i = 0; i < count; i++) {
            Vec<ToolbarRailCell> cells;
            CollectRailCells(children[i].layout, cells);
            Vec<ToolbarRailCell> remaining;
            bool drawing = false;
            for (auto& cell : cells) drawing |= RailDrawingCommand(cell.cmd);
            if (drawing && len(drawingPrefix)) {
                for (auto& cell : drawingPrefix) VecAppend(remaining, cell);
                VecReset(drawingPrefix);
            }
            for (auto& cell : cells)
                if (VecFind(consumed, cell.layout) < 0) VecAppend(remaining, cell);
            AddRailGroup(remaining, width, y);
            if (i == lastDrawing) AddPinnedRail(width, y);
        }
        if (lastDrawing < 0) AddPinnedRail(width, y);
        railContentDy = std::max(0, y - gap);
    }

    int RailFooterHeight(bool scrolling) const {
        if (SingleToolbarColumn(tb)) {
            int height = OverflowButton()->MinIntrinsicHeight(0) + gap;
            if (tb->annotationModeButton && tb->annotationModeButton->IsVisible())
                height += tb->annotationModeButton->MinIntrinsicHeight(0) + gap;
            if (scrolling)
                height += previousButton->MinIntrinsicHeight(0) + nextButton->MinIntrinsicHeight(0) + 2 * gap;
            return height;
        }
        int height = std::max(OverflowButton()->MinIntrinsicHeight(0), previousButton->MinIntrinsicHeight(0));
        int rows = tb->annotationColumns == 2 ? (scrolling ? 2 : 1) : 2;
        return rows * height + (rows - 1) * UiScalePx(2) + gap;
    }

    Size LayoutRail(Constraints bc) {
        int width = bc.HasBoundedWidth() ? bc.max.dx : ToolbarColumnWidth(tb) * tb->annotationColumns;
        BuildRail(width);
        auto* overflow = OverflowButton();
        overflow->SetVisibility(Visibility::Visible);
        previousButton->SetVisibility(Visibility::Collapse);
        nextButton->SetVisibility(Visibility::Collapse);
        int fixedDy = RailFooterHeight(false);
        if (!bc.HasBoundedHeight()) return {width, railContentDy + fixedDy};
        bool scrolling = railContentDy > std::max(0, bc.max.dy - fixedDy);
        fixedDy = RailFooterHeight(scrolling);
        verticalViewportDy = std::max(0, bc.max.dy - fixedDy);
        verticalMaxScroll = std::max(0, railContentDy - verticalViewportDy);
        verticalScrollY = limitValue(verticalScrollY, 0, verticalMaxScroll);
        canScrollBefore = verticalScrollY > 0;
        canScrollAfter = verticalScrollY < verticalMaxScroll;
        for (auto& cell : railCells) {
            int top = cell.bounds.y - verticalScrollY;
            if (top < 0 || top + cell.bounds.dy > verticalViewportDy) HideLayout(cell.layout);
        }
        previousButton->SetVisibility(scrolling ? Visibility::Visible : Visibility::Collapse);
        nextButton->SetVisibility(scrolling ? Visibility::Visible : Visibility::Collapse);
        previousButton->SetIsEnabled(canScrollBefore);
        nextButton->SetIsEnabled(canScrollAfter);
        return bc.Constrain({width, railContentDy + fixedDy});
    }

    void SetRailBounds(Rect bounds) {
        lastBounds = bounds;
        auto containers = [&](auto&& self, ILayout* layout) -> void {
            if (!layout || layout->AsControl() || layout->AsVirtCtrl() || layout == tb->pageBox) return;
            layout->lastBounds = bounds;
            for (int i = 0; i < layout->LayoutChildCount(); i++) self(self, layout->LayoutChildAt(i));
        };
        for (int i = 0; i < len(children) - FixedCount(); i++) containers(containers, children[i].layout);
        for (auto& cell : railCells) {
            if (cell.layout->GetVisibility() != Visibility::Visible) continue;
            Rect rect = cell.bounds;
            rect.x += bounds.x;
            rect.y += bounds.y - verticalScrollY;
            cell.layout->SetBounds(rect);
        }
        if (MultiToolbarColumns(tb)) {
            int cellWidth = bounds.dx / tb->annotationColumns;
            int height = std::max(OverflowButton()->MinIntrinsicHeight(0), previousButton->MinIntrinsicHeight(0));
            int bottomY = bounds.Bottom() - height;
            auto place = [&](VirtIconButton* control, int column, int y) {
                if (control && control->IsVisible())
                    control->SetBounds({bounds.x + column * cellWidth, y, cellWidth, height});
            };
            if (tb->annotationColumns == 2) {
                place(OverflowButton(), 0, bottomY);
                place(tb->annotationModeButton, 1, bottomY);
                place(previousButton, 0, bottomY - height - UiScalePx(2));
                place(nextButton, 1, bottomY - height - UiScalePx(2));
            } else {
                place(tb->annotationModeButton, 1, bottomY);
                int commandsY = bottomY - height - UiScalePx(2);
                place(OverflowButton(), previousButton->IsVisible() ? 2 : 1, commandsY);
                place(previousButton, 0, commandsY);
                place(nextButton, 1, commandsY);
            }
            return;
        }
        int bottom = bounds.Bottom();
        Vec<VirtIconButton*> fixed;
        FixedControls(fixed);
        for (auto* control : fixed) {
            Size size = control->GetIdealSize();
            if (SingleToolbarColumn(tb)) size.dx = bounds.dx;
            bottom -= size.dy;
            control->SetBounds({bounds.x + (bounds.dx - size.dx) / 2, bottom, size.dx, size.dy});
            bottom -= gap;
        }
    }

    Size LayoutVertical(Constraints bc) {
        if (tb->findSlot) tb->findSlot->SetVisibility(Visibility::Collapse);
        int width = bc.HasBoundedWidth() ? bc.max.dx : UiScalePx(80);
        int fieldWidth = tb->win->annotationMode ? std::max(1, width / tb->annotationColumns) : width;
        for (auto& field : locationFields) {
            field.edit->MeasureWidth();
            field.edit->widthCap = fieldWidth;
        }
        if (tb->zoomEdit && tb->win->annotationMode) {
            auto* zoom = (ToolbarLocationEdit*)tb->zoomEdit;
            zoom->MeasureWidth();
            zoom->widthCap = fieldWidth;
        }
        if (tb->win->annotationMode) {
            for (auto* label : {tb->pageLabel, tb->pageLabel2, tb->pageTotal, tb->chapterTotal})
                if (label && !IsCollapsed(label)) HideLayout(label);
        }
        auto* overflow = OverflowButton();
        overflow->SetVisibility(Visibility::Visible);
        previousButton->SetVisibility(Visibility::Collapse);
        nextButton->SetVisibility(Visibility::Collapse);
        int fixedDy = overflow->MinIntrinsicHeight(width) + gap;
        if (lineKind == ToolbarLineKind::Main && tb->annotationModeButton && tb->annotationModeButton->IsVisible())
            fixedDy += tb->annotationModeButton->MinIntrinsicHeight(width) + gap;
        int arrowsDy = previousButton->MinIntrinsicHeight(width) + nextButton->MinIntrinsicHeight(width) + gap;
        int count = len(children) - FixedCount();
        int contentDy = 0;
        for (int i = 0; i < count; i++) {
            auto* group = children[i].layout;
            if (i == brandIdx && !IsCollapsed(group)) HideLayout(group);
            if (auto* column = AsToolbarGroup(group)) {
                for (auto& child : column->children) {
                    VirtCtrl* ctrl = child.layout->AsVirtCtrl();
                    if (ctrl && !ctrl->id && ctrl->HasFlag(vwfNoHitTest) && !IsCollapsed(ctrl)) HideLayout(ctrl);
                }
            }
            children[i].size = IsCollapsed(group) ? Size{} : group->Layout(Loose({width, Inf}));
            if (children[i].size.dy > 0) contentDy += children[i].size.dy + gap;
        }
        contentDy = std::max(0, contentDy - gap);
        // Intrinsic measurement must not reset the bounded viewport's scroll position.
        if (!bc.HasBoundedHeight()) {
            for (int i = count; i < len(children); i++) {
                auto& child = children[i];
                if (!IsCollapsed(child.layout)) child.size = child.layout->Layout(Loose({width, Inf}));
            }
            return {width, contentDy + fixedDy};
        }
        int available = bc.HasBoundedHeight() ? std::max(0, bc.max.dy - fixedDy) : Inf;
        bool scrolling = contentDy > available;
        verticalViewportDy = scrolling ? std::max(0, available - arrowsDy - gap) : available;
        verticalMaxScroll = std::max(0, contentDy - verticalViewportDy);
        verticalScrollY = limitValue(verticalScrollY, 0, verticalMaxScroll);
        canScrollBefore = verticalScrollY > 0;
        canScrollAfter = verticalScrollY < verticalMaxScroll;
        int y = -verticalScrollY;
        for (int i = 0; i < count; i++) {
            auto& group = children[i];
            if (group.size.dy <= 0) continue;
            if (y + group.size.dy <= 0 || y >= verticalViewportDy) {
                HideLayout(group.layout);
            } else if (auto* column = AsToolbarGroup(group.layout)) {
                for (int j = 0; j < len(column->children); j++) {
                    ILayout* item = column->children[j].layout;
                    if (IsCollapsed(item) || column->cells[j].IsEmpty()) continue;
                    Rect cell = column->cells[j];
                    if (y + cell.y < 0 || y + cell.Bottom() > verticalViewportDy) HideLayout(item);
                }
            }
            y += group.size.dy + gap;
        }
        previousButton->SetVisibility(scrolling ? Visibility::Visible : Visibility::Collapse);
        nextButton->SetVisibility(scrolling ? Visibility::Visible : Visibility::Collapse);
        previousButton->SetIsEnabled(canScrollBefore);
        nextButton->SetIsEnabled(canScrollAfter);
        for (int i = count; i < len(children); i++) {
            auto& child = children[i];
            if (IsCollapsed(child.layout)) continue;
            child.size = child.layout->Layout(Loose({width, Inf}));
        }
        return bc.Constrain({width, contentDy + fixedDy + (scrolling ? arrowsDy + gap : 0)});
    }

    void FixedControls(Vec<VirtIconButton*>& out) {
        if (lineKind == ToolbarLineKind::Main && tb->annotationModeButton && tb->annotationModeButton->IsVisible())
            VecAppend(out, tb->annotationModeButton);
        for (auto* control : {OverflowButton(), nextButton, previousButton})
            if (control && control->IsVisible()) VecAppend(out, control);
    }

    void SetVerticalBounds(Rect r) {
        lastBounds = r;
        int y = r.y - verticalScrollY;
        int count = len(children) - FixedCount();
        for (int i = 0; i < count; i++) {
            auto& child = children[i];
            if (child.size.dy <= 0) continue;
            if (!IsCollapsed(child.layout)) child.layout->SetBounds({r.x, y, r.dx, child.size.dy});
            y += child.size.dy + gap;
        }
        int bottom = r.Bottom();
        Vec<VirtIconButton*> fixed;
        FixedControls(fixed);
        for (auto* control : fixed) {
            Size size = control->GetIdealSize();
            if (SingleToolbarColumn(tb)) size.dx = r.dx;
            bottom -= size.dy;
            control->SetBounds({r.x + (r.dx - size.dx) / 2, bottom, size.dx, size.dy});
            bottom -= gap;
        }
    }

    void SetBounds(Rect r) override {
        if (AnnotationToolbarRail(tb) && lineKind == ToolbarLineKind::Main) {
            SetRailBounds(r);
            return;
        }
        if (tb->vertical) {
            SetVerticalBounds(r);
            return;
        }
        HBox::SetBounds(r);
        int edge = rtl ? r.x : r.Right();
        Vec<VirtIconButton*> fixed;
        FixedControls(fixed);
        for (auto* control : fixed) {
            Rect button = control->lastBounds;
            button.x = rtl ? edge : edge - button.dx;
            control->SetBounds(button);
            edge = rtl ? button.Right() + gap : button.x - gap;
        }
    }
};

static ToolbarButtonInfo gToolbarButtons[] = {
    {kEnhancedIconOpen, CmdOpenFile, TrN("Open")},
    {kEnhancedIconSearch, CmdFindFirst, TrN("Find")},
    {nullptr, 0, {}},
    {kEnhancedIconPrevious, CmdGoToPrevPage, TrN("Previous Page")},
    {nullptr, PageInfoId, {}},
    {kEnhancedIconNext, CmdGoToNextPage, TrN("Next Page")},
    {nullptr, 0, {}},
    {gIconZoomOut, CmdZoomOut, TrN("Zoom Out")},
    {gIconZoomIn, CmdZoomIn, TrN("Zoom In")},
    {kEnhancedIconFitWidth, CmdZoomFitWidthAndContinuous, TrN("Fit Width")},
    {kEnhancedIconPage, CmdSinglePageView, TrN("Reading layout")},
    {nullptr, 0, {}},
    {gIconRotateLeft, CmdRotateLeft, TrN("Rotate Left")},
    {gIconRotateRight, CmdRotateRight, TrN("Rotate Right")},
    {nullptr, 0, {}},
    {gIconAnnotationMode, CmdAnnotationMode, TrN("Annotation Mode (Esc to exit)")},
    {kHandQuillIcon, CmdToggleEditPDF, TrN("Edit PDF")},
    {kHandToolIcon, CmdHandTool, TrN("Hand tool: drag to pan")},
    {kTextSelectToolIcon, CmdTextSelectTool, TrN("Select text: drag to select; Shift-click to extend")},
    {kLassoToolIcon, CmdAnnotationLasso, TrN("Lasso: select, move, resize or delete annotations")},
    {kEnhancedIconInk, CmdCreateAnnotInk, TrN("Pen: tools, colors and thickness")},
    {kEnhancedIconHighlight, CmdAnnotationHighlightBrush, TrN("Text highlighter")},
    {kEnhancedIconUnderline, CmdCreateAnnotUnderline, TrN("Underline")},
    {kEnhancedIconStrikeOut, CmdCreateAnnotStrikeOut, TrN("Strike Out")},
    {kEnhancedIconEraser, CmdInkEraser, TrN("Stroke eraser")},
    {gIconLaserPointer, CmdToggleLaserPointer, TrN("Laser pointer: styles and colors")},
    {gIconAnnotSquare, CmdAnnotationShapes, TrN("Shapes: border, fill and thickness")},
    {nullptr, 0, {}},
    {kEnhancedIconBookmark, CmdToggleBookmarks, TrN("Bookmarks")},
    {kEnhancedIconStar, CmdCommandPaletteFavorites, TrN("Favorites")},
    {kEnhancedIconCommand, CmdCommandPalette, TrN("Command Palette")},
    {kEnhancedIconPrint, CmdPrint, TrN("Print")},
    {nullptr, 0, {}},
    {kEnhancedIconSettings, CmdOptions, TrN("Settings")},
    {kEnhancedIconSun, CmdThemeLight, TrN("Day mode")},
    {kEnhancedIconMoon, CmdThemeDark, TrN("Night mode")},
    {kEnhancedIconGrid, CmdChangeTheme, TrN("Theme presets")},
    {kEnhancedIconInvert, CmdInvertColors, TrN("Invert document colors")},
    // Available to custom layouts without widening the default toolbar.
    {gIconDictionary, CmdDictionaryLookup, TrN("Dictionary")},
    {gIconLearning, CmdVocabularyHome, TrN("Vocabulary practice")},
    {gIconStudyExport, CmdExportStudyNotes, TrN("Export highlights and notes")},
    {gIconPresentation, CmdTogglePresentationMode, TrN("Presentation")},
    {gIconHome, CmdGoToHomePage, TrN("Home")},
    {gIconNavigateBack, CmdNavigateBack, TrN("Back")},
    {gIconNavigateForward, CmdNavigateForward, TrN("Forward")},
    {gIconSpeak, CmdToggleReadAloud, TrN("Read Aloud")},
    {kEnhancedIconPage, CmdZoomFitPageAndSinglePage, TrN("Fit a Single Page")},
    {kEnhancedIconInvert, CmdToggleLightDarkTheme, TrN("Light / Dark")},
};
// unicode chars: https://www.compart.com/en/unicode/U+25BC

constexpr int kButtonsCount = dimof(gToolbarButtons);

static const int kShapeFillCommands[] = {CmdCreateAnnotLine,
                                         CmdCreateAnnotPolyLine,
                                         CmdCreateAnnotSquare,
                                         CmdCreateAnnotCircle,
                                         CmdCreateAnnotPolygon,
                                         CmdCreateAnnotArrow,
                                         CmdCreateAnnotDashedLine,
                                         CmdCreateAnnotParabola,
                                         CmdCreateAnnotNegativeParabola,
                                         CmdCreateAnnotCubic,
                                         CmdCreateAnnotNegativeCubic,
                                         CmdCreateAnnotReciprocal,
                                         CmdCreateAnnotNegativeReciprocal,
                                         CmdCreateAnnotAxes2D,
                                         CmdCreateAnnotAxes3D};

static int SelectedShapeCmd() {
    return kShapeFillCommands[gSettings
                                  ? std::clamp(gSettings->annotations.shapeTool, 0, dimofi(kShapeFillCommands) - 1)
                                  : 2];
}

static TempStr ShapeToolTitle() {
    const char* names[] = {"Line",
                           "Polyline",
                           "Rectangle",
                           "Ellipse",
                           "Polygon",
                           "Arrow",
                           "Dashed line",
                           "Parabola",
                           "Negative parabola",
                           "Cubic curve",
                           "Negative cubic curve",
                           "Reciprocal curve",
                           "Negative reciprocal curve",
                           "2D axes",
                           "3D axes"};
    int selected = gSettings ? std::clamp(gSettings->annotations.shapeTool, 0, dimofi(names) - 1) : 2;
    return fmt("%s (%s)", Tr("Shapes"), Tr(names[selected]));
}

static const char* ShapeToolIcon() {
    if (MathShapeForCommand(SelectedShapeCmd()) >= 0) return gIconAnnotPolyLine;
    const char* icons[] = {gIconAnnotLine,    gIconAnnotPolyLine, gIconAnnotSquare, gIconAnnotCircle,
                           gIconAnnotPolygon, gIconArrowUp,       gIconAnnotLine};
    return icons[gSettings ? std::clamp(gSettings->annotations.shapeTool, 0, dimofi(icons) - 1) : 2];
}

struct ShapeIconCacheEntry {
    int cmd = 0;
    int size = 0;
    Color color = 0;
    Pixmap* pixmap = nullptr;
    int style = 0;
    Color fill = kColorUnset;
};

static Vec<ShapeIconCacheEntry> gShapeIconCache;

void FreeShapeToolIcons() {
    for (auto& entry : gShapeIconCache) delete entry.pixmap;
    VecReset(gShapeIconCache);
}

static bool IsLineVariant(int cmd) {
    return cmd == CmdCreateAnnotArrow || cmd == CmdCreateAnnotDashedLine || MathShapeForCommand(cmd) >= 0;
}

static Pixmap* LineVariantIcon(int cmd, int size, Color color, Color fill = kColorUnset) {
    for (auto& entry : gShapeIconCache)
        if (entry.cmd == cmd && entry.size == size && entry.color == color && entry.fill == fill) return entry.pixmap;
    Gdiplus::Bitmap image(size, size, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&image);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        float scale = size / 24.f;
        graphics.ScaleTransform(scale, scale);
        Gdiplus::Pen pen(GdiRgbFromColor(color), 1.8f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        if (cmd == CmdCreateAnnotDashedLine) pen.SetDashStyle(Gdiplus::DashStyleDash);
        int math = MathShapeForCommand(cmd);
        if (math >= 0) {
            Vec<int> counts;
            Vec<PointF> points;
            BuildMathShape(math, {3, 3, 18, 18}, counts, points);
            int first = 0;
            for (int count : counts) {
                for (int i = first + 1; i < first + count; i++)
                    graphics.DrawLine(&pen, points[i - 1].x, points[i - 1].y, points[i].x, points[i].y);
                first += count;
            }
        } else
            graphics.DrawLine(&pen, 5.f, 19.f, 19.f, 5.f);
        if (cmd == CmdCreateAnnotArrow) {
            PointF left, right;
            BuildArrowHead({5, 19}, {19, 5}, 1, left, right);
            Gdiplus::PointF head[] = {{19.f, 5.f}, {left.x, left.y}, {right.x, right.y}};
            if (fill != kColorUnset) {
                Gdiplus::SolidBrush interior(
                    Gdiplus::Color(GetAlpha(fill), GetRed(fill), GetGreen(fill), GetBlue(fill)));
                graphics.FillPolygon(&interior, head, dimof(head));
            }
            graphics.DrawPolygon(&pen, head, dimof(head));
        }
    }
    Pixmap* pixmap = PixmapFromGdiplus(&image);
    if (pixmap) VecAppend(gShapeIconCache, ShapeIconCacheEntry{cmd, size, color, pixmap, 0, fill});
    return pixmap;
}

static Pixmap* ShapeToolPixmap(int size, Color color) {
    int cmd = SelectedShapeCmd();
    return IsLineVariant(cmd)
               ? LineVariantIcon(cmd, size, color)
               : GetCachedPixmapForSvg(Str(ShapeToolIcon()), size, size, color, ThemeControlBackgroundColor());
}

Pixmap* AnnotationModeIconPixmap(int size, Color color, int style) {
    style = limitValue(style > 0 ? style : gSettings ? gSettings->annotationModeIconStyle : 1, 1, 2);
    for (auto& entry : gShapeIconCache)
        if (entry.cmd == CmdAnnotationMode && entry.size == size && entry.color == color && entry.style == style)
            return entry.pixmap;
    Gdiplus::Bitmap image(size, size, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&image);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        float scale = size / 24.f;
        graphics.ScaleTransform(scale, scale);
        Gdiplus::Color ink = GdiRgbFromColor(color);
        Gdiplus::Pen pen(ink, 1.75f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::GraphicsPath frame;
        frame.AddArc(3.f, 3.f, 4.f, 4.f, 180.f, 90.f);
        frame.AddArc(17.f, 3.f, 4.f, 4.f, 270.f, 90.f);
        frame.AddArc(17.f, 12.f, 4.f, 4.f, 0.f, 90.f);
        frame.AddArc(3.f, 12.f, 4.f, 4.f, 90.f, 90.f);
        frame.CloseFigure();
        graphics.DrawPath(&pen, &frame);
        graphics.DrawLine(&pen, 12.f, 16.f, 12.f, 21.f);
        graphics.DrawLine(&pen, 8.f, 21.f, 16.f, 21.f);
        Gdiplus::SolidBrush fill(ink);
        if (style == 1) {
            Gdiplus::PointF body[] = {{9.8f, 11.1f}, {13.3f, 7.6f}, {14.9f, 9.2f}, {11.4f, 12.7f}, {8.9f, 13.5f}};
            Gdiplus::PointF cap[] = {{14.5f, 6.4f}, {16.1f, 8.f}, {15.3f, 8.8f}, {13.7f, 7.2f}};
            graphics.FillPolygon(&fill, body, dimof(body));
            graphics.FillPolygon(&fill, cap, dimof(cap));
        } else {
            pen.SetWidth(1.15f);
            Gdiplus::PointF outline[] = {{8.3f, 13.5f}, {9.4f, 10.9f}, {14.4f, 5.9f},
                                         {16.f, 7.5f},  {11.f, 12.5f}, {8.3f, 13.5f}};
            graphics.DrawLines(&pen, outline, dimof(outline));
            pen.SetWidth(.95f);
            graphics.DrawLine(&pen, 13.f, 7.3f, 14.6f, 8.9f);
            graphics.DrawLine(&pen, 9.4f, 10.9f, 11.f, 12.5f);
            Gdiplus::PointF tip[] = {{8.3f, 13.5f}, {9.1f, 11.7f}, {10.1f, 12.8f}};
            graphics.FillPolygon(&fill, tip, dimof(tip));
        }
    }
    Pixmap* pixmap = PixmapFromGdiplus(&image);
    if (pixmap) VecAppend(gShapeIconCache, ShapeIconCacheEntry{CmdAnnotationMode, size, color, pixmap, style});
    return pixmap;
}

static ToolbarButtonInfo gPdfAnnotationButtons[] = {
    {kHandToolIcon, CmdHandTool, TrN("Hand tool: drag to pan")},
    {kTextSelectToolIcon, CmdTextSelectTool, TrN("Select text: drag to select; Shift-click to extend")},
    {kLassoToolIcon, CmdAnnotationLasso, TrN("Lasso: select, move, resize or delete annotations")},
    {kEnhancedIconHighlight, CmdAnnotationHighlightBrush, TrN("Highlighter: select text to highlight it")},
    {kEnhancedIconInk, CmdCreateAnnotInk, TrN("Ink")},
    {gIconAnnotHighlight, CmdCreateAnnotHighlight, TrN("Highlight Selection")},
    {kEnhancedIconUnderline, CmdCreateAnnotUnderline, TrN("Underline")},
    {gIconAnnotSquiggly, CmdCreateAnnotSquiggly, TrN("Squiggly")},
    {kEnhancedIconStrikeOut, CmdCreateAnnotStrikeOut, TrN("Strike Out")},
    {nullptr, 0, {}},
    {gIconAnnotText, CmdCreateAnnotText, TrN("Text")},
    {gIconAnnotFreeText, CmdCreateAnnotFreeText, TrN("Free Text")},
    {nullptr, 0, {}},
    {gIconAnnotSquare, CmdAnnotationShapes, TrN("Shapes")},
    {nullptr, 0, {}},
    {gIconAnnotRedact, CmdCreateAnnotRedact, TrN("Redact")},
    {gIconApplyRedactions, CmdApplyRedactions, TrN("Apply Redactions")},
    {gIconAnnotStamp, CmdCreateAnnotStamp, TrN("Stamp")},
    {gIconAnnotCaret, CmdCreateAnnotCaret, TrN("Caret")},
    {gIconAnnotFileAttachment, CmdCreateAnnotFileAttachment, TrN("File Attachment")},
    {nullptr, 0, {}},
    {gIconUndo, CmdUndo, TrN("Undo")},
    {gIconRedo, CmdRedo, TrN("Redo")},
    {nullptr, 0, {}},
    {gIconFindAnnotation, CmdFindAnnotation, TrN("Find Annotation")},
    {nullptr, 0, {}},
    // the tooltip names the file, see ToolbarUpdateStateForWindow. Hovering it
    // opens a drop-down with the other two ways to end an editing session
    {gIconSave, CmdSaveAnnotations, TrN("Save changes to existing PDF")},
};

constexpr int kPdfAnnotationButtonsCount = dimof(gPdfAnnotationButtons);
static ToolbarButtonInfo gAnnotationButtons[kPdfAnnotationButtonsCount];

// The built-in buttons actually on the toolbar, which is gToolbarButtons unless
// ToolbarCustomLayout asks for a different set / order (issue #5095). A layout
// can repeat a button, so allow for more than the default count.
constexpr int kMaxLayoutButtons = 64;
static ToolbarButtonInfo gLayoutButtons[kMaxLayoutButtons];
static int gLayoutButtonsCount = 0;
static Str gLayoutParsedFrom;
static Str gOrderParsedFrom;
static bool gLayoutParsed = false;

// 128 should be more than enough
// we use static array so that we don't have to generate
// code for Vec<ToolbarButtonInfo>
constexpr int kMaxCustomButtons = 127;
// +1 to ensure there's always space for WarningsMsgId button
static ToolbarButtonInfo gCustomButtons[kMaxCustomButtons + 1];
static int gCustomButtonsCount = 0;

// Light theme ControlBackgroundColor is white, which is what the old themed
// rebar/toolbar painted. Other themes use their control background.
static Color TbBgColor() {
    return ThemeControlBackgroundColor();
}

Color TbTextColor() {
    if (IsCurrentThemeDefault() && !ThemeColorizeControls()) {
        return SysControlTextColor();
    }
    return ThemeWindowTextColor();
}

static Color TbDisabledColor() {
    if (ThemeUsesHighContrastColors()) return ThemeWindowTextDisabledColor();
    if (IsCurrentThemeDefault() && !ThemeColorizeControls()) {
        return SysDisabledTextColor();
    }
    Color fg = TbTextColor(), bg = TbBgColor();
    return MkRgb((GetRValue(fg) * 3 + GetRValue(bg)) / 4, (GetGValue(fg) * 3 + GetGValue(bg)) / 4,
                 (GetBValue(fg) * 3 + GetBValue(bg)) / 4);
}

static Color TbHoverColor() {
    return ThemeHotBackgroundColor();
}

// A ground a shade off the normal one, for telling two areas of a drop-down
// apart. Well short of the hover highlight, which is 20 units off: this is a
// cue, not something lit up.
static Color TbSubtleBgColor() {
    return AccentColor(TbBgColor(), 8);
}

static Color TbSelectedColor() {
    return ThemeHotBackgroundColor();
}

static Color TbEdgeColor() {
    return ThemeEdgeColor();
}

// Old Win32 toolbar: TBMETRICS.cyPad defaults to 6, then we added UiScalePx(2).
// TB_SETBUTTONSIZE cannot go below image + 2*cyPad, so that was the bar height.
static int ToolbarCyPad() {
    return UiScalePx(4);
}

static int ToolbarOuterPad() {
    return std::max(1, UiScalePx(6) / 5);
}

static int ToolbarSidePad() {
    return std::max(1, UiScalePx(8) / 5);
}

static int ToolbarRowDy(int iconSize) {
    return std::max(iconSize + (2 * ToolbarCyPad()), PlatformFontLineHeight(GetAppFont()) + UiScalePx(4)) +
           2 * ToolbarOuterPad();
}

static bool HasToolbarButtonContent(const ToolbarButtonInfo& tbi) {
    return tbi.icon || tbi.isText || !str::IsEmptyOrWhiteSpace(tbi.svgIcon);
}

static VirtHost* ToolbarHost(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    return tb ? tb->host : nullptr;
}

static VirtCtrl* ToolbarItemAt(MainWindow* win, int idx) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || idx < 0 || idx >= len(tb->items)) {
        return nullptr;
    }
    return tb->items[idx];
}

static VirtCtrl* PdfAnnotationToolbarItemAt(MainWindow* win, int idx) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || idx < 0 || idx >= len(tb->annotationItems)) {
        return nullptr;
    }
    return tb->annotationItems[idx];
}

// Includes disabled items (those are not hit-testable), so a click on a gray
// button is not treated as empty toolbar and does not start a window drag.
VirtCtrl* ToolbarItemFromPoint(MainWindow* win, Point pt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    for (VirtCtrl* button : {tb->overflowButton, tb->annotationOverflowButton, tb->annotationModeButton}) {
        if (button && button->IsVisible() && button->BoundsInWindow().Contains(pt)) return button;
    }
    for (ToolbarLine* line : {tb->mainRow, tb->annotationLine}) {
        if (!line || IsCollapsed(line) || (line == tb->annotationLine && IsCollapsed(tb->annotationRow))) continue;
        for (VirtCtrl* button : {line->previousButton, line->nextButton}) {
            if (button && button->IsVisible() && button->BoundsInWindow().Contains(pt)) return button;
        }
    }
    for (VirtCtrl* button : tb->pinnedItems) {
        if (button && button->IsVisible() && button->BoundsInWindow().Contains(pt)) return button;
    }
    for (VirtCtrl* w : tb->items) {
        if (!w || w->GetVisibility() != Visibility::Visible) {
            continue;
        }
        if (w->BoundsInWindow().Contains(pt)) {
            return w;
        }
    }
    for (VirtCtrl* w : tb->annotationItems) {
        if (!w || w->GetVisibility() != Visibility::Visible) {
            continue;
        }
        if (w->BoundsInWindow().Contains(pt)) {
            return w;
        }
    }
    if (tb->pageTotal && tb->pageTotal->GetVisibility() == Visibility::Visible &&
        tb->pageTotal->BoundsInWindow().Contains(pt)) {
        return tb->pageTotal;
    }
    if (tb->chapterTotal && tb->chapterTotal->GetVisibility() == Visibility::Visible &&
        tb->chapterTotal->BoundsInWindow().Contains(pt)) {
        return tb->chapterTotal;
    }
    return nullptr;
}

static void SetToolbarButtonEnabledByIdx(MainWindow* win, int idx, bool isEnabled) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w || w->IsEnabled() == isEnabled) {
        return;
    }
    w->SetIsEnabled(isEnabled);
    w->Invalidate();
}

static void SetPdfAnnotationButtonToolTipByIdx(MainWindow* win, int idx, Str tip) {
    VirtCtrl* w = PdfAnnotationToolbarItemAt(win, idx);
    if (w) {
        w->SetTooltip(tip);
    }
}

static void SetPdfAnnotationButtonEnabledByIdx(MainWindow* win, int idx, bool isEnabled) {
    VirtCtrl* w = PdfAnnotationToolbarItemAt(win, idx);
    if (!w || w->IsEnabled() == isEnabled) {
        return;
    }
    w->SetIsEnabled(isEnabled);
    w->Invalidate();
}

// true if the row has to be laid out again
static Str ToolbarItemName(int cmd) {
    return cmd == PageInfoId ? StrL("PageInfo") : GetCommandName(cmd);
}

static bool ToolbarItemHidden(int cmd) {
    if (!gSettings || !cmd) return false;
    Str name = ToolbarItemName(cmd);
    if (!len(name)) return false;
    StrVec names;
    Split(&names, gSettings->toolbarHiddenItems, StrL(" "), true);
    for (Str item : names)
        if (str::EqI(item, name)) return true;
    return false;
}

static void SetToolbarItemHidden(int cmd, bool hidden) {
    Str name = ToolbarItemName(cmd);
    if (!gSettings || len(name) == 0) return;
    StrVec names;
    Split(&names, gSettings->toolbarHiddenItems, StrL(" "), true);
    str::Builder saved;
    for (Str item : names) {
        if (str::EqI(item, name)) continue;
        if (len(saved)) saved.Append(StrL(" "));
        saved.Append(item);
    }
    if (hidden) {
        if (len(saved)) saved.Append(StrL(" "));
        saved.Append(name);
    }
    str::ReplaceWithCopy(&gSettings->toolbarHiddenItems, ToStrTemp(saved));
}

static bool SetPdfAnnotationButtonHiddenByIdx(MainWindow* win, int idx, bool isHidden) {
    VirtCtrl* w = PdfAnnotationToolbarItemAt(win, idx);
    if (!w) {
        return false;
    }
    Visibility want = isHidden ? Visibility::Collapse : Visibility::Visible;
    if (w->GetVisibility() == want) {
        return false;
    }
    w->SetVisibility(want);
    return true;
}

// hiding the page box hides the whole group (label + edit + " / N")
static bool SetToolbarButtonHiddenByIdx(MainWindow* win, int idx, bool isHidden) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w) {
        return false;
    }
    Visibility want = isHidden ? Visibility::Collapse : Visibility::Visible;
    if (w->GetVisibility() == want) {
        return false;
    }
    w->SetVisibility(want);
    ToolbarVirt* tb = win->toolbarVirt;
    if (w->id == CmdZoomIn && tb && tb->zoomEdit) tb->zoomEdit->SetVisibility(want);
    if (w->id == PageInfoId && tb) {
        if (tb->pageLabel) {
            tb->pageLabel->SetVisibility(want);
        }
        if (win->pageEdit) {
            win->pageEdit->SetVisibility(want);
        }
        if (tb->pageBox) tb->pageBox->SetVisibility(want);
        if (tb->pageTotal) {
            tb->pageTotal->SetVisibility(want);
        }
        // chapter widgets stay collapsed unless the doc has chapters;
        // UpdateToolbarPageText() narrows this further right after
        if (win->chapterEdit) {
            win->chapterEdit->SetVisibility(want);
        }
        if (tb->chapterTotal) {
            tb->chapterTotal->SetVisibility(want);
        }
    }
    return true;
}

static void SetToolbarButtonCheckedByIdx(MainWindow* win, int idx, bool isChecked) {
    // a custom button with ToolbarText is a VirtButton, which has no
    // checked state (and is not a VirtIconButton)
    auto* ib = AsVirtIconButton(ToolbarItemAt(win, idx));
    if (!ib || ib->isSelected == isChecked) {
        return;
    }
    ib->isSelected = isChecked;
    ib->Invalidate();
}

// Keep separators and the native page box in their slots. New tools remain available.
static void ApplyToolbarOrder(ToolbarButtonInfo* buttons, int count, Str order) {
    Vec<ToolbarButtonInfo> tools;
    Vec<int> slots;
    for (int i = 0; i < count; i++) {
        if (!buttons[i].cmdId || buttons[i].cmdId == PageInfoId || !HasToolbarButtonContent(buttons[i])) continue;
        VecAppend(tools, buttons[i]);
        VecAppend(slots, i);
    }
    Vec<bool> used;
    for (int i = 0; i < len(tools); i++) VecAppend(used, false);
    Vec<ToolbarButtonInfo> sorted;
    StrVec names;
    Split(&names, order, StrL(" "), true);
    for (Str name : names) {
        int cmd = GetCommandIdByName(name);
        for (int i = 0; i < len(tools); i++) {
            if (used[i] || tools[i].cmdId != cmd) continue;
            VecAppend(sorted, tools[i]);
            used[i] = true;
            break;
        }
    }
    for (int i = 0; i < len(tools); i++)
        if (!used[i]) VecAppend(sorted, tools[i]);
    for (int i = 0; i < len(slots); i++) buttons[slots[i]] = sorted[i];
}

static void PopulateAnnotationOrder() {
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) gAnnotationButtons[i] = gPdfAnnotationButtons[i];
    Str order = gSettings->toolbarAnnotationOrder;
    for (Str old : {StrL("CmdCreateAnnotLine"), StrL("CmdCreateAnnotPolyLine"), StrL("CmdCreateAnnotSquare"),
                    StrL("CmdCreateAnnotCircle"), StrL("CmdCreateAnnotPolygon")})
        order = str::ReplaceTemp(order, old, StrL("CmdAnnotationShapes"));
    ApplyToolbarOrder(gAnnotationButtons, kPdfAnnotationButtonsCount, order);
}

// Work out which built-in buttons the toolbar has, and in which order. Empty
// ToolbarCustomLayout (the default) means the standard layout; otherwise the
// setting lists the buttons the user wants: a command name puts that button
// there, `|` a separator, `PageInfo` the page number box, and leaving a button
// out is how you hide it (issue #5095).
static void PopulateToolbarLayout() {
    Str setting = gSettings->toolbarCustomLayout;
    if (gLayoutParsed && str::Eq(setting, gLayoutParsedFrom) && str::Eq(gSettings->toolbarOrder, gOrderParsedFrom)) {
        return;
    }
    str::Free(gLayoutParsedFrom);
    gLayoutParsedFrom = str::Dup(setting);
    str::ReplaceWithCopy(&gOrderParsedFrom, gSettings->toolbarOrder);
    gLayoutParsed = true;
    gLayoutButtonsCount = 0;
    defer {
        ApplyToolbarOrder(gLayoutButtons, gLayoutButtonsCount, gSettings->toolbarOrder);
    };

    auto addButton = [](const ToolbarButtonInfo& tbi) {
        if (gLayoutButtonsCount < kMaxLayoutButtons) {
            gLayoutButtons[gLayoutButtonsCount++] = tbi;
        }
    };
    auto useDefaultLayout = [&addButton]() {
        for (const ToolbarButtonInfo& tbi : gToolbarButtons) {
            if (tbi.cmdId == CmdGoToHomePage) {
                break;
            }
            addButton(tbi);
        }
    };

    if (str::IsEmptyOrWhiteSpace(setting)) {
        useDefaultLayout();
        return;
    }

    // commas and semicolons are a natural way to write a list, so accept them
    TempStr normalized = str::ReplaceTemp(setting, StrL(","), StrL(" "));
    normalized = str::ReplaceTemp(normalized, StrL(";"), StrL(" "));
    StrVec names;
    Split(&names, normalized, StrL(" "), true);
    for (Str name : names) {
        Str tok = name;
        str::TrimWSInPlace(tok, str::TrimOpt::Both);
        if (len(tok) == 0) {
            continue;
        }
        if (str::Eq(tok, StrL("|")) || str::EqI(tok, StrL("Separator"))) {
            addButton({nullptr, 0, {}});
            continue;
        }
        if (str::EqI(tok, StrL("PageInfo"))) {
            addButton({nullptr, PageInfoId, {}});
            continue;
        }
        int cmdId = GetCommandIdByName(tok);
        const ToolbarButtonInfo* found = nullptr;
        for (int i = 0; i < kButtonsCount && cmdId != CmdNone; i++) {
            if (gToolbarButtons[i].cmdId == cmdId) {
                found = &gToolbarButtons[i];
                break;
            }
        }
        if (!found) {
            logf("ToolbarCustomLayout: no built-in toolbar button for '%s'\n", tok);
            continue;
        }
        addButton(*found);
    }
    if (gLayoutButtonsCount == 0) {
        logf("ToolbarCustomLayout: nothing usable in '%s', using the standard layout\n", setting);
        useDefaultLayout();
    }
}

static int TotalButtonsCount() {
    return gLayoutButtonsCount + gCustomButtonsCount;
}

static ToolbarButtonInfo& GetToolbarButtonInfoByIdx(int idx) {
    if (idx < gLayoutButtonsCount) return gLayoutButtons[idx];
    return gCustomButtons[idx - gLayoutButtonsCount];
}

static int OriginalCommandId(int cmdId) {
    CustomCommand* cmd = FindCustomCommand(cmdId);
    return cmd ? cmd->origId : cmdId;
}

void SetToolbarButtonCheckedState(MainWindow* win, int cmdId, bool isChecked) {
    int originalCmdId = OriginalCommandId(cmdId);
    int n = TotalButtonsCount();
    for (int i = 0; i < n; i++) {
        const ToolbarButtonInfo& tbi = GetToolbarButtonInfoByIdx(i);
        if (OriginalCommandId(tbi.cmdId) == originalCmdId) {
            SetToolbarButtonCheckedByIdx(win, i, isChecked);
        }
    }
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        if (gAnnotationButtons[i].cmdId != originalCmdId) continue;
        auto* button = AsVirtIconButton(PdfAnnotationToolbarItemAt(win, i));
        if (!button || button->isSelected == isChecked) continue;
        button->isSelected = isChecked;
        button->Invalidate();
    }
}

// some commands are only avialble in certain contexts
// we remove toolbar buttons for un-availalbe commands
static bool IsCmdAvailable(MainWindow* win, int cmdId, AppCommandCtx* ctx) {
    switch (cmdId) {
        case CmdZoomFitWidthAndContinuous:
        case CmdZoomFitPageAndSinglePage:
        case CmdRotateLeft:
        case CmdRotateRight:
            return !IsBrowserDocController(win->ctrl);
        case CmdFindFirst:
            // CHM has its own (WebView2/IE) find bar even though NeedsFindUI()
            // is false for it; show the Search button so it's reachable
            return NeedsFindUI(win) || IsBrowserDocController(win->ctrl);
        case CmdFindNext:
        case CmdFindPrev:
        case CmdFindToggleMatchCase:
        case CmdFindToggleMatchWholeWord:
            return NeedsFindUI(win);
        case CmdToggleReadAloud:
            // opt-in: the button and its drop-down only show if asked for
            return gSettings->toolbarShowReadAloud;
        case PageInfoId:
            return true;
    }
    // Toolbar buttons stay visible (but disabled) when no document is open, so
    // decide visibility as if a document were loaded; otherwise the no-document
    // gate in GetCommandVisibility would remove them. Document-type-specific
    // removals (e.g. for CHM/image collections) still apply when a real document
    // is loaded, and the enabled state is handled separately in IsCmdEnabled.
    bool savedLoaded = ctx->isDocLoaded;
    ctx->isDocLoaded = true;
    bool remove, disable;
    GetCommandIdState(ctx, cmdId, &remove, &disable);
    ctx->isDocLoaded = savedLoaded;
    return !remove;
}

static bool IsCmdEnabled(MainWindow* win, int cmdId, AppCommandCtx* ctx) {
    switch (cmdId) {
        case CmdCreateAnnotUnderline:
        case CmdCreateAnnotStrikeOut:
            return ctx->isDocLoaded && ctx->supportsAnnots;
        case CmdNextTab:
        case CmdPrevTab:
        case CmdNextTabSmart:
        case CmdPrevTabSmart:
            return SettingsUseTabs();
        case PageInfoId:
            return true;
    }

    bool remove, disable;
    GetCommandIdState(ctx, cmdId, &remove, &disable);
    if (remove || disable) {
        return false;
    }
    switch (cmdId) {
        case CmdOpenFile:
        case CmdOpenFileNoHistory:
            if (!CanAccessDisk()) {
                return false;
            }
            break;
        case CmdPrint:
            if (!HasPermission(Perm::PrinterAccess)) {
                return false;
            }
            break;
    }

    // if no file is open, only enable buttons for commands that don't require a document
    // (custom toolbar buttons use a custom command id, the original command decides)
    // https://github.com/sumatrapdfreader/sumatrapdf/issues/5657
    if (!win->IsDocLoaded()) {
        return CmdWorksWithoutDocument(OriginalCommandId(cmdId));
    }

    switch (cmdId) {
        case CmdOpenFile:
        case CmdOpenFileNoHistory:
            // opening different files isn't allowed in plugin mode
            return !gPluginMode;

#ifndef DISABLE_DOCUMENT_RESTRICTIONS
        case CmdPrint:
            return !win->AsFixed() || win->AsFixed()->GetEngine()->AllowsPrinting();
#endif

        case CmdFindFirst:
            return NeedsFindUI(win) || IsBrowserDocController(win->ctrl);

        case CmdFindNext:
        case CmdFindPrev: {
            // Need non-empty find text (findEdit is the active bar or floating window edit).
            if (CbGetTextLen(win->findEdit) == 0) {
                return false;
            }
            // When we already know there are zero matches, disable next/prev.
            // Unknown count (scan pending / not started) still allows searching.
            if (win->ctrl && win->ctrl->CanFindInPage()) {
                if (win->browserFindTotal == 0) {
                    return false;
                }
                return true;
            }
            if (win->findCountValid && len(win->findCountPositions) == 0) {
                return false;
            }
            return true;
        }

        case CmdGoToNextPage:
            return win->ctrl->CurrentPageNo() < win->ctrl->PageCount();
        case CmdGoToPrevPage:
            return win->ctrl->CurrentPageNo() > 1;

        case CmdNavigateBack:
            return win->ctrl->CanNavigate(-1);
        case CmdNavigateForward:
            return win->ctrl->CanNavigate(1);

        default:
            return true;
    }
}

static TempStr ToolbarTipTemp(int cmdId, Str tip, bool translate) {
    TempStr s = translate ? trans::GetTranslation(tip) : TempStr(tip);
    TempStr accelStr = AppendAccelKeyToMenuStringTemp({}, cmdId);
    if (accelStr) {
        Str accel = accelStr.len > 1 ? Str(accelStr.s + 1, accelStr.len - 1) : accelStr;
        s = str::JoinTemp(s, fmt(" (%s)", accel));
    }
    return s;
}

void UpdateToolbarButtonsToolTipsForWindow(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    for (int i = 0; i < gLayoutButtonsCount; i++) {
        const ToolbarButtonInfo& bi = gLayoutButtons[i];
        if (len(bi.toolTip) == 0 || bi.isText) {
            continue;
        }
        VirtCtrl* w = ToolbarItemAt(win, i);
        if (w) {
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, true));
        }
    }
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        const ToolbarButtonInfo& bi = gAnnotationButtons[i];
        if (len(bi.toolTip) == 0) {
            continue;
        }
        VirtCtrl* w = PdfAnnotationToolbarItemAt(win, i);
        if (w) {
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, true));
        }
    }
}

static void SetToolbarButtonImageByIdx(MainWindow* win, int idx, const char* icon) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w) {
        return;
    }
    auto* ib = AsVirtIconButton(w);
    if (!ib) {
        return;
    }
    ToolbarVirt* tb = win->toolbarVirt;
    int sz = tb ? tb->iconSize : UiScalePx(gSettings->toolbarSize);
    Pixmap* px = GetCachedPixmapForSvg(Str(icon), sz, sz, TbTextColor());
    Pixmap* pxOff = GetCachedPixmapForSvg(Str(icon), sz, sz, TbDisabledColor());
    if (ib->pixmap == px && ib->pixmapDisabled == pxOff) {
        return;
    }
    ib->pixmap = px;
    ib->pixmapDisabled = pxOff;
    ib->Invalidate();
}

static void SetToolbarButtonToolTipByIdx(MainWindow* win, int idx, int cmdId, Str s) {
    VirtCtrl* w = ToolbarItemAt(win, idx);
    if (!w) {
        return;
    }
    w->SetTooltip(ToolbarTipTemp(cmdId, s, false));
}

static void SetPdfAnnotationsToolbarVisible(MainWindow* win, bool visible) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || len(tb->annotationGroups) == 0) {
        return;
    }
    Visibility want = visible ? Visibility::Visible : Visibility::Collapse;
    if (tb->annotationExpanded == visible) {
        return;
    }
    tb->mainRow->Restore();
    tb->annotationExpanded = visible;
    for (auto* group : tb->annotationGroups) group->SetVisibility(want);
    SetToolbarButtonCheckedState(win, CmdToggleEditPDF, visible);
    tb->host->Relayout();
    tb->host->Invalidate(true);
    if (visible) {
        StartLoadingAnnotationsForUi(win->CurrentTab());
        RefreshAnnotFilterAnnotations(win);
    }
    ScheduleUiUpdate(win, kUiForceRelayout | kUiToolbarDirty);
}

// TODO: this is called too often
// TODO: also set checked state instead of calling SetToolbarButtonCheckedState() all over
static void UpdateZoomFieldText(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    Edit* zoom = tb ? tb->zoomEdit : nullptr;
    if (!zoom) return;
    zoom->SetIsEnabled(win->IsDocLoaded());
    if (GetFocus() != zoom->hwnd && win->IsDocLoaded())
        zoom->SetText(win->annotationMode && tb->vertical ? CompactZoomText(win->ctrl->GetZoomVirtual(true))
                                                          : fmt("%.1f%%", win->ctrl->GetZoomVirtual(true)));
}

void ToolbarUpdateStateForWindow(MainWindow* win, bool setButtonsVisibility) {
    if (win->toolbarVirt && win->toolbarVirt->mainRow) win->toolbarVirt->mainRow->Restore();
    if (win->toolbarVirt && win->toolbarVirt->annotationLine) win->toolbarVirt->annotationLine->Restore();
    int n = TotalButtonsCount();
    bool visibilityChanged = false;
    // One command ctx for the whole pass. Building it per button used to call
    // HasToc() (and page hit-testing) once per toolbar item during load.
    auto* ctx = NewBuildMenuCtx(win->CurrentTab(), Point{0, 0});
    AutoCall delCtx(DeleteBuildMenuCtx, ctx);
    for (int i = 0; i < n; i++) {
        auto& tb = GetToolbarButtonInfoByIdx(i);
        int cmdId = tb.cmdId;
        // cmdId 0 is a separator; GetCommandVisibility treats 0 as Hide, but
        // separators are always drawn. Which ones to drop is decided below,
        // by position, not by command availability.
        if (setButtonsVisibility && cmdId != WarningMsgId && cmdId != 0) {
            bool hide = (cmdId == CmdAnnotationMode && win->annotationMode) || !IsCmdAvailable(win, cmdId, ctx) ||
                        ToolbarItemHidden(cmdId);
            visibilityChanged |= SetToolbarButtonHiddenByIdx(win, i, hide);
        }
        if (!HasToolbarButtonContent(tb)) {
            continue;
        }
        bool isEnabled = IsCmdEnabled(win, cmdId, ctx);
        SetToolbarButtonEnabledByIdx(win, i, isEnabled);

        if (cmdId == CmdToggleReadAloud || cmdId == CmdPauseReadAloud) {
            bool speaking = TtsIsSpeaking();
            SetToolbarButtonImageByIdx(win, i, speaking ? gIconPauseSpeaking : gIconSpeak);
            // tooltip reflects what clicking the button will do
            Str tip = Tr("Read Aloud");
            if (speaking) {
                tip = Tr("Pause Reading");
            } else if (CanContinueReadAloud(win->CurrentTab())) {
                tip = Tr("Continue Reading");
            }
            SetToolbarButtonToolTipByIdx(win, i, cmdId, tip);
        }
    }

    if (win->toolbarVirt) {
        auto* presets = gSettings->pinnedAnnotationTools;
        for (int i = 0; presets && i < len(win->toolbarVirt->pinnedItems) && i < len(*presets); i++)
            win->toolbarVirt->pinnedItems[i]->SetIsEnabled(
                IsCmdEnabled(win, PinnedToolCommand((*presets)[i]->tool), ctx));
        UpdateZoomFieldText(win);
    }
    SetToolbarButtonCheckedState(win, CmdCreateAnnotInk, IsPlacingInkAnnotation(win) && win->inkEraseMode == 0);
    SetToolbarButtonCheckedState(win, CmdInkEraser, win->inkEraseMode == 1);
    SetToolbarButtonCheckedState(win, CmdToggleLaserPointer, IsLaserPointerActive(win));
    SetToolbarButtonCheckedState(win, CmdAnnotationMode, win->annotationMode);
    if (win->toolbarVirt && win->toolbarVirt->annotationModeButton) {
        auto* mode = win->toolbarVirt->annotationModeButton;
        if (win->annotationMode)
            mode->SetVisibility(Visibility::Visible);
        else if (VecFind(win->toolbarVirt->items, (VirtCtrl*)mode) < 0)
            mode->SetVisibility(Visibility::Collapse);
        mode->SetIsEnabled(win->annotationMode || win->IsDocLoaded());
        mode->isSelected = win->annotationMode;
    }
    SetToolbarButtonCheckedState(win, CmdHandTool, win->handTool);
    SetToolbarButtonCheckedState(win, CmdTextSelectTool, win->textSelectTool);
    SetToolbarButtonCheckedState(win, CmdAnnotationLasso, win->annotationLasso.active);

    bool showPdfAnnotationsToolbar = win->pdfAnnotationsToolbarEnabled && ctx->isPdf && ctx->supportsAnnots;
    SetPdfAnnotationsToolbarVisible(win, showPdfAnnotationsToolbar);
    // a placement mode (ink, shape, highlighter...) owns the page until it ends
    bool annotButtonsEnabled = showPdfAnnotationsToolbar;
    bool annotVisibilityChanged = false;
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        const ToolbarButtonInfo& bi = gAnnotationButtons[i];
        if (!HasToolbarButtonContent(bi)) {
            continue;
        }
        CommandVisibility v = GetCommandVisibility(bi.cmdId, *ctx, CommandSurface::Toolbar);
        bool remove = CommandShouldRemove(v) || ToolbarItemHidden(bi.cmdId);
        annotVisibilityChanged |= SetPdfAnnotationButtonHiddenByIdx(win, i, remove);
        SetPdfAnnotationButtonEnabledByIdx(win, i, annotButtonsEnabled && !CommandShouldDisable(v) && !remove);
        if (bi.cmdId == CmdSaveAnnotations) {
            // name the file it writes to, like the annotation list's Save button
            WindowTab* tab = win->CurrentTab();
            TempStr base = tab ? path::GetBaseNameTemp(tab->filePath) : TempStr{};
            Str tip = Tr("Save changes to existing PDF");
            if (len(base) > 0) {
                tip = fmt(Tr("Save changes to %s").s, base);
            }
            SetPdfAnnotationButtonToolTipByIdx(win, i, ToolbarTipTemp(bi.cmdId, tip, false));
        }
    }

    if (setButtonsVisibility) {
        // drop a separator that would sit next to another, or at either end
        // (Read Aloud is hidden by default, which would otherwise leave ||)
        bool prevVisibleNonSep = false;
        int lastSep = -1;
        for (int i = 0; i < n; i++) {
            const ToolbarButtonInfo& bi = GetToolbarButtonInfoByIdx(i);
            VirtCtrl* w = ToolbarItemAt(win, i);
            if (!w) {
                continue;
            }
            if (bi.cmdId == 0) {
                bool hide = !prevVisibleNonSep;
                visibilityChanged |= SetToolbarButtonHiddenByIdx(win, i, hide);
                prevVisibleNonSep = false;
                if (!hide) {
                    lastSep = i;
                }
                continue;
            }
            // the page box counts as visible content: a separator right after
            // it is not a leading one (ToolbarCustomLayout = PageInfo | ...)
            if (w->GetVisibility() == Visibility::Visible) {
                prevVisibleNonSep = true;
                lastSep = -1;
            }
        }
        if (lastSep >= 0) {
            visibilityChanged |= SetToolbarButtonHiddenByIdx(win, lastSep, true);
        }
    }

    if (visibilityChanged || annotVisibilityChanged || (win->toolbarVirt && win->toolbarVirt->mainRow)) {
        VirtHost* host = ToolbarHost(win);
        if (host) {
            if (host->vroot) {
                host->vroot->RequestLayout();
            }
            host->Relayout();
            host->Invalidate(true);
        }
    }

    // reposition the floating find bar over the search icon (and hide it if the
    // current document doesn't support find) when toolbar buttons change
    if (setButtonsVisibility) {
        UpdateToolbarFindText(win);
    }

    // update dirty (unsaved annotations) flag and tooltip on each tab
    if (win->tabsCtrl) {
        int nTabs = win->TabCount();
        for (int i = 0; i < nTabs; i++) {
            WindowTab* tab = win->GetTab(i);
            bool dirty = false;
            if (tab && tab->AsFixed()) {
                dirty = EngineHasUnsavedAnnotations(tab->AsFixed()->GetEngine());
            }
            // update tooltip before SetTabDirty (which rebuilds tooltips via LayoutTabs).
            // Must use MakeTabTooltipTemp (path+size); path-only overwrote size here.
            TabInfo* ti = win->tabsCtrl->GetTab(i);
            if (ti && tab && tab->filePath && (ti->isDirty != dirty || len(ti->tooltip) == 0)) {
                TempStr tooltip = MakeTabTooltipTemp(tab->filePath, dirty);
                str::ReplaceWithCopy(&ti->tooltip, tooltip);
            }
            win->tabsCtrl->SetTabDirty(i, dirty);
        }
    }
}

void SetToolbarButtonEnableState(MainWindow* win, int cmdId, bool isEnabled) {
    int originalCmdId = OriginalCommandId(cmdId);
    int n = TotalButtonsCount();
    for (int i = 0; i < n; i++) {
        const ToolbarButtonInfo& tbi = GetToolbarButtonInfoByIdx(i);
        if (OriginalCommandId(tbi.cmdId) == originalCmdId) {
            SetToolbarButtonEnabledByIdx(win, i, isEnabled);
        }
    }
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        if (gAnnotationButtons[i].cmdId == originalCmdId) {
            SetPdfAnnotationButtonEnabledByIdx(win, i, isEnabled);
        }
    }
}

static void SetPdfAnnotationsToolbarEnabled(MainWindow* win, bool enabled) {
    if (!win) {
        return;
    }
    AppCommandCtx ctx = NewAppCommandCtx(win);
    if (!ctx.isPdf || !ctx.supportsAnnots) {
        return;
    }
    if (win->pdfAnnotationsToolbarEnabled == enabled) {
        return;
    }
    if (win->pdfAnnotationsToolbarEnabled) {
        FinishInkAnnotationPlacement(win);
        // a half-placed line / shape / stamp is editing UI too: its notification
        // and cross cursor would outlive the mode it belongs to
        CancelAnnotationPlacement(win);
    }
    win->pdfAnnotationsToolbarEnabled = enabled;
    ToolbarUpdateStateForWindow(win, true);
    if (enabled) {
        RemoveNotificationsForGroup(win->hwndCanvas, kNotifAnnotation);
        UpdateAnnotationHoverOverlay(win);
    } else {
        // leaving the mode leaves no editing UI behind: without this the
        // selection marker and its resize handles stay painted on the page
        WindowTab* tab = win->CurrentTab();
        if (tab && tab->selectedAnnotation) {
            SetSelectedAnnotation(tab, nullptr);
        }
        HideAnnotationHoverOverlay(win);
        HideAnnotEditToolbar(win);
    }
    ScheduleRepaint(win, 0);
}

void TogglePdfAnnotationsToolbar(MainWindow* win) {
    if (!win) {
        return;
    }
    SetPdfAnnotationsToolbarEnabled(win, !win->pdfAnnotationsToolbarEnabled);
}

void EnablePdfAnnotationsToolbar(MainWindow* win) {
    SetPdfAnnotationsToolbarEnabled(win, true);
}

// toolbar mode for this window: Fullscreen.Toolbar in fullscreen, else Toolbar
int ToolbarPositionForWindow(MainWindow* win) {
    return win->annotationMode ? win->annotationToolbarPosition : ToolbarPositionFromPrefs();
}

static int ToolbarModeForWindow(MainWindow* win) {
    if (win->annotationMode) return kToolbarShow;
    if (win->isFullScreen) {
        return FullscreenToolbarModeFromPrefs();
    }
    return ToolbarModeFromPrefs();
}

bool ShouldShowToolbar(MainWindow* win) {
    if (win->presentation || win->isQuickLook) {
        return false;
    }
    int mode = ToolbarModeForWindow(win);
    return mode == kToolbarShow;
}

bool ShouldOverlayToolbar(MainWindow* win) {
    if (win->presentation || win->isQuickLook) {
        return false;
    }
    if (ToolbarModeForWindow(win) != kToolbarOverlay) {
        return false;
    }
    // don't float the overlay toolbar over the home / about page (only the
    // pinned "show" mode shows a toolbar there)
    if (win->IsCurrentTabAbout()) {
        return false;
    }
    return true;
}

// natural width of the toolbar content (buttons + page box); the find bar
// floats separately so the page-total label is the rightmost element
static int ToolbarNaturalWidth(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    VirtHost* host = ToolbarHost(win);
    if (!host || !host->layout) {
        return 0;
    }
    tb->mainRow->Restore();
    int dx = tb->mainRow->HBox::MinIntrinsicWidth(tb->rowDy);
    host->Relayout();
    if (dx <= 0) {
        dx = tb->rowDy * 8;
    }
    return dx + UiScalePx(12);
}

// when the overlay toolbar sits at the bottom, lift it above the horizontal
// scrollbar so it doesn't cover it. The height is reserved even when the
// scrollbar isn't currently visible, so the toolbar's position is stable.
static int OverlayToolbarBottomScrollbarOffset() {
    if (ScrollbarsAreHidden()) {
        return 0;
    }
    if (ScrollbarsUseOverlay()) {
        // smart/overlay: the thick overlay scrollbar height (see OverlayScrollbarCreate)
        return UiScalePx(16);
    }
    return UiHScrollbarDy();
}

// rectangle (frame-client coords) the overlay toolbar occupies when shown
static Rect OverlayToolbarRect(MainWindow* win) {
    Rect canvas = ToolbarCanvasRectInFrame(win);
    int natW = ToolbarNaturalWidth(win);
    if (natW <= 0 || natW > canvas.dx) {
        natW = canvas.dx;
    }
    int h = ToolbarHost(win)->ScreenRect().dy;
    int x = canvas.x + ((canvas.dx - natW) / 2);
    int y = canvas.y;
    if (ToolbarAtBottom()) {
        y = canvas.y + canvas.dy - h - OverlayToolbarBottomScrollbarOffset();
    }
    return {x, y, natW, h};
}

// position/show the floating overlay toolbar; called on relayout and mouse move
void PositionOverlayToolbar(MainWindow* win) {
    VirtHost* host = ToolbarHost(win);
    if (!win->isToolbarOverlay || !host) {
        return;
    }
    Rect r = OverlayToolbarRect(win);
    host->SetPos(r, win->toolbarOverlayShown);
    if (!win->toolbarOverlayShown) {
        ToolbarRepaintUncovered(win, r);
    }
}

// whether the cursor is currently in the reveal band or over the toolbar
static bool OverlayToolbarShouldShowForCursor(MainWindow* win) {
    Point pt = UiCursorScreenPos();
    Point ptFrame = ToolbarScreenToFrame(win, pt);

    Rect tb = OverlayToolbarRect(win);
    // reveal band: spans the full canvas width so the toolbar also appears when
    // the mouse is to the left or right of it, and extends a bit past the
    // toolbar (toward the page) so it shows before the cursor reaches it
    Rect canvas = ToolbarCanvasRectInFrame(win);
    int my = UiScalePx(16);
    int bandY = ToolbarAtBottom() ? (tb.y - my) : tb.y;
    Rect band(canvas.x, bandY, canvas.dx, tb.dy + my);
    bool inBand = band.Contains(Point(ptFrame.x, ptFrame.y));

    // also keep shown while the cursor is over the toolbar window itself
    return inBand || ToolbarHost(win)->ContainsScreenPoint(pt);
}

// the overlay toolbar must not vanish while it owns the keyboard focus (e.g.
// the user is typing a page number into the page box after Ctrl+G)
static bool OverlayToolbarHasFocus(MainWindow* win) {
    VirtHost* host = ToolbarHost(win);
    return host && host->HasFocus();
}

static void CancelOverlayHide(MainWindow* win) {
    if (win->toolbarOverlayHidePending) {
        ToolbarHost(win)->KillTimer(kHideOverlayToolbarTimerId);
        win->toolbarOverlayHidePending = false;
    }
}

static void ScheduleOverlayHide(MainWindow* win) {
    if (win->toolbarOverlayHidePending) {
        return; // already scheduled; don't keep pushing it out on every move
    }
    win->toolbarOverlayHidePending = true;
    ToolbarHost(win)->SetTimer(kHideOverlayToolbarTimerId, kDelayToolbarHide);
}

static void SetOverlayShown(MainWindow* win, bool shown) {
    if (shown == win->toolbarOverlayShown) {
        return;
    }
    win->toolbarOverlayShown = shown;
    PositionOverlayToolbar(win);
}

// re-evaluate overlay toolbar visibility based on the cursor's screen position
void UpdateOverlayToolbarForMouse(MainWindow* win) {
    if (!win->isToolbarOverlay || !ToolbarHost(win)) {
        return;
    }
    bool show = OverlayToolbarShouldShowForCursor(win) || OverlayToolbarHasFocus(win);
    if (show) {
        CancelOverlayHide(win);
        SetOverlayShown(win, true);
    } else if (win->toolbarOverlayShown) {
        // don't hide immediately; give the user kDelayToolbarHide to come back
        ScheduleOverlayHide(win);
    }
}

// reveal the overlay toolbar right now, without waiting for the cursor to enter
// the reveal band. Used by commands that drive the toolbar from the keyboard
// (Ctrl+G): the toolbar stays up while it has the focus and auto-hides once the
// focus and the cursor are away from it.
void RevealOverlayToolbar(MainWindow* win) {
    if (!win->isToolbarOverlay || !ToolbarHost(win)) {
        return;
    }
    CancelOverlayHide(win);
    SetOverlayShown(win, true);
}

// the delayed-hide timer fired on the toolbar's own host
static void OnHoverDropdownTimer(MainWindow* win, int timerId);
static void OnToolbarDragTimer(MainWindow*);
static void CancelToolbarDrag(MainWindow*);

static void OnToolbarTimer(MainWindow* win, int timerId) {
    if (timerId == kToolbarDragScrollTimerId) {
        OnToolbarDragTimer(win);
        return;
    }
    if (timerId == kOpenHoverDropdownTimerId || timerId == kCloseHoverDropdownTimerId) {
        OnHoverDropdownTimer(win, timerId);
        return;
    }
    if (timerId != kHideOverlayToolbarTimerId) {
        return;
    }
    win->toolbarOverlayHidePending = false;
    ToolbarHost(win)->KillTimer(kHideOverlayToolbarTimerId);
    if (!win->isToolbarOverlay) {
        return;
    }
    // if the cursor came back near the top while the timer was pending, keep
    // the toolbar shown; otherwise hide it now
    if (OverlayToolbarShouldShowForCursor(win) || OverlayToolbarHasFocus(win)) {
        SetOverlayShown(win, true);
    } else {
        SetOverlayShown(win, false);
    }
}

void ShowOrHideToolbar(MainWindow* win) {
    bool show = ShouldShowToolbar(win);
    bool overlay = ShouldOverlayToolbar(win);
    if (show == win->isToolbarVisible && overlay == win->isToolbarOverlay) {
        return;
    }
    bool enteredOverlay = overlay && !win->isToolbarOverlay;
    win->isToolbarVisible = show;
    win->isToolbarOverlay = overlay;
    if (!overlay) {
        CancelOverlayHide(win);
        win->toolbarOverlayShown = false;
    }
    if (enteredOverlay) {
        // reveal immediately on entering overlay mode (e.g. via F8) so the
        // change is visible; it auto-hides after kDelayToolbarHide
        win->toolbarOverlayShown = true;
    }
    if (!show && !overlay) {
        // Move the focus out of the toolbar
        if ((win->findEdit && win->findEdit->IsFocused()) || (win->pageEdit && win->pageEdit->IsFocused()) ||
            (win->chapterEdit && win->chapterEdit->IsFocused())) {
            ToolbarFocusFrame(win);
        }
        if (win->hwndToolbar) {
            ShowWindow(win->hwndToolbar, SW_HIDE);
        }
    }
    // overlay <-> hide does not flip isToolbarVisible, so RelayoutFrame would
    // skip without this (sidebar stays at the overlay y, toolbar HWND stays)
    ScheduleUiUpdate(win, kUiForceRelayout | kUiRelayout);
    if (enteredOverlay) {
        ScheduleOverlayHide(win);
    }
}

void UpdateFindbox(MainWindow* win) {
    VirtHost* host = ToolbarHost(win);
    if (host) {
        host->Invalidate(true);
        if (ToolbarFrameIsVisible(win)) {
            host->Repaint();
        }
    }
    ToolbarUpdateFindEditCursor(win);
}

// the find UI is now a floating Chrome-style bar (see FindBar.cpp). When the
// toolbar moves/resizes we keep the bar centered over the search icon.
void UpdateToolbarFindText(MainWindow* win) {
    FindBarReposition(win);
}

static void UpdateZoomHoverDropdown(MainWindow* win);

void UpdateToolbarState(MainWindow* win) {
    if (!win->IsDocLoaded()) {
        return;
    }
    // the zoom buttons' strip may be up: the zoom just moved under it
    UpdateZoomHoverDropdown(win);
    DisplayMode dm = win->ctrl->GetDisplayMode();
    float zoomVirtual = win->ctrl->GetZoomVirtual();
    {
        bool isChecked = dm == DisplayMode::Continuous && zoomVirtual == kZoomFitWidth;
        SetToolbarButtonCheckedState(win, CmdZoomFitWidthAndContinuous, isChecked);
    }
    {
        bool isChecked = dm == DisplayMode::SinglePage && zoomVirtual == kZoomFitPage;
        SetToolbarButtonCheckedState(win, CmdZoomFitPageAndSinglePage, isChecked);
        if (!isChecked) {
            win->CurrentTab()->prevZoomVirtual = kInvalidZoom;
        }
    }
}

void UpdateToolbarPageText(MainWindow* win, int pageCount, bool updateOnly) {
    VirtHost* host = ToolbarHost(win);
    if (!host) {
        return;
    }
    ToolbarVirt* tb = win->toolbarVirt;
    bool hadOverflow = len(tb->overflowItems) > 0;
    if (tb->mainRow) tb->mainRow->Restore();
    if (!tb->pageTotal) {
        return;
    }

    bool hasChapters = ShowChapterUi(win->ctrl);
    if (tb->pageBox) tb->pageBox->UpdateTotal();
    if (tb->pageLabel) {
        bool compact = !hasChapters && str::IsEmptyOrWhiteSpace(gSettings->toolbarCustomLayout);
        tb->pageLabel->SetText(compact ? Str{} : hasChapters ? Tr("Chapter:") : Tr("Page:"));
        tb->pageLabel->padding =
            compact ? Insets{} : Insets{0, UiScalePx(kTextPaddingRight + kButtonSpacingX), 0, UiScalePx(4)};
    }
    Visibility chapterVis = hasChapters && !ToolbarItemHidden(PageInfoId) ? Visibility::Visible : Visibility::Collapse;
    bool chapterVisChanged = false;
    if (win->chapterEdit && win->chapterEdit->GetVisibility() != chapterVis) {
        win->chapterEdit->SetVisibility(chapterVis);
        chapterVisChanged = true;
    }
    if (tb->chapterTotal && tb->chapterTotal->GetVisibility() != chapterVis) {
        tb->chapterTotal->SetVisibility(chapterVis);
        chapterVisChanged = true;
    }
    if (tb->pageLabel2 && tb->pageLabel2->GetVisibility() != chapterVis) {
        tb->pageLabel2->SetVisibility(chapterVis);
        chapterVisChanged = true;
    }
    if (chapterVisChanged) {
        host->Relayout();
    }

    TempStr txt;
    if (-1 == pageCount || !pageCount) {
        txt = StrL(" ");
    } else if (hasChapters) {
        int chapter = win->ctrl->CurrentLocation().chapter;
        txt = fmt(" / %d", win->ctrl->ChapterPageCount(chapter));
        if (tb->chapterTotal) {
            tb->chapterTotal->SetText(fmt(" / %d", win->ctrl->ChapterCount()));
        }
    } else if (!win->ctrl || !win->ctrl->HasPageLabels()) {
        txt = fmt(" / %d", pageCount);
    } else {
        int logical = pageCount;
        DisplayModel* dm = win->ctrl->AsFixed();
        if (dm) {
            logical = dm->LogicalPageCount();
        }
        if (logical > 0 && logical != pageCount) {
            txt = fmt(" / %d (%d / %d)", logical, win->ctrl->CurrentPageNo(), pageCount);
        } else {
            txt = fmt("%d / %d", win->ctrl->CurrentPageNo(), pageCount);
        }
    }
    if (!hadOverflow && updateOnly && tb->pageTotal->s && txt && str::Eq(tb->pageTotal->s, txt)) {
        return;
    }
    tb->pageTotal->SetText(txt);
    host->Relayout();
    host->Invalidate(true);
}

static TempStr ShortcutToolbarToolTipTemp(Shortcut* shortcut) {
    if (!str::IsEmptyOrWhiteSpace(shortcut->name)) {
        return shortcut->name;
    }
    CustomCommand* cmd = FindCustomCommand(shortcut->cmdId);
    if (cmd && cmd->name) {
        return cmd->name;
    }
    int origId = cmd ? cmd->origId : shortcut->cmdId;
    if (origId > 0 && origId < CmdLast) {
        Str desc = GetCommandDescription(origId);
        if (desc) {
            return desc;
        }
    }
    return shortcut->cmd;
}

static TempStr CustomCommandToolbarToolTipTemp(CustomCommand* cmd, Str fallback) {
    if (cmd && !str::IsEmptyOrWhiteSpace(cmd->name)) {
        return cmd->name;
    }
    if (!str::IsEmptyOrWhiteSpace(fallback)) {
        return fallback;
    }
    return StrL("External Viewer");
}

static void PopulateCustomToolbarButtons() {
    gCustomButtonsCount = 0;
    for (Shortcut* shortcut : *gSettings->shortcuts) {
        if (gCustomButtonsCount >= kMaxCustomButtons) {
            break;
        }
        if (!str::IsEmptyOrWhiteSpace(shortcut->toolbarSvgIcon)) {
            ToolbarButtonInfo tbi;
            tbi.cmdId = shortcut->cmdId;
            tbi.svgIcon = shortcut->toolbarSvgIcon;
            tbi.toolTip = ShortcutToolbarToolTipTemp(shortcut);
            gCustomButtons[gCustomButtonsCount++] = tbi;
            continue;
        }
        if (!str::IsEmptyOrWhiteSpace(shortcut->toolbarText)) {
            ToolbarButtonInfo tbi;
            tbi.cmdId = shortcut->cmdId;
            tbi.toolTip = shortcut->toolbarText;
            tbi.isText = true;
            gCustomButtons[gCustomButtonsCount++] = tbi;
        }
    }

    // add toolbar buttons from custom commands with toolbar settings (e.g. ExternalViewers).
    // gFirstCustomCommand is a prepend-only list, so walking it directly yields
    // the commands in reverse creation order and the buttons would show up in
    // the reverse of the order the user listed them in (#5869)
    Vec<CustomCommand*> customCmds;
    for (auto* cc = gFirstCustomCommand; cc; cc = cc->next) {
        VecAppend(customCmds, cc);
    }
    VecReverse(customCmds);
    for (CustomCommand* cc : customCmds) {
        if (gCustomButtonsCount >= kMaxCustomButtons) {
            break;
        }
        Str svgIcon = GetCommandStringArg(cc, kCmdArgToolbarSvgIcon, {});
        Str tbText = GetCommandStringArg(cc, kCmdArgToolbarText, {});
        if (!str::IsEmptyOrWhiteSpace(svgIcon)) {
            ToolbarButtonInfo tbi;
            tbi.cmdId = cc->id;
            tbi.svgIcon = svgIcon;
            tbi.toolTip = CustomCommandToolbarToolTipTemp(cc, tbText);
            gCustomButtons[gCustomButtonsCount++] = tbi;
            continue;
        }
        if (str::IsEmptyOrWhiteSpace(tbText)) {
            continue;
        }
        ToolbarButtonInfo tbi;
        tbi.cmdId = cc->id;
        tbi.toolTip = tbText;
        tbi.isText = true;
        gCustomButtons[gCustomButtonsCount++] = tbi;
    }
}

int ToolbarIconSize(int dpi) {
    if (dpi <= 0) dpi = DpiGet();
    return RoundUp(UiScalePxForDpi(dpi, gSettings->toolbarSize), 4);
}

static void ApplyToolbarItemColors(VirtCtrl* w) {
    Color hover = TbHoverColor();
    Color sel = TbSelectedColor();
    if (auto* ib = AsVirtIconButton(w)) {
        if (w->id == CmdThemeLight) ib->isSelected = IsLightColor(ThemeWindowBackgroundColor());
        if (w->id == CmdThemeDark) ib->isSelected = !IsLightColor(ThemeWindowBackgroundColor());
        if (w->id == CmdInvertColors) ib->isSelected = GetInvertPageColors();
        ib->cornerRadius = UiScalePx(8);
        ib->backgroundColor = w->id == CmdOpenFile ? ThemeBrandColor() : kColorTransparent;
        if (w->id == CmdOpenFile) {
            hover = AccentColor(ThemeBrandColor(), 12);
        }
        ib->SetColor(kColIconBtnBgHover, hover);
        ib->SetColor(kColIconBtnBgSelected, sel);
        ib->SetColor(kColIconBtnChevron, w->id == CmdOpenFile ? ThemeBrandTextColor() : TbTextColor());
        ib->SetColor(kColIconBtnChevronDisabled, TbDisabledColor());
        return;
    }
    if (auto* b = AsVirtButton(w)) {
        b->cornerRadius = UiScalePx(8);
        // a toolbar button is a label that highlights on hover, not a box
        b->SetColor(kColBtnBg, kColorTransparent);
        b->SetColor(kColBtnBorder, kColorTransparent);
        b->SetColor(kColBtnBgHover, hover);
        b->SetColor(kColBtnText, TbTextColor());
        b->SetColor(kColBtnTextDisabled, TbDisabledColor());
        return;
    }
    if (auto* t = AsVirtText(w)) {
        t->SetColor(kColText, TbTextColor());
        return;
    }
    if (auto* line = AsVirtLine(w)) {
        line->SetColor(kColLineFg, TbEdgeColor());
    }
}

static void RefreshToolbarIcons(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (!tb) {
        return;
    }
    int sz = tb->iconSize;
    Color fg = TbTextColor();
    Color dis = TbDisabledColor();
    for (int i = 0; i < len(tb->items); i++) {
        VirtCtrl* w = tb->items[i];
        ApplyToolbarItemColors(w);
        auto* ib = AsVirtIconButton(w);
        if (!ib) {
            continue;
        }
        const ToolbarButtonInfo& bi = GetToolbarButtonInfoByIdx(i);
        if (!HasToolbarButtonContent(bi)) {
            continue;
        }
        Str svg = bi.svgIcon ? bi.svgIcon : Str(bi.cmdId == CmdAnnotationShapes ? ShapeToolIcon() : bi.icon);
        ib->pixmap =
            len(bi.svgIcon) == 0 && bi.cmdId == CmdAnnotationMode ? AnnotationModeIconPixmap(sz, fg)
            : bi.cmdId == CmdAnnotationShapes
                ? ShapeToolPixmap(sz, fg)
                : GetCachedPixmapForSvg(svg, sz, sz, w->id == CmdOpenFile ? ThemeBrandTextColor() : fg, TbBgColor());
        ib->pixmapDisabled = len(bi.svgIcon) == 0 && bi.cmdId == CmdAnnotationMode ? AnnotationModeIconPixmap(sz, dis)
                             : bi.cmdId == CmdAnnotationShapes                     ? ShapeToolPixmap(sz, dis)
                                                               : GetCachedPixmapForSvg(svg, sz, sz, dis, TbBgColor());
    }
    for (int i = 0; i < len(tb->annotationItems); i++) {
        VirtCtrl* w = tb->annotationItems[i];
        ApplyToolbarItemColors(w);
        auto* ib = AsVirtIconButton(w);
        if (!ib) {
            continue;
        }
        const ToolbarButtonInfo& bi = gAnnotationButtons[i];
        if (!HasToolbarButtonContent(bi)) {
            continue;
        }
        ib->pixmap = bi.cmdId == CmdAnnotationShapes ? ShapeToolPixmap(sz, fg)
                                                     : GetCachedPixmapForSvg(Str(bi.icon), sz, sz, fg, TbBgColor());
        ib->pixmapDisabled = bi.cmdId == CmdAnnotationShapes
                                 ? ShapeToolPixmap(sz, dis)
                                 : GetCachedPixmapForSvg(Str(bi.icon), sz, sz, dis, TbBgColor());
    }
    if (tb->overflowButton) {
        tb->overflowButton->pixmap = GetCachedPixmapForSvg(Str(kToolSettingsIcon), sz, sz, fg, TbBgColor());
        tb->overflowButton->pixmapDisabled = GetCachedPixmapForSvg(Str(kToolSettingsIcon), sz, sz, dis, TbBgColor());
    }
    if (tb->annotationModeButton) {
        tb->annotationModeButton->pixmap = AnnotationModeIconPixmap(sz, fg);
        tb->annotationModeButton->pixmapDisabled = AnnotationModeIconPixmap(sz, dis);
    }
    for (auto* control : tb->pinnedItems) {
        auto* button = AsVirtIconButton(control);
        if (!button) continue;
        auto* presets = gSettings->pinnedAnnotationTools;
        int index = button->id - (CmdLast + 2000);
        if (!presets || index < 0 || index >= len(*presets) ||
            !str::Eq((*presets)[index]->tool, StrL("CmdAnnotationMode")))
            continue;
        button->pixmap = AnnotationModeIconPixmap(sz, fg);
        button->pixmapDisabled = AnnotationModeIconPixmap(sz, dis);
    }
    if (tb->pageLabel) {
        tb->pageLabel->SetColor(kColText, TbTextColor());
    }
    if (tb->pageLabel2) {
        tb->pageLabel2->SetColor(kColText, TbTextColor());
    }
    if (tb->pageTotal) {
        tb->pageTotal->SetColor(kColText, TbTextColor());
    }
    if (win->pageEdit) {
        win->pageEdit->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    }
    if (tb->chapterTotal) {
        tb->chapterTotal->SetColor(kColText, TbTextColor());
    }
    if (win->chapterEdit) {
        win->chapterEdit->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    }
}

void UpdateToolbarAfterThemeChange(MainWindow* win) {
    RefreshToolbarIcons(win);
    if (win->toolbarVirt && win->toolbarVirt->zoomEdit) {
        win->toolbarVirt->zoomEdit->SetColors(TbTextColor(), TbBgColor());
    }
    ToolbarUpdateStateForWindow(win, true);
    UpdateToolbarPageText(win, win->ctrl ? win->ctrl->PageCount() : -1);
    VirtHost* host = ToolbarHost(win);
    if (host) {
        host->bgColor = TbBgColor();
        host->Invalidate(true);
    }
    UpdateAnnotFilterToolbar(win);
}

// bounds of a button in the toolbar's client coords, empty if it has none
static VirtCtrl* ToolbarItemForCmd(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    if (cmdId == CmdAnnotationMode && tb->annotationModeButton && tb->annotationModeButton->IsVisible())
        return tb->annotationModeButton;
    if (cmdId == ToolbarOverflowId) return tb->overflowButton;
    if (cmdId == ToolbarAnnotOverflowId) return tb->annotationOverflowButton;
    for (VirtCtrl* w : tb->items) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w;
        }
    }
    for (VirtCtrl* w : tb->annotationItems) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w;
        }
    }
    return nullptr;
}

static Rect ToolbarButtonRect(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return {};
    }
    if (cmdId == CmdAnnotationMode && tb->annotationModeButton && tb->annotationModeButton->IsVisible())
        return tb->annotationModeButton->BoundsInWindow();
    if (cmdId == ToolbarOverflowId && tb->overflowButton && tb->overflowButton->IsVisible()) {
        return tb->overflowButton->BoundsInWindow();
    }
    if (cmdId == ToolbarAnnotOverflowId && tb->annotationOverflowButton && tb->annotationOverflowButton->IsVisible()) {
        return tb->annotationOverflowButton->BoundsInWindow();
    }
    for (VirtCtrl* w : tb->items) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w->BoundsInWindow();
        }
    }
    for (VirtCtrl* w : tb->annotationItems) {
        if (w && w->id == cmdId && w->GetVisibility() == Visibility::Visible) {
            return w->BoundsInWindow();
        }
    }
    for (VirtCtrl* w : tb->overflowItems) {
        if (w->id == cmdId && tb->overflowButton) return tb->overflowButton->BoundsInWindow();
    }
    for (VirtCtrl* w : tb->annotationOverflowItems) {
        if (w->id == cmdId && tb->annotationOverflowButton) return tb->annotationOverflowButton->BoundsInWindow();
    }
    return {};
}

void ToolbarSetFindExpanded(MainWindow* win, bool expanded, int minWidth, int preferredWidth) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) return;
    tb->findExpanded = expanded;
    if (minWidth > 0) tb->findMinWidth = minWidth;
    if (preferredWidth > 0) tb->findPreferredWidth = preferredWidth;
    if (expanded) RevealToolbarTool(win, CmdFindFirst);
    tb->host->Relayout();
    tb->host->Invalidate(true);
}

Rect ToolbarFindScreenRect(MainWindow* win) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->findExpanded || !tb->findSlot || !tb->host->IsVisible() || IsCollapsed(tb->findSlot)) return {};
    return tb->host->ToScreen(tb->findSlot->lastBounds);
}

// screen-coordinates rect of a toolbar button, used to position the FindBar.
// returns an empty rect when the toolbar isn't visible (e.g. fullscreen /
// presentation) so the caller can fall back to a different anchor.
Rect GetToolbarButtonScreenRect(MainWindow* win, int cmdId) {
    VirtHost* host = ToolbarHost(win);
    if (!host || !host->IsVisible()) {
        return {};
    }
    Rect r = ToolbarButtonRect(win, cmdId);
    if (r.IsEmpty()) {
        return {};
    }
    return host->ToScreen(r);
}

// Dump of the toolbar's buttons for -dbg-control tests (tests/issue-5869.ts).
// One line per button: its command id, its rect and the string the toolbar
// shows as its tooltip. Also reports how many tools the toolbar's tooltip
// control ended up with: the toolbar registers one tool per button keyed by
// command id, so duplicate command ids silently collapse into one tooltip.
static TempStr HoverDropdownStateTemp(MainWindow* win);

TempStr ToolbarButtonsResultTemp(int* exitCodeOut, Str action) {
    str::Builder out;
    MainWindow* win = len(gWindows) == 0 ? nullptr : gWindows[0];
    if (!win || !ToolbarHost(win)) {
        *exitCodeOut = 1;
        out.Append(StrL("ERROR no-toolbar\n"));
        return ToStrTemp(out);
    }
    ToolbarVirt* tb = win->toolbarVirt;
    // Observe synthetic hover before TrackMouseEvent delivers WM_MOUSELEAVE
    // for the untouched desktop cursor. The test drives the real edit handler.
    if (str::Eq(action, StrL("page-hover")) && win->pageEdit) {
        SendMessageW(win->pageEdit->hwnd, WM_MOUSELEAVE, 0, 0);
        SendMessageW(win->pageEdit->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(3, 3));
    } else if (str::Eq(action, StrL("page-focus")) && win->pageEdit) {
        HwndSetFocus(win->pageEdit->hwnd);
    } else if (str::Eq(action, StrL("dock-left")) || str::Eq(action, StrL("dock-right"))) {
        // Keep the three drag messages together; real desktop mouse moves
        // between cross-process test calls would otherwise cancel capture.
        Rect frame = HwndClientRect(win->hwndFrame);
        Rect bar = HwndClientRect(win->hwndToolbar);
        Point start = tb->vertical ? Point{bar.dx / 2, UiScalePx(7)} : Point{UiScalePx(7), bar.dy / 2};
        Point target{str::Eq(action, StrL("dock-left")) ? 1 : frame.dx - 1, frame.dy / 2};
        target = HwndMapWindowPoint(win->hwndFrame, win->hwndToolbar, target);
        SendMessageW(win->hwndToolbar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
        SendMessageW(win->hwndToolbar, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(target.x, target.y));
        SendMessageW(win->hwndToolbar, WM_LBUTTONUP, 0, MAKELPARAM(target.x, target.y));
    } else if (len(action) > 0) {
        *exitCodeOut = 1;
        out.Append(StrL("ERROR unknown-toolbar-action\n"));
        return ToStrTemp(out);
    }
    int n = tb ? len(tb->items) : 0;
    int nTools = 0;
    for (int i = 0; i < n; i++) {
        if (tb->items[i] && tb->items[i]->tooltip) {
            nTools++;
        }
    }
    out.Append(fmt("buttons=%d tooltipTools=%d\n", n, nTools));
    out.Append(fmt("annotationMode=%d dock=%d vertical=%d\n", win->annotationMode ? 1 : 0,
                   ToolbarPositionForWindow(win), tb->vertical ? 1 : 0));
    out.Append(fmt("annotationColumns=%d scroll=%d,%d viewport=%d\n", tb->annotationColumns,
                   tb->mainRow->verticalScrollY, tb->mainRow->verticalMaxScroll, tb->mainRow->verticalViewportDy));
    out.Append(fmt("toolbarStyle=%d\nthreeColumnPageStyle=%d\n", gSettings->annotationToolbarStyle,
                   gSettings->annotationThreeColumnPageStyle));
    if (AnnotationToolbarRail(tb)) {
        ToolbarLine* line = tb->mainRow;
        out.Append(fmt("railScroll offset=%d max=%d viewport=%d content=%d\n", line->verticalScrollY,
                       line->verticalMaxScroll, line->verticalViewportDy, line->railContentDy));
        for (auto& cell : line->railCells) {
            Str role = StrL("tool");
            if (cell.role == ToolbarRailRole::Page) role = StrL("page");
            if (cell.role == ToolbarRailRole::Chapter) role = StrL("chapter");
            if (cell.role == ToolbarRailRole::Zoom) role = StrL("zoom");
            if (cell.role == ToolbarRailRole::Pin) role = StrL("pin");
            Rect rect = cell.bounds;
            rect.x += line->lastBounds.x;
            rect.y += line->lastBounds.y - line->verticalScrollY;
            out.Append(fmt("railCell role=%s cmd=%d pinIndex=%d group=%d rect=%d,%d,%d,%d visible=%d\n", role, cell.cmd,
                           cell.pinIndex, cell.group, rect.x, rect.y, rect.dx, rect.dy,
                           cell.layout->GetVisibility() == Visibility::Visible ? 1 : 0));
        }
    }
#if IS_DEBUG
    out.Append(ScrollMenuStateTemp());
#endif
    out.Append(fmt("toolbarHwnd=%p\n", (const void*)win->hwndToolbar));
    Rect toolbarClient = HwndClientRect(win->hwndToolbar);
    out.Append(fmt("toolbarHeight client=%d intrinsic=%d base=%d\n", toolbarClient.dy,
                   tb->host->layout->MinIntrinsicHeight(toolbarClient.dx), tb->rowDy));
    for (auto* button : {tb->mainRow->nextButton, tb->overflowButton}) {
        if (!button || !button->IsVisible()) continue;
        Rect rect = button->BoundsInWindow();
        out.Append(fmt("footer=%s rect=%d,%d,%d,%d\n", button == tb->overflowButton ? StrL("commands") : StrL("scroll"),
                       rect.x, rect.y, rect.dx, rect.dy));
    }
    for (auto* edit : {win->pageEdit, tb->zoomEdit}) {
        if (!edit) continue;
        Rect rect = edit->lastBounds;
        Str name = edit == win->pageEdit ? StrL("pageField") : StrL("zoomField");
        out.Append(fmt("%s hwnd=%lld hidden=%d rect=%d,%d,%d,%d text=%s\n", name, (i64)(LONG_PTR)edit->hwnd,
                       edit->IsVisible() ? 0 : 1, rect.x, rect.y, rect.dx, rect.dy, edit->GetTextTemp()));
    }
    if (win->pageEdit) {
        auto* field = (ToolbarLocationEdit*)win->pageEdit;
        out.Append(fmt("pageDetails=%s\n", field->DetailsText()));
        out.Append(fmt("pageDetailsTip=%lld visible=%d\n",
                       (i64)(LONG_PTR)(field->detailsTip ? field->detailsTip->hwnd : nullptr),
                       field->detailsTip && HwndIsVisible(field->detailsTip->hwnd) ? 1 : 0));
    }
    if (tb->pageBox) {
        Rect rect = tb->pageBox->lastBounds;
        out.Append(fmt("pageStack style=%d rect=%d,%d,%d,%d total=%s active=%d\n", tb->pageBox->Style(), rect.x, rect.y,
                       rect.dx, rect.dy, tb->pageBox->total->s,
                       tb->pageBox->Active() && tb->pageBox->GetVisibility() == Visibility::Visible ? 1 : 0));
    }
    if (tb->annotationModeButton) {
        Rect r = tb->annotationModeButton->BoundsInWindow();
        out.Append(fmt("modeButton cmd=%d hidden=%d rect=%d,%d,%d,%d\n", CmdAnnotationMode,
                       tb->annotationModeButton->IsVisible() ? 0 : 1, r.x, r.y, r.Right(), r.Bottom()));
    }
    int toolIdx = 0;
    for (int i = 0; i < n; i++) {
        VirtCtrl* w = tb->items[i];
        Rect r = w ? w->BoundsInWindow() : Rect{};
        bool hidden = !w || w->GetVisibility() != Visibility::Visible;
        if (!hidden && AsVirtIconButton(w))
            out.Append(fmt("toolCell cmd=%d rect=%d,%d,%d,%d\n", w->id, r.x, r.y, r.dx, r.dy));
        // Match the old Win32 dump: TBIF_TEXT. Built-in buttons store the
        // tooltip (with accelerator); custom ones stored the raw name.
        Str text{};
        if (auto* b = AsVirtButton(w)) {
            text = b->s;
        } else if (i >= gLayoutButtonsCount) {
            text = GetToolbarButtonInfoByIdx(i).toolTip;
        } else if (w && w->tooltip) {
            text = w->tooltip;
        }
        out.Append(fmt("idx=%d cmd=%d hidden=%d rect=%d,%d,%d,%d text=%s\n", i, w ? w->id : 0, hidden ? 1 : 0, r.x, r.y,
                       r.x + r.dx, r.y + r.dy, text));
        if (w && w->tooltip) {
            out.Append(fmt("tool=%d uid=%d rect=%d,%d,%d,%d\n", toolIdx, w->id, r.x, r.y, r.x + r.dx, r.y + r.dy));
            toolIdx++;
        }
    }
    int nAnnotations = len(tb->annotationItems);
    bool annotationsVisible = tb->annotationExpanded;
    out.Append(fmt("annotationButtons=%d visible=%d\n", nAnnotations, annotationsVisible ? 1 : 0));
    for (int i = 0; i < nAnnotations; i++) {
        VirtCtrl* w = tb->annotationItems[i];
        const ToolbarButtonInfo& bi = gAnnotationButtons[i];
        if (!w && bi.cmdId) w = ToolbarItemForCmd(win, bi.cmdId);
        Rect r = w ? w->BoundsInWindow() : Rect{};
        bool hidden = !annotationsVisible || !w || w->GetVisibility() != Visibility::Visible;
        Str tip = w ? w->tooltip : Str{};
        out.Append(fmt("annotation-idx=%d cmd=%d hidden=%d enabled=%d rect=%d,%d,%d,%d text=%s tip=%s\n", i,
                       w ? w->id : 0, hidden ? 1 : 0, w && w->IsEnabled() ? 1 : 0, r.x, r.y, r.x + r.dx, r.y + r.dy,
                       bi.toolTip, tip));
    }
    out.Append(AnnotFilterToolbarStateTemp(win));
    out.Append(HoverDropdownStateTemp(win));
    *exitCodeOut = 0;
    return ToStrTemp(out);
}

// A drop-down menu is modal: the click that dismisses it is delivered to the
// toolbar after the menu closes, and when it lands on the split button that
// opened the menu it would open it right back up. So the button ignores a click
// that arrives on the heels of its menu closing.
static u64 gToolbarDropdownClosedAt = 0;

static bool ToolbarDropdownJustClosed() {
    return GetTickCount64() - gToolbarDropdownClosedAt < 200;
}

// called when a toolbar drop-down menu was dismissed
void ToolbarNoteDropdownClosed() {
    gToolbarDropdownClosedAt = GetTickCount64();
}

static bool ShowToolbarButtonDropdown(MainWindow*, int cmdId);
static bool IsAnnotColorCmd(int cmdId);

static void OnToolbarButtonClicked(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* w = ev->target;
    if (!w || !win) {
        return;
    }
    int cmdId = w->id;
    if (cmdId == PageInfoId || cmdId == 0) {
        return;
    }
    if (cmdId != CmdFindFirst && IsFindBarVisible(win)) CollapseFindBar(win);
    if (ToolbarDropdownJustClosed() && (cmdId == CmdToggleReadAloud || cmdId == CmdPauseReadAloud)) {
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 &&
        (cmdId == CmdCreateAnnotInk || cmdId == CmdAnnotationHighlightBrush || cmdId == CmdCreateAnnotUnderline ||
         cmdId == CmdCreateAnnotStrikeOut)) {
        if (win->toolbarVirt && win->toolbarVirt->hoverCmdId == cmdId) {
            HideToolbarHoverDropdown(win);
            ev->didHandle = true;
            return;
        }
        bool ink = cmdId == CmdCreateAnnotInk;
        bool started = ink ? IsPlacingInkAnnotation(win) : IsPlacingHighlighterAnnotation(win);
        if (!started || !ink) {
            HwndSendCommand(win->hwndFrame, cmdId);
        }
        ShowToolbarButtonDropdown(win, cmdId);
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 && cmdId == CmdToggleLaserPointer) {
        if (win->toolbarVirt && win->toolbarVirt->hoverCmdId == cmdId) {
            HideToolbarHoverDropdown(win);
        } else {
            if (!IsLaserPointerActive(win)) {
                HwndSendCommand(win->hwndFrame, cmdId);
            }
            ShowToolbarButtonDropdown(win, cmdId);
        }
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 && cmdId == CmdAnnotationShapes) {
        if (win->toolbarVirt && win->toolbarVirt->hoverCmdId == cmdId) {
            HideToolbarHoverDropdown(win);
            win->toolbarVirt->hoverPendingCmdId = cmdId;
        } else {
            ShowShapesToolbarPopup(win);
        }
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 && cmdId == CmdSinglePageView) {
        ShowToolbarButtonDropdown(win, cmdId);
        ev->didHandle = true;
        return;
    }
    if (w->IsEnabled() && ev->button == 0 && cmdId == CmdAnnotationLasso) {
        ShowToolbarButtonDropdown(win, cmdId);
        ev->didHandle = true;
        return;
    }
    // right-click: the drop-down, not the button's command
    if (ev->button == 1) {
        if (ShowPinToolMenu(win, cmdId)) {
            ev->didHandle = true;
            return;
        }
        ShowToolbarButtonDropdown(win, cmdId);
        ev->didHandle = true;
        return;
    }
    if (!w->IsEnabled()) {
        return;
    }
    if (auto* ib = AsVirtIconButton(w)) {
        if (ib->hasDropdown) {
            int dropDx = ib->DropdownDx();
            if (dropDx > 0 && ev->pt.x >= w->bounds.dx - dropDx) {
                ShowTtsVoiceMenu(win, GetToolbarButtonScreenRect(win, cmdId));
                ev->didHandle = true;
                return;
            }
        }
    }
    // save: the hover menu's rows end the session; they no longer apply.
    // an annotation button: picking the tool is done, its colors are in the way
    if (cmdId == CmdSaveAnnotations || IsAnnotColorCmd(cmdId)) {
        HideToolbarHoverDropdown(win);
        // not again for as long as the mouse stays on the button
        if (ToolbarVirt* tb = win->toolbarVirt) {
            tb->hoverPendingCmdId = cmdId;
        }
    }
    ToolbarPostCommand(win, cmdId);
    ev->didHandle = true;
}

//--- hover drop-down

// A row of NewToolbarHoverMenu(): an icon on the left, text on the right, and a
// background that lights up under the mouse, like a menu item.
constexpr int kHoverRowPadY = 6;
constexpr int kHoverRowPadX = 10;
constexpr int kHoverRowIconGapX = 8;
// between the label and the shortcut that sits at the right edge, as in a menu
constexpr int kHoverRowShortcutGapX = 24;
constexpr int kHoverMenuBorder = 1;
// around a label in the single-row strip; less than a menu row's, it is a row
// of them and the gaps add up
constexpr int kHoverCellPadX = 8;
// the mouse crosses a seam going from the button to the drop-down; don't close
// on the frame where it is over neither
constexpr int kCloseHoverDropdownDelayMs = 150;

struct ToolbarHoverRow : VirtCtrl {
    Pixmap* pixmap = nullptr; // not owned, from GetCachedPixmapForSvg()
    Str text;                 // owned
    Str shortcut;             // owned; empty when the command has no key
    PlatformFont* font = nullptr;
    int iconSize = 0;
    bool isCurrent = false;

    ToolbarHoverRow() = default;
    ~ToolbarHoverRow() override {
        str::Free(text);
        str::Free(shortcut);
    }

    int ShortcutDx() {
        if (len(shortcut) == 0) {
            return 0;
        }
        return PlatformFontMeasureText(font, shortcut).dx + UiScalePx(kHoverRowShortcutGapX);
    }

    Size GetIdealSize() override {
        Size ts = PlatformFontMeasureText(font, text);
        int dx = (2 * UiScalePx(kHoverRowPadX)) + iconSize + UiScalePx(kHoverRowIconGapX) + ts.dx + ShortcutDx();
        int dy = std::max(ts.dy, iconSize) + (2 * UiScalePx(kHoverRowPadY));
        return {dx, dy};
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool enabled = IsEnabled();
        Rect r = ctx.bounds;
        if (isCurrent || (enabled && (HasFlag(vwfHovered) || HasFlag(vwfFocused)))) {
            ctx.gfx->FillRoundedRect(r, UiScalePx(6), TbHoverColor(), isCurrent ? ThemeBrandColor() : TbHoverColor());
        }
        int x = r.x + UiScalePx(kHoverRowPadX);
        if (pixmap) {
            int y = r.y + ((r.dy - pixmap->height) / 2);
            ctx.gfx->DrawPixmap(pixmap, {x, y, pixmap->width, pixmap->height});
        }
        x += iconSize + UiScalePx(kHoverRowIconGapX);
        int right = r.Right() - UiScalePx(kHoverRowPadX);
        Color col = enabled ? TbTextColor() : TbDisabledColor();
        if (shortcut) {
            // right-aligned and dimmer, the way a menu shows its accelerator
            Rect sr{x, r.y, right - x, r.dy};
            ctx.gfx->DrawText(shortcut, sr, gfxTextRight | gfxTextVCenter, font, TbDisabledColor());
            right -= ShortcutDx();
        }
        Rect tr{x, r.y, right - x, r.dy};
        ctx.gfx->DrawText(text, tr, gfxTextVCenter | gfxTextEllipsis, font, col);
    }

    void OnMouseEnter() { Invalidate(); }
    void OnMouseLeave() { Invalidate(); }
};

// A cell of NewToolbarHoverStrip(): a label in a row of them, no icon. The one
// in use is boxed rather than ticked; a tick per cell would double the width of
// a strip whose whole point is to be compact.
struct ToolbarHoverCell : VirtCtrl {
    Str text; // owned
    PlatformFont* font = nullptr;
    bool isCurrent = false;

    ToolbarHoverCell() = default;
    ~ToolbarHoverCell() override { str::Free(text); }

    Size GetIdealSize() override {
        Size ts = PlatformFontMeasureText(font, text);
        return {ts.dx + (2 * UiScalePx(kHoverCellPadX)), ts.dy + (2 * UiScalePx(kHoverRowPadY))};
    }

    void Paint(VirtPaintCtx& ctx) override {
        bool enabled = IsEnabled();
        Rect r = ctx.bounds;
        if (enabled && (HasFlag(vwfHovered) || HasFlag(vwfFocused))) {
            ctx.gfx->FillRect(r, TbHoverColor());
        }
        if (isCurrent) {
            ctx.gfx->DrawRect(r, TbTextColor(), UiScalePx(1));
        }
        Color col = enabled ? TbTextColor() : TbDisabledColor();
        ctx.gfx->DrawText(text, r, gfxTextCenter | gfxTextVCenter, font, col);
    }

    void OnMouseEnter() { Invalidate(); }
    void OnMouseLeave() { Invalidate(); }
};

static void PostedHideHoverDropdown(MainWindow* win) {
    if (IsMainWindowValidAndNotClosing(win)) {
        HideToolbarHoverDropdown(win);
    }
}

static void OnHoverRowClicked(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* w = ev ? ev->target : nullptr;
    if (!w || !w->IsEnabled()) {
        return;
    }
    int cmdId = w->id;
    // the click is being handled by the drop-down's own window, so it can only
    // be torn down once that returns
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "HideToolbarHoverDropdown");
    ToolbarPostCommand(win, cmdId);
}

static void OnPaletteKey(MainWindow* win, VirtKeyEvent* ev) {
    if (ev->vkey == VK_ESCAPE) {
        uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "Hide toolbar settings");
        FocusToolbar(win);
        ev->didHandle = true;
        return;
    }
    if (auto* slider = AsVirtSlider(ev->target)) {
        int value = slider->value;
        if (ev->vkey == VK_LEFT || ev->vkey == VK_DOWN)
            value--;
        else if (ev->vkey == VK_RIGHT || ev->vkey == VK_UP)
            value++;
        else if (ev->vkey == VK_HOME)
            value = slider->minVal;
        else if (ev->vkey == VK_END)
            value = slider->maxVal;
        else
            return;
        slider->SetValue(value, true);
        if (slider->onValueCommitted.IsValid()) slider->onValueCommitted.Call();
        ev->didHandle = true;
        return;
    }
    if (ev->vkey != VK_SPACE && ev->vkey != VK_RETURN) return;
    VirtMouseEvent click;
    click.target = ev->target;
    click.hit = ev->target;
    if (ev->target->onClick.IsValid()) ev->target->onClick.Call(&click);
    ev->didHandle = true;
}

// Remember a row/cell for HoverDropdownStateTemp(). `text` has to be the ctrl's
// own copy: what the caller built the item from is often temp-allocated and
// gone by the time the dump is asked for.
static void RecordHoverItem(ToolbarVirt* tb, VirtCtrl* w, Str text, const ToolbarHoverMenuItem& it) {
    ToolbarHoverItemState st;
    st.ctrl = w;
    st.text = text;
    st.cmdId = it.cmdId;
    st.isCurrent = it.isCurrent;
    VecAppend(tb->hoverItems, st);
}

ILayout* NewToolbarHoverMenu(MainWindow* win, const Vec<ToolbarHoverMenuItem>& items) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    int iconSize = tb->iconSize;
    Color fg = TbTextColor();
    Color dis = TbDisabledColor();
    Color bg = TbBgColor();
    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    for (const ToolbarHoverMenuItem& it : items) {
        auto* row = new ToolbarHoverRow();
        row->id = it.cmdId;
        row->font = tb->platformFont;
        row->isCurrent = it.isCurrent;
        row->iconSize = iconSize;
        str::ReplaceWithCopy(&row->text, it.text);
        str::ReplaceWithCopy(&row->shortcut, ShortcutsForCmdTemp(it.cmdId, 1));
        if (it.svgIcon) {
            row->pixmap = GetCachedPixmapForSvg(it.svgIcon, iconSize, iconSize, it.enabled ? fg : dis, bg);
        }
        row->SetIsEnabled(it.enabled);
        row->onClick = MkFunc1(OnHoverRowClicked, win);
        vbox->AddChild(row);
        RecordHoverItem(tb, row, row->text, it);
    }
    int b = UiScalePx(kHoverMenuBorder);
    return new Padding(vbox, Insets{b, b, b, b});
}

struct ZoomPickerScroll : ScrollBox {
    Size maximum;

    ZoomPickerScroll(ILayout* child, Size maximum) : ScrollBox(child), maximum(maximum) {}

    Size Layout(Constraints bc) override {
        bc.max.dx = std::min(bc.max.dx, maximum.dx);
        bc.max.dy = std::min(bc.max.dy, maximum.dy);
        return ScrollBox::Layout(bc);
    }
    void SetBounds(Rect r) override {
        ScrollBox::SetBounds(r);
        // ScrollBox owns an ILayout tree rather than VirtCtrl children. Give its
        // interactive descendants the host for capture, focus and invalidation.
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(child, controls);
        for (VirtCtrl* ctrl : controls) ctrl->SetRoot(root);
    }
};

ILayout* NewToolbarHoverStrip(MainWindow* win, const Vec<ToolbarHoverMenuItem>& items) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return nullptr;
    }
    // a cell per item, made in the order they came in so that hoverItems (and
    // the -dbg-control dump built from it) stays in that order whatever row a
    // cell ends up in
    Vec<VirtCtrl*> cells;
    for (const ToolbarHoverMenuItem& it : items) {
        auto* cell = new ToolbarHoverCell();
        cell->id = it.cmdId;
        cell->font = tb->platformFont;
        cell->isCurrent = it.isCurrent;
        str::ReplaceWithCopy(&cell->text, it.text);
        cell->SetIsEnabled(it.enabled);
        cell->onClick = MkFunc1(OnHoverRowClicked, win);
        VecAppend(cells, (VirtCtrl*)cell);
        RecordHoverItem(tb, cell, cell->text, it);
        tb->hoverItems[len(tb->hoverItems) - 1].isStripCell = true;
    }

    int b = UiScalePx(kHoverMenuBorder);
    Rect frame = HwndClientRect(win->hwndFrame);
    Rect work = PlatformWindowWorkArea(win->hwndFrame);
    constexpr int kPickerMaxColumns = 6;
    int availableWidth = std::max(1, std::min(frame.dx, work.dx) - 2 * UiScalePx(16));
    int availableHeight = std::max(1, std::min(frame.dy, work.dy) - UiScalePx(120));
    int cellWidth = 1;
    for (VirtCtrl* cell : cells) {
        cellWidth = std::max(cellWidth, cell->GetIdealSize().dx);
    }
    int columns = limitValue((availableWidth - 2 * b) / cellWidth, 1, kPickerMaxColumns);
    auto* table = new Table();
    table->SetSize((len(cells) + columns - 1) / columns, columns);
    for (int i = 0; i < len(cells); i++) {
        auto& slot = table->SetCell(i / columns, i % columns, cells[i]);
        slot.alignH = CrossAxisAlign::Stretch;
        slot.alignV = CrossAxisAlign::Stretch;
    }
    auto* scroll = new ZoomPickerScroll(table, Size{availableWidth - 2 * b, availableHeight - 2 * b});
    scroll->syncScrollbar = false;
    scroll->lineDy = PlatformFontLineHeight(tb->platformFont) + UiScalePx(8);
    return new Padding(scroll, Insets{b, b, b, b});
}

// The rows/cells of the drop-down that is up, in screen coordinates, for
// -dbg-control tests (tests/toolbar-hover-dropdown.ts).
static TempStr HoverDropdownStateTemp(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    VirtHost* host = tb ? tb->hoverHost : nullptr;
    if (!host) {
        return StrL("dropdown cmd=0 items=0\n");
    }
    str::Builder out;
    int n = len(tb->hoverItems);
    out.Append(fmt("dropdown cmd=%d items=%d\n", tb->hoverCmdId, n));
    for (int i = 0; i < n; i++) {
        ToolbarHoverItemState& st = tb->hoverItems[i];
        Rect r = st.ctrl ? host->ToScreen(st.ctrl->BoundsInWindow()) : Rect{};
        out.Append(fmt("dropdown-item idx=%d cmd=%d current=%d rect=%d,%d,%d,%d text=%s\n", i, st.cmdId,
                       st.isCurrent ? 1 : 0, r.x, r.y, r.x + r.dx, r.y + r.dy, st.text));
    }
    return ToStrTemp(out);
}

// The toolbar sees no mouse moves once the cursor is inside the drop-down, so
// the drop-down has to say when the cursor leaves it.
static void OnHoverDropdownMouseLeave(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (tb && tb->host && tb->hoverCmdId != 0 && !tb->hoverSticky) {
        tb->host->SetTimer(kCloseHoverDropdownTimerId, kCloseHoverDropdownDelayMs);
    }
}

static void OnHoverDropdownMouseMove(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (tb && tb->host && tb->hoverCmdId != 0) {
        tb->host->KillTimer(kCloseHoverDropdownTimerId);
    }
}

// The pyramid's right half - the values above the middle - gets its own
// ground, so which way is bigger can be seen rather than read. The rows are
// staggered, so the two halves meet along a staircase, and each band runs to
// the right edge, covering the empty space beside a short row as well.
static void PaintHoverDropdownRightHalf(MainWindow* win, VirtHostPaintEvent* ev) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    Vec<ToolbarHoverItemState>& items = tb->hoverItems;
    int n = len(items);
    constexpr int kMaxRows = 32;
    int rowTop[kMaxRows];
    int rowBottom[kMaxRows];
    int rowSplit[kMaxRows];
    int nRows = 0;
    int prevTop = INT_MIN;
    while (nRows < kMaxRows) {
        // the topmost row below the last one found
        int y = INT_MAX;
        for (int i = 0; i < n; i++) {
            // only a strip has halves; a menu's rows are all one ground
            VirtCtrl* c = items[i].isStripCell ? items[i].ctrl : nullptr;
            if (c) {
                int cy = c->BoundsInWindow().y;
                if (cy > prevTop && cy < y) {
                    y = cy;
                }
            }
        }
        if (y == INT_MAX) {
            break;
        }
        int bottom = y;
        int right = INT_MIN;
        int split = INT_MAX;
        for (int i = 0; i < n; i++) {
            VirtCtrl* c = items[i].ctrl;
            if (!c) {
                continue;
            }
            Rect r = c->BoundsInWindow();
            if (r.y != y) {
                continue;
            }
            bottom = std::max(bottom, r.y + r.dy);
            right = std::max(right, r.x + r.dx);
            if (items[i].isRightHalf) {
                split = std::min(split, r.x);
            }
        }
        if (split == INT_MAX) {
            // nothing of the larger side in this row: only the space past its
            // end is on that side
            split = right;
        }
        rowTop[nRows] = y;
        rowBottom[nRows] = bottom;
        rowSplit[nRows] = split;
        nRows++;
        prevTop = y;
    }
    Rect cr = ev->clientRect;
    Color col = TbSubtleBgColor();
    for (int i = 0; i < nRows; i++) {
        // the first and last bands take in the border, so no strip of the
        // other ground is left above or below them
        int y = (i == 0) ? cr.y : rowTop[i];
        int bottom = (i == nRows - 1) ? cr.y + cr.dy : rowBottom[i];
        int x = rowSplit[i];
        ev->gfx->FillRect(Rect{x, y, (cr.x + cr.dx) - x, bottom - y}, col);
    }
}

static void PaintHoverDropdownBg(MainWindow* win, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, TbBgColor());
    PaintHoverDropdownRightHalf(win, ev);
}

static void PaintHoverFocus(MainWindow*, VirtHostPaintEvent* ev) {
    VirtCtrl* focused = ev->host->vroot ? ev->host->vroot->focused : nullptr;
    if (focused && focused->IsVisible() && ev->host->HasFocus()) ev->gfx->DrawFocusRect(focused->BoundsInWindow());
    int diameter = UiCornerDiameter(DpiGetForHwnd(ev->host->native), 6);
    ev->gfx->FillRoundedRect(ev->clientRect, diameter, kColorTransparent, ThemeEdgeColor());
}

// The button a drop-down is up for goes without its tooltip: the bubble would
// sit on top of the drop-down, and WM_SETCURSOR would keep bringing it back.
static void TakeHoverButtonTooltip(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (VirtCtrl* btn = ToolbarItemForCmd(win, cmdId)) {
        str::ReplaceWithCopy(&tb->hoverSavedTip, btn->tooltip);
        btn->SetTooltip({});
    }
}

static void GiveHoverButtonTooltipBack(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (tb->hoverCmdId == 0) {
        return;
    }
    if (VirtCtrl* btn = ToolbarItemForCmd(win, tb->hoverCmdId)) {
        btn->SetTooltip(tb->hoverSavedTip);
    }
    str::Free(tb->hoverSavedTip);
    tb->hoverSavedTip = {};
}

// SetWindowPos / DestroyWindow of the drop-down can deliver a mouse move
// back into this code. That move must not open a second window.
static bool gInOpenHover = false;

struct HoverOpenScope {
    HoverOpenScope() { gInOpenHover = true; }
    ~HoverOpenScope() { gInOpenHover = false; }
};

void HideToolbarHoverDropdown(MainWindow* win) {
    if (gInOpenHover) {
        return;
    }
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    HoverOpenScope openScope;
    VecReset(tb->hoverItems);
    GiveHoverButtonTooltipBack(win);
    // A second dismissal after a tool command must preserve its hover suppression.
    if (tb->hoverCmdId != 0) tb->hoverPendingCmdId = 0;
    tb->hoverCmdId = 0;
    tb->hoverSticky = false;
    tb->hoverMoveTick = 0;
    if (tb->host) {
        tb->host->KillTimer(kOpenHoverDropdownTimerId);
        tb->host->KillTimer(kCloseHoverDropdownTimerId);
    }
    if (tb->hoverHost) {
        VirtHost* h = tb->hoverHost;
        tb->hoverHost = nullptr;
        delete h;
    }
}

// Geometry, not WindowFromPoint(): the toolbar only gets a mouse move while
// the cursor is over it, so who is on top does not come into it.
static bool HostHasPoint(VirtHost* host, Point pt) {
    return host && host->IsVisible() && host->ScreenRect().Contains(pt);
}

bool ToolbarHoverDropdownContainsScreenPoint(MainWindow* win, Point pt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    return HostHasPoint(tb ? tb->hoverHost : nullptr, pt);
}

static ToolbarHoverReg* FindHoverReg(ToolbarVirt* tb, int cmdId) {
    if (!tb || cmdId == 0) {
        return nullptr;
    }
    for (ToolbarHoverReg& reg : tb->hoverRegs) {
        if (reg.cmdId == cmdId) {
            return &reg;
        }
    }
    return nullptr;
}

void SetToolbarHoverDropdown(MainWindow* win, int cmdId, const Func1<ToolbarHoverBuildEvent*>& build, int groupId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return;
    }
    if (ToolbarHoverReg* reg = FindHoverReg(tb, cmdId)) {
        reg->build = build;
        reg->groupId = groupId;
        return;
    }
    ToolbarHoverReg reg;
    reg.cmdId = cmdId;
    reg.groupId = groupId;
    reg.build = build;
    VecAppend(tb->hoverRegs, reg);
}

static void CollectPaletteControls(ILayout* layout, Vec<VirtCtrl*>& out) {
    if (!layout || IsCollapsed(layout)) return;
    if (auto* ctrl = layout->AsVirtCtrl()) VecAppend(out, ctrl);
    for (int i = 0; i < layout->LayoutChildCount(); i++) CollectPaletteControls(layout->LayoutChildAt(i), out);
}

static int ToolbarPaletteWidth(PlatformFont* font, int availableDx) {
    int width = UiScalePx(320);
    if (font) width = std::max(width, 8 * PlatformFontLineHeight(font));
    return std::max(1, std::min(width, availableDx));
}

static Size ToolbarPaletteViewport(VirtHost* host, Size work) {
    Rect window = host->ScreenRect();
    Rect client = host->ClientRect();
    int frameDx = window.dx - client.dx;
    int frameDy = window.dy - client.dy;
    int barDx = GetAppScrollbarWidth(DpiGetForHwnd(host->native));
    int margin = UiScalePx(16);
    // Reserve the caption and a possible scrollbar before measuring content.
    // Shifting an oversized popup cannot keep its close button on the monitor.
    return {std::max(1, work.dx - margin - frameDx - barDx), std::max(1, work.dy - margin - frameDy)};
}

// Preserve the natural height of every control when a palette exceeds the monitor.
struct ToolbarPaletteScroll : ScrollBox {
    Size limit;
    ToolbarPaletteScroll(ILayout* content, Size available, PlatformFont* font) : ScrollBox(content), limit(available) {
        limit.dx = ToolbarPaletteWidth(font, available.dx);
    }
    Size Layout(Constraints bc) override {
        bc.max.dx = std::min(bc.max.dx, limit.dx);
        bc.max.dy = std::min(bc.max.dy, limit.dy);
        bc.min.dx = std::min(bc.min.dx, bc.max.dx);
        bc.min.dy = std::min(bc.min.dy, bc.max.dy);
        return ScrollBox::Layout(bc);
    }
    void SetBounds(Rect r) override {
        ScrollBox::SetBounds(r);
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(child, controls);
        for (VirtCtrl* ctrl : controls) ctrl->SetRoot(root);
    }
};

static void PaletteNativeMsg(ScrollBox* scroll, VirtHostNativeMsg* ev) {
    if (ev->msg == WM_CLOSE) {
        auto* win = (MainWindow*)ev->host->userData;
        if (win) uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "Close tool settings");
        ev->didHandle = true;
        return;
    }
    if (ev->msg == WM_MOUSEWHEEL) {
        VirtMouseEvent wheel;
        wheel.wheelDelta = GET_WHEEL_DELTA_WPARAM(ev->wp);
        scroll->OnMouseWheel(&wheel);
        ev->didHandle = true;
        return;
    }
    if (ev->msg == WM_VSCROLL) {
        scroll->OnVScroll(ev->wp);
        ev->didHandle = true;
        return;
    }
    if (ev->msg != WM_KEYDOWN) return;
    auto* root = ev->host->vroot;
    if (ev->wp == VK_TAB && root && !IsCtrlPressed() && !IsAltPressed()) {
        Vec<VirtCtrl*> all, stops;
        CollectPaletteControls(scroll->child, all);
        for (VirtCtrl* ctrl : all)
            if (ctrl->IsHitTestable() && ctrl->HasFlag(vwfFocusable) && !ctrl->HasFlag(vwfSkipTabStop))
                VecAppend(stops, ctrl);
        if (len(stops)) {
            int index = VecFind(stops, root->focused);
            index = IsShiftPressed() ? (index <= 0 ? len(stops) - 1 : index - 1) : (index + 1) % len(stops);
            root->SetFocus(stops[index]);
        }
        if (root->focused) {
            Rect focused = root->focused->BoundsInWindow();
            Rect view = scroll->BoundsInWindow();
            if (focused.y < view.y)
                scroll->ScrollBy(focused.y - view.y);
            else if (focused.Bottom() > view.Bottom())
                scroll->ScrollBy(focused.Bottom() - view.Bottom());
        }
        ev->didHandle = true;
    } else if (ev->wp == VK_NEXT || ev->wp == VK_PRIOR) {
        scroll->ScrollPage(ev->wp == VK_NEXT ? 1 : -1);
        ev->didHandle = true;
    }
}

static void NoteHoverButtonMove(ToolbarVirt* tb) {
    tb->hoverMoveTick = GetTickCount64();
}

static void OpenHoverDropdown(MainWindow* win, int cmdId) {
    if (gInOpenHover) {
        return;
    }
    HoverOpenScope openScope;
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    ToolbarHoverReg* reg = FindHoverReg(tb, cmdId);
    if (!reg || !reg->build.IsValid() || !tb->host) {
        return;
    }
    Rect anchor = GetToolbarButtonScreenRect(win, cmdId);
    if (anchor.IsEmpty()) {
        return;
    }
    ToolbarHoverBuildEvent ev;
    ev.win = win;
    ev.cmdId = cmdId;
    VecReset(tb->hoverItems);
    reg->build.Call(&ev);
    if (!ev.layout) {
        return;
    }

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStrL(L"SumatraToolbarHoverMenu");
    if (IsAnnotColorCmd(cmdId) || cmdId == CmdToggleLaserPointer || cmdId == CmdAnnotationShapes) {
        auto* button = ToolbarItemForCmd(win, cmdId);
        args.title = cmdId == CmdAnnotationShapes     ? ShapeToolTitle()
                     : cmdId == CmdCreateAnnotInk     ? Tr("Pen types")
                     : cmdId == CmdToggleLaserPointer ? Tr("Laser pointer")
                     : button                         ? button->tooltip
                                                      : Tr("Annotation");
    }
    args.isPopup = true;
    args.visible = false;
    args.noActivate = true;
    args.userData = win;
    args.bgColor = TbBgColor();
    args.isRtl = IsUIRtl();
    args.initialSize = {100, 100};
    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        delete ev.layout;
        return;
    }
    if (len(args.title)) WindowApplyScaledCaption(host->native);
    host->onPaintBackground = MkFunc1(PaintHoverDropdownBg, win);
    host->onPaint = MkFunc1(PaintHoverFocus, win);
    host->onMouseMove = MkFunc0(OnHoverDropdownMouseMove, win);
    host->onMouseLeave = MkFunc0(OnHoverDropdownMouseLeave, win);
    Rect work = PlatformWindowWorkArea(win->hwndFrame);
    auto* paletteScroll =
        new ToolbarPaletteScroll(ev.layout, ToolbarPaletteViewport(host, work.Size()), tb->platformFont);
    paletteScroll->lineDy = PlatformFontLineHeight(tb->platformFont) + UiScalePx(8);
    ev.layout = paletteScroll;
    host->onNativeMsg = MkFunc1(PaletteNativeMsg, (ScrollBox*)paletteScroll);
    Size sz = host->SetLayoutSizedToContent(ev.layout);
    Vec<VirtCtrl*> controls;
    CollectPaletteControls(ev.layout, controls);
    for (VirtCtrl* control : controls) {
        if (!control->onClick.IsValid() && !AsVirtSlider(control)) continue;
        control->SetFlag(vwfFocusable, true);
        control->onKeyDown = MkFunc1(OnPaletteKey, win);
    }

    // under the button, left edges aligned, kept on the monitor. A build that
    // asked for it instead hangs off the middle of the button, so it opens
    // around where the mouse already is
    int x = anchor.x;
    if (ev.centerOnButton) {
        x = anchor.x + ((anchor.dx - sz.dx) / 2);
    }
    Rect r{x, anchor.Bottom(), sz.dx, sz.dy};
    if (cmdId == CmdZoomIn || cmdId == CmdZoomOut) {
        Rect frame = HwndMapLtrClientRectToScreen(win->hwndFrame, HwndClientRect(win->hwndFrame));
        Rect bounds = frame.Intersect(PlatformWindowWorkArea(win->hwndFrame));
        r.x = limitValue(r.x, bounds.x, std::max(bounds.x, bounds.Right() - r.dx));
        r.y = limitValue(r.y, bounds.y, std::max(bounds.y, bounds.Bottom() - r.dy));
    }
    r = ShiftRectToWorkArea(r, win->hwndFrame, true);

    // Live before the show. SetWindowPos can deliver a mouse move; gInOpenHover
    // makes that move a no-op so it cannot open a second window.
    tb->hoverHost = host;
    tb->hoverCmdId = cmdId;
    tb->hoverPendingCmdId = 0;
    NoteHoverButtonMove(tb);
    host->SetPos(r, true);

    tb->host->KillTimer(kOpenHoverDropdownTimerId);
    tb->host->KillTimer(kCloseHoverDropdownTimerId);

    TakeHoverButtonTooltip(win, cmdId);
    if (tb->host->vroot) {
        tb->host->vroot->HideTooltip();
    }
}

// Open the drop-down this button has, if any. One already up is left as it is.
static bool ShowToolbarButtonDropdown(MainWindow* win, int cmdId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb) {
        return false;
    }

    if (auto* ib = AsVirtIconButton(ToolbarItemForCmd(win, cmdId))) {
        if (ib->hasDropdown && cmdId != CmdAnnotationShapes) {
            ShowTtsVoiceMenu(win, GetToolbarButtonScreenRect(win, cmdId));
            return true;
        }
    }

    ToolbarHoverReg* to = FindHoverReg(tb, cmdId);
    if (!to) {
        return false;
    }
    if (tb->hoverCmdId == cmdId) {
        if (tb->host) {
            tb->host->KillTimer(kCloseHoverDropdownTimerId);
        }
        tb->hoverSticky = true;
        return true;
    }
    if (tb->hoverCmdId != 0) {
        ToolbarHoverReg* from = FindHoverReg(tb, tb->hoverCmdId);
        int group = from ? from->groupId : 0;
        if (group != 0 && to->groupId == group) {
            if (tb->host) {
                tb->host->KillTimer(kCloseHoverDropdownTimerId);
            }
            GiveHoverButtonTooltipBack(win);
            tb->hoverCmdId = cmdId;
            TakeHoverButtonTooltip(win, cmdId);
            tb->hoverSticky = true;
            return true;
        }
        HideToolbarHoverDropdown(win);
    }
    OpenHoverDropdown(win, cmdId);
    tb->hoverSticky = true;
    return true;
}

// The mouse moved over the toolbar (or left it): open, keep or close the
// drop-down of whatever button it is resting on. clientPt is the move; null
// on leave, which uses the real cursor so the drop-down stays up in it.
static void ToolbarHoverDropdownOnMouseMove(MainWindow* win, const Point* clientPt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (gInOpenHover || !tb || !tb->host || len(tb->hoverRegs) == 0) {
        return;
    }
    Point ptScreen = UiCursorScreenPos();
    bool overMenu = ToolbarHoverDropdownContainsScreenPoint(win, ptScreen);
    int cmdId = 0;
    VirtCtrl* w = nullptr;
    if (clientPt) {
        w = ToolbarItemFromPoint(win, *clientPt);
    } else if (HostHasPoint(tb->host, ptScreen)) {
        // a disabled button still gets its drop-down, the way it still gets its
        // tooltip: the rows say what could be done and why they are greyed
        w = ToolbarItemFromPoint(win, tb->host->FromScreen(ptScreen));
    }
    if (w && FindHoverReg(tb, w->id) && (w->id != CmdCreateAnnotInk || tb->hoverCmdId == w->id)) {
        cmdId = w->id;
    }

    if (tb->hoverCmdId != 0) {
        // one is open: keep it while the mouse is on its button or in it
        if (overMenu || cmdId == tb->hoverCmdId) {
            tb->host->KillTimer(kCloseHoverDropdownTimerId);
            if (cmdId == tb->hoverCmdId) {
                NoteHoverButtonMove(tb);
            }
            return;
        }
        if (cmdId != 0) {
            ToolbarHoverReg* from = FindHoverReg(tb, tb->hoverCmdId);
            ToolbarHoverReg* to = FindHoverReg(tb, cmdId);
            int group = from ? from->groupId : 0;
            if (group != 0 && to && to->groupId == group) {
                // both buttons share this drop-down, so it stays put: the two
                // sit side by side and sliding it between them would be a
                // twitch, not a new drop-down
                tb->host->KillTimer(kCloseHoverDropdownTimerId);
                GiveHoverButtonTooltipBack(win);
                tb->hoverCmdId = cmdId;
                TakeHoverButtonTooltip(win, cmdId);
                NoteHoverButtonMove(tb);
                return;
            }
            if (tb->hoverSticky) return;
            // moved straight onto another button that has one: swap to it
            // without the delay, the way a menu bar follows the mouse
            HideToolbarHoverDropdown(win);
            OpenHoverDropdown(win, cmdId);
            return;
        }
        if (tb->hoverSticky) return;
        tb->host->SetTimer(kCloseHoverDropdownTimerId, kCloseHoverDropdownDelayMs);
        return;
    }
    if (cmdId == tb->hoverPendingCmdId) {
        return;
    }
    tb->hoverPendingCmdId = cmdId;
    tb->host->KillTimer(kOpenHoverDropdownTimerId);
    if (cmdId != 0) {
        tb->host->SetTimer(kOpenHoverDropdownTimerId, UiTooltipDelayMs());
    }
}

// Cursor is on this drop-down, or on a button that shares it.
static bool CursorKeepsHoverMenu(MainWindow* win, Point pt) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || tb->hoverCmdId == 0) {
        return false;
    }
    if (ToolbarHoverDropdownContainsScreenPoint(win, pt)) {
        return true;
    }
    if (GetToolbarButtonScreenRect(win, tb->hoverCmdId).Contains(pt)) {
        return true;
    }
    ToolbarHoverReg* from = FindHoverReg(tb, tb->hoverCmdId);
    int group = from ? from->groupId : 0;
    if (group == 0) {
        return false;
    }
    for (ToolbarHoverReg& reg : tb->hoverRegs) {
        if (reg.groupId != group) {
            continue;
        }
        if (GetToolbarButtonScreenRect(win, reg.cmdId).Contains(pt)) {
            return true;
        }
    }
    return false;
}

static void OnHoverDropdownTimer(MainWindow* win, int timerId) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->host) {
        return;
    }
    if (timerId == kOpenHoverDropdownTimerId) {
        tb->host->KillTimer(kOpenHoverDropdownTimerId);
        int cmdId = tb->hoverPendingCmdId;
        tb->hoverPendingCmdId = 0;
        if (cmdId != 0 && cmdId != CmdCreateAnnotInk && !tb->hoverHost) {
            OpenHoverDropdown(win, cmdId);
        }
        return;
    }
    tb->host->KillTimer(kCloseHoverDropdownTimerId);
    if (tb->hoverSticky) {
        return;
    }
    if (CursorKeepsHoverMenu(win, UiCursorScreenPos())) {
        return;
    }
    // Posted moves are not the real cursor. A fresh one on the button keeps
    // the menu across a cursor yank between them.
    if (tb->hoverMoveTick != 0 && GetTickCount64() - tb->hoverMoveTick < (u64)kCloseHoverDropdownDelayMs) {
        tb->host->SetTimer(kCloseHoverDropdownTimerId, kCloseHoverDropdownDelayMs);
        return;
    }
    HideToolbarHoverDropdown(win);
}

struct ZoomHoverLevel {
    float zoom;
    int cmdId;
};

static void ZoomHoverLevels(Vec<ZoomHoverLevel>& out) {
    static Vec<ZoomHoverLevel> cached;
    if (len(cached) > 0) {
        for (const auto& level : cached) {
            VecAppend(out, level);
        }
        return;
    }
    Vec<float> levels;
    CollectZoomPickerLevels(levels);
    for (float zoom : levels) {
        int cmdId = CmdIdFromVirtualZoom(zoom);
        if (cmdId == CmdZoomCustom) {
            auto* cmd = CreateCommandFromDefinition(fmt("CmdZoomCustom %.2f", zoom));
            if (!cmd) {
                continue;
            }
            cmdId = cmd->id;
        }
        VecAppend(cached, ZoomHoverLevel{zoom, cmdId});
        VecAppend(out, ZoomHoverLevel{zoom, cmdId});
    }
}

// which of the levels the document is at, exact match only, -1 when it is at
// none of them (a zoom typed into Custom Zoom, or a fit mode not listed)
static int ZoomHoverCurrentIdx(MainWindow* win, const Vec<ZoomHoverLevel>& levels) {
    DocController* ctrl = win ? win->ctrl : nullptr;
    if (!ctrl) {
        return -1;
    }
    float current = ctrl->GetZoomVirtual(false);
    // the same fuzz DisplayModel::GetNextZoomStep uses to match a level
    constexpr float kZoomFuzz = 0.01f;
    for (int i = 0; i < len(levels); i++) {
        float zl = levels[i].zoom;
        if (current + kZoomFuzz >= zl && current - kZoomFuzz <= zl) {
            return i;
        }
    }
    return -1;
}

// Compact presets; custom entry and document zoom retain the full supported range.
static void BuildZoomHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    DocController* ctrl = win ? win->ctrl : nullptr;
    if (!ctrl) {
        return;
    }
    Vec<ZoomHoverLevel> levels;
    ZoomHoverLevels(levels);
    int currentIdx = ZoomHoverCurrentIdx(win, levels);

    Vec<ToolbarHoverMenuItem> items;
    for (int i = 0; i < len(levels); i++) {
        ToolbarHoverMenuItem it;
        it.text = ZoomLevelStrExact(levels[i].zoom);
        it.cmdId = levels[i].cmdId;
        it.isCurrent = i == currentIdx;
        VecAppend(items, it);
    }

    ev->layout = NewToolbarHoverStrip(win, items);
    ev->centerOnButton = true;
}

// The zoom moved (the buttons step it, and their strip stays up while they are
// clicked): box the level it landed on, if it landed on one. The strip stays
// where it is; sliding it out from under the mouse mid-click would be worse
// than the mark being off-centre.
static void UpdateZoomHoverDropdown(MainWindow* win) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->hoverHost) {
        return;
    }
    if (tb->hoverCmdId != CmdZoomIn && tb->hoverCmdId != CmdZoomOut) {
        return;
    }
    Vec<ZoomHoverLevel> levels;
    ZoomHoverLevels(levels);
    int currentIdx = ZoomHoverCurrentIdx(win, levels);
    for (int i = 0; i < len(tb->hoverItems); i++) {
        ToolbarHoverItemState& st = tb->hoverItems[i];
        if (!st.isStripCell || !st.ctrl) {
            continue;
        }
        bool isCurrent = i == currentIdx;
        auto* cell = (ToolbarHoverCell*)st.ctrl;
        if (cell->isCurrent == isCurrent) {
            continue;
        }
        cell->isCurrent = isCurrent;
        st.isCurrent = isCurrent;
        cell->Invalidate();
    }
}

// The Save button's drop-down: the three ways to end an editing session.
static void BuildLayoutHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    if (!win->ctrl) {
        return;
    }
    DisplayMode mode = win->ctrl->GetDisplayMode();
    Vec<ToolbarHoverMenuItem> items;
    VecAppend(items, {Str(kEnhancedIconPage), Tr("Single Page"), CmdSinglePageView, true, IsSingle(mode)});
    VecAppend(items, {Str(kEnhancedIconFitWidth), Tr("Facing"), CmdFacingView, true, IsFacing(mode)});
    VecAppend(items, {Str(kEnhancedIconFitWidth), Tr("Book View"), CmdBookView, true, IsBookView(mode)});
    VecAppend(items, {Str(kEnhancedIconFitWidth), Tr("Continuous scrolling"), CmdToggleContinuousView, true,
                      IsContinuous(mode)});
    ev->layout = NewToolbarHoverMenu(win, items);
    ev->centerOnButton = true;
}

static void BuildLassoHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    bool rectangular = win->annotationLasso.rectangular;
    Vec<ToolbarHoverMenuItem> items;
    VecAppend(items, {Str(kLassoToolIcon), Tr("Freehand selection"), CmdLassoFreehand, true, !rectangular});
    VecAppend(items, {Str(gIconAnnotSquare), Tr("Rectangle selection"), CmdLassoRectangle, true, rectangular});
    ev->layout = NewToolbarHoverMenu(win, items);
}

void ShowLassoToolbarActions(MainWindow* win) {
    if (!win || len(win->annotationLasso.selected) == 0) return;
    HideToolbarHoverDropdown(win);
    ShowLassoSelectionToolbar(win);
}

static void BuildSaveHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    auto* ctx = NewBuildMenuCtx(tab, Point{0, 0});
    AutoCall delCtx(DeleteBuildMenuCtx, ctx);
    bool dirty = ctx->hasUnsavedAnnotations;

    TempStr base = tab ? path::GetBaseNameTemp(tab->filePath) : TempStr{};
    Str saveText = Tr("Save changes to existing PDF");
    if (len(base) > 0) {
        saveText = fmt(Tr("Save changes to %s").s, base);
    }

    Vec<ToolbarHoverMenuItem> items;
    VecAppend(items, {Str(gIconSave), saveText, CmdSaveAnnotations, dirty});
    VecAppend(items, {Str(gIconSaveToNewFile), Tr("Save changes to a new PDF"), CmdSaveAnnotationsNewFile, dirty});
    VecAppend(items, {Str(gIconTrash), Tr("Discard changes"), CmdDiscardChanges, dirty});
    ev->layout = NewToolbarHoverMenu(win, items);
}

//--- the annotation buttons' color drop-down

// Every annotation button that creates something with a color offers the colors
// in Annotations.PresetColors: picking one becomes the color of new annotations
// of that type, and the pencil opens the color dialog on the whole set.
constexpr int kAnnotSwatchDx = 22;
// ring space around the circle, where the mark on the color in use goes
constexpr int kAnnotSwatchPad = 5;
constexpr int kAnnotColorsPad = 10;

// the buttons that offer the preset colors
// Redact is left out: its color is the box that covers the text, not a choice
static const int kAnnotColorCmds[] = {
    CmdAnnotationHighlightBrush, CmdCreateAnnotHighlight,  CmdCreateAnnotUnderline, CmdCreateAnnotSquiggly,
    CmdCreateAnnotStrikeOut,     CmdCreateAnnotText,       CmdCreateAnnotFreeText,  CmdCreateAnnotLine,
    CmdCreateAnnotPolyLine,      CmdCreateAnnotSquare,     CmdCreateAnnotCircle,    CmdCreateAnnotPolygon,
    CmdCreateAnnotInk,           CmdCreateAnnotStamp,      CmdCreateAnnotCaret,     CmdCreateAnnotFileAttachment,
    CmdCreateAnnotArrow,         CmdCreateAnnotDashedLine,
};

static bool IsAnnotColorCmd(int cmdId) {
    if (MathShapeForCommand(cmdId) >= 0) return true;
    for (int id : kAnnotColorCmds) {
        if (id == cmdId) {
            return true;
        }
    }
    return false;
}

static ParsedColor* AnnotPresetColorSetting(int cmdId) {
    if (!gSettings) {
        return nullptr;
    }
    Annotations& a = gSettings->annotations;
    if (MathShapeForCommand(cmdId) >= 0) return &a.lineColor;
    switch (cmdId) {
        // the highlighter makes highlight annotations
        case CmdAnnotationHighlightBrush:
        case CmdCreateAnnotHighlight:
            return &a.highlightColor;
        case CmdCreateAnnotUnderline:
            return &a.underlineColor;
        case CmdCreateAnnotSquiggly:
            return &a.squigglyColor;
        case CmdCreateAnnotStrikeOut:
            return &a.strikeOutColor;
        case CmdCreateAnnotText:
            return &a.textIconColor;
        case CmdCreateAnnotFreeText:
            // the text's color; the box behind it is FreeTextBackgroundColor
            return &a.freeTextColor;
        case CmdCreateAnnotLine:
        case CmdCreateAnnotArrow:
        case CmdCreateAnnotDashedLine:
            return &a.lineColor;
        case CmdCreateAnnotPolyLine:
            return &a.polyLineColor;
        case CmdCreateAnnotSquare:
            return &a.squareColor;
        case CmdCreateAnnotCircle:
            return &a.circleColor;
        case CmdCreateAnnotPolygon:
            return &a.polygonColor;
        case CmdCreateAnnotInk:
            return nullptr;
        case CmdCreateAnnotStamp:
            return &a.stampColor;
        case CmdCreateAnnotCaret:
            return &a.caretColor;
        case CmdCreateAnnotFileAttachment:
            return &a.fileAttachmentColor;
    }
    return nullptr;
}

static float* ShapeBorderWidth(int cmd) {
    auto& a = gSettings->annotations;
    if (MathShapeForCommand(cmd) >= 0) return &a.lineBorderWidth;
    switch (cmd) {
        case CmdCreateAnnotLine:
        case CmdCreateAnnotArrow:
        case CmdCreateAnnotDashedLine:
            return &a.lineBorderWidth;
        case CmdCreateAnnotPolyLine:
            return &a.polyLineBorderWidth;
        case CmdCreateAnnotSquare:
            return &a.squareBorderWidth;
        case CmdCreateAnnotCircle:
            return &a.circleBorderWidth;
        case CmdCreateAnnotPolygon:
            return &a.polygonBorderWidth;
    }
    return nullptr;
}

static void ShapeWidthPicked(const int* cmd, float width) {
    if (float* setting = ShapeBorderWidth(*cmd)) *setting = limitValue(width, 0.f, 64.f);
    ScheduleSaveSettings();
}

struct ShapeFillSettings {
    ParsedColor* color = nullptr;
    int* opacity = nullptr;
};

static ShapeFillSettings ShapeFillForCmd(int cmd) {
    if (!gSettings) return {};
    auto& a = gSettings->annotations;
    switch (cmd) {
        case CmdCreateAnnotLine:
        case CmdCreateAnnotArrow:
        case CmdCreateAnnotDashedLine:
            return {&a.lineInteriorColor, &a.lineInteriorOpacity};
        case CmdCreateAnnotPolyLine:
            return {&a.polyLineInteriorColor, &a.polyLineInteriorOpacity};
        case CmdCreateAnnotSquare:
            return {&a.squareInteriorColor, &a.squareInteriorOpacity};
        case CmdCreateAnnotCircle:
            return {&a.circleInteriorColor, &a.circleInteriorOpacity};
        case CmdCreateAnnotPolygon:
            return {&a.polygonInteriorColor, &a.polygonInteriorOpacity};
    }
    return {};
}

static const int* ShapeFillCommandPtr(int cmd) {
    for (const int& tool : kShapeFillCommands)
        if (tool == cmd) return &tool;
    return nullptr;
}

static void ShapeFillColorPicked(const int* cmd, Color color) {
    auto setting = ShapeFillForCmd(*cmd);
    if (!setting.color) return;
    SetColorText(*setting.color, color == kColorUnset ? Str{} : SerializeColorTemp(color & 0xffffff));
    ScheduleSaveSettings();
}

static void ShapeFillOpacityPicked(const int* cmd, int opacity) {
    auto setting = ShapeFillForCmd(*cmd);
    if (!setting.opacity) return;
    *setting.opacity = std::clamp(opacity, 0, 100);
    ScheduleSaveSettings();
}

static Color ParseAnnotColor(Str text) {
    Vec<Color> colors;
    ParseColorList(text, colors, 1);
    return len(colors) ? colors[0] : kColorUnset;
}

static TempStr AnnotColorText(Color color) {
    Vec<Color> colors;
    VecAppend(colors, color);
    return SerializeColorList(colors);
}

// What an annotation is made in when its setting is empty: MuPDF's defaults,
// which are also what Acrobat, PDF-XChange and Foxit use
static Color AnnotDefaultColor(int cmdId) {
    if (MathShapeForCommand(cmdId) >= 0) return MkRgba(0xff, 0, 0, 255);
    switch (cmdId) {
        case CmdCreateAnnotText:
        case CmdCreateAnnotFileAttachment:
            return MkRgba(0xff, 0xff, 0, 255);
        case CmdCreateAnnotFreeText:
            return MkRgba(0, 0, 0, 255);
        case CmdCreateAnnotCaret:
            return MkRgba(0, 0, 0xff, 255);
        case CmdCreateAnnotLine:
        case CmdCreateAnnotArrow:
        case CmdCreateAnnotDashedLine:
        case CmdCreateAnnotPolyLine:
        case CmdCreateAnnotSquare:
        case CmdCreateAnnotCircle:
        case CmdCreateAnnotPolygon:
        case CmdCreateAnnotStamp:
            return MkRgba(0xff, 0, 0, 255);
        case CmdCreateAnnotInk:
            // 40% yellow, Annotations.InkColor's default
            return 0x6600ffff;
    }
    return kColorUnset;
}

// the color the button's next annotation is made in
static Color AnnotCurrentColor(MainWindow* win, int cmdId) {
    if (cmdId == CmdCreateAnnotInk) return InkPenColor(win);
    if (cmdId == CmdToggleLaserPointer) return win->laserPointerColor;
    ParsedColor* setting = AnnotPresetColorSetting(cmdId);
    Color col = setting ? ParseAnnotColor(setting->s) : kColorUnset;
    return col != kColorUnset ? col : AnnotDefaultColor(cmdId);
}

// The colors a button offers. Ink has its own, translucent ones: they are
// exactly what it paints. cmdId 0 is not a button, and gets the presets
static Str* AnnotPresetColorList(int cmdId) {
    if (!gSettings) {
        return nullptr;
    }
    Annotations& a = gSettings->annotations;
    if (cmdId == CmdToggleLaserPointer) return &gSettings->laserColors;
    return (cmdId == CmdCreateAnnotInk) ? &a.inkColors : &a.presetColors;
}

static void AnnotPresetColors(int cmdId, Vec<Color>& out) {
    if (Str* list = AnnotPresetColorList(cmdId)) {
        ParseColorList(*list, out, 0);
    }
}

static void SetAnnotPresetColor(MainWindow* win, int cmdId, Color col) {
    if (cmdId == CmdToggleLaserPointer) {
        SetLaserPointerColor(win, col);
        return;
    }
    if (cmdId == CmdCreateAnnotInk) {
        SetInkPenColor(win, col);
        SetInkPenOpacity(win, ((int)GetAlpha(col) * 100 + 127) / 255);
        return;
    }
    ParsedColor* setting = AnnotPresetColorSetting(cmdId);
    if (!setting) {
        return;
    }
    SetColorText(*setting, AnnotColorText(col));
    ScheduleSaveSettings();
}

// A color as a filled circle, the way a highlighter's colors are shown. The one
// in use is ringed, and so is the one under the mouse.
struct ToolbarColorSwatch : VirtCtrl {
    Color col = kColorUnset;
    Str text; // owned; the color as text, for the -dbg-control dump
    bool isCurrent = false;
    bool isNone = false; // no color at all, drawn as an empty circle with a slash

    ToolbarColorSwatch() { cursor = CursorId::Hand; }
    ~ToolbarColorSwatch() override { str::Free(text); }

    Size GetIdealSize() override {
        int dx = UiScalePx(kAnnotSwatchDx) + (2 * UiScalePx(kAnnotSwatchPad));
        return {dx, dx};
    }

    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        int d = std::min(r.dx, r.dy);
        Rect ring{r.x + ((r.dx - d) / 2), r.y + ((r.dy - d) / 2), d, d};
        int t = UiScalePx(1);
        if (isCurrent || HasFlag(vwfHovered) || HasFlag(vwfFocused)) {
            // a filled disc with a smaller one of the background punched out of
            // it: Gfx fills ellipses but doesn't outline them
            ctx.gfx->FillEllipse(ring, TbTextColor());
            Rect hole = ring;
            hole.Inflate(-t, -t);
            ctx.gfx->FillEllipse(hole, TbBgColor());
        }
        Rect circle = ring;
        int p = UiScalePx(kAnnotSwatchPad);
        circle.Inflate(-p, -p);
        ctx.gfx->FillEllipse(circle, TbEdgeColor());
        circle.Inflate(-t, -t);
        if (isNone) {
            ctx.gfx->FillEllipse(circle, TbBgColor());
            // a slash from the lower left to the upper right, inset so it
            // stays inside the circle
            int inset = (int)((float)circle.dx * 0.15f);
            Point p1{circle.x + inset, circle.Bottom() - inset};
            Point p2{circle.Right() - inset, circle.y + inset};
            ctx.gfx->DrawLineAA(p1, p2, TbTextColor(), (float)t + 0.5f);
            return;
        }
        u8 a = GetAlpha(col);
        ctx.gfx->FillEllipse(circle, TbBgColor());
        ctx.gfx->FillEllipse(circle, col & 0xffffff, a);
    }
};

// whether the button's annotation can be made out of the text selected right now
static bool CanCreateAnnotFromSelection(MainWindow* win, int cmdId) {
    switch (cmdId) {
        case CmdCreateAnnotHighlight:
        case CmdCreateAnnotUnderline:
        case CmdCreateAnnotSquiggly:
        case CmdCreateAnnotStrikeOut:
            break;
        default:
            return false;
    }
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!tab || !win->showSelection || !tab->selectionOnPage) {
        return false;
    }
    DisplayModel* dm = win->AsFixed();
    return dm && ((cmdId == CmdCreateAnnotHighlight && len(*tab->selectionOnPage) > 0) ||
                  (dm->textSelection && dm->textSelection->result.len > 0));
}

static void OnAnnotColorClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* sw = ev ? (ToolbarColorSwatch*)ev->target : nullptr;
    if (!sw) {
        return;
    }
    if (ev->button != 0) {
        ev->didHandle = true;
        return;
    }
    SetAnnotPresetColor(win, sw->id, sw->col);
    if (CanCreateAnnotFromSelection(win, sw->id)) {
        // with text selected, picking a color is also a request to mark it up
        ToolbarPostCommand(win, sw->id);
    }
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "HideToolbarHoverDropdown");
}

// which button's color the generic color dialog is editing; a valid onPick
// instead means it was opened for something that is not a toolbar button
struct AnnotColorsTarget {
    MainWindow* win = nullptr;
    InkPenStyle style = InkPenStyle::Ballpoint;
    int cmdId = 0;
    Func1<Color> onPick;
    int shapeFillCmd = 0;
    bool forAnnotEditor = false;
    AnnotEditPickerContext editorContext;
};

static void AnnotColorsPicked(AnnotColorsTarget* target, ChangeColorsArgs* args) {
    if (target->forAnnotEditor && !IsAnnotEditPickerContextValid(target->editorContext)) {
        delete target;
        return;
    }
    Str* list = AnnotPresetColorList(target->cmdId);
    if (args->colorsChanged && list) {
        str::ReplaceWithCopy(list, SerializeColorList(args->colors));
        ScheduleSaveSettings();
    }
    const int* shapeCmd = ShapeFillCommandPtr(target->shapeFillCmd);
    if (args->didSelect && shapeCmd) {
        ShapeFillColorPicked(shapeCmd, args->color);
        ShapeFillOpacityPicked(shapeCmd, args->opacityPercent);
    } else if (args->didSelect && target->onPick.IsValid()) {
        target->onPick.Call(args->color);
    } else if (args->didSelect && args->color != kColorUnset) {
        if (target->cmdId == CmdCreateAnnotInk) {
            SetInkPenColor(target->style, args->color);
            SetInkPenOpacity(target->style, args->opacityPercent);
        } else
            SetAnnotPresetColor(target->win, target->cmdId, args->color);
    }
    delete target;
}

static void OnAnnotColorsEditClicked(MainWindow* win, VirtMouseEvent* ev) {
    VirtCtrl* w = ev ? ev->target : nullptr;
    if (!w) {
        return;
    }
    int cmdId = w->id;
    uitask::Post(MkFunc0(PostedHideHoverDropdown, win), "HideToolbarHoverDropdown");

    auto* target = new AnnotColorsTarget();
    target->win = win;
    target->style = win->inkPenStyle;
    target->cmdId = cmdId;

    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Annotation Colors");
    args->color = AnnotCurrentColor(win, cmdId);
    args->withOpacity = true;
    if (cmdId == CmdCreateAnnotInk) args->opacityPercent = InkPenOpacity(win);
    AnnotPresetColors(cmdId, args->colors);
    args->onClose = MkFunc1(AnnotColorsPicked, target);
    ShowChangeColorsDialog(args);
}

static void ShowShapeFillDialog(MainWindow* win, int cmd) {
    auto setting = ShapeFillForCmd(cmd);
    if (!setting.color || !setting.opacity) return;
    auto* target = new AnnotColorsTarget();
    target->shapeFillCmd = cmd;
    target->win = win;
    HideToolbarHoverDropdown(win);
    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Default background color");
    args->color = GetParsedColor(*setting.color, kColorUnset);
    args->withOpacity = true;
    args->opacityPercent = std::clamp(*setting.opacity, 0, 100);
    AnnotPresetColors(0, args->colors);
    args->onClose = MkFunc1(AnnotColorsPicked, target);
    ShowChangeColorsDialog(args);
}

static bool SameColorAndAlpha(Color a, Color b) {
    return a == b;
}

static bool AddAnnotPresetColor(int cmdId, Color col) {
    Str* list = AnnotPresetColorList(cmdId);
    if (!list || col == kColorUnset) return false;
    Vec<Color> colors;
    AnnotPresetColors(cmdId, colors);
    for (Color c : colors)
        if (SameColorAndAlpha(c, col)) return false;
    VecAppend(colors, col);
    str::ReplaceWithCopy(list, SerializeColorList(colors));
    ScheduleSaveSettings();
    return true;
}

static bool RemoveAnnotPresetColor(int cmdId, Color col) {
    Str* list = AnnotPresetColorList(cmdId);
    if (!list) return false;
    Vec<Color> colors;
    AnnotPresetColors(cmdId, colors);
    int count = len(colors);
    for (int i = len(colors) - 1; i >= 0; i--)
        if (SameColorAndAlpha(colors[i], col)) VecRemoveAt(colors, i);
    if (len(colors) == count) return false;
    str::ReplaceWithCopy(list, SerializeColorList(colors));
    ScheduleSaveSettings();
    return true;
}

static void RefreshAnnotColorPopup(MainWindow*);
static HWND BeginAnnotColorPopupMenu(MainWindow*, HWND);
static void EndAnnotColorPopupMenu(MainWindow*, HWND);

static void ReopenColorPalette(MainWindow* win) {
    if (!IsMainWindowValidAndNotClosing(win)) return;
    RefreshAnnotColorPopup(win);
    auto* tb = win->toolbarVirt;
    if (!tb || !tb->hoverHost || !tb->hoverCmdId) return;
    int cmd = tb->hoverCmdId;
    HideToolbarHoverDropdown(win);
    ShowToolbarButtonDropdown(win, cmd);
}

static Rect ColorActionAnchor(VirtCtrl* control) {
    Rect bounds = control->BoundsInWindow();
    return HwndMapRectToWindow(bounds, control->GetHwnd(), HWND_DESKTOP);
}

static void HoldColorPalette(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    if (!tb || !tb->host) return;
    tb->hoverSticky = true;
    tb->host->KillTimer(kCloseHoverDropdownTimerId);
}

static void SetShapeFillPopupCmd(int);

enum class ShapeFillAction {
    Color,
    Opacity
};

struct ShapeFillMenuRequest {
    MainWindow* win = nullptr;
    int cmd = 0;
    Rect anchor;
    ShapeFillAction action = ShapeFillAction::Color;
};

static void ShowShapeFillSettings(ShapeFillMenuRequest* request) {
    defer {
        delete request;
    };
    if (!IsMainWindowValidAndNotClosing(request->win)) return;
    const int* cmd = ShapeFillCommandPtr(request->cmd);
    auto setting = ShapeFillForCmd(request->cmd);
    if (!cmd || !setting.color || !setting.opacity) return;
    HideToolbarHoverDropdown(request->win);
    if (request->action == ShapeFillAction::Color) {
        ShowAnnotColorPopup(request->win, request->anchor, GetParsedColor(*setting.color, kColorUnset), true,
                            Tr("Default background color"), MkFunc1(ShapeFillColorPicked, cmd));
        SetShapeFillPopupCmd(request->cmd);
    } else
        ShowAnnotSliderPopup(request->win, request->anchor, Tr("Default background opacity (%)"),
                             std::clamp(*setting.opacity, 0, 100), 0, 100, MkFunc1(ShapeFillOpacityPicked, cmd));
}

static void QueueShapeFillSettings(MainWindow* win, int cmd, Rect anchor, ShapeFillAction action) {
    auto* request = new ShapeFillMenuRequest{win, cmd, anchor, action};
    uitask::Post(MkFunc0(ShowShapeFillSettings, request), "Shape background settings");
}

static void OnPresetColorMenu(MainWindow* win, VirtMouseEvent* ev) {
    auto* swatch = (ToolbarColorSwatch*)ev->target;
    if (swatch->isNone) return;
    ev->didHandle = true;
    int cmd = swatch->id;
    Color color = swatch->col;
    Rect anchor = ColorActionAnchor(swatch);
    HWND source = swatch->GetHwnd();
    HWND owner = BeginAnnotColorPopupMenu(win, source);
    HoldColorPalette(win);
    enum {
        PinColor = 1,
        RemoveColor = 2,
        BackgroundColor = 3,
        BackgroundOpacity = 4
    };
    HMENU menu = CreatePopupMenu();
    bool pinnable = IsAnnotColorCmd(cmd) || cmd == CmdToggleLaserPointer;
    bool pinned = pinnable && PinnedToolIndex(win, cmd, color) >= 0;
    if (pinnable)
        AppendMenuW(menu, MF_STRING | (pinned ? MF_CHECKED : 0), PinColor,
                    CWStrTemp(pinned ? Tr("Unpin tool with this color") : Tr("Pin tool with this color")));
    AppendMenuW(menu, MF_STRING, RemoveColor, CWStrTemp(Tr("Remove color from palette")));
    if (ShapeFillForCmd(cmd).color) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, BackgroundColor, CWStrTemp(Tr("Default background color...")));
        AppendMenuW(menu, MF_STRING, BackgroundOpacity, CWStrTemp(Tr("Default background opacity...")));
    }
    MarkMenuOwnerDraw(menu);
    if (pinnable) SetMenuPinIcon(menu, PinColor);
    int picked = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.Bottom(), 0, owner, nullptr);
    FreeMenuOwnerDrawInfoData(menu);
    DestroyMenu(menu);
    EndAnnotColorPopupMenu(win, source);
    if (!IsMainWindowValidAndNotClosing(win)) return;
    if (picked == PinColor) TogglePinnedTool(win, cmd, color);
    if (picked == BackgroundColor || picked == BackgroundOpacity) {
        SetAnnotPresetColor(win, cmd, color);
        QueueShapeFillSettings(win, cmd, anchor,
                               picked == BackgroundColor ? ShapeFillAction::Color : ShapeFillAction::Opacity);
    }
    if (picked == RemoveColor && RemoveAnnotPresetColor(cmd, color))
        uitask::Post(MkFunc0(ReopenColorPalette, win), "Refresh color palette");
}

static void OnAddPresetColor(MainWindow* win, VirtMouseEvent* ev) {
    ev->didHandle = true;
    int cmd = ev->target->id;
    Rect anchor = ColorActionAnchor(ev->target);
    HWND source = ev->target->GetHwnd();
    HWND owner = BeginAnnotColorPopupMenu(win, source);
    Vec<Color> choices;
    Settings* defaults = NewSettings({});
    Str available = cmd == CmdCreateAnnotInk       ? defaults->annotations.inkColors
                    : cmd == CmdToggleLaserPointer ? defaults->laserColors
                                                   : defaults->annotations.presetColors;
    ParseColorList(available, choices, 0);
    DeleteSettings(defaults);
    Vec<Color> custom;
    ParseColorList(gSettings->customColors, custom, 0);
    VecAppend(custom, AnnotCurrentColor(win, cmd));
    for (Color color : custom) {
        if (color == kColorUnset) continue;
        bool duplicate = false;
        for (Color existing : choices) duplicate |= SameColorAndAlpha(existing, color);
        if (!duplicate) VecAppend(choices, color);
    }
    Vec<Color> present;
    AnnotPresetColors(cmd, present);
    HoldColorPalette(win);
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < len(choices); i++) {
        bool added = false;
        for (Color color : present) added |= SameColorAndAlpha(color, choices[i]);
        AppendMenuW(menu, MF_STRING | (added ? MF_CHECKED | MF_GRAYED : 0), i + 1,
                    CWStrTemp(AnnotColorText(choices[i])));
    }
    MarkMenuOwnerDraw(menu);
    int picked = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.Bottom(), 0, owner, nullptr);
    FreeMenuOwnerDrawInfoData(menu);
    DestroyMenu(menu);
    EndAnnotColorPopupMenu(win, source);
    if (!IsMainWindowValidAndNotClosing(win)) return;
    if (picked > 0 && picked <= len(choices) && AddAnnotPresetColor(cmd, choices[picked - 1]))
        uitask::Post(MkFunc0(ReopenColorPalette, win), "Refresh color palette");
}

// The drop-down's content: the preset colors as swatches with the one in use
// ringed, and a button that opens the color dialog on the whole set. cmdId is 0
// when this is not a toolbar button's drop-down, and nothing is recorded then.
// swatchesOut, when given, collects the swatches in the order they are laid
// out, for the -dbg-control dump
static ILayout* MakeAnnotColorsPanel(MainWindow* win, Str label, Color current, int cmdId, bool withNone,
                                     Vec<ToolbarColorSwatch*>* swatchesOut, const Func1<VirtMouseEvent*>& onSwatch,
                                     const Func1<VirtMouseEvent*>& onEdit, ILayout* extra = nullptr, Str title = {},
                                     VirtIconButton** editOut = nullptr, ILayout* advanced = nullptr) {
    ToolbarVirt* tb = win->toolbarVirt;
    Vec<Color> colors;
    AnnotPresetColors(cmdId, colors);

    auto* row = new Wrap();
    row->alignCross = CrossAxisAlign::CrossCenter;
    if (withNone) {
        // for a color that can be left out, like a shape's interior
        auto* sw = new ToolbarColorSwatch();
        sw->id = cmdId;
        sw->col = kColorUnset;
        sw->isNone = true;
        sw->isCurrent = (current == kColorUnset);
        str::ReplaceWithCopy(&sw->text, StrL("none"));
        // the named-color menu calls it that, untranslated like the other color names
        sw->SetTooltip(StrL("Transparent"));
        sw->onClick = onSwatch;
        sw->SetFlag(vwfFocusable, true);
        row->AddChild(sw);
        if (swatchesOut) {
            VecAppend(*swatchesOut, sw);
        }
    }
    for (Color col : colors) {
        auto* sw = new ToolbarColorSwatch();
        sw->id = cmdId;
        sw->col = col;
        sw->isCurrent = SameColorAndAlpha(col, current);
        str::ReplaceWithCopy(&sw->text, AnnotColorText(col));
        sw->onClick = onSwatch;
        sw->onContextMenu = MkFunc1(OnPresetColorMenu, win);
        sw->SetFlag(vwfFocusable, true);
        sw->SetTooltip(fmt("%s. %s", sw->text, Tr("Right-click for pin and palette options")));
        row->AddChild(sw);
        if (swatchesOut) {
            VecAppend(*swatchesOut, sw);
        }
        if (cmdId != 0) {
            RecordHoverItem(tb, sw, sw->text, {{}, sw->text, cmdId, true, sw->isCurrent});
        }
    }

    auto* edit = new VirtIconButton();
    int iconSize = tb->iconSize;
    int pad = UiScalePx(kAnnotSwatchPad);
    edit->id = cmdId;
    edit->padding = {pad, pad, pad, pad};
    edit->pixmap = GetCachedPixmapForSvg(Str(GetColorPickerIconSvg()), iconSize, iconSize, TbTextColor(), TbBgColor());
    edit->SetTooltip(fmt("%s: %s", label, Tr("Edit colors")));
    RecordHoverItem(tb, edit, edit->tooltip, {{}, edit->tooltip, cmdId, true, false});
    edit->onClick = onEdit;
    if (editOut) *editOut = edit;
    auto* labelRow = (HBox*)NewPaletteHeader(win, label, cmdId, advanced);
    auto* add = new VirtIconButton();
    add->id = cmdId;
    add->name = StrL("palette-add");
    add->padding = {pad, pad, pad, pad};
    static const char* plus =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round"><path d="M12 5v14M5 12h14"/></svg>)";
    add->pixmap = GetCachedPixmapForSvg(Str(plus), iconSize, iconSize, TbTextColor(), TbBgColor());
    add->SetTooltip(fmt("%s: %s", label, Tr("Add a preset color")));
    RecordHoverItem(tb, add, add->tooltip, {{}, add->tooltip, cmdId, true, false});
    add->onClick = MkFunc1(OnAddPresetColor, win);
    labelRow->AddChild(add);
    labelRow->AddChild(edit);
    Insets labelInsets{.bottom = UiScalePx(4)};

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    vbox->AddChild(new Padding(labelRow, labelInsets));
    vbox->AddChild(row);
    if (extra) vbox->AddChild(extra);
    if (advanced) {
        advanced->SetVisibility(Visibility::Collapse);
        vbox->AddChild(advanced);
    }
    int b = UiScalePx(kHoverMenuBorder);
    int p = UiScalePx(kAnnotColorsPad);
    return new Padding(vbox, Insets{b + p, b + p, b + p, b + p});
}

//--- the ink button's drop-down also sets how thick the stroke is

// Annotations.InkBorderWidth is in PDF points, which is about a pixel at 100%
constexpr float kInkThicknessMin = 0.1f;
constexpr int kInkThicknessMax = 16;
constexpr int kInkPreviewDy = 28;
constexpr int kInkSliderDx = 190;
// how far the preview's wave swings, as a part of the room left by the stroke
constexpr float kInkPreviewWave = 0.42f;

static float InkThickness(MainWindow* win) {
    float v = InkPenWidth(win);
    return limitValue(v, gSettings->penMinWidth, gSettings->penMaxWidth);
}

// What the ink button will lay down: the color in use, drawn as thick as the
// slider is set to. It follows the slider while it's being dragged.
struct InkStrokePreview : VirtCtrl {
    Color col = kColRed | 0xff000000;
    float thickness = kInkThicknessMin;

    Size GetIdealSize() override { return {UiScalePx(kInkSliderDx), UiScalePx(kInkPreviewDy)}; }

    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        float w = thickness * (float)UiScalePx(100) / 100.f;
        // the stroke has to fit the preview whatever the thickness
        w = std::min(w, (float)r.dy / 2.f);
        w = std::max(w, 1.f);
        int inset = (int)(w / 2.f) + UiScalePx(2);
        int x0 = r.x + inset;
        int x1 = r.Right() - inset;
        if (x1 <= x0) {
            return;
        }
        float midY = (float)r.y + ((float)r.dy / 2.f);
        float amp = (((float)r.dy / 2.f) - (float)inset) * kInkPreviewWave;
        u8 a = GetAlpha(col);
        Color c = col & 0xffffff;
        // one period of a sine, as a run of short anti-aliased segments, with a
        // disc at every joint: the segments are butt-capped and a thick curve
        // would be notched without them
        constexpr int kSegs = 48;
        int d = (int)w;
        u8 alpha = a;
        Point prev{};
        for (int i = 0; i <= kSegs; i++) {
            float u = (float)i / (float)kSegs;
            int x = x0 + (int)(u * (float)(x1 - x0));
            int y = (int)(midY + (amp * sinf(u * 2.f * 3.14159265f)));
            Point pt{x, y};
            if (i > 0) {
                ctx.gfx->DrawLineAA(prev, pt, c, w, alpha);
            }
            if (d > 2) {
                ctx.gfx->FillEllipse(Rect{pt.x - (d / 2), pt.y - (d / 2), d, d}, c, alpha);
            }
            prev = pt;
        }
    }
};

// Virtual fields share the palette's clipping and cannot leave child HWND trails while scrolling.
struct PaletteNumberEdit : VirtCtrl {
    PlatformFont* font = nullptr;
    Str text;
    float minimum = 0, maximum = 100;
    int decimals = 0;
    int caret = 0, anchor = 0;
    bool invalid = false;
    Func1<float> onValue;

    PaletteNumberEdit(PlatformFont* f, float min, float max, int places)
        : font(f), minimum(min), maximum(max), decimals(places) {
        name = StrL("palette-number");
        cursor = CursorId::IBeam;
        flags |= vwfFocusable;
        onMouseDown = MkMethod1<PaletteNumberEdit, VirtMouseEvent*, &PaletteNumberEdit::MouseDown>(this);
        onKeyDown = MkMethod1<PaletteNumberEdit, VirtKeyEvent*, &PaletteNumberEdit::KeyDown>(this);
        onChar = MkMethod1<PaletteNumberEdit, VirtCharEvent*, &PaletteNumberEdit::Char>(this);
        onFocusChanged = MkMethod1<PaletteNumberEdit, VirtFocusEvent*, &PaletteNumberEdit::Focus>(this);
    }
    ~PaletteNumberEdit() override { str::Free(text); }
    Size GetIdealSize() override {
        return {std::max(PlatformFontMeasureText(font, StrL("000")).dx, PlatformFontMeasureText(font, text).dx) +
                    UiScalePx(16),
                PlatformFontLineHeight(font) + UiScalePx(12)};
    }
    void SetValue(float value) {
        int previousWidth = GetIdealSize().dx;
        str::ReplaceWithCopy(&text, decimals ? fmt("%g", value) : fmt("%.0f", value));
        caret = anchor = len(text);
        invalid = false;
        if (GetIdealSize().dx != previousWidth) RequestLayout();
        Invalidate();
    }
    bool Parse(float& value) {
        TempStr input = str::DupTemp(text);
        str::TrimWSInPlace(input, str::TrimOpt::Both);
        if (!len(input)) return false;
        char* end = nullptr;
        double parsed = strtod(CStrTemp(input), &end);
        if (!end || *end || !isfinite(parsed) || parsed < minimum || parsed > maximum ||
            (!decimals && floor(parsed) != parsed))
            return false;
        value = (float)parsed;
        return true;
    }
    bool Commit() {
        float value = 0;
        if (!Parse(value)) {
            invalid = true;
            Invalidate();
            return false;
        }
        SetValue(value);
        onValue.Call(value);
        return true;
    }
    void ReplaceSelection(Str insert) {
        int previousWidth = GetIdealSize().dx;
        int start = std::min(caret, anchor), end = std::max(caret, anchor);
        if (len(text) - (end - start) + len(insert) > 12) return;
        str::Builder next;
        next.Append(Str(text.s, start));
        next.Append(insert);
        next.Append(Str(text.s + end, len(text) - end));
        str::ReplaceWithCopy(&text, ToStr(next));
        caret = anchor = start + len(insert);
        invalid = false;
        if (GetIdealSize().dx != previousWidth) RequestLayout();
        Invalidate();
    }
    void MoveCaret(int to, bool select) {
        caret = std::clamp(to, 0, len(text));
        if (!select) anchor = caret;
        Invalidate();
    }
    void MouseDown(VirtMouseEvent* ev) {
        if (ev->button != 0) return;
        if (GetHwnd()) ::SetFocus(GetHwnd());
        anchor = 0;
        caret = len(text);
        Invalidate();
        ev->didHandle = true;
    }
    void Focus(VirtFocusEvent* ev) {
        if (ev->gotFocus) {
            anchor = 0;
            caret = len(text);
        } else {
            Commit();
        }
        Invalidate();
    }
    void KeyDown(VirtKeyEvent* ev) {
        if (ev->isCtrl && ev->vkey == 'A') {
            anchor = 0;
            caret = len(text);
        } else if (ev->isCtrl && ev->vkey == 'C') {
            int start = std::min(caret, anchor), count = abs(caret - anchor);
            if (count) CopyTextToClipboard(Str(text.s + start, count));
        } else if (ev->isCtrl && ev->vkey == 'V') {
            if (OpenClipboard(GetHwnd())) {
                HANDLE data = GetClipboardData(CF_UNICODETEXT);
                auto* wide = data ? (wchar_t*)GlobalLock(data) : nullptr;
                if (wide) {
                    TempStr paste = ToUtf8Temp(WStr(wide));
                    str::TrimWSInPlace(paste, str::TrimOpt::Both);
                    bool allowed = len(paste) != 0;
                    for (int i = 0; i < len(paste); i++) {
                        char c = paste.s[i];
                        allowed &= (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+';
                    }
                    if (allowed) ReplaceSelection(paste);
                    GlobalUnlock(data);
                }
                CloseClipboard();
            }
        } else if (ev->vkey == VK_LEFT || ev->vkey == VK_RIGHT) {
            int next = caret + (ev->vkey == VK_LEFT ? -1 : 1);
            if (!ev->isShift && caret != anchor)
                next = ev->vkey == VK_LEFT ? std::min(caret, anchor) : std::max(caret, anchor);
            MoveCaret(next, ev->isShift);
        } else if (ev->vkey == VK_HOME || ev->vkey == VK_END) {
            MoveCaret(ev->vkey == VK_HOME ? 0 : len(text), ev->isShift);
        } else if (ev->vkey == VK_DELETE) {
            if (caret == anchor && caret < len(text)) anchor++;
            ReplaceSelection({});
        } else if (ev->vkey == VK_RETURN) {
            Commit();
        } else if (ev->vkey == VK_ESCAPE) {
            // Escape belongs to the popup; valid edits were already committed on blur.
            return;
        } else if (ev->vkey == VK_UP || ev->vkey == VK_DOWN) {
            float value = 0;
            if (Parse(value)) {
                SetValue(std::clamp(value + (ev->vkey == VK_UP ? 1.f : -1.f), minimum, maximum));
                Commit();
            }
        } else
            return;
        ev->didHandle = true;
        Invalidate();
    }
    void Char(VirtCharEvent* ev) {
        int c = ev->c;
        if (c == VK_BACK) {
            if (caret == anchor && caret > 0) anchor = caret - 1;
            ReplaceSelection({});
        } else if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            char byte = (char)c;
            ReplaceSelection(Str(&byte, 1));
        }
        ev->didHandle = true;
    }
    void Paint(VirtPaintCtx& ctx) override {
        ctx.gfx->FillRoundedRect(ctx.bounds, UiScalePx(4), ThemeWindowControlBackgroundColor(),
                                 invalid               ? kColRed
                                 : HasFlag(vwfFocused) ? ThemeBrandColor()
                                                       : ThemeEdgeColor());
        Rect area = ctx.bounds;
        area.SubLR(UiScalePx(7), UiScalePx(7));
        ctx.gfx->PushClip(area.Intersect(ctx.clip));
        int start = std::min(caret, anchor), count = abs(caret - anchor);
        int textY = area.y + (area.dy - PlatformFontLineHeight(font)) / 2;
        if (HasFlag(vwfFocused) && count) {
            int left = PlatformFontMeasureText(font, Str(text.s, start)).dx;
            int width = PlatformFontMeasureText(font, Str(text.s + start, count)).dx;
            ctx.gfx->FillRect({area.x + left, textY, width, PlatformFontLineHeight(font)}, TbHoverColor());
        }
        ctx.gfx->DrawText(text, area, gfxTextVCenter, font, TbTextColor());
        if (HasFlag(vwfFocused) && !count) {
            int x = area.x + PlatformFontMeasureText(font, Str(text.s, caret)).dx;
            ctx.gfx->DrawLine({x, textY, 0, PlatformFontLineHeight(font)}, TbTextColor());
        }
        ctx.gfx->PopClip();
    }
};

// Dragging it is the width of the next ink annotation, and of the stroke the
// preview shows. The preview is a sibling in the same drop-down, so it lives
// exactly as long as the slider does.
struct InkThicknessSlider : VirtSlider {
    MainWindow* win = nullptr;
    InkStrokePreview* preview = nullptr;
    // the width as a number, under the slider; a sibling like the preview
    VirtText* valueText = nullptr;
    PaletteNumberEdit* valueEdit = nullptr;
    float exactWidth = -1;
    Str text; // owned; what the -dbg-control dump shows for the slider
    // where the width goes when let go. Without one it's the setting the ink
    // button makes its strokes with
    Func1<float> onThickness;
    Func1<int> onInteger;
    bool fractional = false;
    float minimum = 0.1f;
    float step = 0.1f;
    float Width() { return exactWidth >= 0 ? exactWidth : fractional ? minimum + (float)value * step : (float)value; }
    // committed by letting go of a drag, so the mouse capture is about to be
    // released; the color popup that holds the mouse takes it back
    bool releasingMouse = false;

    ~InkThicknessSlider() override { str::Free(text); }

    void UpdateDisplay() {
        if (preview) {
            preview->thickness = Width();
            preview->Invalidate();
        }
        if (valueText) {
            valueText->SetText(fractional ? fmt("%.2f", Width()) : fmt("%d", value));
            valueText->Invalidate();
        }
        if (valueEdit) valueEdit->SetValue(Width());
        if (!onThickness.IsValid() && !onInteger.IsValid() && win) {
            SetInkPenWidth(win, Width());
        }
    }
    void OnChanged() {
        exactWidth = -1;
        UpdateDisplay();
    }
    void SetTypedWidth(float width) {
        value = limitValue((int)roundf((width - minimum) / step), minVal, maxVal);
        exactWidth = width;
        UpdateDisplay();
        Invalidate();
        if (onThickness.IsValid())
            onThickness.Call(width);
        else
            ScheduleSaveSettings();
    }
    // rewriting an annotation is too slow to do on every step of a drag, so
    // the width lands when the slider is let go
    void OnCommitted() {
        // a mouse-up commits while still adjusting, a wheel step doesn't
        releasingMouse = IsAdjusting();
        OnChanged();
        if (onInteger.IsValid()) {
            onInteger.Call(value);
        } else if (onThickness.IsValid()) {
            onThickness.Call(Width());
        } else {
            ScheduleSaveSettings();
        }
    }
};

// sliderOut gets the slider, for the caller to record once the colors are in.
// thickness < 0 starts the slider at Annotations.InkBorderWidth and leaves the
// width there; otherwise it starts there and onThickness gets it
static ILayout* MakeInkThicknessPanel(MainWindow* win, Color current, float thickness, const Func1<float>& onThickness,
                                      Str label, float minThickness, InkThicknessSlider** sliderOut,
                                      float maxThickness = -1, bool editable = false) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (thickness < 0) {
        thickness = InkThickness(win);
    }
    float minWidth = minThickness > 0 ? std::max(gSettings->penMinWidth, minThickness) : 0.f;
    float maxWidth = std::max(minWidth, maxThickness > 0 ? maxThickness : gSettings->penMaxWidth);
    float step = limitValue(gSettings->penWidthStep, 0.1f, 16.f);
    thickness = limitValue(thickness, minWidth, maxWidth);

    auto* preview = new InkStrokePreview();
    preview->col = (current == kColorUnset) ? kColRed : current;
    preview->thickness = thickness;

    auto* slider = new InkThicknessSlider();
    slider->win = win;
    slider->fractional = true;
    slider->minimum = minWidth;
    slider->step = step;
    slider->minVal = 0;
    slider->maxVal = std::max(0, (int)floorf((maxWidth - minWidth) / step + 0.001f));
    slider->value = limitValue((int)roundf((thickness - minWidth) / step), 0, slider->maxVal);
    slider->idealDx = UiScalePx(kInkSliderDx);
    slider->preview = preview;
    slider->exactWidth = editable ? thickness : -1;
    slider->onThickness = onThickness;
    slider->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(slider);
    slider->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(slider);
    str::ReplaceWithCopy(&slider->text, fmt("thickness=%.2f", thickness));

    auto* ends = new HBox();
    ends->alignMain = MainAxisAlign::Homogeneous;
    ends->alignCross = CrossAxisAlign::CrossCenter;
    auto mkLabel = [tb](Str s) {
        return NewVirtText({
            .s = s,
            .font = tb->platformFont,
            .textColor = TbDisabledColor(),
            .isRtl = IsUIRtl(),
            .padding = {.right = UiScalePx(2), .left = UiScalePx(2)},
        });
    };
    // the width as a number, centered between the ends. Padded out to the
    // widest value so it keeps its place when a drag adds a digit.
    TempStr valueStr = fmt("%.2f", thickness);
    int widestDx = PlatformFontMeasureText(tb->platformFont, fmt("%.2f", maxWidth)).dx;
    int extraDx = std::max(widestDx - PlatformFontMeasureText(tb->platformFont, valueStr).dx, 0);
    auto* valueText = NewVirtText({
        .s = valueStr,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .align = VirtTextAlign::Center,
        .isRtl = IsUIRtl(),
        .padding = {.right = extraDx - (extraDx / 2), .left = extraDx / 2},
    });
    slider->valueText = editable ? nullptr : valueText;

    ends->AddChild(mkLabel(Tr("Thin")));
    if (!editable)
        ends->AddChild(valueText);
    else
        delete valueText;
    auto* thick = mkLabel(Tr("Thick"));
    thick->align = VirtTextAlign::Right;
    ends->AddChild(thick);

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    int gap = UiScalePx(6);
    vbox->AddChild(new Padding(preview, Insets{gap, 0, gap, 0}));
    vbox->AddChild(NewVirtText({
        .s = label,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .isRtl = IsUIRtl(),
    }));
    if (editable) {
        auto* row = new HBox();
        row->alignCross = CrossAxisAlign::CrossCenter;
        row->gap = UiScalePx(10);
        auto* field = new PaletteNumberEdit(tb->platformFont, minWidth, maxWidth, 2);
        field->name = StrL("shape-border-width-value");
        field->SetTooltip(fmt(Tr("Border thickness: %g to %g pt").s, minWidth, maxWidth));
        field->SetValue(thickness);
        field->onValue = MkMethod1<InkThicknessSlider, float, &InkThicknessSlider::SetTypedWidth>(slider);
        slider->valueEdit = field;
        row->AddChild(slider, 1);
        row->AddChild(field);
        vbox->AddChild(row);
    } else {
        vbox->AddChild(slider);
    }
    vbox->AddChild(ends);
    *sliderOut = slider;
    return vbox;
}

struct PaletteIconButton : VirtButton {
    Pixmap* pixmap = nullptr;
    bool isCurrent = false;
    PaletteIconButton(Str text, PlatformFont* font) : VirtButton(text, font) {
        name = s;
        SetTooltip(s);
    }
    Size GetIdealSize() override {
        int pad = UiScalePx(6);
        return {pixmap ? pixmap->width + 2 * pad : 2 * pad, pixmap ? pixmap->height + 2 * pad : 2 * pad};
    }
    void Paint(VirtPaintCtx& ctx) override {
        if (isCurrent || HasFlag(vwfHovered) || HasFlag(vwfFocused)) {
            ctx.gfx->FillRoundedRect(ctx.bounds, UiScalePx(6), TbHoverColor(),
                                     isCurrent || HasFlag(vwfFocused) ? ThemeBrandColor() : TbHoverColor());
        }
        if (pixmap) {
            ctx.gfx->DrawPixmap(pixmap,
                                {ctx.bounds.x + (ctx.bounds.dx - pixmap->width) / 2,
                                 ctx.bounds.y + (ctx.bounds.dy - pixmap->height) / 2, pixmap->width, pixmap->height});
        }
    }
};

static PaletteIconButton* NewPaletteButton(MainWindow* win, Str svg, Str text) {
    ToolbarVirt* tb = win->toolbarVirt;
    auto* button = new PaletteIconButton(text, tb->platformFont);
    button->pixmap = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
    return button;
}

struct InkPenTile : VirtButton {
    Pixmap* pixmap = nullptr;
    bool isCurrent = false;
    bool showLabel = true;
    int imagePad = 0;
    int preferredDx = 0;
    int index = 0;
    Color color = kColorUnset;
    InkPenTile() : VirtButton({}) {}
    Size GetIdealSize() override {
        int imageDx = pixmap ? pixmap->width : UiScalePx(40);
        int imageDy = pixmap ? pixmap->height : UiScalePx(64);
        Size label = showLabel ? PlatformFontMeasureText(font, s) : Size{};
        return {std::max(imageDx, label.dx) + 2 * imagePad, imageDy + label.dy + 2 * imagePad};
    }
    void Paint(VirtPaintCtx& ctx) override {
        Rect r = ctx.bounds;
        if (isCurrent || HasFlag(vwfHovered) || HasFlag(vwfFocused)) {
            ctx.gfx->FillRoundedRect(r, UiScalePx(10), TbHoverColor(),
                                     isCurrent || HasFlag(vwfFocused) ? ThemeBrandColor() : TbHoverColor());
        }
        if (pixmap) {
            ctx.gfx->DrawPixmap(pixmap,
                                {r.x + (r.dx - pixmap->width) / 2, r.y + imagePad, pixmap->width, pixmap->height});
        }
        if (!showLabel) return;
        int labelDy = PlatformFontLineHeight(font);
        Rect label{r.x, r.Bottom() - labelDy - imagePad, r.dx, labelDy};
        ctx.gfx->DrawText(s, label, gfxTextCenter | gfxTextVCenter, font, TbTextColor());
    }
};

static void PinToolClick(MainWindow*, VirtMouseEvent*);

static void OnPaletteSettings(MainWindow* win, VirtMouseEvent* ev) {
    auto* extra = (ILayout*)ev->target->userData;
    extra->SetVisibility(IsCollapsed(extra) ? Visibility::Visible : Visibility::Collapse);
    ev->didHandle = true;
    auto* host = win->toolbarVirt->hoverHost;
    if (!host) return;
    Size size = host->SetLayoutSizedToContent(host->layout);
    Rect bounds = host->ScreenRect();
    bounds.dx = size.dx;
    bounds.dy = size.dy;
    host->SetPos(ShiftRectToWorkArea(bounds, win->hwndFrame, true), true);
}

static ILayout* NewPaletteHeader(MainWindow* win, Str title, int cmd, ILayout* advanced) {
    auto* row = new HBox();
    row->gap = UiScalePx(4);
    row->alignCross = CrossAxisAlign::CrossCenter;
    row->rtl = IsUIRtl();
    row->AddChild(NewVirtText({.s = title,
                               .font = win->toolbarVirt->platformFont,
                               .textColor = TbTextColor(),
                               .isRtl = IsUIRtl(),
                               .ellipsis = true}),
                  1);
    if (cmd != 0) {
        bool pinned = PinnedToolIndex(win, cmd) >= 0;
        auto* pin = NewPaletteButton(win, Str(GetPinIconSvg()), pinned ? Tr("Unpin this tool") : Tr("Pin this tool"));
        pin->isCurrent = pinned;
        pin->id = cmd;
        pin->onClick = MkFunc1(PinToolClick, win);
        row->AddChild(pin);
    }
    if (advanced) {
        auto* settings = NewPaletteButton(win, Str(kToolSettingsIcon), Tr("Additional tool settings"));
        settings->userData = (intptr_t)advanced;
        settings->onClick = MkFunc1(OnPaletteSettings, win);
        row->AddChild(settings);
    }
    return row;
}

// Original upright instruments: color barrels, metal nibs and distinct tips.
static TempStr InkPenSvg(int index, Color color) {
    const char* shapes[] = {
        R"(<path d="M23 89V42l4-18 5-10 5 10 4 18v47Z" fill="#bac4cf"/><path d="M23 89V55h18v34Z" fill="%s"/><path d="M29 25l3-11 3 11v8h-6Z" fill="%s"/><path d="M25 44h14M27 60v21" fill="none"/><path d="M37 43v12" fill="none" stroke-width="3"/>)",
        R"(<path d="M22 89V57l-2-16L32 8l12 33-2 16v32Z" fill="#bac4cf"/><path d="M22 89V57h20v32Z" fill="%s"/><path d="M32 8v31" fill="none"/><circle cx="32" cy="42" r="3" fill="%s"/><path d="M23 52h18M26 65v17" fill="none"/>)",
        R"(<path d="M23 89V53h18v36Z" fill="%s"/><path d="M24 52C17 36 24 18 32 8c-2 14 14 17 8 44Z" fill="%s"/><path d="M24 48h16v9H24Z" fill="#bac4cf"/><path d="M29 24c-4 9-4 14-1 19M27 63v19" fill="none"/>)",
        R"(<path d="M22 89V37l10-27 10 27v52Z" fill="%s"/><path d="m22 37 10-27 10 27-7-4-3 5-3-5Z" fill="#e9caa1"/><path d="m29 18 3-8 3 8Z" fill="%s"/><path d="M29 42v47M35 42v47" fill="none"/>)",
        R"(<path d="M19 89V47l4-11V20l18-7v23l4 11v42Z" fill="#bac4cf"/><path d="M23 20l18-7v16l-18 7Z" fill="%s"/><path d="M19 89V54h26v35Z" fill="%s"/><path d="M23 46h18M24 63v18" fill="none"/>)",
    };
    TempStr barrel = SerializeColorTemp(color & 0xffffff);
    TempStr shape = fmt(shapes[std::clamp(index, 0, 4)], barrel, barrel);
    return fmt(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="96" viewBox="0 0 64 96"><g stroke="currentColor" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">%s</g></svg>)",
        shape);
}

struct InkPenRow : HBox {
    int preferredWidth = 0;
    InkPenRow() {
        alignMain = MainAxisAlign::Homogeneous;
        alignCross = CrossAxisAlign::CrossStart;
        rtl = IsUIRtl();
    }
    int MinIntrinsicWidth(int) override { return preferredWidth; }
    int MinIntrinsicHeight(int width) override {
        return Layout(ExpandHeight(width == Inf ? preferredWidth : width)).dy;
    }
    Size Layout(Constraints bc) override {
        int width = bc.ConstrainWidth(bc.HasBoundedWidth() ? bc.max.dx : preferredWidth);
        int count = ChildrenCount();
        if (!count) return bc.Constrain({});
        gap = std::min(UiScalePx(2), width / (4 * count));
        int slot = std::max(0, (width - gap * (count - 1)) / count);
        int pad = std::min(UiScalePx(6), slot / 6);
        bool labelsFit = true;
        for (auto& item : children) {
            auto* tile = (InkPenTile*)item.layout;
            labelsFit &= PlatformFontMeasureText(tile->font, tile->s).dx + 2 * pad <= slot;
        }
        for (auto& item : children) {
            auto* tile = (InkPenTile*)item.layout;
            tile->showLabel = labelsFit;
            tile->imagePad = pad;
            int dx = std::max(1, std::min(tile->preferredDx, slot - 2 * pad));
            tile->pixmap = GetCachedPixmapForSvg(InkPenSvg(tile->index, tile->color), dx, std::max(1, dx * 3 / 2),
                                                 TbTextColor(), TbBgColor());
        }
        return HBox::Layout(bc.TightenWidth(width));
    }
};

static ILayout* BuildInkPenTypes(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    Str labels[] = {Tr("Ballpoint"), Tr("Fountain"), Tr("Brush"), Tr("Pencil"), Tr("Highlighter")};
    int cmds[] = {CmdInkPen, CmdInkFountain, CmdInkBrush, CmdInkPencil, CmdInkHighlighter};
    InkPenStyle styles[] = {InkPenStyle::Ballpoint, InkPenStyle::Fountain, InkPenStyle::Brush, InkPenStyle::Pencil,
                            InkPenStyle::Highlighter};
    auto* row = new InkPenRow();
    int preferredSlot = 0;
    for (int i = 0; i < dimof(cmds); i++) {
        auto* tile = new InkPenTile();
        tile->id = cmds[i];
        tile->font = tb->platformFont;
        tile->isCurrent = win->inkEraseMode == 0 && win->inkPenStyle == styles[i];
        tile->SetText(labels[i]);
        tile->name = tile->s;
        tile->SetTooltip(labels[i]);
        tile->index = i;
        tile->color = InkPenColor(styles[i]);
        tile->preferredDx = std::max(UiScalePx(28), tb->iconSize);
        tile->imagePad = UiScalePx(6);
        tile->pixmap = GetCachedPixmapForSvg(InkPenSvg(i, tile->color), tile->preferredDx, tile->preferredDx * 3 / 2,
                                             TbTextColor(), TbBgColor());
        preferredSlot = std::max(preferredSlot, tile->GetIdealSize().dx);
        tile->onClick = MkFunc1(OnHoverRowClicked, win);
        row->AddChild(tile);
        RecordHoverItem(tb, tile, tile->s, {{}, labels[i], cmds[i], true, tile->isCurrent});
    }
    row->preferredWidth = preferredSlot * dimof(cmds) + UiScalePx(2) * (dimof(cmds) - 1);
    return new Padding(row, Insets{UiScalePx(8), UiScalePx(8), UiScalePx(8), UiScalePx(8)});
}

static void OnLaserLifetime(float seconds) {
    gSettings->laserLifetimeSeconds = limitValue(seconds, 0.1f, 120.f);
    ScheduleSaveSettings();
}

static void OnLaserWidth(float width) {
    gSettings->laserWidth = NormalizeLaserWidth(width);
    ScheduleSaveSettings();
    for (MainWindow* win : gWindows) {
        if (win->laserPointerActive) InvalidateRect(win->hwndCanvas, nullptr, FALSE);
    }
}

static void StepLaserWidth(InkThicknessSlider* slider, int direction, VirtMouseEvent* ev) {
    slider->SetValue(slider->value + direction, false);
    slider->OnCommitted();
    slider->Invalidate();
    ev->didHandle = true;
}

static void LaserWidthThinner(InkThicknessSlider* slider, VirtMouseEvent* ev) {
    StepLaserWidth(slider, -1, ev);
}
static void LaserWidthThicker(InkThicknessSlider* slider, VirtMouseEvent* ev) {
    StepLaserWidth(slider, 1, ev);
}

struct PaletteNote : VirtText {
    PaletteNote(Str text, PlatformFont* font) : VirtText(text, font) { isRtl = IsUIRtl(); }
    Size Layout(Constraints bc) override {
        int width = bc.HasBoundedWidth() ? std::max(1, bc.max.dx) : -1;
        return bc.Constrain(PlatformFontMeasureText(font, s, width));
    }
    int MinIntrinsicHeight(int width) override { return Layout(ExpandHeight(width)).dy; }
    void Paint(VirtPaintCtx& ctx) override {
        ctx.gfx->DrawText(s, ctx.content, gfxTextWrap | (isRtl ? gfxTextRtl : 0), font, TbDisabledColor());
    }
};

static void BuildLaserHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    ToolbarVirt* tb = win->toolbarVirt;
    auto* panel = new VBox();
    panel->alignCross = CrossAxisAlign::Stretch;

    Vec<ToolbarHoverMenuItem> modes;
    VecAppend(modes, {Str(gIconLaserSolid), Tr("Solid line"), CmdLaserSolid, true,
                      win->laserPointerMode == LaserPointerMode::Solid});
    VecAppend(modes, {Str(gIconLaserHollow), Tr("Hollow line"), CmdLaserHollow, true,
                      win->laserPointerMode == LaserPointerMode::Hollow});
    VecAppend(modes, {Str(gIconLaserDot), Tr("Single dot"), CmdLaserDot, true,
                      win->laserPointerMode == LaserPointerMode::Dot});
    auto* modeRow = new Wrap();
    modeRow->colGap = UiScalePx(4);
    modeRow->rtl = IsUIRtl();
    for (const auto& mode : modes) {
        auto* button = new VirtIconButton();
        button->id = mode.cmdId;
        button->label = mode.text;
        button->labelFont = tb->platformFont;
        button->pixmap = GetCachedPixmapForSvg(mode.svgIcon, tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
        button->isSelected = mode.isCurrent;
        button->cornerRadius = UiScalePx(6);
        button->padding = {UiScalePx(4), UiScalePx(6), UiScalePx(4), UiScalePx(6)};
        button->SetTooltip(mode.text);
        button->onClick = MkFunc1(OnHoverRowClicked, win);
        modeRow->AddChild(button);
        RecordHoverItem(tb, button, button->tooltip, mode);
    }
    panel->AddChild(modeRow);
    auto* width = new InkThicknessSlider();
    width->fractional = true;
    width->minimum = 0.1f;
    width->step = 0.1f;
    width->minVal = 0;
    width->maxVal = 319;
    width->value = limitValue((int)roundf((NormalizeLaserWidth(gSettings->laserWidth) - 0.1f) / 0.1f), 0, 319);
    width->idealDx = UiScalePx(kInkSliderDx);
    width->onThickness = MkFunc1Void(OnLaserWidth);
    width->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(width);
    width->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(width);
    str::ReplaceWithCopy(&width->text, StrL("laser-width-px"));
    width->valueText =
        NewVirtText({.s = fmt("%.2f", width->Width()), .font = tb->platformFont, .textColor = TbTextColor()});
    width->SetTooltip(Tr("Laser width: 0.1 to 32 pixels. Independent of pen thickness."));
    auto* widthRow = new Wrap();
    widthRow->colGap = UiScalePx(8);
    widthRow->alignCross = CrossAxisAlign::CrossCenter;
    auto* thinner = new VirtButton(StrL("-"), tb->platformFont);
    thinner->SetTooltip(Tr("Decrease laser width by 0.1 px"));
    thinner->onClick = MkFunc1(LaserWidthThinner, width);
    auto* thicker = new VirtButton(StrL("+"), tb->platformFont);
    thicker->SetTooltip(Tr("Increase laser width by 0.1 px"));
    thicker->onClick = MkFunc1(LaserWidthThicker, width);
    widthRow->AddChild(NewVirtText({.s = Tr("Width (px)"), .font = tb->platformFont, .textColor = TbTextColor()}));
    widthRow->AddChild(thinner);
    widthRow->AddChild(width->valueText);
    widthRow->AddChild(thicker);
    panel->AddChild(widthRow);
    panel->AddChild(width);
    auto* lifetime = new InkThicknessSlider();
    lifetime->fractional = true;
    lifetime->minimum = 0.1f;
    lifetime->step = 0.1f;
    lifetime->minVal = 0;
    lifetime->maxVal = 1199;
    lifetime->value = limitValue((int)roundf((gSettings->laserLifetimeSeconds - 0.1f) / 0.1f), 0, 1199);
    lifetime->idealDx = UiScalePx(kInkSliderDx);
    lifetime->onThickness = MkFunc1Void(OnLaserLifetime);
    lifetime->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(lifetime);
    lifetime->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(lifetime);
    str::ReplaceWithCopy(&lifetime->text, Str("laser-lifetime-seconds"));
    lifetime->valueText =
        NewVirtText({.s = fmt("%.2f", lifetime->Width()), .font = tb->platformFont, .textColor = TbTextColor()});
    auto* advanced = new VBox();
    advanced->alignCross = CrossAxisAlign::Stretch;
    auto* timeRow = new Wrap();
    timeRow->colGap = UiScalePx(8);
    timeRow->alignCross = CrossAxisAlign::CrossCenter;
    timeRow->AddChild(
        NewVirtText({.s = Tr("Disappear after (s)"), .font = tb->platformFont, .textColor = TbTextColor()}));
    timeRow->AddChild(lifetime->valueText);
    advanced->AddChild(timeRow);
    advanced->AddChild(lifetime);
    advanced->AddChild(new PaletteNote(Tr("Timed from pen lift. Esc stops drawing."), tb->platformFont));
    panel->AddChild(MakeAnnotColorsPanel(win, Tr("Color"), win->laserPointerColor, CmdToggleLaserPointer, false,
                                         nullptr, MkFunc1(OnAnnotColorClicked, win),
                                         MkFunc1(OnAnnotColorsEditClicked, win), nullptr, {}, nullptr, advanced));
    ev->layout = new Padding(panel, Insets{UiScalePx(10), UiScalePx(10), UiScalePx(10), UiScalePx(10)});
    ev->centerOnButton = true;
}

static void PinToolClick(MainWindow*, VirtMouseEvent*);

static void OnShapeFillSwatch(MainWindow* win, VirtMouseEvent* ev) {
    if (ev->button != 0) {
        ev->didHandle = true;
        return;
    }
    auto* swatch = (ToolbarColorSwatch*)ev->target;
    int cmd = (int)swatch->userData;
    const int* tool = ShapeFillCommandPtr(cmd);
    if (!tool) return;
    ShapeFillColorPicked(tool, swatch->col);
    auto* host = win->toolbarVirt->hoverHost;
    if (host) {
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(host->layout, controls);
        for (auto* control : controls) {
            if (!str::Eq(control->name, StrL("shape-background-color")) || control->userData != cmd) continue;
            auto* color = (ToolbarColorSwatch*)control;
            color->isCurrent =
                color->isNone ? swatch->isNone : !swatch->isNone && (color->col & 0xffffff) == (swatch->col & 0xffffff);
            color->Invalidate();
        }
    }
    ev->didHandle = true;
}

static void OnShapeFillEdit(MainWindow* win, VirtMouseEvent* ev) {
    if (ev->button != 0) return;
    const int* cmd = ShapeFillCommandPtr((int)ev->target->userData);
    if (!cmd) return;
    ShowShapeFillDialog(win, *cmd);
    ev->didHandle = true;
}

struct ShapeFillSlider : VirtSlider {
    int cmd = 0;
    VirtText* label = nullptr;
    PaletteNumberEdit* valueEdit = nullptr;

    void OnChanged() {
        if (label) label->SetText(fmt(Tr("Fill opacity: %d%%").s, value));
        if (valueEdit) valueEdit->SetValue((float)value);
    }
    void OnCommitted() {
        if (const int* tool = ShapeFillCommandPtr(cmd)) ShapeFillOpacityPicked(tool, value);
        OnChanged();
    }
    void SetTypedOpacity(float opacity) {
        value = (int)opacity;
        Invalidate();
        OnCommitted();
    }
};

static ILayout* MakeShapeFillPanel(MainWindow* win, int cmd) {
    auto setting = ShapeFillForCmd(cmd);
    if (!setting.color || !setting.opacity) return nullptr;
    Vec<ToolbarColorSwatch*> swatches;
    VirtIconButton* edit = nullptr;
    Color current = GetParsedColor(*setting.color, kColorUnset);
    auto* panel = new VBox();
    panel->alignCross = CrossAxisAlign::Stretch;
    auto* colors = MakeAnnotColorsPanel(win, Tr("Fill"), current, 0, true, &swatches, MkFunc1(OnShapeFillSwatch, win),
                                        MkFunc1(OnShapeFillEdit, win), nullptr, {}, &edit);
    for (auto* swatch : swatches) {
        swatch->name = StrL("shape-background-color");
        swatch->userData = cmd;
        swatch->isCurrent = swatch->isNone ? current == kColorUnset
                                           : current != kColorUnset && (swatch->col & 0xffffff) == (current & 0xffffff);
    }
    edit->userData = cmd;
    Vec<VirtCtrl*> fillControls;
    CollectPaletteControls(colors, fillControls);
    for (auto* control : fillControls) {
        if (str::Eq(control->name, StrL("palette-add"))) {
            control->name = StrL("shape-fill-add");
            control->userData = cmd;
        }
    }
    panel->AddChild(colors);
    bool offered = current == kColorUnset;
    for (auto* swatch : swatches) offered |= !swatch->isNone && (swatch->col & 0xffffff) == (current & 0xffffff);
    if (!offered) {
        auto* saved = new ToolbarColorSwatch();
        saved->col = current;
        saved->isCurrent = true;
        saved->name = StrL("shape-background-color");
        saved->userData = cmd;
        str::ReplaceWithCopy(&saved->text, SerializeColorTemp(current));
        saved->SetTooltip(Tr("Saved background color"));
        saved->SetFlag(vwfFocusable, true);
        saved->onClick = MkFunc1(OnShapeFillSwatch, win);
        saved->onKeyDown = MkFunc1(OnPaletteKey, win);
        panel->AddChild(saved);
    }
    auto* label = NewVirtText({.s = fmt(Tr("Fill opacity: %d%%").s, std::clamp(*setting.opacity, 0, 100)),
                               .font = win->toolbarVirt->platformFont,
                               .textColor = TbTextColor(),
                               .isRtl = IsUIRtl()});
    panel->AddChild(label);
    auto* slider = new ShapeFillSlider();
    slider->cmd = cmd;
    slider->name = StrL("shape-background-opacity");
    slider->SetTooltip(Tr("Background opacity (%)"));
    slider->label = label;
    slider->minVal = 0;
    slider->maxVal = 100;
    slider->value = std::clamp(*setting.opacity, 0, 100);
    slider->idealDx = UiScalePx(kInkSliderDx);
    slider->onValueChanged = MkMethod0<ShapeFillSlider, &ShapeFillSlider::OnChanged>(slider);
    slider->onValueCommitted = MkMethod0<ShapeFillSlider, &ShapeFillSlider::OnCommitted>(slider);
    slider->SetFlag(vwfFocusable, true);
    slider->onKeyDown = MkFunc1(OnPaletteKey, win);
    auto* opacityRow = new HBox();
    opacityRow->alignCross = CrossAxisAlign::CrossCenter;
    opacityRow->gap = UiScalePx(10);
    auto* opacityEdit = new PaletteNumberEdit(win->toolbarVirt->platformFont, 0, 100, 0);
    opacityEdit->name = StrL("shape-background-opacity-value");
    opacityEdit->SetTooltip(Tr("Fill opacity: 0 to 100%"));
    opacityEdit->SetValue((float)slider->value);
    opacityEdit->onValue = MkMethod1<ShapeFillSlider, float, &ShapeFillSlider::SetTypedOpacity>(slider);
    slider->valueEdit = opacityEdit;
    opacityRow->AddChild(slider, 1);
    opacityRow->AddChild(opacityEdit);
    panel->AddChild(opacityRow);
    Vec<VirtCtrl*> controls;
    CollectPaletteControls(colors, controls);
    for (auto* control : controls)
        if (control->onClick.IsValid()) control->onKeyDown = MkFunc1(OnPaletteKey, win);
    return panel;
}

static void BuildAnnotColorsHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    ParsedColor* setting = AnnotPresetColorSetting(ev->cmdId);
    if (!tb || (!setting && ev->cmdId != CmdCreateAnnotInk)) return;
    Color current = AnnotCurrentColor(win, ev->cmdId);
    InkThicknessSlider* slider = nullptr;
    ILayout* extra = ev->cmdId == CmdCreateAnnotInk
                         ? MakeInkThicknessPanel(win, current, -1, {}, Tr("Thickness"), kInkThicknessMin, &slider)
                         : nullptr;
    auto* tools = new Wrap();
    tools->colGap = UiScalePx(4);
    tools->rtl = IsUIRtl();
    if (ev->cmdId == CmdCreateAnnotInk) {
        ToolbarHoverMenuItem items[] = {
            {Str(kEnhancedIconEraser), Tr("Stroke eraser"), CmdInkEraser, true, win->inkEraseMode == 1},
            {Str(kEnhancedIconEraser), Tr("Erase highlights only"), CmdHighlightEraser, true, win->inkEraseMode == 2},
            {{}, Tr("Ignore touch while writing"), CmdTogglePenOnly, true, win->penOnly}};
        for (const auto& item : items) {
            auto* button = new VirtButton(item.text, tb->platformFont);
            button->id = item.cmdId;
            button->cornerRadius = UiScalePx(6);
            button->padding = {UiScalePx(4), UiScalePx(6), UiScalePx(4), UiScalePx(6)};
            button->SetTooltip(item.text);
            button->onClick = MkFunc1(OnHoverRowClicked, win);
            if (item.isCurrent) button->SetColor(kColBtnBg, TbHoverColor());
            tools->AddChild(button);
            RecordHoverItem(tb, button, button->tooltip, item);
        }
    }
    ILayout* advanced = tools->LayoutChildCount() ? (ILayout*)tools : nullptr;
    if (!advanced) delete tools;
    if (ILayout* shape = MakeShapeFillPanel(win, ev->cmdId)) advanced = shape;
    Str label = ev->cmdId == CmdCreateAnnotText    ? Tr("Background Color")
                : ShapeFillForCmd(ev->cmdId).color ? Tr("Outline color")
                                                   : Tr("Color");
    ev->layout = MakeAnnotColorsPanel(win, label, current, ev->cmdId, false, nullptr, MkFunc1(OnAnnotColorClicked, win),
                                      MkFunc1(OnAnnotColorsEditClicked, win), extra, {}, nullptr, advanced);
    if (slider) RecordHoverItem(tb, slider, slider->text, {{}, slider->text, ev->cmdId, true, false});
    if (ev->cmdId == CmdCreateAnnotInk) {
        auto* panel = new VBox();
        panel->alignCross = CrossAxisAlign::Stretch;
        panel->AddChild(BuildInkPenTypes(win));
        panel->AddChild(ev->layout);
        ev->layout = panel;
    }
    ev->centerOnButton = true;
}

static void BuildShapesHoverMenu(MainWindow*, ToolbarHoverBuildEvent*);

static void RefreshShapesPopup(MainWindow* win) {
    if (!IsMainWindowValidAndNotClosing(win)) return;
    auto* tb = win->toolbarVirt;
    if (!tb || !tb->hoverHost || tb->hoverCmdId != CmdAnnotationShapes) return;
    ToolbarHoverBuildEvent ev;
    ev.win = win;
    ev.cmdId = CmdAnnotationShapes;
    VecReset(tb->hoverItems);
    BuildShapesHoverMenu(win, &ev);
    if (!ev.layout) return;
    auto* host = tb->hoverHost;
    SetWindowTextW(host->native, CWStrTemp(ShapeToolTitle()));
    auto* scroll = new ToolbarPaletteScroll(
        ev.layout, ToolbarPaletteViewport(host, PlatformWindowWorkArea(win->hwndFrame).Size()), tb->platformFont);
    scroll->lineDy = PlatformFontLineHeight(tb->platformFont) + UiScalePx(8);
    host->onNativeMsg = MkFunc1(PaletteNativeMsg, (ScrollBox*)scroll);
    host->SetLayout(scroll);
    Vec<VirtCtrl*> controls;
    CollectPaletteControls(scroll, controls);
    for (auto* ctrl : controls) {
        if (!ctrl->onClick.IsValid() && !AsVirtSlider(ctrl)) continue;
        ctrl->SetFlag(vwfFocusable, true);
        ctrl->onKeyDown = MkFunc1(OnPaletteKey, win);
    }
    for (auto* ctrl : controls)
        if (str::Eq(ctrl->name, StrL("shape-type")) && ctrl->id == SelectedShapeCmd() && host->vroot)
            host->vroot->SetFocus(ctrl);
    host->Invalidate(true);
}

static void OnShapeTypeClicked(MainWindow* win, VirtMouseEvent* ev) {
    if (ev->button != 0) return;
    int cmd = ev->target->id;
    for (int i = 0; i < dimofi(kShapeFillCommands); i++)
        if (cmd == kShapeFillCommands[i]) gSettings->annotations.shapeTool = i;
    ScheduleSaveSettings();
    if (auto* button = AsVirtIconButton(ToolbarItemForCmd(win, CmdAnnotationShapes))) {
        auto* tb = win->toolbarVirt;
        button->pixmap = ShapeToolPixmap(tb->iconSize, TbTextColor());
        button->pixmapDisabled = ShapeToolPixmap(tb->iconSize, TbDisabledColor());
        button->Invalidate();
    }
    HwndSendCommand(win->hwndFrame, cmd);
    HoldColorPalette(win);
    uitask::Post(MkFunc0(RefreshShapesPopup, win), "Update shape palette");
    ev->didHandle = true;
}

static void OnShapeBorderColor(MainWindow* win, VirtMouseEvent* ev) {
    if (ev->button != 0) return;
    auto* swatch = (ToolbarColorSwatch*)ev->target;
    SetAnnotPresetColor(win, swatch->id, swatch->col);
    auto* tb = win->toolbarVirt;
    if (tb && tb->hoverHost) {
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(tb->hoverHost->layout, controls);
        for (auto* ctrl : controls) {
            if (str::Eq(ctrl->name, StrL("shape-border-color"))) {
                auto* color = (ToolbarColorSwatch*)ctrl;
                color->isCurrent = SameColorAndAlpha(color->col, swatch->col);
                for (auto& item : tb->hoverItems)
                    if (item.ctrl == color) item.isCurrent = color->isCurrent;
                color->Invalidate();
            }
            if (str::Eq(ctrl->name, StrL("shape-border-width"))) {
                auto* slider = (InkThicknessSlider*)ctrl;
                slider->preview->col = swatch->col;
                slider->preview->Invalidate();
            }
        }
    }
    ev->didHandle = true;
}

static void ShapeRadiusPicked(MainWindow*, float radius) {
    gSettings->annotations.shapeCornerRadius = isfinite(radius) ? limitValue(radius, 0.f, 1000.f) : 0;
    ScheduleSaveSettings();
}

static void BuildShapesHoverMenu(MainWindow* win, ToolbarHoverBuildEvent* ev) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !gSettings) return;
    int cmd = SelectedShapeCmd();
    const char* icons[] = {gIconAnnotLine,    gIconAnnotPolyLine, gIconAnnotSquare, gIconAnnotCircle,
                           gIconAnnotPolygon, gIconArrowUp,       gIconAnnotLine};
    Str names[] = {Tr("Line"),
                   Tr("Polyline"),
                   Tr("Rectangle"),
                   Tr("Ellipse"),
                   Tr("Polygon"),
                   Tr("Arrow"),
                   Tr("Dashed line"),
                   Tr("Parabola: y = x²"),
                   Tr("Negative parabola: y = −x²"),
                   Tr("Cubic: y = x³"),
                   Tr("Negative cubic: y = −x³"),
                   Tr("Reciprocal: y = 1/x"),
                   Tr("Negative reciprocal: y = −1/x"),
                   Tr("2D axes"),
                   Tr("3D axes")};
    auto* panel = new VBox();
    panel->alignCross = CrossAxisAlign::Stretch;

    auto* tools = new Wrap();
    tools->colGap = UiScalePx(6);
    tools->rowGap = UiScalePx(6);
    for (int i : {0, 5, 6, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14}) {
        const char* icon = i < dimofi(icons) ? icons[i] : gIconAnnotPolyLine;
        auto* button = NewPaletteButton(win, Str(icon), names[i]);
        button->id = kShapeFillCommands[i];
        button->name = StrL("shape-type");
        button->padding = {UiScalePx(8), UiScalePx(8), UiScalePx(8), UiScalePx(8)};
        button->pixmap = IsLineVariant(button->id) ? LineVariantIcon(button->id, tb->iconSize, TbTextColor())
                                                   : GetCachedPixmapForSvg(Str(icons[i]), tb->iconSize, tb->iconSize,
                                                                           TbTextColor(), TbBgColor());
        button->SetTooltip(names[i]);
        button->onClick = MkFunc1(OnShapeTypeClicked, win);
        ApplyToolbarItemColors(button);
        button->isCurrent = cmd == button->id;
        tools->AddChild(button);
        RecordHoverItem(tb, button, names[i], {Str(icon), names[i], button->id, true, cmd == button->id});
    }
    panel->AddChild(new Padding(tools, Insets{UiScalePx(8), 0, UiScalePx(8), 0}));
    Vec<ToolbarColorSwatch*> swatches;
    panel->AddChild(MakeAnnotColorsPanel(win, Tr("Border"), AnnotCurrentColor(win, cmd), cmd, false, &swatches,
                                         MkFunc1(OnShapeBorderColor, win), MkFunc1(OnAnnotColorsEditClicked, win)));
    for (auto* swatch : swatches) swatch->name = StrL("shape-border-color");
    panel->AddChild(MakeShapeFillPanel(win, cmd));
    InkThicknessSlider* slider = nullptr;
    auto* width = ShapeBorderWidth(cmd);
    if (width) {
        auto* thickness = MakeInkThicknessPanel(win, AnnotCurrentColor(win, cmd), *width,
                                                MkFunc1(ShapeWidthPicked, ShapeFillCommandPtr(cmd)),
                                                Tr("Border thickness (pt)"), 0, &slider, 64, true);
        slider->name = StrL("shape-border-width");
        slider->id = cmd;
        panel->AddChild(new Padding(thickness, Insets{UiScalePx(8), UiScalePx(12), UiScalePx(12), UiScalePx(12)}));
        RecordHoverItem(tb, slider, Tr("Border thickness"), {{}, Tr("Border thickness"), cmd, true, false});
    }
    if (cmd == CmdCreateAnnotSquare) {
        auto* row = new HBox();
        row->alignCross = CrossAxisAlign::CrossCenter;
        row->gap = UiScalePx(10);
        row->AddChild(
            NewVirtText({.s = Tr("Corner radius (pt)"), .font = tb->platformFont, .textColor = TbTextColor()}), 1);
        auto* radius = new PaletteNumberEdit(tb->platformFont, 0, 1000, 2);
        radius->name = StrL("shape-corner-radius");
        radius->SetTooltip(Tr("Corner radius: 0 to 1000 pt. Clamped to half the shorter edge."));
        radius->SetValue(gSettings->annotations.shapeCornerRadius);
        radius->onValue = MkFunc1(ShapeRadiusPicked, win);
        row->AddChild(radius);
        panel->AddChild(new Padding(row, Insets{UiScalePx(8), UiScalePx(12), UiScalePx(8), UiScalePx(12)}));
        RecordHoverItem(tb, radius, radius->tooltip, {{}, radius->tooltip, cmd, true, false});
    }
    ev->layout = new Padding(panel, Insets{UiScalePx(8), UiScalePx(8), UiScalePx(8), UiScalePx(8)});
}

bool IsShapesToolbarPopupOpen(MainWindow* win) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    return tb && tb->hoverHost && tb->hoverCmdId == CmdAnnotationShapes;
}

void ShowShapesToolbarPopup(MainWindow* win) {
    if (!win || !win->toolbarVirt) return;
    RevealToolbarTool(win, CmdAnnotationShapes);
    ShowToolbarButtonDropdown(win, CmdAnnotationShapes);
}

//--- the same drop-down, opened from a chip of the annotation edit toolbar

// There is no toolbar button to hover here, so the popup keeps the mouse and
// the first click outside it dismisses it, the way a menu does.
struct AnnotColorPopup {
    Str label;
    Str thicknessLabel;
    bool withNone = false;
    float minThickness = 0;
    bool menuActive = false;
    bool closing = false;
    int shapeFillCmd = 0;
    bool forAnnotEditor = false;
    AnnotEditPickerContext editorContext;
    Func1<float> onThickness;
    Func1<int> onInteger;
    ~AnnotColorPopup() {
        str::Free(label);
        str::Free(thicknessLabel);
    }
    VirtHost* host = nullptr;
    MainWindow* win = nullptr;
    Color current = kColorUnset;
    Func1<Color> onPick;
    Color picked = kColorUnset;
    bool hasPick = false;
    bool openDialog = false;
    // non-owning, for tests; the layout tree owns them
    Vec<ToolbarColorSwatch*> swatches;
    InkThicknessSlider* slider = nullptr;
    VirtIconButton* edit = nullptr;
};

static AnnotColorPopup* gAnnotColorPopup = nullptr;

static void SetShapeFillPopupCmd(int cmd) {
    if (gAnnotColorPopup) gAnnotColorPopup->shapeFillCmd = cmd;
}

static void ShowAnnotColorsDialog(MainWindow* win, Color current, const Func1<Color>& onPick,
                                  const AnnotEditPickerContext* editorContext = nullptr);
static void ShowAnnotPopupHost(AnnotColorPopup* p, ILayout* layout, Rect anchor);

static void PostedCloseAnnotColorPopup(AnnotColorPopup* p) {
    if (p != gAnnotColorPopup) {
        return;
    }
    gAnnotColorPopup = nullptr;
    MainWindow* win = p->win;
    Func1<Color> onPick = p->onPick;
    bool hasPick = p->hasPick;
    bool openDialog = p->openDialog;
    Color col = p->picked;
    Color current = p->current;
    int shapeFillCmd = p->shapeFillCmd;
    bool forAnnotEditor = p->forAnnotEditor;
    AnnotEditPickerContext editorContext = p->editorContext;
    delete p->host;
    delete p;
    if (!IsMainWindowValidAndNotClosing(win)) return;
    if (forAnnotEditor && !IsAnnotEditPickerContextValid(editorContext)) return;
    if (hasPick) {
        onPick.Call(col);
    }
    if (openDialog) {
        // only now: destroying the popup activates its owner, which would put
        // the dialog behind the main window if it were already up
        if (shapeFillCmd)
            ShowShapeFillDialog(win, shapeFillCmd);
        else
            ShowAnnotColorsDialog(win, current, onPick, forAnnotEditor ? &editorContext : nullptr);
    }
}

// the click is handled by the popup's own window, so the window can only be
// torn down once that returns
static void CloseAnnotColorPopup(AnnotColorPopup* p) {
    if (p != gAnnotColorPopup || p->closing) return;
    p->closing = true;
    if (p->host->native && ::GetCapture() == p->host->native) {
        ::ReleaseCapture();
    }
    uitask::Post(MkFunc0(PostedCloseAnnotColorPopup, p), "CloseAnnotColorPopup");
}

static void OnAnnotColorPopupSwatch(AnnotColorPopup* p, VirtMouseEvent* ev) {
    auto* sw = ev ? (ToolbarColorSwatch*)ev->target : nullptr;
    if (!sw || p != gAnnotColorPopup || p->closing || ev->button != 0) {
        return;
    }
    p->picked = sw->col;
    p->hasPick = true;
    CloseAnnotColorPopup(p);
}

static void ShowAnnotColorsDialog(MainWindow* win, Color current, const Func1<Color>& onPick,
                                  const AnnotEditPickerContext* editorContext) {
    auto* target = new AnnotColorsTarget();
    target->win = win;
    target->onPick = onPick;
    target->forAnnotEditor = editorContext != nullptr;
    if (editorContext) target->editorContext = *editorContext;

    auto* args = new ChangeColorsArgs();
    args->win = win;
    args->title = Tr("Annotation Colors");
    args->color = current;
    args->withOpacity = true;
    if (current != kColorUnset && GetAlpha(current) == 0) args->opacityPercent = 0;
    AnnotPresetColors(0, args->colors);
    args->onClose = MkFunc1(AnnotColorsPicked, target);
    ShowChangeColorsDialog(args);
}

static void OnAnnotColorPopupEdit(AnnotColorPopup* p, VirtMouseEvent*) {
    if (p != gAnnotColorPopup || p->closing) {
        return;
    }
    p->openDialog = true;
    CloseAnnotColorPopup(p);
}

static void PaintAnnotColorPopupBg(MainWindow*, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, TbBgColor());
    ev->gfx->DrawRect(ev->clientRect, ThemeEdgeColor(), UiScalePx(kHoverMenuBorder));
}

static void PostedReclaimAnnotColorPopupCapture(AnnotColorPopup* p) {
    if (p == gAnnotColorPopup && !p->closing && IsMainWindowValidAndNotClosing(p->win) && p->host && p->host->native) {
        ::SetCapture(p->host->native);
    }
}

static void AnnotColorPopupNativeMsg(AnnotColorPopup* p, VirtHostNativeMsg* ev) {
    if (p != gAnnotColorPopup || p->closing) {
        return;
    }
    switch (ev->msg) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            // the mouse is captured, so clicks meant for another window come
            // here too: they dismiss the popup and go no further
            Point pt{GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)};
            if (p->host->ClientRect().Contains(pt)) {
                return;
            }
            CloseAnnotColorPopup(p);
            ev->didHandle = true;
            ev->res = 0;
            break;
        }
        case WM_NCDESTROY:
            CloseAnnotColorPopup(p);
            break;
        case WM_CAPTURECHANGED:
            if (p->menuActive) break;
            if ((HWND)ev->lp == p->host->native) {
                // Reclaiming the same HWND keeps the slider's virtual capture.
                ev->didHandle = true;
                break;
            }
            // the slider lets go of the mouse when its drag ends; the popup
            // takes it back instead of treating that as a click elsewhere
            if (!ev->lp && p->slider && p->slider->releasingMouse) {
                p->slider->releasingMouse = false;
                uitask::Post(MkFunc0(PostedReclaimAnnotColorPopupCapture, p), "ReclaimAnnotColorPopupCapture");
                break;
            }
            CloseAnnotColorPopup(p);
            break;
    }
}

static bool AnnotPopupTargetValid(AnnotColorPopup* p) {
    return p == gAnnotColorPopup && !p->closing && IsMainWindowValidAndNotClosing(p->win) &&
           (!p->forAnnotEditor || IsAnnotEditPickerContextValid(p->editorContext));
}

static void AnnotPopupThicknessPicked(AnnotColorPopup* p, float value) {
    if (AnnotPopupTargetValid(p)) p->onThickness.Call(value);
}

static void AnnotPopupIntegerPicked(AnnotColorPopup* p, int value) {
    if (AnnotPopupTargetValid(p)) p->onInteger.Call(value);
}

void ShowAnnotColorPopup(MainWindow* win, Rect anchor, Color current, bool withNone, Str label,
                         const Func1<Color>& onPick, float thickness, const Func1<float>& onThickness,
                         Str thicknessLabel, float minThickness, bool forAnnotEditor) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || gAnnotColorPopup) {
        return;
    }
    auto* p = new AnnotColorPopup();
    p->win = win;
    p->forAnnotEditor = forAnnotEditor;
    if (forAnnotEditor) p->editorContext = CaptureAnnotEditPickerContext(win);
    p->onThickness = onThickness;
    p->current = current;
    p->onPick = onPick;
    p->label = str::Dup(label);
    p->thicknessLabel = str::Dup(len(thicknessLabel) ? thicknessLabel : Tr("Thickness"));
    p->minThickness = minThickness;
    p->withNone = withNone;
    // an ink annotation's stroke is as much a choice as its color, so its
    // popup has the same Thickness slider the ink button's drop-down has
    InkThicknessSlider* slider = nullptr;
    ILayout* extra =
        (thickness >= 0)
            ? MakeInkThicknessPanel(win, current, thickness, MkFunc1(AnnotPopupThicknessPicked, p),
                                    len(thicknessLabel) > 0 ? thicknessLabel : Tr("Thickness"), minThickness, &slider)
            : nullptr;
    ILayout* layout =
        MakeAnnotColorsPanel(win, label, current, 0, withNone, &p->swatches, MkFunc1(OnAnnotColorPopupSwatch, p),
                             MkFunc1(OnAnnotColorPopupEdit, p), extra, {}, &p->edit);
    p->slider = slider;
    ShowAnnotPopupHost(p, layout, anchor);
}

static HWND BeginAnnotColorPopupMenu(MainWindow* win, HWND source) {
    auto* p = gAnnotColorPopup;
    if (p && p->win == win && p->host->native == source) {
        p->menuActive = true;
        // The document frame handles owner-drawn menu measurement and paint.
        return win->hwndFrame;
    }
    return win->hwndFrame;
}

static void EndAnnotColorPopupMenu(MainWindow* win, HWND source) {
    auto* p = gAnnotColorPopup;
    if (p && p->win == win && p->host->native == source) {
        p->menuActive = false;
        ::SetCapture(source);
    }
}

static void RefreshAnnotColorPopup(MainWindow* win) {
    auto* p = gAnnotColorPopup;
    if (!p || p->win != win || !len(p->label)) return;
    InkThicknessSlider* slider = nullptr;
    ILayout* extra = p->slider ? MakeInkThicknessPanel(win, p->current, p->slider->Width(), p->slider->onThickness,
                                                       p->thicknessLabel, p->minThickness, &slider)
                               : nullptr;
    VecReset(p->swatches);
    auto* layout = MakeAnnotColorsPanel(win, p->label, p->current, 0, p->withNone, &p->swatches,
                                        MkFunc1(OnAnnotColorPopupSwatch, p), MkFunc1(OnAnnotColorPopupEdit, p), extra,
                                        {}, &p->edit);
    p->slider = slider;
    auto* host = p->host;
    Rect bounds = host->ScreenRect();
    Size size = host->SetLayoutSizedToContent(layout);
    bounds.dx = size.dx;
    bounds.dy = size.dy;
    host->SetPos(ShiftRectToWorkArea(bounds, win->hwndFrame, true), true);
    ::SetCapture(host->native);
}

// A number picked with a slider: a label, the slider, and the value under it.
// onValue gets the value when the slider is let go.
void ShowAnnotSliderPopup(MainWindow* win, Rect anchor, Str label, int value, int minVal, int maxVal,
                          const Func1<int>& onValue, bool forAnnotEditor) {
    ToolbarVirt* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || gAnnotColorPopup) {
        return;
    }
    auto* p = new AnnotColorPopup();
    p->win = win;
    p->forAnnotEditor = forAnnotEditor;
    if (forAnnotEditor) p->editorContext = CaptureAnnotEditPickerContext(win);
    p->onInteger = onValue;
    value = limitValue(value, minVal, maxVal);

    auto* slider = new InkThicknessSlider();
    slider->minVal = minVal;
    slider->maxVal = maxVal;
    slider->value = value;
    slider->idealDx = UiScalePx(kInkSliderDx);
    slider->onInteger = MkFunc1(AnnotPopupIntegerPicked, p);
    slider->onValueChanged = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnChanged>(slider);
    slider->onValueCommitted = MkMethod0<InkThicknessSlider, &InkThicknessSlider::OnCommitted>(slider);
    str::ReplaceWithCopy(&slider->text, fmt("thickness=%d", value));

    auto mkEnd = [tb](int v) {
        return NewVirtText({
            .s = fmt("%d", v),
            .font = tb->platformFont,
            .textColor = TbDisabledColor(),
            .isRtl = IsUIRtl(),
        });
    };
    // centered between the ends, padded out to the widest value so it keeps
    // its place when a drag adds a digit
    TempStr valueStr = fmt("%d", value);
    int widestDx = PlatformFontMeasureText(tb->platformFont, fmt("%d", maxVal)).dx;
    int extraDx = std::max(widestDx - PlatformFontMeasureText(tb->platformFont, valueStr).dx, 0);
    auto* valueText = NewVirtText({
        .s = valueStr,
        .font = tb->platformFont,
        .textColor = TbTextColor(),
        .align = VirtTextAlign::Center,
        .isRtl = IsUIRtl(),
        .padding = {.right = extraDx - (extraDx / 2), .left = extraDx / 2},
    });
    slider->valueText = valueText;

    auto* ends = new HBox();
    ends->alignMain = MainAxisAlign::SpaceBetween;
    ends->alignCross = CrossAxisAlign::CrossCenter;
    ends->AddChild(mkEnd(minVal));
    ends->AddChild(valueText);
    ends->AddChild(mkEnd(maxVal));

    auto* vbox = new VBox();
    vbox->alignCross = CrossAxisAlign::Stretch;
    vbox->AddChild(new Padding(NewVirtText({
                                   .s = label,
                                   .font = tb->platformFont,
                                   .textColor = TbTextColor(),
                                   .isRtl = IsUIRtl(),
                               }),
                               Insets{.bottom = UiScalePx(4)}));
    vbox->AddChild(slider);
    vbox->AddChild(ends);
    int b = UiScalePx(kHoverMenuBorder);
    int pad = UiScalePx(kAnnotColorsPad);
    ILayout* layout = new Padding(vbox, Insets{b + pad, b + pad, b + pad, b + pad});
    p->slider = slider;
    ShowAnnotPopupHost(p, layout, anchor);
}

static void ShowAnnotPopupHost(AnnotColorPopup* p, ILayout* layout, Rect anchor) {
    MainWindow* win = p->win;
    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = WStrL(L"SumatraAnnotColorPopup");
    args.isPopup = true;
    args.visible = false;
    args.noActivate = true;
    args.userData = win;
    args.bgColor = TbBgColor();
    args.isRtl = IsUIRtl();
    args.initialSize = {100, 100};
    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        delete layout;
        delete p;
        return;
    }
    p->host = host;
    host->onPaintBackground = MkFunc1(PaintAnnotColorPopupBg, win);
    host->onNativeMsg = MkFunc1(AnnotColorPopupNativeMsg, p);
    Size sz = host->SetLayoutSizedToContent(layout);

    // under the chip, centered on it, kept on the monitor
    Rect r{anchor.x + ((anchor.dx - sz.dx) / 2), anchor.Bottom(), sz.dx, sz.dy};
    r = ShiftRectToWorkArea(r, win->hwndFrame, true);
    host->SetPos(r, true);
    gAnnotColorPopup = p;
    ::SetCapture(host->native);
}

// for tests: the swatches of the drop-down that is up, if any
TempStr AnnotColorPopupStateTemp() {
    AnnotColorPopup* p = gAnnotColorPopup;
    if (!p || !p->host) {
        return fmt("annotColorPopup visible=0 n=0 thickness= swatches=\n");
    }
    Rect r = p->host->ScreenRect();
    str::Builder swatches;
    for (int i = 0; i < len(p->swatches); i++) {
        if (i > 0) {
            swatches.AppendChar(';');
        }
        ToolbarColorSwatch* sw = p->swatches[i];
        Rect sr = sw->BoundsInWindow();
        swatches.Append(
            fmt("%s:%d,%d,%d,%d:%d", sw->text, r.x + sr.x, r.y + sr.y, sr.dx, sr.dy, sw->isCurrent ? 1 : 0));
    }
    Str thickness = StrL("");
    if (p->slider) {
        Rect sr = p->slider->BoundsInWindow();
        thickness = fmt("%g:%d,%d,%d,%d", p->slider->Width(), r.x + sr.x, r.y + sr.y, sr.dx, sr.dy);
    }
    Rect er{};
    if (p->edit) {
        er = p->edit->BoundsInWindow();
        er.x += r.x;
        er.y += r.y;
    }
    return fmt("annotColorPopup visible=1 n=%d placed=%d,%d,%d,%d edit=%d,%d,%d,%d thickness=%s swatches=%s\n",
               len(p->swatches), r.x, r.y, r.dx, r.dy, er.x, er.y, er.dx, er.dy, thickness, ToStrTemp(swatches));
}

static void OnToolbarMouseMove(MainWindow* win, Point pt) {
    if (win->toolbarVirt && win->toolbarVirt->dragItem) return;
    UpdateOverlayToolbarForMouse(win);
    ToolbarHoverDropdownOnMouseMove(win, &pt);
}

static void OnToolbarMouseLeave(MainWindow* win) {
    if (win->toolbarVirt && win->toolbarVirt->dragItem) return;
    UpdateOverlayToolbarForMouse(win);
    ToolbarHoverDropdownOnMouseMove(win, nullptr);
}

static void PaintToolbarSeparator(VirtCustom*, VirtPaintCtx* ctx) {
    Rect r = ctx->bounds;
    int inset = UiScalePx(6);
    int dy = r.dy - (2 * inset);
    if (dy <= 0) {
        return;
    }
    int x = r.x + (r.dx / 2);
    ctx->gfx->FillRect({x, r.y + inset, 1, dy}, ThemeEdgeColor());
}

static VirtCtrl* MakeToolbarSeparator(int rowDy) {
    auto* sep = new VirtCustom();
    sep->idealSize = {UiScalePx(8), rowDy - UiScalePx(12)};
    sep->onPaint = MkFunc1(PaintToolbarSeparator, sep);
    sep->SetFlag(vwfNoHitTest, true);
    return sep;
}

// (re)build the tree of virtual controls the toolbar is made of, one per button
static bool IsAppearanceCmd(int cmdId) {
    return cmdId == CmdOptions || cmdId == CmdThemeLight || cmdId == CmdThemeDark || cmdId == CmdChangeTheme ||
           cmdId == CmdInvertColors;
}

static void PaintToolbarBrand(VirtCustom*, VirtPaintCtx* ctx) {
    Rect r = ctx->bounds;
    int sz = UiScalePx(std::max(28, limitValue(gSettings->toolbarSize, 8, 64)));
    HICON icon = (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(GetAppIconID()), IMAGE_ICON, sz, sz, 0);
    Pixmap* badge = icon ? PixmapFromHICON(icon) : nullptr;
    if (badge) {
        ctx->gfx->DrawPixmap(badge, {r.x, r.y + (r.dy - sz) / 2, sz, sz});
    }
    delete badge;
    if (icon) {
        DestroyIcon(icon);
    }
    PlatformFont* font = GetAppFont();
    int lineDy = PlatformFontLineHeight(font);
    Rect title{r.x + sz + UiScalePx(6), r.y + (r.dy - lineDy * 2) / 2, r.dx - sz - UiScalePx(6), lineDy};
    ctx->gfx->DrawText(StrL("SumatraPDF Enhanced"), title, gfxTextEllipsis, font, TbTextColor());
    title.y += lineDy;
    ctx->gfx->DrawText(Tr("Focused reading"), title, gfxTextEllipsis, GetAppFont(), TbDisabledColor());
}

static void ZoomEntryChar(MainWindow* win, Edit::CharEvent* ev) {
    if (ev->c != VK_RETURN && ev->c != VK_ESCAPE) return;
    ev->didHandle = true;
    auto* edit = win->toolbarVirt ? win->toolbarVirt->zoomEdit : nullptr;
    if (ev->c == VK_RETURN && edit && win->IsDocLoaded()) {
        TempStr text = edit->GetTextTemp();
        text.len -= str::RemoveCharsInPlace(text, StrL("% "));
        char* end = nullptr;
        float zoom = (float)strtod(text.s, &end);
        if (text.len > 0 && end == text.s + text.len && zoom >= kZoomMin && zoom <= kZoomMax) {
            SmartZoom(win, zoom, nullptr, true);
        }
    }
    HwndSetFocus(win->hwndFrame);
}

static Edit* CreateZoomEntry(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    auto* edit = new ToolbarLocationEdit();
    edit->win = win;
    edit->fieldKind = ToolbarFieldKind::Zoom;
    Edit::CreateArgs args;
    args.parent = win->hwndToolbar;
    args.font = tb->platformFont;
    args.withFrame = true;
    args.noTheme = true;
    args.selectAllOnFocus = true;
    args.alignRight = true;
    args.cueText = StrL("Zoom %");
    args.text = StrL("100%");
    edit->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    edit->Create(args);
    edit->SetIdealWidthChars(7);
    edit->SetMaxWidthChars(7);
    edit->idealDy = std::max(tb->iconSize, PlatformFontLineHeight(tb->platformFont) + UiScalePx(4));
    edit->mapRtlX = true;
    edit->MeasureWidth();
    edit->onTextChanged = MkMethod0<ToolbarLocationEdit, &ToolbarLocationEdit::OnTextChanged>(edit);
    edit->onChar = MkFunc1(ZoomEntryChar, win);
    return edit;
}

static Str PinnedToolName(MainWindow* win, int cmd) {
    if (cmd == CmdAnnotationHighlightBrush) return StrL("highlight");
    if (cmd == CmdCreateAnnotUnderline) return StrL("underline");
    if (cmd == CmdCreateAnnotStrikeOut) return StrL("strikeout");
    if (cmd != CmdCreateAnnotInk) return GetCommandName(cmd);
    switch (win->inkPenStyle) {
        case InkPenStyle::Fountain:
            return StrL("fountain");
        case InkPenStyle::Brush:
            return StrL("brush");
        case InkPenStyle::Pencil:
            return StrL("pencil");
        case InkPenStyle::Highlighter:
            return StrL("marker");
        default:
            return StrL("ballpoint");
    }
}

static int PinnedToolCommand(Str tool) {
    if (str::Eq(tool, StrL("highlight"))) return CmdAnnotationHighlightBrush;
    if (str::Eq(tool, StrL("underline"))) return CmdCreateAnnotUnderline;
    if (str::Eq(tool, StrL("strikeout"))) return CmdCreateAnnotStrikeOut;
    if (str::Eq(tool, StrL("fountain"))) return CmdInkFountain;
    if (str::Eq(tool, StrL("brush"))) return CmdInkBrush;
    if (str::Eq(tool, StrL("pencil"))) return CmdInkPencil;
    if (str::Eq(tool, StrL("marker"))) return CmdInkHighlighter;
    if (str::Eq(tool, StrL("ballpoint"))) return CmdInkPen;
    return GetCommandIdByName(tool);
}

static bool IsPenPresetCommand(int cmd) {
    return cmd == CmdInkPen || cmd == CmdInkFountain || cmd == CmdInkBrush || cmd == CmdInkPencil ||
           cmd == CmdInkHighlighter;
}

static TempStr PinnedMenuLabel(PinnedAnnotationTool* preset, int cmd) {
    Str name = GetCommandDescription(cmd);
    if (len(name) == 0) name = preset->tool;
    if (preset->width > 0) return fmt("%s, %.2f pt", name, preset->width);
    return name;
}

static bool IsPinnableTool(int cmd) {
    if (MathShapeForCommand(cmd) >= 0) return true;
    for (const auto& button : gAnnotationButtons)
        if (button.cmdId == cmd && cmd != 0) return true;
    return cmd == CmdInkEraser || cmd == CmdToggleLaserPointer;
}

static void RefreshPinnedBars() {
    for (MainWindow* window : gWindows) {
        ReCreateToolbar(window);
        ToolbarUpdateStateForWindow(window, true);
        ScheduleUiUpdate(window, kUiForceRelayout | kUiToolbarDirty);
    }
}

static void RemovePinnedTool(int index) {
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets || index < 0 || index >= len(*presets)) return;
    auto* preset = (*presets)[index];
    str::Free(preset->tool);
    str::Free(preset->color);
    str::Free(preset->fillColor);
    free(preset);
    VecRemoveAt(*presets, index);
    ScheduleSaveSettings();
    uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh pinned tools");
}

static int PinnedToolIndex(MainWindow* win, int cmd, Color selected) {
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets) return -1;
    Str tool = PinnedToolName(win, cmd);
    bool presetColor = IsAnnotColorCmd(cmd) || cmd == CmdToggleLaserPointer;
    Color color = presetColor ? (selected != kColorUnset ? selected : AnnotCurrentColor(win, cmd)) : kColorUnset;
    float* shapeWidth = ShapeBorderWidth(cmd);
    float width = cmd == CmdCreateAnnotInk       ? InkPenWidth(win)
                  : cmd == CmdToggleLaserPointer ? gSettings->laserWidth
                  : shapeWidth                   ? *shapeWidth
                                                 : 0;
    for (int i = 0; i < len(*presets); i++) {
        auto* preset = (*presets)[i];
        Color saved = ParseAnnotColor(preset->color);
        bool shapeMatches = true;
        if (preset->shapeStyle && shapeWidth) {
            auto fill = ShapeFillForCmd(cmd);
            shapeMatches = fabsf(preset->width - width) < .001f &&
                           (MathShapeForCommand(cmd) >= 0 ||
                            (fill.color && fill.opacity &&
                             GetParsedColor(*fill.color, kColorUnset) == ParseAnnotColor(preset->fillColor) &&
                             *fill.opacity == preset->fillOpacity));
            if (cmd == CmdCreateAnnotSquare)
                shapeMatches &= fabsf(preset->cornerRadius - gSettings->annotations.shapeCornerRadius) < .001f;
        }
        if (str::Eq(preset->tool, tool) && (!presetColor || SameColorAndAlpha(saved, color)) && shapeMatches &&
            ((cmd != CmdCreateAnnotInk && cmd != CmdToggleLaserPointer) || fabsf(preset->width - width) < 0.001f)) {
            return i;
        }
    }
    return -1;
}

static void TogglePinnedTool(MainWindow* win, int cmd, Color selected) {
    int index = PinnedToolIndex(win, cmd, selected);
    if (index >= 0) {
        RemovePinnedTool(index);
        return;
    }
    auto*& presets = gSettings->pinnedAnnotationTools;
    if (!presets) presets = new Vec<PinnedAnnotationTool*>();
    if (len(*presets) >= 32) return;
    Str tool = PinnedToolName(win, cmd);
    bool presetColor = IsAnnotColorCmd(cmd) || cmd == CmdToggleLaserPointer;
    Color color = presetColor ? (selected != kColorUnset ? selected : AnnotCurrentColor(win, cmd)) : kColorUnset;
    Str colorName = presetColor ? AnnotColorText(color) : StrL("");
    float* shapeWidth = ShapeBorderWidth(cmd);
    float width = cmd == CmdCreateAnnotInk       ? InkPenWidth(win)
                  : cmd == CmdToggleLaserPointer ? gSettings->laserWidth
                  : shapeWidth                   ? *shapeWidth
                                                 : 0;
    auto* preset = AllocStruct<PinnedAnnotationTool>();
    str::ReplaceWithCopy(&preset->tool, tool);
    str::ReplaceWithCopy(&preset->color, colorName);
    preset->width = width;
    auto fill = ShapeFillForCmd(cmd);
    if (shapeWidth) {
        preset->shapeStyle = true;
        if (fill.color && fill.opacity) {
            Color fillColor = GetParsedColor(*fill.color, kColorUnset);
            str::ReplaceWithCopy(&preset->fillColor, fillColor == kColorUnset ? Str{} : AnnotColorText(fillColor));
            preset->fillOpacity = std::clamp(*fill.opacity, 0, 100);
        }
        if (cmd == CmdCreateAnnotSquare) preset->cornerRadius = gSettings->annotations.shapeCornerRadius;
    }
    VecAppend(*presets, preset);
    ScheduleSaveSettings();
    uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh pinned tools");
}

static void PinToolClick(MainWindow* win, VirtMouseEvent* ev) {
    TogglePinnedTool(win, ev->target->id);
    ev->didHandle = true;
}

struct PinnedToolButton : VirtIconButton {
    int index = 0;
    Color inkColor = kColBlack;
    bool hasColor = true;
    void Paint(VirtPaintCtx& ctx) override {
        VirtIconButton::Paint(ctx);
        if (!hasColor) return;
        int sz = UiScalePx(9);
        Rect dot{ctx.bounds.Right() - sz - UiScalePx(2), ctx.bounds.Bottom() - sz - UiScalePx(2), sz, sz};
        ctx.gfx->FillEllipse(dot, TbTextColor());
        dot.Inflate(-UiScalePx(1), -UiScalePx(1));
        ctx.gfx->FillEllipse(dot, TbBgColor());
        ctx.gfx->FillEllipse(dot, inkColor & 0xffffff, GetAlpha(inkColor));
    }
};

static void RestorePinnedShape(PinnedAnnotationTool* preset, int cmd) {
    if (!preset->shapeStyle) return;
    float* width = ShapeBorderWidth(cmd);
    auto fill = ShapeFillForCmd(cmd);
    if (!width) return;
    *width = isfinite(preset->width) ? std::clamp(preset->width, 0.f, 64.f) : 1.f;
    if (fill.color && fill.opacity) {
        SetColorText(*fill.color, preset->fillColor);
        *fill.opacity = std::clamp(preset->fillOpacity, 0, 100);
    }
    if (cmd == CmdCreateAnnotSquare) ShapeRadiusPicked(nullptr, preset->cornerRadius);
    for (int i = 0; i < dimofi(kShapeFillCommands); i++)
        if (kShapeFillCommands[i] == cmd) gSettings->annotations.shapeTool = i;
    ScheduleSaveSettings();
}

static TempStr PinnedShapeSvg(PinnedAnnotationTool* preset, int cmd) {
    if (IsLineVariant(cmd)) return {};
    const char* icons[] = {gIconAnnotLine, gIconAnnotPolyLine, gIconAnnotSquare, gIconAnnotCircle, gIconAnnotPolygon};
    const char* icon = nullptr;
    for (int i = 0; i < dimofi(kShapeFillCommands); i++)
        if (kShapeFillCommands[i] == cmd) icon = icons[i];
    if (!icon) return {};
    Color border = ParseAnnotColor(preset->color), fill = ParseAnnotColor(preset->fillColor);
    TempStr result = str::ReplaceTemp(Str(icon), StrL("<path stroke=\"none\" d=\"M0 0h24v24H0z\"/>"), StrL(""));
    result = str::ReplaceTemp(result, StrL("stroke=\"currentColor\""),
                              fmt("stroke=\"#%02x%02x%02x\" stroke-opacity=\"%.3f\"", GetRed(border), GetGreen(border),
                                  GetBlue(border), GetAlpha(border) / 255.));
    TempStr fillText = fill == kColorUnset
                           ? StrL("fill=\"none\"")
                           : fmt("fill=\"#%02x%02x%02x\" fill-opacity=\"%.3f\"", GetRed(fill), GetGreen(fill),
                                 GetBlue(fill), std::clamp(preset->fillOpacity, 0, 100) / 100.);
    result = str::ReplaceTemp(result, StrL("fill=\"none\""), fillText);
    return result;
}

static void PinnedToolClick(MainWindow* win, VirtMouseEvent* ev) {
    int index = ((PinnedToolButton*)ev->target)->index;
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets || index < 0 || index >= len(*presets)) return;
    if (ev->button == 1) {
        RemovePinnedTool(index);
        ev->didHandle = true;
        return;
    }
    auto* preset = (*presets)[index];
    int cmd = PinnedToolCommand(preset->tool);
    Color color = ParseAnnotColor(preset->color);
    if (IsAnnotColorCmd(cmd) && cmd != CmdCreateAnnotInk) {
        SetAnnotPresetColor(win, cmd, color);
        RestorePinnedShape(preset, cmd);
        HwndSendCommand(win->hwndFrame, cmd);
    } else if (IsPenPresetCommand(cmd)) {
        HandlePenToolCommand(win, cmd);
        SetInkPenColor(win, color);
        SetInkPenOpacity(win, ((int)GetAlpha(color) * 100 + 127) / 255);
        SetInkPenWidth(win, preset->width);
        StartAnnotationPlacement(win, CmdCreateAnnotInk);
    } else if (cmd == CmdToggleLaserPointer) {
        SetLaserPointerColor(win, color);
        gSettings->laserWidth = NormalizeLaserWidth(preset->width);
        ScheduleSaveSettings();
        if (!win->laserPointerActive) ToggleLaserPointer(win);
    } else if (cmd > CmdNone) {
        HwndSendCommand(win->hwndFrame, cmd);
    }
    HideToolbarHoverDropdown(win);
    ToolbarUpdateStateForWindow(win, true);
    HwndSetFocus(win->hwndFrame);
    ev->didHandle = true;
}

static void OnToolbarKey(MainWindow*, VirtKeyEvent*);

static ILayout* BuildPinnedTools(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    auto* row = new HBox();
    row->gap = UiScalePx(3);
    auto* presets = gSettings->pinnedAnnotationTools;
    if (!presets) return row;
    for (int i = 0; i < std::min(len(*presets), 32); i++) {
        auto* preset = (*presets)[i];
        auto* button = new PinnedToolButton();
        button->index = i;
        button->id = CmdLast + 2000 + i;
        button->inkColor = ParseAnnotColor(preset->color);
        button->hasColor = len(preset->color) > 0;
        button->padding = {UiScalePx(4), UiScalePx(6), UiScalePx(4), UiScalePx(6)};
        button->cornerRadius = UiScalePx(6);
        int cmd = PinnedToolCommand(preset->tool);
        const char* svg = kEnhancedIconInk;
        for (const auto& info : gAnnotationButtons)
            if (info.cmdId == cmd && info.icon) svg = info.icon;
        if (cmd == CmdInkEraser) svg = kEnhancedIconEraser;
        if (cmd == CmdToggleLaserPointer) svg = gIconLaserPointer;
        Str penSvg = preset->shapeStyle ? PinnedShapeSvg(preset, cmd) : Str(svg);
        int penCommands[] = {CmdInkPen, CmdInkFountain, CmdInkBrush, CmdInkPencil, CmdInkHighlighter};
        for (int pen = 0; pen < dimof(penCommands); pen++) {
            if (cmd == penCommands[pen]) penSvg = InkPenSvg(pen, button->inkColor);
        }
        Color shapeFill = preset->shapeStyle ? ParseAnnotColor(preset->fillColor) : kColorUnset;
        if (shapeFill != kColorUnset)
            shapeFill = (shapeFill & 0xffffff) | (Color)(limitValue(preset->fillOpacity, 0, 100) * 255 / 100) << 24;
        button->pixmap = IsLineVariant(cmd) ? LineVariantIcon(cmd, tb->iconSize, button->inkColor, shapeFill)
                         : cmd == CmdAnnotationMode
                             ? AnnotationModeIconPixmap(tb->iconSize, TbTextColor())
                             : GetCachedPixmapForSvg(penSvg, tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
        button->pixmapDisabled = IsLineVariant(cmd)         ? LineVariantIcon(cmd, tb->iconSize, TbDisabledColor())
                                 : cmd == CmdAnnotationMode ? AnnotationModeIconPixmap(tb->iconSize, TbDisabledColor())
                                                            : GetCachedPixmapForSvg(penSvg, tb->iconSize, tb->iconSize,
                                                                                    TbDisabledColor(), TbBgColor());
        Str name = GetCommandDescription(cmd);
        button->SetTooltip(preset->shapeStyle ? fmt("%s - Border %s, fill %s, %d%%, %.2f pt.", name, preset->color,
                                                    len(preset->fillColor) ? preset->fillColor : Tr("none"),
                                                    preset->fillOpacity, preset->width)
                           : button->hasColor ? fmt("%s - %s - %.2f pt.", name, preset->color, preset->width)
                                              : name);
        button->onClick = MkFunc1(PinnedToolClick, win);
        button->onContextMenu = MkFunc1(PinnedToolClick, win);
        button->onKeyDown = MkFunc1(OnToolbarKey, win);
        button->SetFlag(vwfFocusable, true);
        row->AddChild(button);
        VecAppend(tb->pinnedItems, (VirtCtrl*)button);
    }
    return row;
}

static bool ShowPinToolMenu(MainWindow* win, int cmd) {
    if (!IsPinnableTool(cmd)) return false;
    Rect anchor = GetToolbarButtonScreenRect(win, cmd);
    HideToolbarHoverDropdown(win);
    HMENU menu = CreatePopupMenu();
    bool isPinned = PinnedToolIndex(win, cmd) >= 0;
    AppendMenuW(menu, MF_STRING | (isPinned ? MF_CHECKED : 0), 1,
                CWStrTemp(ToWStrTemp(isPinned ? Tr("Unpin this tool") : Tr("Pin this tool"))));
    if (IsAnnotColorCmd(cmd) || cmd == CmdToggleLaserPointer || cmd == CmdAnnotationLasso)
        AppendMenuW(menu, MF_STRING, 2, ToWStrTemp(Tr("Tools, colors and settings")).s);
    MarkMenuOwnerDraw(menu);
    SetMenuPinIcon(menu, 1);
    int picked =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.Bottom(), 0, win->hwndFrame, nullptr);
    FreeMenuOwnerDrawInfoData(menu);
    DestroyMenu(menu);
    if (!IsMainWindowValidAndNotClosing(win)) return true;
    if (picked == 1) {
        VirtCtrl target;
        target.id = cmd;
        VirtMouseEvent event;
        event.target = &target;
        PinToolClick(win, &event);
    } else if (picked == 2) {
        ShowToolbarButtonDropdown(win, cmd);
    }
    ToolbarNoteDropdownClosed();
    return true;
}

static void ShowToolbarOverflow(MainWindow* win, int anchorId);
static void OnToolbarKey(MainWindow* win, VirtKeyEvent* ev);

static void OnOverflowClicked(MainWindow* win, VirtMouseEvent* ev) {
    if (!ToolbarDropdownJustClosed()) ShowToolbarOverflow(win, ev->target->id);
    ev->didHandle = true;
}

static void OnToolbarScrollClicked(MainWindow* win, VirtMouseEvent* ev) {
    auto* tb = win->toolbarVirt;
    auto* line = ev->target->userData ? tb->annotationLine : tb->mainRow;
    HideToolbarHoverDropdown(win);
    line->Scroll(ev->target == line->previousButton ? -1 : 1);
    ev->didHandle = true;
}

void RevealToolbarTool(MainWindow* win, int cmd) {
    auto* tb = win->toolbarVirt;
    if (!tb) return;
    auto contains = [&](auto&& self, ILayout* item) -> bool {
        if (auto* ctrl = item->AsVirtCtrl()) {
            if (ctrl->id == cmd) return true;
            int index = VecFind(tb->pinnedItems, ctrl);
            if (index >= 0 && gSettings) {
                auto* presets = gSettings->pinnedAnnotationTools;
                if (presets && index < len(*presets) && (*presets)[index] &&
                    PinnedToolCommand((*presets)[index]->tool) == cmd)
                    return true;
            }
        }
        for (int i = 0; i < item->LayoutChildCount(); i++)
            if (self(self, item->LayoutChildAt(i))) return true;
        return false;
    };
    for (ToolbarLine* line : {tb->mainRow, tb->annotationLine}) {
        if (!line || (line == tb->annotationLine && IsCollapsed(tb->annotationRow))) continue;
        if (tb->vertical) {
            line->Restore();
            if (AnnotationToolbarRail(tb) && len(line->railCells)) {
                for (auto& cell : line->railCells) {
                    if (!contains(contains, cell.layout)) continue;
                    int top = cell.bounds.y;
                    int bottom = cell.bounds.Bottom();
                    if (top < line->verticalScrollY || bottom > line->verticalScrollY + line->verticalViewportDy)
                        line->verticalScrollY = limitValue(cell.rowTop, 0, line->verticalMaxScroll);
                    tb->host->Relayout();
                    tb->host->Invalidate(true);
                    return;
                }
                continue;
            }
            int y = 0;
            for (int i = 0; i < len(line->children) - line->FixedCount(); i++) {
                auto& item = line->children[i];
                if (item.size.dy <= 0) continue;
                if (auto* group = AsToolbarGroup(item.layout)) {
                    for (int j = 0; j < len(group->children) && j < len(group->cells); j++) {
                        if (group->cells[j].IsEmpty() || !contains(contains, group->children[j].layout)) continue;
                        int top = y + group->cells[j].y;
                        int bottom = y + group->cells[j].Bottom();
                        if (top < line->verticalScrollY || bottom > line->verticalScrollY + line->verticalViewportDy)
                            line->verticalScrollY = limitValue(top, 0, line->verticalMaxScroll);
                        tb->host->Relayout();
                        tb->host->Invalidate(true);
                        return;
                    }
                }
                y += item.size.dy + line->gap;
            }
            tb->host->Relayout();
            continue;
        }
        for (int i = 0; i < len(line->children) - line->FixedCount(); i++) {
            auto* group = line->children[i].layout;
            if (!contains(contains, group)) continue;
            if (IsCollapsed(group)) {
                line->firstGroup = i;
                tb->host->Relayout();
                tb->host->Invalidate(true);
            }
            return;
        }
    }
}

bool FocusToolbar(MainWindow* win, bool backwards) {
    auto* tb = win ? win->toolbarVirt : nullptr;
    if (!tb || !tb->host->IsVisible() || !tb->host->vroot) return false;
    Vec<TabStop> stops;
    CollectTabStops(tb->host->layout, stops);
    for (int j = 0; j < len(stops); j++) {
        auto& stop = stops[backwards ? len(stops) - 1 - j : j];
        if (!stop.vwnd) continue;
        tb->host->vroot->SetFocus(stop.vwnd);
        HwndSetFocusForce(tb->host->native);
        return true;
    }
    return false;
}

static void OnToolbarKey(MainWindow* win, VirtKeyEvent* ev) {
    if (ev->vkey == VK_ESCAPE) {
        HideToolbarHoverDropdown(win);
        if (win->annotationMode) ToggleAnnotationMode(win);
        HwndSetFocus(win->hwndFrame);
        ev->didHandle = true;
        return;
    }
    if (ev->vkey == VK_LEFT || ev->vkey == VK_RIGHT) {
        auto* root = win->toolbarVirt->host->vroot;
        if (root) root->TabNavigate((ev->vkey == VK_LEFT) != IsUIRtl());
        ev->didHandle = true;
        return;
    }
    if (win->toolbarVirt->vertical && (ev->vkey == VK_PRIOR || ev->vkey == VK_NEXT)) {
        win->toolbarVirt->mainRow->Scroll(ev->vkey == VK_PRIOR ? -1 : 1);
        ev->didHandle = true;
        return;
    }
    bool palette = ev->vkey == VK_DOWN || (ev->vkey == VK_F10 && ev->isShift);
    if (palette && ev->target->id != ToolbarOverflowId && ev->target->id != ToolbarAnnotOverflowId) {
        if (ShowToolbarButtonDropdown(win, ev->target->id)) {
            auto* hover = win->toolbarVirt->hoverHost;
            if (hover && hover->vroot) hover->vroot->TabNavigate(false);
            ev->didHandle = true;
        }
        return;
    }
    if (ev->vkey != VK_RETURN && ev->vkey != VK_SPACE && !palette) return;
    VirtMouseEvent click;
    click.target = ev->target;
    click.hit = ev->target;
    if (ev->target->onClick.IsValid()) ev->target->onClick.Call(&click);
    ev->didHandle = true;
}

enum class ToolbarMenuActionKind {
    Palette,
    Pinned,
    Unpin,
    Visibility,
    Restore
};
struct ToolbarMenuAction {
    ToolbarMenuActionKind kind = ToolbarMenuActionKind::Palette;
    int id = 0;
};

static void AppendToolbarMenu(HMENU menu, int cmdId, Str text, bool enabled, bool checked = false) {
    UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) | (checked ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, flags, cmdId, CWStrTemp(ToWStrTemp(text)));
}

static void ShowToolbarOverflow(MainWindow* win, int anchorId) {
    auto* tb = win->toolbarVirt;
    Rect anchor = GetToolbarButtonScreenRect(win, anchorId);
    if (!tb || anchor.IsEmpty()) return;
    Vec<VirtCtrl*> hidden = anchorId == ToolbarAnnotOverflowId ? tb->annotationItems : tb->items;
    if (anchorId != ToolbarAnnotOverflowId && tb->annotationExpanded)
        for (VirtCtrl* annotation : tb->annotationItems) VecAppend(hidden, annotation);
    if (anchorId != ToolbarAnnotOverflowId)
        for (VirtCtrl* pinned : tb->pinnedItems) VecAppend(hidden, pinned);
    CollapseFindBar(win);
    HideToolbarHoverDropdown(win);
    HMENU menu = CreatePopupMenu();
    Vec<ToolbarMenuAction> actions;
    Vec<ScrollMenuDecoration> decorations;
    constexpr int kActionFirst = 60000;
    auto addPalette = [&](HMENU sub, int cmdId, bool enabled) {
        int localId = kActionFirst + len(actions);
        VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Palette, cmdId});
        AppendToolbarMenu(sub, localId, Tr("Tools, colors and settings"), enabled);
    };
    auto* ctx = NewBuildMenuCtx(win->CurrentTab(), Point{0, 0});
    AutoCall delCtx(DeleteBuildMenuCtx, ctx);
    Vec<int> added;
    auto addItem = [&](VirtCtrl* ctrl) {
        if (!ctrl || !ctrl->id || ctrl->id == WarningMsgId) return;
        int pinned = VecFind(tb->pinnedItems, ctrl);
        if (pinned >= 0) {
            auto* presets = gSettings->pinnedAnnotationTools;
            if (!presets || pinned >= len(*presets)) return;
            auto* preset = (*presets)[pinned];
            int localId = kActionFirst + len(actions);
            VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Pinned, pinned});
            AppendToolbarMenu(menu, localId, PinnedMenuLabel(preset, PinnedToolCommand(preset->tool)),
                              ctrl->IsEnabled());
            ScrollMenuDecoration detail;
            detail.command = localId;
            if (len(preset->color) > 0 || preset->shapeStyle) {
                detail.swatchCount = preset->shapeStyle ? 2 : 1;
                detail.swatches[0] = ParseAnnotColor(preset->color);
                if (preset->shapeStyle) {
                    Color fill = ParseAnnotColor(preset->fillColor);
                    detail.swatches[1] =
                        fill == kColorUnset
                            ? kColorUnset
                            : (fill & 0xffffff) | (Color)(limitValue(preset->fillOpacity, 0, 100) * 255 / 100) << 24;
                }
            }
            detail.actionCommand = kActionFirst + len(actions);
            VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Unpin, pinned});
            detail.actionIconSize = std::min(tb->iconSize, UiScalePx(18));
            detail.actionIcon = GetCachedPixmapForSvg(Str(GetPinIconSvg()), detail.actionIconSize,
                                                      detail.actionIconSize, TbTextColor(), TbBgColor());
            detail.actionSlash = true;
            detail.actionLabel = Tr("Unpin this tool");
            detail.tooltip = ctrl->tooltip;
            VecAppend(decorations, detail);
            return;
        }
        int cmdId = ctrl->id == PageInfoId ? CmdGoToPage : ctrl->id;
        if (VecFind(added, cmdId) >= 0) return;
        VecAppend(added, cmdId);
        auto* icon = AsVirtIconButton(ctrl);
        bool checked = icon && icon->isSelected;
        bool enabled = IsCmdEnabled(win, cmdId, ctx) && ctrl->IsEnabled();
        Str label = GetCommandDescription(cmdId);
        if (auto* custom = FindCustomCommand(cmdId)) label = custom->name;
        if (len(label) == 0) label = ctrl->tooltip;
        if (len(label) == 0) return;
        if (cmdId == CmdSinglePageView || cmdId == CmdZoomIn || cmdId == CmdZoomOut || IsAnnotColorCmd(cmdId) ||
            cmdId == CmdToggleLaserPointer || cmdId == CmdSaveAnnotations) {
            HMENU sub = CreatePopupMenu();
            AppendMenuW(menu, MF_POPUP | (enabled ? 0 : MF_GRAYED), (UINT_PTR)sub, CWStrTemp(ToWStrTemp(label)));
            if (cmdId == CmdSinglePageView) {
                DisplayMode mode = win->ctrl ? win->ctrl->GetDisplayMode() : DisplayMode::SinglePage;
                AppendToolbarMenu(sub, CmdSinglePageView, Tr("Single Page"), enabled, IsSingle(mode));
                AppendToolbarMenu(sub, CmdFacingView, Tr("Facing"), enabled, IsFacing(mode));
                AppendToolbarMenu(sub, CmdBookView, Tr("Book View"), enabled, IsBookView(mode));
                AppendToolbarMenu(sub, CmdToggleContinuousView, Tr("Continuous scrolling"), enabled,
                                  IsContinuous(mode));
                return;
            }
            AppendToolbarMenu(sub, cmdId, label, enabled, checked);
            if (cmdId == CmdZoomIn || cmdId == CmdZoomOut) {
                Vec<ZoomHoverLevel> levels;
                ZoomHoverLevels(levels);
                int current = ZoomHoverCurrentIdx(win, levels);
                for (int i = 0; i < len(levels); i++) {
                    Str text = levels[i].zoom == kZoomFitPage    ? Tr("Fit page")
                               : levels[i].zoom == kZoomFitWidth ? Tr("Fit width")
                                                                 : fmt("%.0f%%", levels[i].zoom);
                    AppendToolbarMenu(sub, levels[i].cmdId, text, enabled, i == current);
                }
                AppendToolbarMenu(sub, CmdZoomCustom, Tr("Custom zoom"), enabled);
                return;
            }
            if (cmdId == CmdCreateAnnotInk) {
                int pens[] = {CmdInkPen, CmdInkFountain, CmdInkBrush, CmdInkPencil, CmdInkHighlighter};
                InkPenStyle styles[] = {InkPenStyle::Ballpoint, InkPenStyle::Fountain, InkPenStyle::Brush,
                                        InkPenStyle::Pencil, InkPenStyle::Highlighter};
                for (int i = 0; i < dimof(pens); i++)
                    AppendToolbarMenu(sub, pens[i], GetCommandDescription(pens[i]), enabled,
                                      win->inkEraseMode == 0 && win->inkPenStyle == styles[i]);
            }
            addPalette(sub, cmdId, enabled);
            return;
        }
        if (cmdId == CmdThemeLight) checked = IsLightColor(ThemeWindowBackgroundColor());
        if (cmdId == CmdThemeDark) checked = !IsLightColor(ThemeWindowBackgroundColor());
        if (cmdId == CmdInvertColors) checked = GetInvertPageColors();
        if (cmdId == CmdToggleBookmarks) checked = IsSidebarViewShown(win, SidebarView::Bookmarks);
        AppendToolbarMenu(menu, cmdId, label, enabled, checked);
    };
    for (VirtCtrl* ctrl : hidden) addItem(ctrl);
    if (GetMenuItemCount(menu) > 0) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU visibility = CreatePopupMenu();
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)visibility, CWStrTemp(ToWStrTemp(Tr("Show or hide toolbar items"))));
    Vec<int> listed;
    auto addVisibility = [&](const ToolbarButtonInfo& item) {
        int cmd = item.cmdId;
        if (!cmd || cmd == WarningMsgId || VecFind(listed, cmd) >= 0) return;
        Str label = cmd == PageInfoId ? Tr("Page number") : GetCommandDescription(cmd);
        if (len(label) == 0) label = item.toolTip;
        if (len(label) == 0) return;
        VecAppend(listed, cmd);
        int localId = kActionFirst + len(actions);
        VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Visibility, cmd});
        AppendToolbarMenu(visibility, localId, label, true, !ToolbarItemHidden(cmd));
    };
    for (int i = 0; i < TotalButtonsCount(); i++) addVisibility(GetToolbarButtonInfoByIdx(i));
    for (const auto& item : gAnnotationButtons) addVisibility(item);
    AppendMenuW(visibility, MF_SEPARATOR, 0, nullptr);
    int restoreId = kActionFirst + len(actions);
    VecAppend(actions, ToolbarMenuAction{ToolbarMenuActionKind::Restore, 0});
    AppendToolbarMenu(visibility, restoreId, Tr("Reset hidden toolbar items"), true);
    int picked =
        TrackScrollMenu(win->hwndFrame, menu, {IsUIRtl() ? anchor.Right() : anchor.x, anchor.Bottom()},
                        tb->platformFont, gSettings->tabListVisibleItems, UiScalePx(480), TbBgColor(),
                        ThemeWindowTextColor(), AccentColor(TbBgColor(), 25), IsUIRtl(), IsUIRtl(), &decorations);
    DestroyMenu(menu);
    ToolbarNoteDropdownClosed();
    if (!picked || !IsMainWindowValidAndNotClosing(win)) return;
    if (picked >= kActionFirst && picked - kActionFirst < len(actions)) {
        auto action = actions[picked - kActionFirst];
        if (action.kind == ToolbarMenuActionKind::Palette) {
            RevealToolbarTool(win, action.id);
            ShowToolbarButtonDropdown(win, action.id);
            auto* hover = win->toolbarVirt->hoverHost;
            if (hover && hover->vroot) hover->vroot->TabNavigate(false);
        } else if (action.kind == ToolbarMenuActionKind::Visibility || action.kind == ToolbarMenuActionKind::Restore) {
            if (action.kind == ToolbarMenuActionKind::Restore)
                str::ReplaceWithCopy(&gSettings->toolbarHiddenItems, StrL(""));
            else
                SetToolbarItemHidden(action.id, !ToolbarItemHidden(action.id));
            ScheduleSaveSettings();
            uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh toolbar visibility");
        } else if (action.kind == ToolbarMenuActionKind::Unpin) {
            RemovePinnedTool(action.id);
        } else if (action.id < len(win->toolbarVirt->pinnedItems)) {
            VirtMouseEvent ev;
            ev.target = win->toolbarVirt->pinnedItems[action.id];
            PinnedToolClick(win, &ev);
        }
        return;
    }
    ToolbarPostCommand(win, picked);
}

static void BuildToolbarLayout(MainWindow* win) {
    PopulateToolbarLayout();
    PopulateAnnotationOrder();
    PopulateCustomToolbarButtons();

    ToolbarVirt* tb = win->toolbarVirt;
    VecReset(tb->items);
    tb->win = win;
    VecReset(tb->annotationItems);
    VecReset(tb->annotationGroups);
    tb->annotationExpanded = false;
    tb->annotationModeButton = nullptr;
    tb->annotationLine = nullptr;
    tb->annotationOverflowButton = nullptr;
    VecReset(tb->pinnedItems);
    tb->zoomEdit = nullptr;
    tb->findSlot = nullptr;
    tb->findButton = nullptr;
    tb->annotationRow = nullptr;
    tb->pageLabel = nullptr;
    tb->pageLabel2 = nullptr;
    tb->pageTotal = nullptr;
    tb->pageBox = nullptr;
    tb->chapterTotal = nullptr;
    win->pageEdit = nullptr;
    win->chapterEdit = nullptr;

    int cyPad = ToolbarCyPad();
    int iconPad = UiScalePx(6);
    tb->rowDy = ToolbarRowDy(tb->iconSize);
    Color fg = TbTextColor();
    Color dis = TbDisabledColor();

    auto* mainRow = new ToolbarLine(tb);
    tb->mainRow = mainRow;
    mainRow->gap = UiScalePx(kButtonSpacingX);
    mainRow->rtl = IsUIRtl();
    bool prettyLayout = str::IsEmptyOrWhiteSpace(gSettings->toolbarCustomLayout);
    if (prettyLayout) {
        auto* brand = new VirtCustom();
        int badgeDx = UiScalePx(std::max(28, limitValue(gSettings->toolbarSize, 8, 64)) + 6);
        int brandDx = PlatformFontMeasureText(GetAppFont(), StrL("SumatraPDF Enhanced")).dx + badgeDx;
        brand->idealSize = {std::max(UiScalePx(176), brandDx),
                            std::max(UiScalePx(36), PlatformFontLineHeight(GetAppFont()) * 2 + UiScalePx(4))};
        brand->SetFlag(vwfNoHitTest, true);
        brand->onPaint = MkFunc1(PaintToolbarBrand, brand);
        mainRow->brandIdx = 0;
        mainRow->AddChild(brand);
    }

    auto newGroup = [&]() {
        auto* group = new ToolbarGroup(tb);
        group->alignCross = CrossAxisAlign::CrossCenter;
        group->rtl = mainRow->rtl;
        return group;
    };
    ToolbarGroup* group = newGroup();
    ToolbarGroup* pinnedGroup = nullptr;
    ToolbarGroup* appearance = newGroup();
    int n = TotalButtonsCount();
    for (int i = 0; i < n; i++) {
        const ToolbarButtonInfo& bi = GetToolbarButtonInfoByIdx(i);
        VirtCtrl* w = nullptr;
        bool noTranslate = i >= gLayoutButtonsCount;
        if (bi.cmdId == PageInfoId) {
            // Old toolbar: label HWND was text + kTextPaddingRight + kButtonSpacingX
            // (10dpi) so "Page:" and "/ N" were not flush against the edit.
            int pageGap = UiScalePx(kTextPaddingRight) + UiScalePx(kButtonSpacingX);
            auto* label = new VirtText(Tr("Page:"), tb->platformFont);
            label->isRtl = mainRow->rtl;
            label->SetColor(kColText, fg);
            label->padding = {0, pageGap, 0, UiScalePx(4)};
            label->id = PageInfoId;
            tb->pageLabel = label;
            group->AddChild(label);

            // chapter box: [chapterEdit] / N, hidden unless HasChapters()
            Edit* chapterEdit = ToolbarCreateChapterEdit(win, tb->platformFont, tb->iconSize);
            chapterEdit->SetVisibility(Visibility::Collapse);
            win->chapterEdit = chapterEdit;
            group->AddChild(chapterEdit);
            VecAppend(mainRow->locationFields, ToolbarLocationField{(ToolbarLocationEdit*)chapterEdit, group});

            auto* chapterTotal = new VirtText(StrL(" "), tb->platformFont);
            chapterTotal->isRtl = mainRow->rtl;
            chapterTotal->SetColor(kColText, fg);
            chapterTotal->padding = {0, UiScalePx(4), 0, pageGap};
            chapterTotal->id = PageInfoId;
            chapterTotal->SetVisibility(Visibility::Collapse);
            tb->chapterTotal = chapterTotal;
            group->AddChild(chapterTotal);

            // second "Page:" label, shown before pageEdit only for HasChapters() docs
            auto* label2 = new VirtText(Tr("Page:"), tb->platformFont);
            label2->isRtl = mainRow->rtl;
            label2->SetColor(kColText, fg);
            label2->padding = {0, pageGap, 0, UiScalePx(4)};
            label2->id = PageInfoId;
            label2->SetVisibility(Visibility::Collapse);
            tb->pageLabel2 = label2;
            group->AddChild(label2);

            Edit* pageEdit = ToolbarCreatePageEdit(win, tb->platformFont, tb->iconSize);
            win->pageEdit = pageEdit;
            tb->pageBox = new ToolbarPageBox(tb, (ToolbarLocationEdit*)pageEdit);
            group->AddChild(tb->pageBox);
            VecAppend(mainRow->locationFields, ToolbarLocationField{(ToolbarLocationEdit*)pageEdit, group});

            auto* total = new VirtText(StrL(" "), tb->platformFont);
            total->isRtl = mainRow->rtl;
            total->SetColor(kColText, fg);
            total->padding = {0, UiScalePx(4), 0, pageGap};
            total->id = PageInfoId;
            tb->pageTotal = total;
            group->AddChild(total);
            VecAppend(tb->items, label);
            continue;
        }
        if (bi.cmdId == 0 || !HasToolbarButtonContent(bi)) {
            w = MakeToolbarSeparator(tb->rowDy);
        } else if (bi.isText) {
            auto* b = new VirtButton(noTranslate ? bi.toolTip : trans::GetTranslation(bi.toolTip), tb->platformFont);
            b->isRtl = mainRow->rtl;
            b->textPadding = {cyPad, iconPad, cyPad, iconPad};
            w = b;
        } else {
            auto* ib = new VirtIconButton();
            ib->padding = {cyPad, iconPad, cyPad, iconPad};
            ib->hasDropdown = (bi.cmdId == CmdToggleReadAloud || bi.cmdId == CmdAnnotationShapes);
            if (bi.cmdId == CmdOpenFile) {
                ib->label = Tr("Open");
                ib->labelFont = tb->platformFont;
            }
            Str svg = bi.svgIcon ? bi.svgIcon : Str(bi.cmdId == CmdAnnotationShapes ? ShapeToolIcon() : bi.icon);
            ib->pixmap = len(bi.svgIcon) == 0 && bi.cmdId == CmdAnnotationMode
                             ? AnnotationModeIconPixmap(tb->iconSize, fg)
                         : bi.cmdId == CmdAnnotationShapes
                             ? ShapeToolPixmap(tb->iconSize, fg)
                             : GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize,
                                                     bi.cmdId == CmdOpenFile ? ThemeBrandTextColor() : fg, TbBgColor());
            ib->pixmapDisabled = len(bi.svgIcon) == 0 && bi.cmdId == CmdAnnotationMode
                                     ? AnnotationModeIconPixmap(tb->iconSize, dis)
                                 : bi.cmdId == CmdAnnotationShapes
                                     ? ShapeToolPixmap(tb->iconSize, dis)
                                     : GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, dis, TbBgColor());
            w = ib;
        }
        w->id = bi.cmdId;
        if (bi.cmdId == CmdToggleEditPDF) pinnedGroup = group;
        ApplyToolbarItemColors(w);
        if (bi.toolTip) {
            bool translate = !noTranslate && !bi.isText;
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, translate));
        }
        if (bi.cmdId != 0 && bi.cmdId != PageInfoId) {
            w->onClick = MkFunc1(OnToolbarButtonClicked, win);
            w->onContextMenu = MkFunc1(OnToolbarButtonClicked, win);
            w->onKeyDown = MkFunc1(OnToolbarKey, win);
            w->SetFlag(vwfFocusable, true);
        }
        VecAppend(tb->items, w);
        if (prettyLayout && !len(gSettings->toolbarOrder) && IsAppearanceCmd(bi.cmdId)) {
            appearance->AddChild(w);
        } else {
            group->AddChild(w);
        }
        if (bi.cmdId == CmdZoomIn) {
            tb->zoomEdit = CreateZoomEntry(win);
            group->AddChild(tb->zoomEdit);
        }
        if (bi.cmdId == CmdFindFirst && !tb->findSlot) {
            tb->findButton = w;
            mainRow->findGroupIdx = len(mainRow->children);
            if (auto* button = AsVirtIconButton(w)) button->cornerRadius = tb->iconSize;
            tb->findSlot = new Spacer(0, tb->rowDy - UiScalePx(12));
            tb->findSlot->SetVisibility(Visibility::Collapse);
            group->AddChild(tb->findSlot);
        }
        if (bi.cmdId == 0) {
            mainRow->AddChild(group);
            group = newGroup();
        }
    }
    mainRow->AddChild(group);
    if (gSettings->pinnedAnnotationTools && len(*gSettings->pinnedAnnotationTools) > 0) {
        auto* pinned = (HBox*)BuildPinnedTools(win);
        if (!pinnedGroup) {
            pinnedGroup = newGroup();
            mainRow->AddChild(pinnedGroup);
        }
        int insertAt = len(pinnedGroup->children);
        if (insertAt > 0) {
            auto* last = pinnedGroup->children[insertAt - 1].layout->AsVirtCtrl();
            if (last && last->id == 0) insertAt--;
        }
        for (auto& item : pinned->children) VecInsertAt(pinnedGroup->children, insertAt++, item);
        VecClear(pinned->children);
        delete pinned;
    }

    int annotationInsertAt = len(mainRow->children);
    for (int i = 0; i < len(mainRow->children); i++) {
        if (mainRow->children[i].layout == pinnedGroup) {
            annotationInsertAt = i + 1;
            break;
        }
    }
    auto* annotationGroup = newGroup();
    auto addAnnotationGroup = [&]() {
        if (len(annotationGroup->children) == 0) return;
        annotationGroup->SetVisibility(Visibility::Collapse);
        VecAppend(tb->annotationGroups, (ILayout*)annotationGroup);
        if (mainRow->findGroupIdx >= annotationInsertAt) mainRow->findGroupIdx++;
        VecInsertAt(mainRow->children, annotationInsertAt++, boxElementInfo{annotationGroup});
    };
    for (const ToolbarButtonInfo& bi : gAnnotationButtons) {
        bool duplicate = false;
        for (VirtCtrl* existing : tb->items) {
            if (bi.cmdId && existing && existing->id == bi.cmdId) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || (!bi.cmdId && len(annotationGroup->children) == 0)) {
            VecAppend(tb->annotationItems, (VirtCtrl*)nullptr);
            continue;
        }
        VirtCtrl* w = nullptr;
        if (!HasToolbarButtonContent(bi)) {
            w = MakeToolbarSeparator(tb->rowDy);
        } else {
            auto* ib = new VirtIconButton();
            ib->padding = {cyPad, iconPad, cyPad, iconPad};
            ib->hasDropdown = bi.cmdId == CmdAnnotationShapes;
            Str svg(bi.cmdId == CmdAnnotationShapes ? ShapeToolIcon() : bi.icon);
            ib->pixmap = bi.cmdId == CmdAnnotationShapes
                             ? ShapeToolPixmap(tb->iconSize, fg)
                             : GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, fg, TbBgColor());
            ib->pixmapDisabled = bi.cmdId == CmdAnnotationShapes
                                     ? ShapeToolPixmap(tb->iconSize, dis)
                                     : GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, dis, TbBgColor());
            w = ib;
        }
        w->id = bi.cmdId;
        ApplyToolbarItemColors(w);
        if (bi.toolTip) {
            w->SetTooltip(ToolbarTipTemp(bi.cmdId, bi.toolTip, true));
        }
        if (bi.cmdId != 0) {
            w->onClick = MkFunc1(OnToolbarButtonClicked, win);
            w->onContextMenu = MkFunc1(OnToolbarButtonClicked, win);
            w->onKeyDown = MkFunc1(OnToolbarKey, win);
            w->SetFlag(vwfFocusable, true);
        }
        VecAppend(tb->annotationItems, w);
        annotationGroup->AddChild(w);
        if (bi.cmdId == 0) {
            addAnnotationGroup();
            annotationGroup = newGroup();
        }
    }
    if (len(annotationGroup->children) > 0)
        addAnnotationGroup();
    else
        delete annotationGroup;

    mainRow->AddChild(appearance);
    {
        auto* mode = new VirtIconButton();
        mode->id = CmdAnnotationMode;
        mode->padding = {cyPad, iconPad, cyPad, iconPad};
        mode->pixmap = AnnotationModeIconPixmap(tb->iconSize, fg);
        mode->pixmapDisabled = AnnotationModeIconPixmap(tb->iconSize, dis);
        mode->onClick = MkFunc1(OnToolbarButtonClicked, win);
        mode->onKeyDown = MkFunc1(OnToolbarKey, win);
        mode->SetTooltip(Tr("Exit Annotation Mode (Esc)"));
        mode->SetFlag(vwfFocusable, true);
        ApplyToolbarItemColors(mode);
        mode->SetVisibility(win->annotationMode ? Visibility::Visible : Visibility::Collapse);
        tb->annotationModeButton = mode;
    }
    mainRow->AddChild(tb->annotationModeButton);
    auto addScrollButtons = [&](ToolbarLine* line, bool annotations) {
        for (int direction : {-1, 1}) {
            auto* button = new VirtIconButton();
            button->id = CmdLast + 30 + (annotations ? 2 : 0) + (direction > 0 ? 1 : 0);
            button->userData = annotations ? 1 : 0;
            button->padding = {cyPad, iconPad, cyPad, iconPad};
            Str svg = Str(direction < 0 ? kEnhancedIconPrevious : kEnhancedIconNext);
            button->pixmap = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, fg, TbBgColor());
            button->pixmapDisabled = GetCachedPixmapForSvg(svg, tb->iconSize, tb->iconSize, dis, TbBgColor());
            button->SetTooltip(direction < 0 ? Tr("Scroll toolbar left") : Tr("Scroll toolbar right"));
            button->SetFlag(vwfFocusable, true);
            button->onClick = MkFunc1(OnToolbarScrollClicked, win);
            button->onKeyDown = MkFunc1(OnToolbarKey, win);
            ApplyToolbarItemColors(button);
            if (direction < 0)
                line->previousButton = button;
            else
                line->nextButton = button;
            line->AddChild(button);
        }
    };
    addScrollButtons(mainRow, false);
    auto* overflow = new VirtIconButton();
    overflow->id = ToolbarOverflowId;
    overflow->padding = {cyPad, iconPad, cyPad, iconPad};
    overflow->pixmap = GetCachedPixmapForSvg(Str(kToolSettingsIcon), tb->iconSize, tb->iconSize, fg, TbBgColor());
    overflow->SetTooltip(Tr("Toolbar commands and visibility"));
    overflow->SetFlag(vwfFocusable, true);
    overflow->onClick = MkFunc1(OnOverflowClicked, win);
    overflow->onKeyDown = MkFunc1(OnToolbarKey, win);
    ApplyToolbarItemColors(overflow);
    tb->overflowButton = overflow;
    mainRow->AddChild(overflow);

    SetToolbarHoverDropdown(win, CmdToggleLaserPointer, MkFunc1(BuildLaserHoverMenu, win));
    SetToolbarHoverDropdown(win, CmdSinglePageView, MkFunc1(BuildLayoutHoverMenu, win));
    SetToolbarHoverDropdown(win, CmdAnnotationLasso, MkFunc1(BuildLassoHoverMenu, win));
    SetToolbarHoverDropdown(win, CmdSaveAnnotations, MkFunc1(BuildSaveHoverMenu, win));
    // one strip for the two of them, so it doesn't jump when the mouse crosses
    // from one to the other
    SetToolbarHoverDropdown(win, CmdZoomIn, MkFunc1(BuildZoomHoverMenu, win), CmdZoomIn);
    SetToolbarHoverDropdown(win, CmdZoomOut, MkFunc1(BuildZoomHoverMenu, win), CmdZoomIn);
    // no shared group: each of them shows the color it is set to
    for (int cmdId : kAnnotColorCmds) {
        SetToolbarHoverDropdown(win, cmdId, MkFunc1(BuildAnnotColorsHoverMenu, win));
    }

    SetToolbarHoverDropdown(win, CmdAnnotationShapes, MkFunc1(BuildShapesHoverMenu, win));

    auto* root = new VBox();
    root->alignCross = CrossAxisAlign::Stretch;
    tb->contentPadding =
        new Padding(mainRow, Insets{ToolbarOuterPad(), ToolbarSidePad(), ToolbarOuterPad(), ToolbarSidePad()});
    root->AddChild(tb->contentPadding, 1);
    tb->host->SetLayout(root);
}

static void PaintToolbarBackground(MainWindow* win, VirtHostPaintEvent* ev) {
    ev->gfx->FillRect(ev->clientRect, TbBgColor());
    ToolbarVirt* tb = win->toolbarVirt;
    if (win->annotationMode) {
        int dot = UiScalePx(2);
        int step = UiScalePx(5);
        for (int i = 0; i < 6; i++) {
            int x = tb->vertical ? ev->clientRect.dx / 2 - step + (i % 3) * step : UiScalePx(6) + (i % 2) * step;
            int y = tb->vertical ? UiScalePx(6) + (i / 3) * step : ev->clientRect.dy / 2 - step + (i / 2) * step;
            ev->gfx->FillEllipse({x, y, dot, dot}, TbTextColor());
        }
    }
    auto groupBounds = [&](auto&& self, ILayout* layout, Rect& bounds) -> void {
        if (layout->GetVisibility() != Visibility::Visible) return;
        if (layout == tb->pageBox && tb->pageBox->Active()) {
            bounds = bounds.IsEmpty() ? layout->lastBounds : bounds.Union(layout->lastBounds);
            return;
        }
        if (auto* control = layout->AsVirtCtrl()) {
            if (control->id == 0) return;
            Rect rect = control->BoundsInWindow();
            bounds = bounds.IsEmpty() ? rect : bounds.Union(rect);
            return;
        }
        for (int i = 0; i < layout->LayoutChildCount(); i++) self(self, layout->LayoutChildAt(i), bounds);
    };
    if (AnnotationToolbarRail(tb) && tb->mainRow) {
        ToolbarLine* line = tb->mainRow;
        Rect viewport = line->lastBounds;
        viewport.dy = line->verticalViewportDy;
        ev->gfx->PushClip(viewport);
        bool continuous = MultiToolbarColumns(tb) && gSettings->annotationToolbarStyle == 2;
        if (continuous && line->railContentDy > 0) {
            Rect surface = viewport;
            surface.dy = std::min(surface.dy, line->railContentDy);
            ev->gfx->FillRoundedRect(surface, UiScalePx(9), ThemeHotBackgroundColor(), ThemeEdgeColor());
        }
        for (int i = 0; i < len(line->railGroups); i++) {
            Rect bounds = line->railGroups[i];
            bounds.x += viewport.x;
            bounds.y += viewport.y - line->verticalScrollY;
            if (continuous) {
                if (i < len(line->railGroups) - 1)
                    ev->gfx->FillRect({bounds.x + UiScalePx(4), bounds.Bottom() + line->gap / 2,
                                       std::max(1, bounds.dx - UiScalePx(8)), 1},
                                      ThemeEdgeColor());
            } else {
                ev->gfx->FillRoundedRect(bounds, UiScalePx(9), ThemeHotBackgroundColor(), ThemeEdgeColor());
            }
        }
        ev->gfx->PopClip();
    } else if (tb->mainRow) {
        for (auto& item : tb->mainRow->children) {
            if (item.layout->LayoutChildCount() == 0) continue;
            Rect bounds;
            groupBounds(groupBounds, item.layout, bounds);
            if (!bounds.IsEmpty())
                ev->gfx->FillRoundedRect(bounds, UiScalePx(9), ThemeHotBackgroundColor(), ThemeEdgeColor());
        }
    }
    if (tb->pageBox) tb->pageBox->Paint(ev->gfx);
    if (AnnotationToolbarRail(tb)) {
        for (Edit* edit : {tb->zoomEdit, win->chapterEdit}) {
            if (!edit || edit->GetVisibility() != Visibility::Visible) continue;
            auto* field = (ToolbarLocationEdit*)edit;
            ev->gfx->FillRoundedRect(field->railBounds, UiScalePx(6), ThemeWindowControlBackgroundColor(),
                                     edit->IsFocused() ? ThemeBrandColor() : ThemeEdgeColor());
        }
    }
    VirtCtrl* focused = tb->host->vroot ? tb->host->vroot->focused : nullptr;
    if (focused && focused->IsVisible() && tb->host->HasFocus()) {
        ev->gfx->FillRoundedRect(focused->BoundsInWindow(), UiScalePx(8), kColorTransparent, ThemeBrandColor());
    }
}

// the default theme separates the toolbar from the canvas with a hairline.
// Use the document background, not ThemeEdgeColor: on Light that is #c0c0c0
// and reads as a dark strip against the page.
static void PaintToolbarEdge(MainWindow* win, VirtHostPaintEvent* ev) {
    auto* tb = win->toolbarVirt;
    if (tb && tb->dragActive && !tb->dragMarker.IsEmpty()) ev->gfx->FillRect(tb->dragMarker, ThemeBrandColor());
    if (!IsCurrentThemeDefault() || ThemeColorizeControls()) {
        return;
    }
    Color canvasBg;
    ThemeDocumentColors(canvasBg);
    Rect rc = ev->clientRect;
    int position = ToolbarPositionForWindow(win);
    if (position == kToolbarLeft || position == kToolbarRight) {
        int x = position == kToolbarLeft ? rc.Right() - 1 : rc.x;
        ev->gfx->FillRect({x, rc.y, 1, rc.dy}, canvasBg);
        return;
    }
    int y = position == kToolbarBottom ? rc.y : (rc.Bottom() - 1);
    ev->gfx->FillRect({rc.x, y, rc.dx, 1}, canvasBg);
}

static void OnToolbarSize(MainWindow* win, Size size) {
    VirtHost* host = ToolbarHost(win);
    if (!host || !host->layout || size.dx <= 0) {
        return;
    }
    if (win->toolbarVirt->vertical) return;
    // Fit the groups at the new width before measuring; compact rows can
    // otherwise clamp the two-line brand and disagree with the frame's slot.
    int height = host->layout->Layout(ExpandHeight(size.dx)).dy;
    if (height > 0 && height != size.dy) {
        ToolbarSetHeight(win, height);
        ScheduleUiUpdate(win, kUiForceRelayout);
    }
}

static const WStr kToolbarHostClass = WStrL(L"SUMATRA_VIRT_TOOLBAR");

void CreateToolbar(MainWindow* win) {
    if (win->frameDpi > 0) {
        DpiSet(win->frameDpi, win->frameDpi);
    }
    int iconSize = ToolbarIconSize();

    VirtHost::CreateArgs args;
    args.parent = win->hwndFrame;
    args.className = kToolbarHostClass;
    args.initialSize = {100, ToolbarRowDy(iconSize)};
    args.bgColor = TbBgColor();
    args.isRtl = IsUIRtl();
    args.visible = true;
    // the old Win32 toolbar did not take the keyboard focus; a generic child
    // would, and then accelerators (Ctrl+W, â€¦) never reached the frame
    args.noActivate = true;
    // in overlay mode the canvas is a lower-Z sibling and would otherwise
    // paint over the floating toolbar
    args.clipSiblings = true;
    args.userData = win;

    VirtHost* host = VirtHost::Create(args);
    if (!host) {
        return;
    }
    host->onPaintBackground = MkFunc1(PaintToolbarBackground, win);
    host->onPaint = MkFunc1(PaintToolbarEdge, win);
    host->onTimer = MkFunc1(OnToolbarTimer, win);
    host->onSizeChanged = MkFunc1(OnToolbarSize, win);
    host->onMouseMove = MkFunc1(OnToolbarMouseMove, win);
    host->onMouseLeave = MkFunc0(OnToolbarMouseLeave, win);
    ToolbarSetNativeHooks(win, host);

    auto* tb = new ToolbarVirt();
    tb->host = host;
    tb->win = win;
    tb->iconSize = iconSize;
    tb->platformFont = GetAppFontForDpi(win->frameDpi > 0 ? win->frameDpi : DpiGet());
    win->toolbarVirt = tb;
    win->hwndToolbar = host->native;
    host->SetFont(tb->platformFont);

    BuildToolbarLayout(win);

    DocController* ctrl = win->ctrl;
    UpdateToolbarPageText(win, ctrl ? ctrl->PageCount() : -1);
    if (ctrl && win->pageEdit) {
        if (ShowChapterUi(ctrl)) {
            Location cur = ctrl->CurrentLocation();
            win->pageEdit->SetText(fmt("%d", cur.page));
            if (win->chapterEdit) {
                win->chapterEdit->SetText(fmt("%d", cur.chapter));
            }
        } else {
            TempStr label = ctrl->GetPageLabeTemp(ctrl->CurrentPageNo());
            win->pageEdit->SetText(label);
        }
        EditSetNumbersOnly(win->pageEdit, !ctrl->HasPageLabels());
    }
    UpdateToolbarFindText(win);
    ToolbarUpdateStateForWindow(win, true);
}

void DestroyToolbar(MainWindow* win) {
    ToolbarVirt* tb = win->toolbarVirt;
    if (!tb) {
        win->hwndToolbar = nullptr;
        return;
    }
    CancelToolbarDrag(win);
    HideToolbarHoverDropdown(win);
    DeleteAnnotFilterToolbar(win);
    win->pageEdit = nullptr;
    win->chapterEdit = nullptr;
    win->toolbarVirt = nullptr;
    win->hwndToolbar = nullptr;
    delete tb->host;
    delete tb;
}

void ReCreateToolbar(MainWindow* win) {
    DestroyToolbar(win);
    CreateToolbar(win);
}

// What the toolbar still needs Win32 for, now that VirtHost owns its window:
// the colors of the native page-number edit, dragging the frame by an empty
// part of the toolbar, eating the click that dismissed a drop-down menu, and
// reaching the frame and canvas windows (which are not VirtHosts yet).

//--- the frame and the canvas are still plain HWNDs

// canvas rectangle in frame-client coordinates
Rect ToolbarCanvasRectInFrame(MainWindow* win) {
    Rect rc = HwndWindowRect(win->hwndCanvas);
    Point tl = HwndScreenToClient(win->hwndFrame, rc.TL());
    return {tl, rc.Size()};
}

// a screen point in frame-client coordinates
Point ToolbarScreenToFrame(MainWindow* win, Point pt) {
    return HwndScreenToClient(win->hwndFrame, pt);
}

// repaint what the overlay toolbar was covering after it hides
void ToolbarRepaintUncovered(MainWindow* win, Rect rInFrame) {
    HwndInvalidate(win->hwndCanvas);
    HwndInvalidateRect(win->hwndFrame, rInFrame, false);
}

void ToolbarFocusFrame(MainWindow* win) {
    HwndSetFocus(win->hwndFrame);
}

bool ToolbarFrameIsVisible(MainWindow* win) {
    return HwndIsVisible(win->hwndFrame);
}

void ToolbarPostCommand(MainWindow* win, int cmdId) {
    RevealToolbarTool(win, cmdId);
    LPARAM commandPoint = 0;
    if (cmdId >= CmdCreateAnnotFirst && cmdId <= CmdCreateAnnotLast && !CommandUsesPlacementMode(cmdId)) {
        Rect canvas = HwndClientRect(win->hwndCanvas);
        Point pt{canvas.dx / 2, canvas.dy / 2};
        commandPoint = MAKELPARAM(pt.x, pt.y);
    }
    HwndPostCommand(win->hwndFrame, cmdId, commandPoint);
}

int ToolbarDockSize(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    if (!tb) return 0;
    if (!tb->vertical) {
        if (!tb->host || !tb->host->layout) return tb->rowDy;
        return tb->host->layout->MinIntrinsicHeight(HwndClientRect(win->hwndToolbar).dx);
    }
    if (AnnotationToolbarRail(tb)) return ToolbarColumnWidth(tb) * tb->annotationColumns + 2 * ToolbarSidePad();
    int cellDx = tb->annotationModeButton ? tb->annotationModeButton->MinIntrinsicWidth(0) : tb->rowDy;
    for (auto* control : tb->items) {
        if (auto* button = AsVirtIconButton(control)) cellDx = std::max(cellDx, button->MinIntrinsicWidth(0));
    }
    for (auto* control : tb->annotationItems) {
        if (auto* button = AsVirtIconButton(control)) cellDx = std::max(cellDx, button->MinIntrinsicWidth(0));
    }
    if (win->annotationMode) {
        int cap =
            std::max(cellDx, std::min(UiScalePx(160), (HwndClientRect(win->hwndFrame).dx / 3 - 2 * ToolbarSidePad()) /
                                                          tb->annotationColumns));
        if (tb->zoomEdit) {
            auto* zoom = (ToolbarLocationEdit*)tb->zoomEdit;
            zoom->MeasureWidth();
            cellDx = std::max(cellDx, LocationEditWidth(zoom->preferredDx, zoom->minimumDx, cap));
        }
        for (auto& field : tb->mainRow->locationFields) {
            if (IsCollapsed(field.edit)) continue;
            field.edit->MeasureWidth();
            cellDx = std::max(cellDx, LocationEditWidth(field.edit->preferredDx, field.edit->minimumDx, cap));
        }
        return cellDx * tb->annotationColumns + 2 * ToolbarSidePad();
    }
    int width = std::max(UiScalePx(68), cellDx);
    if (tb->zoomEdit) width = std::max(width, tb->zoomEdit->GetIdealSize().dx);
    for (auto& field : tb->mainRow->locationFields) width = std::max(width, field.edit->preferredDx);
    for (auto* label : {tb->pageTotal, tb->chapterTotal}) {
        if (label && label->IsVisible()) width = std::max(width, label->MinIntrinsicWidth(0));
    }
    return width + 2 * ToolbarSidePad();
}

void ToolbarSetDock(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    if (!tb || !tb->mainRow || !tb->contentPadding) return;
    int position = ToolbarPositionForWindow(win);
    bool vertical = position == kToolbarLeft || position == kToolbarRight;
    int columns = win->annotationMode ? limitValue(gSettings->annotationToolbarColumns, 1, 3) : 1;
    for (auto* control : tb->items) {
        auto* button = AsVirtIconButton(control);
        if (button && button->id == CmdOpenFile) button->label = win->annotationMode ? Str{} : Tr("Open");
    }
    if (vertical != tb->vertical || columns != tb->annotationColumns) {
        tb->mainRow->Restore();
        tb->mainRow->firstGroup = 0;
        tb->mainRow->verticalScrollY = 0;
        tb->vertical = vertical;
        tb->annotationColumns = columns;
        UpdateZoomFieldText(win);
        for (VirtIconButton* button : {tb->mainRow->previousButton, tb->mainRow->nextButton}) {
            bool previous = button == tb->mainRow->previousButton;
            const char* svg = vertical ? (previous ? gIconArrowUp : gIconChevronDownBold)
                                       : (previous ? kEnhancedIconPrevious : kEnhancedIconNext);
            button->pixmap = GetCachedPixmapForSvg(Str(svg), tb->iconSize, tb->iconSize, TbTextColor(), TbBgColor());
            button->pixmapDisabled =
                GetCachedPixmapForSvg(Str(svg), tb->iconSize, tb->iconSize, TbDisabledColor(), TbBgColor());
            button->SetTooltip(vertical ? (previous ? Tr("Scroll toolbar up") : Tr("Scroll toolbar down"))
                                        : (previous ? Tr("Scroll toolbar left") : Tr("Scroll toolbar right")));
        }
    }
    tb->contentPadding->insets = vertical ? Insets{UiScalePx(20), ToolbarSidePad(), ToolbarSidePad(), ToolbarSidePad()}
                                          : Insets{ToolbarOuterPad(), ToolbarSidePad(), ToolbarOuterPad(),
                                                   win->annotationMode ? UiScalePx(20) : ToolbarSidePad()};
    if (!vertical) {
        for (auto& field : tb->mainRow->locationFields) field.edit->HideDetails();
    }
    for (auto& field : tb->mainRow->locationFields) field.edit->SetRailPaint(AnnotationToolbarRail(tb));
    if (tb->zoomEdit) ((ToolbarLocationEdit*)tb->zoomEdit)->SetRailPaint(AnnotationToolbarRail(tb));
    if (tb->pageBox) tb->pageBox->UpdateTotal();
}

void ToolbarSetHeight(MainWindow* win, int dy) {
    HWND hwnd = win ? win->hwndToolbar : nullptr;
    if (!hwnd || dy <= 0) {
        return;
    }
    Rect r = ChildPosWithinParent(hwnd);
    if (r.dy == dy) {
        return;
    }
    SetWindowPos(hwnd, nullptr, 0, 0, r.dx, dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//--- the native page-number edit

// Enter in either the page or chapter edit navigates; chaptered docs read
// both boxes and go by Location, single-chapter docs keep the page-label path
static void OnLocationEditChar(MainWindow* win, Edit::CharEvent* ev) {
    if (!win || !win->IsDocLoaded()) {
        return;
    }
    switch ((Key)ev->c) {
        case Key::Enter: {
            DocController* ctrl = win->ctrl;
            if (ShowChapterUi(ctrl)) {
                int chapter = win->chapterEdit ? ParseInt(win->chapterEdit->GetTextTemp()) : 1;
                int page = win->pageEdit ? ParseInt(win->pageEdit->GetTextTemp()) : 1;
                Location loc = ctrl->ClampLocation({chapter, page});
                ctrl->GoToLocation(loc, true);
            } else if (win->pageEdit) {
                TempStr s = win->pageEdit->GetTextTemp();
                int newPageNo = ctrl->GetPageByLabel(s);
                if (!ctrl->ValidPageNo(newPageNo)) {
                    ev->didHandle = true;
                    return;
                }
                ctrl->GoToPage(newPageNo, true);
            }
            HwndSetFocus(win->hwndFrame);
            // the overlay toolbar was kept up by the focus; now that
            // it's gone, let it hide again
            UpdateOverlayToolbarForMouse(win);
            ev->didHandle = true;
            return;
        }
        case Key::Escape:
            if (win->annotationMode) ToggleAnnotationMode(win);
            HwndSetFocus(win->hwndFrame);
            UpdateOverlayToolbarForMouse(win);
            ev->didHandle = true;
            return;
        case Key::Tab:
            AdvanceFocus(win);
            ev->didHandle = true;
            return;
        default:
            return;
    }
}

static int PageEditPadL() {
    return UiEdgeDx();
}

static int PageEditPadR() {
    return PageEditPadL() + UiScalePx(4);
}

static Edit* ToolbarCreateLocationEdit(MainWindow* win, PlatformFont* font, int iconDy) {
    Edit::CreateArgs args;
    args.parent = win->hwndToolbar;
    args.font = font;
    args.isRtl = IsUIRtl();
    // no WS_EX_CLIENTEDGE: a themed edit draws a blue bottom accent (Win11)
    args.withFrame = true;
    args.noTheme = true;
    args.numbersOnly = true;
    args.alignRight = true;
    args.selectAllOnFocus = true;
    // the box is as tall as the icons, so without this the digits would sit at
    // its top instead of on the same line as "Page:" and "/ N"
    args.centerTextVert = true;
    args.marginLeft = PageEditPadL();
    args.marginRight = PageEditPadR();
    auto* e = new ToolbarLocationEdit();
    e->win = win;
    e->SetColors(TbTextColor(), ThemeWindowControlBackgroundColor());
    e->Create(args);
    // the toolbar tree arranges itself right-to-left (HBox.rtl), so its bounds
    // are offsets from the physical left; don't let the RTL host mirror them
    e->mapRtlX = true;
    e->idealDy = std::max(iconDy, PlatformFontLineHeight(font) + UiScalePx(4));
    e->MeasureWidth();
    e->onTextChanged = MkMethod0<ToolbarLocationEdit, &ToolbarLocationEdit::OnTextChanged>(e);
    e->onFocus = MkMethod0<ToolbarLocationEdit, &ToolbarLocationEdit::ShowDetails>(e);
    e->onKillFocus = MkMethod0<ToolbarLocationEdit, &ToolbarLocationEdit::HideDetails>(e);
    e->onChar = MkFunc1(OnLocationEditChar, win);
    return e;
}

Edit* ToolbarCreatePageEdit(MainWindow* win, PlatformFont* font, int iconDy) {
    return ToolbarCreateLocationEdit(win, font, iconDy);
}

Edit* ToolbarCreateChapterEdit(MainWindow* win, PlatformFont* font, int iconDy) {
    auto* edit = (ToolbarLocationEdit*)ToolbarCreateLocationEdit(win, font, iconDy);
    edit->fieldKind = ToolbarFieldKind::Chapter;
    return edit;
}

// no document: the find edit does nothing, so don't offer a text cursor
void ToolbarUpdateFindEditCursor(MainWindow* win) {
    LPWSTR cursorId = win->IsDocLoaded() ? nullptr : IDC_ARROW;
    if (win->findEdit) {
        win->findEdit->SetCursorId(cursorId);
    }
}

//--- the messages VirtHost doesn't model

// the native edit control asks its parent what colors to draw itself in
static bool OnCtlColor(MainWindow* win, VirtHostNativeMsg* ev) {
    LRESULT reflected = TryReflectMessages(win->hwndToolbar, ev->msg, ev->wp, ev->lp);
    if (reflected) {
        ev->res = reflected;
        return true;
    }
    if (ev->msg == WM_COMMAND) {
        return false;
    }
    HDC hdc = (HDC)ev->wp;
    SetTextColor(hdc, TbTextColor());
    SetBkColor(hdc, ThemeWindowControlBackgroundColor());
    if (IsCurrentThemeDefault() && !ThemeColorizeControls() && !ThemeUsesHighContrastColors()) {
        ev->res = (LRESULT)GetStockObject(WHITE_BRUSH);
    } else {
        ev->res = (LRESULT)win->brControlBgColor;
    }
    return true;
}

// with the tabs in the title bar the toolbar is part of the caption, so
// dragging an empty part of it moves the window and a double click maximizes it
static bool OnCaptionDrag(MainWindow* win, VirtHostNativeMsg* ev) {
    HWND hwnd = win->hwndToolbar;
    Point pt = {GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)};
    HWND childAtPoint = ChildWindowFromPoint(hwnd, ToPOINT(pt));
    bool overChild = childAtPoint && childAtPoint != hwnd;
    // layout bounds are physical-left; WM_LBUTTONDOWN x is mirrored on RTL
    Point hitPt = pt;
    UnmirrorRtl(hwnd, hitPt);
    VirtCtrl* hit = ToolbarItemFromPoint(win, hitPt);
    if (overChild || (hit && hit->id != 0 && hit->id != PageInfoId)) {
        return false;
    }
    HWND hwndFrame = GetAncestor(hwnd, GA_ROOT);
    if (ev->msg == WM_LBUTTONDBLCLK) {
        WPARAM cmd = IsZoomed(hwndFrame) ? SC_RESTORE : SC_MAXIMIZE;
        PostMessageW(hwndFrame, WM_SYSCOMMAND, cmd, 0);
    } else {
        ReleaseCapture();
        SendMessageW(hwndFrame, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    ev->res = 0;
    return true;
}

enum class ToolbarDragKind {
    None,
    Main,
    Annotation,
    Pinned
};

static ToolbarDragKind ToolbarDragFamily(ToolbarVirt* tb, VirtCtrl* item) {
    if (!item || item == tb->annotationModeButton || !AsVirtIconButton(item)) return ToolbarDragKind::None;
    int index = VecFind(tb->items, item);
    if (index >= 0 && index < gLayoutButtonsCount) return ToolbarDragKind::Main;
    if (VecFind(tb->annotationItems, item) >= 0) return ToolbarDragKind::Annotation;
    if (VecFind(tb->pinnedItems, item) >= 0) return ToolbarDragKind::Pinned;
    return ToolbarDragKind::None;
}

static Vec<VirtCtrl*>& ToolbarDragItems(ToolbarVirt* tb) {
    switch (ToolbarDragFamily(tb, tb->dragItem)) {
        case ToolbarDragKind::Annotation:
            return tb->annotationItems;
        case ToolbarDragKind::Pinned:
            return tb->pinnedItems;
        default:
            return tb->items;
    }
}

static void UpdateToolbarDrag(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    Point pt = tb->dragPoint;
    tb->dragTarget = nullptr;
    tb->dragMarker = {};
    tb->dragScroll = 0;
    tb->host->Invalidate();
    ToolbarLine* line =
        tb->annotationLine && tb->annotationLine->lastBounds.Contains(pt) ? tb->annotationLine : tb->mainRow;
    if (!line || !line->lastBounds.Contains(pt)) return;
    for (auto* arrow : {line->previousButton, line->nextButton}) {
        if (!arrow || !arrow->IsVisible() || !arrow->IsEnabled() || !arrow->BoundsInWindow().Contains(pt)) continue;
        tb->dragScroll = arrow == line->previousButton ? -1 : 1;
        return;
    }
    int distance = INT_MAX;
    bool rtl = line->rtl;
    ToolbarDragKind kind = ToolbarDragFamily(tb, tb->dragItem);
    for (VirtCtrl* item : ToolbarDragItems(tb)) {
        if (item == tb->dragItem || !item || !item->IsVisible() || ToolbarDragFamily(tb, item) != kind) continue;
        Rect rect = item->BoundsInWindow();
        if (tb->vertical ? (pt.x < rect.x || pt.x >= rect.Right()) : (pt.y < rect.y || pt.y >= rect.Bottom())) continue;
        bool after = tb->vertical ? pt.y >= rect.y + rect.dy / 2 : (pt.x >= rect.x + rect.dx / 2) != rtl;
        int x = after != rtl ? rect.Right() : rect.x;
        int y = after ? rect.Bottom() : rect.y;
        int delta = tb->vertical ? std::abs(pt.y - y) : std::abs(pt.x - x);
        if (delta >= distance) continue;
        distance = delta;
        tb->dragTarget = item;
        tb->dragAfter = after;
        tb->dragMarker = tb->vertical ? Rect{rect.x, y - UiScalePx(1), rect.dx, UiScalePx(2)}
                                      : Rect{x - UiScalePx(1), rect.y, UiScalePx(2), rect.dy};
    }
    tb->host->Invalidate();
}

static void OnToolbarDragTimer(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    if (!tb || !tb->dragActive || !tb->dragScroll) return;
    ToolbarLine* line =
        tb->annotationLine && tb->annotationLine->lastBounds.Contains(tb->dragPoint) ? tb->annotationLine : tb->mainRow;
    if (line && line->Scroll(tb->dragScroll)) UpdateToolbarDrag(win);
}

static void CancelToolbarDrag(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    if (!tb) return;
    bool active = tb->dragActive || tb->dockPending;
    tb->dockPending = tb->dockDragging = false;
    tb->dragItem = tb->dragTarget = nullptr;
    tb->dragActive = false;
    tb->dragMarker = {};
    tb->dragScroll = 0;
    tb->host->KillTimer(kToolbarDragScrollTimerId);
    if (active && tb->host->vroot) tb->host->vroot->ClearPressed();
    if (active && GetCapture() == tb->host->native) ReleaseCapture();
    if (active) tb->host->Invalidate();
}

static bool SaveToolbarOrder(MainWindow* win) {
    auto* tb = win->toolbarVirt;
    if (!tb->dragTarget) return false;
    auto& items = ToolbarDragItems(tb);
    Vec<int> slots;
    for (int i = 0; i < len(items); i++)
        if (ToolbarDragFamily(tb, items[i]) == ToolbarDragFamily(tb, tb->dragItem)) VecAppend(slots, i);
    int source = VecFind(slots, VecFind(items, tb->dragItem));
    int target = VecFind(slots, VecFind(items, tb->dragTarget)) + (tb->dragAfter ? 1 : 0);
    if (source < 0 || target < 0) return false;
    if (target > source) target--;
    if (source == target) return false;
    int moved = slots[source];
    VecRemoveAt(slots, source);
    VecInsertAt(slots, target, moved);
    if (ToolbarDragFamily(tb, tb->dragItem) == ToolbarDragKind::Pinned) {
        auto* presets = gSettings->pinnedAnnotationTools;
        if (!presets || len(*presets) != len(items)) return false;
        auto* preset = (*presets)[source];
        VecRemoveAt(*presets, source);
        VecInsertAt(*presets, target, preset);
    } else {
        str::Builder order;
        for (int index : slots) {
            Str name = GetCommandName(items[index]->id);
            if (!len(name)) continue;
            if (len(order)) order.Append(StrL(" "));
            order.Append(name);
        }
        Str* setting = ToolbarDragFamily(tb, tb->dragItem) == ToolbarDragKind::Annotation
                           ? &gSettings->toolbarAnnotationOrder
                           : &gSettings->toolbarOrder;
        str::ReplaceWithCopy(setting, ToStrTemp(order));
    }
    ScheduleSaveSettings();
    uitask::Post(MkFunc0Void(RefreshPinnedBars), "Refresh toolbar order");
    return true;
}

static int ToolbarDockFromPoint(Rect frame, Point pt) {
    int distances[] = {std::abs(pt.y - frame.y), std::abs(pt.y - frame.Bottom()), std::abs(pt.x - frame.x),
                       std::abs(pt.x - frame.Right())};
    int position = kToolbarTop;
    for (int i = 1; i < dimof(distances); i++)
        if (distances[i] < distances[position]) position = i;
    return position;
}

static bool OnToolbarDockDrag(MainWindow* win, VirtHostNativeMsg* ev) {
    auto* tb = win->toolbarVirt;
    if (!tb || !win->annotationMode) return false;
    Point pt{GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)};
    UnmirrorRtl(ev->host->native, pt);
    auto cancel = [&]() {
        tb->dockPending = tb->dockDragging = false;
        if (GetCapture() == tb->host->native) ReleaseCapture();
    };
    switch (ev->msg) {
        case WM_LBUTTONDOWN: {
            VirtCtrl* item = ToolbarItemFromPoint(win, pt);
            if (item && item->id != 0 && item->id != PageInfoId) return false;
            HWND child = ChildWindowFromPointEx(tb->host->native, ToPOINT(pt), CWP_SKIPINVISIBLE);
            if (child && child != tb->host->native) return false;
            HideToolbarHoverDropdown(win);
            tb->dockPending = true;
            tb->dockDragging = false;
            tb->dragStart = pt;
            tb->dockTarget = ToolbarPositionForWindow(win);
            SetCapture(tb->host->native);
            return true;
        }
        case WM_MOUSEMOVE: {
            if (!tb->dockPending) return false;
            if (!(ev->wp & MK_LBUTTON)) {
                cancel();
                return true;
            }
            if (!tb->dockDragging && std::abs(pt.x - tb->dragStart.x) < GetSystemMetrics(SM_CXDRAG) &&
                std::abs(pt.y - tb->dragStart.y) < GetSystemMetrics(SM_CYDRAG))
                return true;
            tb->dockDragging = true;
            Point screen = HwndMapWindowPoint(tb->host->native, HWND_DESKTOP, pt);
            Point frame = ToolbarScreenToFrame(win, screen);
            tb->dockTarget = ToolbarDockFromPoint(HwndClientRect(win->hwndFrame), frame);
            SetCursorCached(IDC_SIZEALL);
            return true;
        }
        case WM_LBUTTONUP: {
            if (!tb->dockPending) return false;
            bool moved = tb->dockDragging;
            int position = tb->dockTarget;
            cancel();
            if (moved && position != ToolbarPositionForWindow(win)) {
                win->annotationToolbarPosition = position;
                ToolbarSetDock(win);
                ScheduleUiUpdate(win, kUiForceRelayout | kUiToolbarDirty);
            }
            return true;
        }
        case WM_KEYDOWN:
            if (ev->wp != VK_ESCAPE || !tb->dockPending) return false;
            cancel();
            ToggleAnnotationMode(win);
            return true;
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            if (!tb->dockPending) return false;
            cancel();
            return true;
    }
    return false;
}

static bool OnToolbarDrag(MainWindow* win, VirtHostNativeMsg* ev) {
    auto* tb = win->toolbarVirt;
    if (!tb) return false;
    Point pt{GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)};
    UnmirrorRtl(ev->host->native, pt);
    switch (ev->msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            CancelToolbarDrag(win);
            VirtCtrl* item = ToolbarItemFromPoint(win, pt);
            if (ToolbarDragFamily(tb, item) == ToolbarDragKind::None) return false;
            tb->dragItem = item;
            tb->dragStart = tb->dragPoint = pt;
            tb->host->KillTimer(kOpenHoverDropdownTimerId);
            return false;
        }
        case WM_MOUSEMOVE:
            if (!tb->dragItem || !(ev->wp & MK_LBUTTON)) return tb->dragActive;
            tb->dragPoint = pt;
            if (!tb->dragActive) {
                if (std::abs(pt.x - tb->dragStart.x) < GetSystemMetrics(SM_CXDRAG) &&
                    std::abs(pt.y - tb->dragStart.y) < GetSystemMetrics(SM_CYDRAG))
                    return false;
                HideToolbarHoverDropdown(win);
                tb->dragActive = true;
                if (tb->host->vroot) {
                    tb->host->vroot->ClearPressed();
                    tb->host->vroot->HideTooltip();
                }
                SetCapture(tb->host->native);
                tb->host->SetTimer(kToolbarDragScrollTimerId, 250);
            }
            UpdateToolbarDrag(win);
            return true;
        case WM_LBUTTONUP: {
            bool active = tb->dragActive;
            if (active) {
                tb->dragPoint = pt;
                UpdateToolbarDrag(win);
                if (tb->host->ClientRect().Contains(pt)) SaveToolbarOrder(win);
            }
            CancelToolbarDrag(win);
            return active;
        }
        case WM_KEYDOWN:
            if (ev->wp != VK_ESCAPE || !tb->dragItem) return false;
            CancelToolbarDrag(win);
            if (tb->host->vroot) tb->host->vroot->ClearPressed();
            return true;
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            CancelToolbarDrag(win);
            return false;
    }
    return false;
}

static void OnToolbarNativeMsg(MainWindow* win, VirtHostNativeMsg* ev) {
    if (OnToolbarDockDrag(win, ev) || OnToolbarDrag(win, ev)) {
        ev->didHandle = true;
        ev->res = 0;
        return;
    }
    switch (ev->msg) {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            if (GET_KEYSTATE_WPARAM(ev->wp) & MK_CONTROL) return;
            auto* tb = win->toolbarVirt;
            if (!tb || !tb->mainRow) return;
            Point pt =
                HwndMapWindowPoint(HWND_DESKTOP, win->hwndToolbar, Point{GET_X_LPARAM(ev->lp), GET_Y_LPARAM(ev->lp)});
            auto* line =
                tb->annotationLine && tb->annotationLine->lastBounds.Contains(pt) ? tb->annotationLine : tb->mainRow;
            int delta = GET_WHEEL_DELTA_WPARAM(ev->wp);
            if (line->Wheel(ev->msg == WM_MOUSEHWHEEL ? delta : -delta)) HideToolbarHoverDropdown(win);
            ev->didHandle = true;
            ev->res = 0;
            return;
        }
        case WM_KEYDOWN:
            if (ev->wp == VK_ESCAPE && win->annotationMode) {
                ToggleAnnotationMode(win);
                ev->didHandle = true;
                return;
            }
            if (ev->wp == VK_TAB && !IsCtrlPressed() && !IsAltPressed()) {
                auto* tb = win->toolbarVirt;
                Vec<TabStop> stops;
                CollectTabStops(tb->host->layout, stops);
                VirtCtrl* focused = tb->host->vroot ? tb->host->vroot->focused : nullptr;
                VirtCtrl* edge = nullptr;
                for (auto& stop : stops) {
                    if (!stop.vwnd) continue;
                    if (!edge || !IsShiftPressed()) edge = stop.vwnd;
                }
                if (focused && focused == edge) {
                    AdvanceFocus(win);
                    ev->didHandle = true;
                }
            }
            return;
        case WM_COMMAND:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            ev->didHandle = OnCtlColor(win, ev);
            return;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (win->tabsInTitlebar && !win->annotationMode) {
                ev->didHandle = OnCaptionDrag(win, ev);
            }
            return;
    }
}

void ToolbarSetNativeHooks(MainWindow* win, VirtHost* host) {
    host->onNativeMsg = MkFunc1(OnToolbarNativeMsg, win);
}

#if IS_DEBUG
static void ToolbarInteractionTests() {
    MainWindow win(nullptr);
    win.tabsCtrl = new TabsCtrl();
    ToolbarVirt toolbar;
    win.toolbarVirt = &toolbar;
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraToolbarInteractionTest");
    args.initialSize = {240, 40};
    args.isPopup = true;
    args.visible = false;
    auto* host = VirtHost::Create(args);
    utassert(host != nullptr);
    if (!host) {
        win.toolbarVirt = nullptr;
        return;
    }
    toolbar.host = host;
    win.hwndToolbar = host->native;
    toolbar.rowDy = 36;
    auto* line = new ToolbarLine(&toolbar);
    toolbar.mainRow = line;
    line->gap = 6;
    int commands[] = {CmdGoToPrevPage, CmdGoToNextPage, CmdHandTool, CmdAnnotationLasso};
    for (int cmd : commands) {
        auto* button = new VirtIconButton();
        button->id = cmd;
        button->padding = {12, 50, 12, 50};
        line->AddChild(button);
        VecAppend(toolbar.items, (VirtCtrl*)button);
    }
    auto* pinned = new VirtIconButton();
    pinned->id = CmdLast + 50;
    pinned->padding = {12, 50, 12, 50};
    line->AddChild(pinned);
    VecAppend(toolbar.pinnedItems, (VirtCtrl*)pinned);
    for (int direction : {-1, 1}) {
        auto* button = new VirtIconButton();
        button->id = CmdLast + 30 + (direction > 0 ? 1 : 0);
        button->padding = {12, 12, 12, 12};
        button->onClick = MkFunc1(OnToolbarScrollClicked, &win);
        button->onKeyDown = MkFunc1(OnToolbarKey, &win);
        button->SetFlag(vwfFocusable, true);
        line->AddChild(button);
        if (direction < 0)
            line->previousButton = button;
        else
            line->nextButton = button;
    }
    toolbar.overflowButton = new VirtIconButton();
    toolbar.overflowButton->id = ToolbarOverflowId;
    toolbar.overflowButton->padding = {12, 12, 12, 12};
    line->AddChild(toolbar.overflowButton);
    ToolbarSetNativeHooks(&win, host);
    host->SetLayout(line);
    auto center = [](VirtCtrl* button) {
        Rect rect = button->BoundsInWindow();
        return Point{rect.x + rect.dx / 2, rect.y + rect.dy / 2};
    };
    Point screen = HwndMapWindowPoint(host->native, HWND_DESKTOP, {10, 10});
    LPARAM wheelPoint = MAKELPARAM(screen.x, screen.y);
    utassert(line->firstGroup == 0 && line->canScrollAfter && !line->canScrollBefore);
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), wheelPoint);
    utassert(line->firstGroup == 0);
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), wheelPoint);
    utassert(line->firstGroup == 1 && line->canScrollBefore);
    SendMessageW(host->native, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup == 1);
    SendMessageW(host->native, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup == 0);

    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, 1 - WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup == 0 && line->wheelRemainder == 0);
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup == 1);
    while (line->Scroll(1)) {
    }
    int lastGroup = line->firstGroup;
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA - 1), wheelPoint);
    utassert(line->firstGroup == lastGroup && line->wheelRemainder == 0);
    SendMessageW(host->native, WM_MOUSEHWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), wheelPoint);
    utassert(line->firstGroup < lastGroup);
    line->wheelRemainder = 0;
    while (line->Scroll(-1)) {
    }

    // Caption dragging must leave these controls' clicks in the virtual input path.
    win.tabsInTitlebar = true;
    Point next = center(line->nextButton);
    bool hitNext = ToolbarItemFromPoint(&win, next) == line->nextButton;
    utassert(hitNext);
    if (hitNext) {
        SendMessageW(host->native, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(next.x, next.y));
        SendMessageW(host->native, WM_LBUTTONUP, 0, MAKELPARAM(next.x, next.y));
        utassert(line->firstGroup == 1);
    }
    host->vroot->SetFocus(line->nextButton);
    SendMessageW(host->native, WM_KEYDOWN, VK_RETURN, 0);
    utassert(line->firstGroup == 2);
    Point previous = center(line->previousButton);
    utassert(ToolbarItemFromPoint(&win, previous) == line->previousButton);
    host->vroot->SetFocus(line->previousButton);
    SendMessageW(host->native, WM_KEYDOWN, VK_SPACE, 0);
    utassert(line->firstGroup == 1);

    ToolbarPostCommand(&win, CmdAnnotationLasso);
    utassert(line->firstGroup == 3 && toolbar.items[3]->IsVisible());
    line->Scroll(1);
    utassert(line->firstGroup == 4 && !line->canScrollAfter && pinned->IsVisible());
    utassert(ToolbarItemFromPoint(&win, center(pinned)) == pinned);
    host->SetBounds({0, 0, 640, 40});
    host->Relayout();
    utassert(line->firstGroup == 0 && !line->canScrollAfter && !line->canScrollBefore);
    utassert(toolbar.overflowButton->GetVisibility() == Visibility::Visible);
    win.toolbarVirt = nullptr;
    win.hwndToolbar = nullptr;
    delete host;
}

// Use the production palette builders in hidden native hosts. No live app state.
static void CaptureToolbarPalette(VirtHost* host, Str name) {
    TempStr directory = GetEnvVariableTemp(StrL("SUMATRA_PALETTE_CAPTURES"));
    if (!len(directory)) return;
    Rect rect = host->ClientRect();
    Rect window = host->ScreenRect();
    Pixmap* image = AllocPixmapDIB(window.dx, window.dy);
    if (!image) return;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(dc, image->hbmp);
    HdcFillRect(dc, {0, 0, window.dx, window.dy}, TbBgColor());
    SendMessageW(host->native, WM_PRINT, (WPARAM)dc, PRF_NONCLIENT);
    Point origin = HwndClientToScreen(host->native, {0, 0});
    SetViewportOrgEx(dc, origin.x - window.x, origin.y - window.y, nullptr);
    auto* gfx = GfxCreate(dc);
    gfx->FillRect(rect, TbBgColor());
    host->vroot->Paint(gfx, rect);
    gfx->FillRoundedRect(rect, UiCornerDiameter(DpiGetForHwnd(host->native), 6), kColorTransparent, ThemeEdgeColor());
    delete gfx;
    GdiFlush();
    SelectObject(dc, old);
    DeleteDC(dc);
    Str bitmap = PixmapToBmpFormat(image);
    utassert(file::WriteFile(path::JoinTemp(directory, name), bitmap));
    str::Free(bitmap);
    FreePixmap(image);
}

static void PaletteTestClick(int* count, VirtMouseEvent* ev) {
    (*count)++;
    ev->didHandle = true;
}

static void ToolbarReorderTests() {
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    defer {
        DeleteSettings(gSettings);
        gSettings = saved;
    };
    str::ReplaceWithCopy(&gSettings->toolbarCustomLayout,
                         StrL("CmdHandTool CmdZoomOut | CmdZoomIn CmdAnnotationLasso"));
    MainWindow win(nullptr);
    win.tabsCtrl = new TabsCtrl();
    ToolbarVirt tb;
    win.toolbarVirt = &tb;
    tb.platformFont = GetAppFont();
    tb.iconSize = ToolbarIconSize();
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraToolbarReorderTest");
    args.isPopup = true;
    args.visible = false;
    args.initialSize = {1600, 100};
    tb.host = VirtHost::Create(args);
    utassert(tb.host != nullptr);
    if (!tb.host) {
        win.toolbarVirt = nullptr;
        return;
    }
    win.hwndToolbar = tb.host->native;
    ToolbarSetNativeHooks(&win, tb.host);
    tb.host->onTimer = MkFunc1(OnToolbarTimer, &win);
    BuildToolbarLayout(&win);
    auto center = [](VirtCtrl* ctrl) {
        Rect bounds = ctrl->BoundsInWindow();
        return Point{bounds.x + bounds.dx / 2, bounds.y + bounds.dy / 2};
    };
    auto send = [&](UINT msg, WPARAM keys, Point point) {
        SendMessageW(tb.host->native, msg, keys, MAKELPARAM(point.x, point.y));
    };
    auto expand = [&]() {
        tb.annotationExpanded = true;
        for (auto* group : tb.annotationGroups) group->SetVisibility(Visibility::Visible);
        tb.host->Relayout();
    };
    int clicks = 0;
    VirtCtrl* hand = ToolbarItemForCmd(&win, CmdHandTool);
    hand->onClick = MkFunc1(PaletteTestClick, &clicks);
    Point start = center(hand);
    send(WM_LBUTTONDOWN, MK_LBUTTON, start);
    send(WM_MOUSEMOVE, MK_LBUTTON, {start.x + 1, start.y});
    send(WM_LBUTTONUP, 0, start);
    utassert(clicks == 1);
    utassert(tb.items[0]->id == CmdHandTool);

    VirtCtrl* zoom = ToolbarItemForCmd(&win, CmdZoomIn);
    Point end = center(zoom);
    end.x += zoom->BoundsInWindow().dx / 2 - 1;
    send(WM_LBUTTONDOWN, MK_LBUTTON, start);
    send(WM_MOUSEMOVE, MK_LBUTTON, end);
    send(WM_LBUTTONUP, 0, end);
    utassert(clicks == 1);
    BuildToolbarLayout(&win);
    utassert(tb.items[0]->id == CmdZoomOut);
    utassert(tb.items[1]->id == CmdZoomIn);
    utassert(tb.items[2]->id == 0);
    utassert(tb.items[3]->id == CmdHandTool);

    expand();
    VirtCtrl* text = ToolbarItemForCmd(&win, CmdCreateAnnotText);
    VirtCtrl* freeText = ToolbarItemForCmd(&win, CmdCreateAnnotFreeText);
    utassert(text && freeText && text->IsVisible() && freeText->IsVisible());
    if (text && freeText) {
        text->onClick = MkFunc1(PaletteTestClick, &clicks);
        start = center(text);
        end = center(freeText);
        end.x += freeText->BoundsInWindow().dx / 2 - 1;
        send(WM_LBUTTONDOWN, MK_LBUTTON, start);
        send(WM_MOUSEMOVE, MK_LBUTTON, end);
        send(WM_LBUTTONUP, 0, end);
        utassert(clicks == 1);
        BuildToolbarLayout(&win);
        expand();
        text = ToolbarItemForCmd(&win, CmdCreateAnnotText);
        freeText = ToolbarItemForCmd(&win, CmdCreateAnnotFreeText);
        utassert(text->BoundsInWindow().x > freeText->BoundsInWindow().x);
    }
    Str encoded = SerializeSettings(gSettings, {});
    Settings* reopened = NewSettings(encoded);
    str::Free(encoded);
    DeleteSettings(gSettings);
    gSettings = reopened;
    BuildToolbarLayout(&win);
    utassert(tb.items[0]->id == CmdZoomOut && tb.items[3]->id == CmdHandTool);
    expand();
    text = ToolbarItemForCmd(&win, CmdCreateAnnotText);
    freeText = ToolbarItemForCmd(&win, CmdCreateAnnotFreeText);
    utassert(text->BoundsInWindow().x > freeText->BoundsInWindow().x);

    // Dragging across the native scroll arrows reveals hidden groups.
    str::ReplaceWithCopy(&gSettings->toolbarCustomLayout, Str{});
    BuildToolbarLayout(&win);
    expand();
    tb.mainRow->firstGroup = 0;
    int narrowWidth = tb.mainRow->children[tb.mainRow->brandIdx + 1].layout->MinIntrinsicWidth(0) +
                      tb.mainRow->OverflowButton()->MinIntrinsicWidth(0) +
                      tb.mainRow->previousButton->MinIntrinsicWidth(0) + tb.mainRow->nextButton->MinIntrinsicWidth(0) +
                      4 * tb.mainRow->gap + UiScalePx(16);
    tb.host->SetBounds({0, 0, narrowWidth, 100});
    tb.host->Relayout();
    VirtCtrl* first = nullptr;
    for (auto* items : {&tb.items, &tb.annotationItems}) {
        for (auto* item : *items) {
            if (item && item->id && AsVirtIconButton(item) && item->IsVisible()) {
                first = item;
                break;
            }
        }
        if (first) break;
    }
    utassert(first && tb.mainRow->canScrollAfter);
    if (first && tb.mainRow->canScrollAfter) {
        start = center(first);
        end = center(tb.mainRow->nextButton);
        int previous = tb.mainRow->firstGroup;
        send(WM_LBUTTONDOWN, MK_LBUTTON, start);
        send(WM_MOUSEMOVE, MK_LBUTTON, end);
        SendMessageW(tb.host->native, WM_TIMER, 0x104, 0);
        utassert(tb.mainRow->firstGroup > previous);
        SendMessageW(tb.host->native, WM_KEYDOWN, VK_ESCAPE, 0);
        utassert(GetCapture() != tb.host->native);
        utassert(!tb.host->vroot->pressed);
    }
    win.toolbarVirt = nullptr;
    win.hwndToolbar = nullptr;
    win.pageEdit = nullptr;
    win.chapterEdit = nullptr;
    delete tb.host;
}

struct ShapeMenuTestState {
    int opened = 0;
    bool backgroundColor = false;
    bool backgroundOpacity = false;
};

static void ShapeMenuTestNative(ShapeMenuTestState* state, VirtHostNativeMsg* ev) {
    if (ev->msg != WM_INITMENUPOPUP) return;
    HMENU menu = (HMENU)ev->wp;
    state->opened++;
    state->backgroundColor = GetMenuState(menu, 3, MF_BYCOMMAND) != (UINT)-1;
    state->backgroundOpacity = GetMenuState(menu, 4, MF_BYCOMMAND) != (UINT)-1;
    EndMenu();
}

void Toolbar_UnitTestsShapes() {
    RenderCache* previousCache = gRenderCache;
    if (!previousCache) gRenderCache = new RenderCache();
    defer {
        if (!previousCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    if (!ThemeGetCount()) CreateThemeCommands();
    defer {
        DeleteSettings(gSettings);
        gSettings = saved;
        if (saved) SetCurrentThemeFromSettings();
        RefreshUiFonts();
    };
    int grouped = 0;
    for (auto& button : gPdfAnnotationButtons) {
        grouped += button.cmdId == CmdAnnotationShapes ? 1 : 0;
        utassert(ShapeFillCommandPtr(button.cmdId) == nullptr);
    }
    utassert(grouped == 1);
    str::ReplaceWithCopy(&gSettings->toolbarAnnotationOrder,
                         StrL("CmdHandTool CmdCreateAnnotLine CmdCreateAnnotCircle CmdCreateAnnotText"));
    PopulateAnnotationOrder();
    int shapeIndex = -1, textIndex = -1;
    for (int i = 0; i < kPdfAnnotationButtonsCount; i++) {
        if (gAnnotationButtons[i].cmdId == CmdAnnotationShapes) shapeIndex = i;
        if (gAnnotationButtons[i].cmdId == CmdCreateAnnotText) textIndex = i;
    }
    utassert(shapeIndex >= 0 && textIndex > shapeIndex);
    str::Free(gSettings->toolbarAnnotationOrder);
    gSettings->toolbarAnnotationOrder = {};
    for (int variant = 0; variant < 2; variant++) {
        str::ReplaceWithCopy(&gSettings->theme, variant ? StrL("Sumatra Dark") : StrL("Sumatra Light"));
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        ToolbarVirt tb;
        win.toolbarVirt = &tb;
        tb.platformFont = GetAppFont();
        tb.iconSize = ToolbarIconSize();
        VirtHost::CreateArgs args;
        args.className = WStrL(L"SumatraGroupedShapesTest");
        args.isPopup = true;
        args.visible = false;
        args.title = Tr("Shapes");
        args.initialSize = {500, 600};
        auto* host = VirtHost::Create(args);
        utassert(host);
        if (!host) {
            win.toolbarVirt = nullptr;
            continue;
        }
        win.hwndFrame = host->native;
        tb.host = tb.hoverHost = host;
        for (int i = 0; i < dimofi(kShapeFillCommands); i++) {
            gSettings->annotations.shapeTool = i;
            int cmd = SelectedShapeCmd();
            utassert(cmd == kShapeFillCommands[i]);
            ShapeWidthPicked(ShapeFillCommandPtr(cmd), 1.f + i * .5f);
            utassert(*ShapeBorderWidth(cmd) == 1.f + i * .5f);
            ToolbarHoverBuildEvent ev;
            ev.cmdId = CmdAnnotationShapes;
            VecReset(tb.hoverItems);
            BuildShapesHoverMenu(&win, &ev);
            utassert(ev.layout);
            Size natural = host->SetLayoutSizedToContent(ev.layout);
            host->SetPos({0, 0, natural.dx, natural.dy}, false);
            Vec<VirtCtrl*> controls;
            CollectPaletteControls(host->layout, controls);
            int types = 0, border = 0, fill = 0, widths = 0, opacity = 0;
            PaletteNumberEdit* widthInput = nullptr;
            PaletteNumberEdit* opacityInput = nullptr;
            InkThicknessSlider* widthSlider = nullptr;
            ShapeFillSlider* opacitySlider = nullptr;
            for (auto* ctrl : controls) {
                if (str::Eq(ctrl->name, StrL("shape-border-width-value"))) widthInput = (PaletteNumberEdit*)ctrl;
                if (str::Eq(ctrl->name, StrL("shape-background-opacity-value")))
                    opacityInput = (PaletteNumberEdit*)ctrl;
                if (str::Eq(ctrl->name, StrL("shape-background-opacity"))) opacitySlider = (ShapeFillSlider*)ctrl;
                types += str::Eq(ctrl->name, StrL("shape-type")) ? 1 : 0;
                border += str::Eq(ctrl->name, StrL("shape-border-color")) ? 1 : 0;
                fill += str::Eq(ctrl->name, StrL("shape-background-color")) ? 1 : 0;
                opacity += str::Eq(ctrl->name, StrL("shape-background-opacity")) ? 1 : 0;
                if (str::Eq(ctrl->name, StrL("shape-border-width"))) {
                    widths++;
                    auto* slider = (InkThicknessSlider*)ctrl;
                    widthSlider = slider;
                    utassert(fabsf(slider->Width() - *ShapeBorderWidth(cmd)) < .01f);
                    slider->onThickness.Call(2.7f);
                    utassert(fabsf(*ShapeBorderWidth(cmd) - 2.7f) < .001f);
                }
            }
            bool math = MathShapeForCommand(cmd) >= 0;
            utassert(types == dimofi(kShapeFillCommands) && border > 0 && widths == 1);
            utassert(math ? fill == 0 && opacity == 0 : fill > 0 && opacity == 1);
            utassert(widthInput && widthSlider && (math || (opacityInput && opacitySlider)));
            if (widthInput && opacityInput && widthSlider && opacitySlider) {
                utassert(widthInput->BoundsInWindow().x >= widthSlider->BoundsInWindow().Right());
                utassert(opacityInput->BoundsInWindow().x >= opacitySlider->BoundsInWindow().Right());
                auto enter = [&](PaletteNumberEdit* input, Str value) {
                    host->vroot->SetFocus(input);
                    input->anchor = 0;
                    input->caret = len(input->text);
                    for (int c = 0; c < len(value); c++) SendMessageW(host->native, WM_CHAR, value.s[c], 0);
                    SendMessageW(host->native, WM_KEYDOWN, VK_RETURN, 0);
                    utassert(IsWindow(host->native));
                };
                widthInput->SetValue(1);
                int compactWidth = widthInput->GetIdealSize().dx;
                utassert(compactWidth == PlatformFontMeasureText(widthInput->font, StrL("000")).dx + UiScalePx(16));
                enter(widthInput, StrL("12.35"));
                utassert(widthInput->GetIdealSize().dx > compactWidth);
                utassert(fabsf(*ShapeBorderWidth(cmd) - 12.35f) < .001f);
                utassert(fabsf(widthSlider->Width() - 12.35f) < .001f);
                utassert(fabsf(widthSlider->preview->thickness - 12.35f) < .001f);
                enter(widthInput, StrL("64.01"));
                utassert(widthInput->invalid && fabsf(*ShapeBorderWidth(cmd) - 12.35f) < .001f);
                enter(widthInput, StrL("64"));
                utassert(!widthInput->invalid && *ShapeBorderWidth(cmd) == 64.f);
                enter(widthInput, StrL("0"));
                utassert(*ShapeBorderWidth(cmd) == 0);
                enter(widthInput, StrL("2.7"));
                utassert(str::Eq(widthInput->text, StrL("2.7")));
                enter(opacityInput, StrL("33"));
                utassert(*ShapeFillForCmd(cmd).opacity == 33 && opacitySlider->value == 33);
                enter(opacityInput, StrL("101"));
                utassert(opacityInput->invalid && *ShapeFillForCmd(cmd).opacity == 33);
                enter(opacityInput, StrL("33.5"));
                utassert(*ShapeFillForCmd(cmd).opacity == 33);
                enter(opacityInput, StrL("0"));
                utassert(*ShapeFillForCmd(cmd).opacity == 0);
                enter(opacityInput, StrL("100"));
                utassert(*ShapeFillForCmd(cmd).opacity == 100);
                opacitySlider->value = 42;
                opacitySlider->OnCommitted();
                utassert(str::Eq(opacityInput->text, StrL("42")));
                widthSlider->value = 15;
                widthSlider->OnCommitted();
                utassert(fabsf(widthSlider->Width() - 1.5f) < .001f);
                utassert(str::Eq(widthInput->text, StrL("1.5")));
                enter(widthInput, StrL("2.7"));
                host->vroot->SetFocus(nullptr);
            }
            AnnotCreateArgs create{CmdIdToAnnotationType(cmd)};
            SetAnnotCreateArgs(create, nullptr);
            ConfigureShapeCreateArgs(cmd, create);
            utassert(fabsf(create.borderWidth - 2.7f) < .001f);
            if (i == 2) {
                SetWindowTextW(host->native, CWStrTemp(ShapeToolTitle()));
                WindowApplyScaledCaption(host->native);
                CaptureToolbarPalette(host, fmt("grouped-shapes-%d.bmp", variant));
            }
        }
        tb.host = tb.hoverHost = nullptr;
        win.toolbarVirt = nullptr;
        win.hwndFrame = nullptr;
        delete host;
    }
}

static void ShapePaletteTests() {
    MainWindow win(nullptr);
    win.tabsCtrl = new TabsCtrl();
    ToolbarVirt tb;
    win.toolbarVirt = &tb;
    tb.platformFont = GetAppFont();
    tb.iconSize = ToolbarIconSize();
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraShapePaletteTest");
    args.isPopup = true;
    args.visible = false;
    args.initialSize = {500, 400};
    auto* host = VirtHost::Create(args);
    utassert(host != nullptr);
    if (!host) {
        win.toolbarVirt = nullptr;
        return;
    }
    tb.host = tb.hoverHost = host;
    args.className = WStrL(L"SumatraShapeMenuOwnerTest");
    auto* owner = VirtHost::Create(args);
    utassert(owner != nullptr);
    if (!owner) {
        win.toolbarVirt = nullptr;
        delete host;
        return;
    }
    win.hwndFrame = owner->native;
    ShapeMenuTestState menuState;
    owner->onNativeMsg = MkFunc1(ShapeMenuTestNative, &menuState);
    const int commands[] = {CmdCreateAnnotLine, CmdCreateAnnotPolyLine, CmdCreateAnnotSquare, CmdCreateAnnotCircle,
                            CmdCreateAnnotPolygon};
    const Str prefixes[] = {StrL("Line"), StrL("PolyLine"), StrL("Square"), StrL("Circle"), StrL("Polygon")};
    auto click = [&](VirtCtrl* control) {
        Rect rect = control->BoundsInWindow();
        LPARAM point = MAKELPARAM(rect.x + rect.dx / 2, rect.y + rect.dy / 2);
        SendMessageW(host->native, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(host->native, WM_LBUTTONUP, 0, point);
    };
    for (int i = 0; i < dimof(commands); i++) {
        ToolbarHoverBuildEvent ev;
        ev.cmdId = commands[i];
        BuildAnnotColorsHoverMenu(&win, &ev);
        host->SetLayoutSizedToContent(ev.layout);
        Vec<VirtCtrl*> controls;
        CollectPaletteControls(ev.layout, controls);
        VirtCtrl* advanced = nullptr;
        VirtCtrl* fill = nullptr;
        VirtCtrl* none = nullptr;
        VirtSlider* opacity = nullptr;
        VirtCtrl* outlinePreset = nullptr;
        for (auto* control : controls) {
            if (str::Eq(control->tooltip, Tr("Additional tool settings"))) advanced = control;
            if (!outlinePreset && control->id == commands[i] && control->onContextMenu.IsValid())
                outlinePreset = control;
            if (str::Eq(control->name, StrL("shape-background-color"))) {
                auto* swatch = (ToolbarColorSwatch*)control;
                if (swatch->isNone)
                    none = control;
                else if (!fill)
                    fill = control;
            }
            if (str::Eq(control->name, StrL("shape-background-opacity"))) opacity = AsVirtSlider(control);
        }
        utassert(outlinePreset != nullptr);
        if (outlinePreset) {
            Rect rect = outlinePreset->BoundsInWindow();
            LPARAM point = MAKELPARAM(rect.x + rect.dx / 2, rect.y + rect.dy / 2);
            menuState = {};
            SendMessageW(host->native, WM_RBUTTONDOWN, MK_RBUTTON, point);
            SendMessageW(host->native, WM_RBUTTONUP, 0, point);
            utassert(menuState.opened == 1 && menuState.backgroundColor && menuState.backgroundOpacity);
        }
        utassert(advanced != nullptr);
        if (!advanced) continue;
        click(advanced);
        VecReset(controls);
        CollectPaletteControls(ev.layout, controls);
        for (auto* control : controls) {
            if (str::Eq(control->name, StrL("shape-background-color"))) {
                auto* swatch = (ToolbarColorSwatch*)control;
                if (swatch->isNone)
                    none = control;
                else if (!fill)
                    fill = control;
            }
            if (str::Eq(control->name, StrL("shape-background-opacity"))) opacity = AsVirtSlider(control);
        }
        utassert(fill && none && opacity);
        if (!fill || !none || !opacity) continue;
        utassert(fill->IsVisible() && opacity->IsVisible());
        Color chosen = ((ToolbarColorSwatch*)fill)->col & 0xffffff;
        click(fill);
        opacity->onKeyDown = MkFunc1(OnPaletteKey, &win);
        host->vroot->SetFocus(opacity);
        SendMessageW(host->native, WM_KEYDOWN, VK_HOME, 0);
        for (int percent = 0; percent < 37; percent++) SendMessageW(host->native, WM_KEYDOWN, VK_RIGHT, 0);
        utassert(opacity->minVal == 0 && opacity->maxVal == 100 && opacity->value == 37);
        Color outline = MkRgb(17, 33, 55);
        SetAnnotPresetColor(&win, commands[i], outline);
        Str saved = SerializeSettings(gSettings, {});
        utassert(str::Contains(saved, fmt("%sInteriorColor = %s", prefixes[i], SerializeColorTemp(chosen))));
        utassert(str::Contains(saved, fmt("%sInteriorOpacity = 37", prefixes[i])));
        Settings* reopened = NewSettings(saved);
        str::Free(saved);
        DeleteSettings(gSettings);
        gSettings = reopened;
        BuildAnnotColorsHoverMenu(&win, &ev);
        host->SetLayoutSizedToContent(ev.layout);
        VecReset(controls);
        CollectPaletteControls(ev.layout, controls);
        for (auto* control : controls)
            if (str::Eq(control->tooltip, Tr("Additional tool settings"))) click(control);
        VecReset(controls);
        CollectPaletteControls(ev.layout, controls);
        bool restoredFill = false, restoredOpacity = false;
        for (auto* control : controls) {
            if (str::Eq(control->name, StrL("shape-background-color"))) {
                auto* swatch = (ToolbarColorSwatch*)control;
                restoredFill |= swatch->col != kColorUnset && swatch->isCurrent && (swatch->col & 0xffffff) == chosen;
            }
            if (str::Eq(control->name, StrL("shape-background-opacity")))
                restoredOpacity = AsVirtSlider(control)->value == 37;
        }
        utassert(restoredFill && restoredOpacity && SameColorAndAlpha(AnnotCurrentColor(&win, commands[i]), outline));
        CaptureToolbarPalette(host, fmt("shape-settings-%s.bmp", prefixes[i]));
        auto setting = ShapeFillForCmd(commands[i]);
        Color beforeCancel = GetParsedColor(*setting.color, kColorUnset);
        ChangeColorsArgs colorArgs;
        colorArgs.color = kColorUnset;
        colorArgs.opacityPercent = 0;
        for (bool selected : {false, true}) {
            auto* target = new AnnotColorsTarget();
            target->shapeFillCmd = commands[i];
            colorArgs.didSelect = selected;
            AnnotColorsPicked(target, &colorArgs);
            utassert(GetParsedColor(*setting.color, kColorUnset) == (selected ? kColorUnset : beforeCancel));
            utassert(*setting.opacity == (selected ? 0 : 37));
        }
        VecReset(tb.hoverItems);
    }
    delete owner;
    win.hwndFrame = nullptr;
    tb.hoverHost = nullptr;
    win.toolbarVirt = nullptr;
    delete host;
}

struct PopupCloseTestState {
    MainWindow* win = nullptr;
    AnnotColorPopup* replacement = nullptr;
    int picked = 0;
};

static void PopupCloseTestPick(PopupCloseTestState* state, Color) {
    state->picked++;
    state->replacement = new AnnotColorPopup();
    state->replacement->win = state->win;
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraPopupCloseTest");
    args.isPopup = true;
    args.visible = false;
    args.initialSize = {160, 80};
    state->replacement->host = VirtHost::Create(args);
    gAnnotColorPopup = state->replacement;
}

static void PopupCloseTests() {
    uitask::DrainQueue();
    MainWindow win(nullptr);
    win.tabsCtrl = new TabsCtrl();
    VecAppend(gWindows, &win);
    defer {
        VecRemove(gWindows, &win);
    };
    PopupCloseTestState state;
    state.win = &win;
    for (bool closingOwner : {false, true}) {
        auto* popup = new AnnotColorPopup();
        popup->win = &win;
        popup->hasPick = true;
        popup->picked = MkRgb(10, 20, 30);
        popup->onPick = MkFunc1(PopupCloseTestPick, &state);
        VirtHost::CreateArgs args;
        args.className = WStrL(L"SumatraPopupCloseTest");
        args.isPopup = true;
        args.visible = false;
        args.initialSize = {160, 80};
        popup->host = VirtHost::Create(args);
        utassert(popup->host != nullptr);
        if (!popup->host) {
            delete popup;
            continue;
        }
        popup->host->onNativeMsg = MkFunc1(AnnotColorPopupNativeMsg, popup);
        gAnnotColorPopup = popup;
        SetCapture(popup->host->native);
        // ReleaseCapture also sends WM_CAPTURECHANGED; both close requests
        // must stay attached to this popup, never to the one opened by its callback.
        CloseAnnotColorPopup(popup);
        CloseAnnotColorPopup(popup);
        win.isBeingClosed = closingOwner;
        uitask::DrainQueue();
        if (closingOwner) {
            utassert(state.picked == 1 && gAnnotColorPopup == nullptr);
        } else {
            utassert(state.picked == 1 && gAnnotColorPopup == state.replacement);
        }
        auto* current = gAnnotColorPopup;
        gAnnotColorPopup = nullptr;
        if (current) {
            delete current->host;
            delete current;
        }
        state.replacement = nullptr;
        win.isBeingClosed = false;
    }
    auto* popup = new AnnotColorPopup();
    popup->win = &win;
    popup->menuActive = true;
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraPopupCloseTest");
    args.isPopup = true;
    args.visible = false;
    args.initialSize = {160, 80};
    popup->host = VirtHost::Create(args);
    utassert(popup->host != nullptr);
    if (!popup->host) {
        delete popup;
        return;
    }
    popup->host->onNativeMsg = MkFunc1(AnnotColorPopupNativeMsg, popup);
    gAnnotColorPopup = popup;
    SetCapture(popup->host->native);
    win.isBeingClosed = true;
    DestroyWindow(popup->host->native);
    uitask::DrainQueue();
    utassert(gAnnotColorPopup == nullptr);
    if (gAnnotColorPopup) {
        popup = gAnnotColorPopup;
        gAnnotColorPopup = nullptr;
        delete popup->host;
        delete popup;
    }
    win.isBeingClosed = false;
}

static void RecordPaletteAlpha(Color* picked, Color color) {
    *picked = color;
}

static void ToolbarAlphaTests() {
    MainWindow win(nullptr);
    win.tabsCtrl = new TabsCtrl();
    ToolbarVirt toolbar;
    win.toolbarVirt = &toolbar;
    VecAppend(gWindows, &win);
    Color savedInk = InkPenColor(&win);
    int savedOpacity = InkPenOpacity(&win);
    defer {
        VecRemove(gWindows, &win);
        win.toolbarVirt = nullptr;
        SetInkPenColor(&win, savedInk);
        SetInkPenOpacity(&win, savedOpacity);
        uitask::DrainQueue();
    };
    Color transparent = MkRgba(31, 117, 201, 0);
    Color opaque = MkRgba(31, 117, 201, 255);
    utassert(!SameColorAndAlpha(transparent, opaque));
    Str* palette = AnnotPresetColorList(CmdCreateAnnotInk);
    Str saved = str::Dup(*palette);
    str::ReplaceWithCopy(palette, StrL(""));
    utassert(AddAnnotPresetColor(CmdCreateAnnotInk, transparent));
    utassert(AddAnnotPresetColor(CmdCreateAnnotInk, opaque));
    Vec<Color> colors;
    AnnotPresetColors(CmdCreateAnnotInk, colors);
    utassert(len(colors) == 2);
    if (len(colors) == 2) utassert(colors[0] == transparent && colors[1] == opaque);
    utassert(RemoveAnnotPresetColor(CmdCreateAnnotInk, transparent));
    VecReset(colors);
    AnnotPresetColors(CmdCreateAnnotInk, colors);
    utassert(len(colors) == 1 && colors[0] == opaque);
    str::ReplaceWithCopy(palette, saved);
    str::Free(saved);

    SetAnnotPresetColor(&win, CmdCreateAnnotInk, opaque);
    utassert(InkPenOpacity(&win) == 100);
    SetAnnotPresetColor(&win, CmdCreateAnnotInk, transparent);
    utassert(InkPenOpacity(&win) == 0 && GetAlpha(InkPenColor(&win)) == 0);

    auto* popup = new AnnotColorPopup();
    popup->win = &win;
    Color picked = kColorUnset;
    popup->onPick = MkFunc1(RecordPaletteAlpha, &picked);
    VirtHost::CreateArgs args;
    args.className = WStrL(L"SumatraPaletteAlphaTest");
    args.isPopup = true;
    args.visible = false;
    args.initialSize = {160, 80};
    popup->host = VirtHost::Create(args);
    utassert(popup->host != nullptr);
    if (popup->host) {
        popup->host->onNativeMsg = MkFunc1(AnnotColorPopupNativeMsg, popup);
        gAnnotColorPopup = popup;
        SetCapture(popup->host->native);
        ToolbarColorSwatch swatch;
        swatch.col = transparent;
        VirtMouseEvent click;
        click.target = &swatch;
        OnAnnotColorPopupSwatch(popup, &click);
        uitask::DrainQueue();
        utassert(picked == transparent && !gAnnotColorPopup);
    } else {
        delete popup;
    }

    VecRemove(gWindows, &win);
    VecAppend(gWindows, &win);
    Color editorPick = kColorUnset;
    auto* editorTarget = new AnnotColorsTarget();
    editorTarget->win = &win;
    editorTarget->forAnnotEditor = true;
    editorTarget->editorContext = CaptureAnnotEditPickerContext(&win);
    editorTarget->onPick = MkFunc1(RecordPaletteAlpha, &editorPick);
    ChangeColorsArgs editorArgs;
    editorArgs.didSelect = true;
    editorArgs.color = opaque;
    AnnotColorsPicked(editorTarget, &editorArgs);
    utassert(editorPick == kColorUnset);
    VecRemove(gWindows, &win);

    Color fill = opaque;
    ChangeColorsArgs fillArgs;
    fillArgs.color = kColorUnset;
    auto* fillTarget = new AnnotColorsTarget();
    fillTarget->onPick = MkFunc1(RecordPaletteAlpha, &fill);
    AnnotColorsPicked(fillTarget, &fillArgs);
    utassert(fill == opaque);
    fillArgs.didSelect = true;
    fillTarget = new AnnotColorsTarget();
    fillTarget->onPick = MkFunc1(RecordPaletteAlpha, &fill);
    AnnotColorsPicked(fillTarget, &fillArgs);
    utassert(fill == kColorUnset);

    const int paletteCommands[] = {0, CmdCreateAnnotInk, CmdCreateAnnotHighlight, CmdCreateAnnotSquare};
    for (int cmd : paletteCommands) {
        Str* list = AnnotPresetColorList(cmd);
        Str original = str::Dup(*list);
        str::ReplaceWithCopy(list, StrL("#ff000000"));
        for (int mode = 0; mode < 2; mode++) {
            auto* target = new AnnotColorsTarget();
            target->cmdId = cmd;
            target->onPick = MkFunc1(RecordPaletteAlpha, &fill);
            ChangeColorsArgs selected;
            selected.didSelect = mode != 0;
            selected.color = MkRgba(23, 151, (u8)(211 + mode), 173);
            AnnotColorsPicked(target, &selected);
            VecReset(colors);
            AnnotPresetColors(cmd, colors);
            bool present = false;
            for (Color color : colors) present |= color == selected.color;
            utassert(!present);
            utassert(str::Eq(*list, StrL("#ff000000")));
        }
        str::ReplaceWithCopy(list, original);
        str::Free(original);
    }

    gSettings->annotations.squareBorderWidth = 3.7f;
    SetColorText(gSettings->annotations.squareInteriorColor, StrL("#0000ff"));
    gSettings->annotations.squareInteriorOpacity = 33;
    TogglePinnedTool(&win, CmdCreateAnnotSquare, transparent);
    int first = PinnedToolIndex(&win, CmdCreateAnnotSquare, transparent);
    TogglePinnedTool(&win, CmdCreateAnnotSquare, opaque);
    int second = PinnedToolIndex(&win, CmdCreateAnnotSquare, opaque);
    utassert(first >= 0 && second >= 0 && first != second);
    auto* pins = gSettings->pinnedAnnotationTools;
    if (pins && first >= 0 && first < len(*pins)) {
        VecReset(colors);
        ParseColorList((*pins)[first]->color, colors, 1);
        utassert(len(colors) == 1 && colors[0] == transparent);
        auto* shape = (*pins)[first];
        utassert(fabsf(shape->width - 3.7f) < .001f);
        utassert(shape->shapeStyle && shape->fillOpacity == 33 && len(shape->fillColor));
        gSettings->annotations.squareBorderWidth = 1;
        SetColorText(gSettings->annotations.squareInteriorColor, StrL("#00ff00"));
        gSettings->annotations.squareInteriorOpacity = 99;
        RestorePinnedShape(shape, CmdCreateAnnotSquare);
        utassert(gSettings->annotations.squareInteriorOpacity == 33 &&
                 fabsf(gSettings->annotations.squareBorderWidth - 3.7f) < .001f);
        utassert(GetBlue(GetParsedColor(gSettings->annotations.squareInteriorColor, kColorUnset)) == 255);
        Str serialized = SerializeSettings(gSettings, {});
        Settings* restored = NewSettings(serialized);
        auto* savedShape = (*restored->pinnedAnnotationTools)[first];
        utassert(savedShape->shapeStyle && savedShape->fillOpacity == 33 &&
                 str::Eq(savedShape->fillColor, shape->fillColor));
        DeleteSettings(restored);
        str::Free(serialized);
        TempStr shapeSvg = PinnedShapeSvg(shape, CmdCreateAnnotSquare);
        utassert(str::Contains(shapeSvg, StrL("#0000ff")) && str::Contains(shapeSvg, StrL("0.330")));
    }
    while (pins && len(*pins)) RemovePinnedTool(len(*pins) - 1);

    Pixmap* bitmap = AllocPixmapDIB(64, 64);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, bitmap->hbmp);
    {
        GfxHdc gfx(dc);
        ToolbarColorSwatch swatch;
        swatch.col = transparent;
        VirtPaintCtx context;
        context.gfx = &gfx;
        context.bounds = {0, 0, 32, 32};
        gfx.FillRect({0, 0, 64, 64}, kColWhite);
        swatch.Paint(context);
        GdiFlush();
        utassert(GetPixel(dc, 16, 16) != (transparent & 0xffffff));
        gfx.FillRect({0, 0, 64, 64}, kColWhite);
        InkStrokePreview stroke;
        stroke.col = transparent;
        stroke.thickness = 2.f;
        context.bounds = {0, 0, 64, 64};
        stroke.Paint(context);
    }
    GdiFlush();
    bool unchanged = true;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            if (GetPixel(dc, x, y) != kColWhite) unchanged = false;
    utassert(unchanged);
    SelectObject(dc, previous);
    DeleteDC(dc);
    FreePixmap(bitmap);
}

static void ToolbarPaletteTests() {
    auto savedCornerRadius = gUiCornerRadius;
    gUiCornerRadius = GetAppCornerRadius;
    defer {
        gUiCornerRadius = savedCornerRadius;
    };
    RenderCache* savedCache = gRenderCache;
    if (!savedCache) gRenderCache = new RenderCache();
    defer {
        if (!savedCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    if (!ThemeGetCount()) CreateThemeCommands();
    PopupCloseTests();
    ToolbarAlphaTests();
    ToolbarReorderTests();
    ShapePaletteTests();
    defer {
        DeleteSettings(gSettings);
        gSettings = saved;
        if (gSettings) SetCurrentThemeFromSettings();
        RefreshUiFonts();
    };
    {
        VirtHost::CreateArgs args;
        args.className = WStrL(L"SumatraPopupShapeTest");
        args.isPopup = true;
        args.visible = false;
        args.initialSize = {300, 180};
        auto* host = VirtHost::Create(args);
        utassert(host != nullptr);
        if (host) {
            for (int scale : {100, 150, 250}) {
                gSettings->interfaceScale = scale;
                gSettings->uIFontSize = scale == 100 ? 0 : 36;
                for (int radius : {6, 10}) {
                    host->ClipToRoundedRect(radius, {300, 180});
                    for (Size size : {Size{340, 220}, Size{220, 110}, Size{80, 12}}) {
                        host->SetBounds({0, 0, size.dx, size.dy});
                        HRGN actual = CreateRectRgn(0, 0, 0, 0);
                        utassert(GetWindowRgn(host->native, actual) != ERROR);
                        int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(host->native), radius),
                                                std::min(size.dx, size.dy));
                        HRGN expected = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
                        utassert(EqualRgn(actual, expected));
                        DeleteObject(expected);
                        DeleteObject(actual);
                    }
                }
            }
            gSettings->interfaceScale = 100;
            gSettings->uIFontSize = 0;
            SendMessageW(host->native, WM_PAINT, 0, 0);
            Size size = HwndWindowRect(host->native).Size();
            HRGN actual = CreateRectRgn(0, 0, 0, 0);
            GetWindowRgn(host->native, actual);
            int diameter =
                std::min(2 * GetAppCornerRadius(DpiGetForHwnd(host->native), 10), std::min(size.dx, size.dy));
            HRGN expected = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
            utassert(EqualRgn(actual, expected));
            DeleteObject(expected);
            DeleteObject(actual);
            delete host;
        }
        HWND owner = CreateWindowExW(0, L"STATIC", L"Host owner test", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
        utassert(owner != nullptr);
        if (owner) {
            args.parent = owner;
            host = VirtHost::Create(args);
            utassert(host != nullptr);
            if (host) {
                host->SetLayout(new VirtButton(StrL("Tool"), GetAppFont()));
                HWND native = host->native;
                DestroyWindow(owner);
                utassert(!IsWindow(native));
                utassert(host->native == nullptr);
                utassert(host->vroot && host->vroot->hwnd == nullptr);
                delete host;
            } else {
                DestroyWindow(owner);
            }
        }
        auto* bitmap = AllocPixmapDIB(64, 64);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, bitmap->hbmp);
        auto* gfx = new GfxHdc(dc);
        gfx->FillRect({0, 0, 64, 64}, kColWhite);
        Rect tightRect{12, 20, 28, 12};
        gfx->FillRoundedRect(tightRect, 200, kColBlack, kColorTransparent);
        delete gfx;
        GdiFlush();
        utassert(GetPixel(dc, tightRect.x + tightRect.dx / 2, tightRect.y + tightRect.dy / 2) == kColBlack);
        bool outsideClean = true;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                if (!tightRect.Contains(Point{x, y}) && GetPixel(dc, x, y) != kColWhite) outsideClean = false;
        utassert(outsideClean);
        SelectObject(dc, old);
        DeleteDC(dc);
        FreePixmap(bitmap);
        gSettings->interfaceScale = 100;
        gSettings->uIFontSize = 0;
        RefreshUiFonts();
    }
    {
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        ToolbarVirt tb;
        win.toolbarVirt = &tb;
        tb.platformFont = GetAppFont();
        tb.iconSize = UiScalePx(28);
        Color chosen = MkRgba(31, 117, 201, 255);
        for (int cmd : kAnnotColorCmds) {
            TogglePinnedTool(&win, cmd, chosen);
            utassert(PinnedToolIndex(&win, cmd, chosen) >= 0);
            Str encoded = SerializeSettings(gSettings, {});
            Settings* reopened = NewSettings(encoded);
            str::Free(encoded);
            utassert(reopened->pinnedAnnotationTools && len(*reopened->pinnedAnnotationTools) == 1);
            utassert(SameColorAndAlpha(ParseColor((*reopened->pinnedAnnotationTools)[0]->color), chosen));
            DeleteSettings(reopened);
            TogglePinnedTool(&win, cmd, chosen);
            utassert(PinnedToolIndex(&win, cmd, chosen) == -1);
            Str* list = AnnotPresetColorList(cmd);
            Str original = str::Dup(*list);
            utassert(AddAnnotPresetColor(cmd, chosen));
            utassert(!AddAnnotPresetColor(cmd, chosen | 0xff000000));
            utassert(RemoveAnnotPresetColor(cmd, chosen));
            utassert(!RemoveAnnotPresetColor(cmd, chosen));
            str::ReplaceWithCopy(list, original);
            str::Free(original);
        }
        utassert(IsPinnableTool(CmdToggleLaserPointer));
        win.laserPointerColor = chosen;
        TogglePinnedTool(&win, CmdToggleLaserPointer);
        utassert(PinnedToolIndex(&win, CmdToggleLaserPointer) >= 0);
        ILayout* pins = BuildPinnedTools(&win);
        utassert(len(tb.pinnedItems) == 1 && AsVirtIconButton(tb.pinnedItems[0])->pixmap);
        delete pins;
        VecReset(tb.pinnedItems);
        TogglePinnedTool(&win, CmdToggleLaserPointer);
        utassert(PinnedToolIndex(&win, CmdToggleLaserPointer) == -1);
        Color current = AnnotCurrentColor(&win, CmdCreateAnnotInk);
        utassert(RemoveAnnotPresetColor(CmdCreateAnnotInk, current));
        ToolbarHoverBuildEvent ev;
        ev.cmdId = CmdCreateAnnotInk;
        BuildAnnotColorsHoverMenu(&win, &ev);
        Vec<Color> colors;
        AnnotPresetColors(CmdCreateAnnotInk, colors);
        for (Color c : colors) utassert(!SameColorAndAlpha(c, current));
        delete ev.layout;
        VecReset(tb.hoverItems);
        utassert(AddAnnotPresetColor(CmdCreateAnnotInk, current));
        win.toolbarVirt = nullptr;
    }
    auto*& presets = gSettings->pinnedAnnotationTools;
    if (!presets) presets = new Vec<PinnedAnnotationTool*>();
    Str names[] = {StrL("ballpoint"), StrL("fountain"), StrL("brush"), StrL("pencil"), StrL("marker")};
    for (Str name : names) {
        auto* preset = AllocStruct<PinnedAnnotationTool>();
        preset->tool = str::Dup(name);
        preset->color = str::Dup(StrL("#287ae0"));
        preset->width = 2.f;
        VecAppend(*presets, preset);
    }
    SetToolbarItemHidden(CmdCreateAnnotInk, true);
    SetToolbarItemHidden(PageInfoId, true);
    utassert(ToolbarItemHidden(CmdCreateAnnotInk) && ToolbarItemHidden(PageInfoId));
    utassert(!ToolbarItemHidden(CmdInkFountain) && len(*presets) == 5);
    SetToolbarItemHidden(CmdCreateAnnotInk, true);
    Str data = SerializeSettings(gSettings, {});
    Settings* roundTrip = NewSettings(data);
    str::Free(data);
    utassert(str::Eq(roundTrip->toolbarHiddenItems, gSettings->toolbarHiddenItems));
    utassert(roundTrip->pinnedAnnotationTools && len(*roundTrip->pinnedAnnotationTools) == 5);
    DeleteSettings(roundTrip);
    SetToolbarItemHidden(CmdCreateAnnotInk, false);
    utassert(!ToolbarItemHidden(CmdCreateAnnotInk) && ToolbarItemHidden(PageInfoId));
    SetToolbarItemHidden(PageInfoId, false);
    utassert(len(gSettings->toolbarHiddenItems) == 0);

    {
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        ToolbarVirt tb;
        win.toolbarVirt = &tb;
        VirtHost::CreateArgs args;
        args.className = WStrL(L"SumatraPenClickTest");
        args.isPopup = true;
        args.visible = false;
        args.initialSize = {200, 50};
        tb.host = VirtHost::Create(args);
        auto* row = new HBox();
        for (int command : {CmdCreateAnnotInk, CmdZoomIn, CmdCreateAnnotLine}) {
            auto* button = new VirtButton(StrL("Tool"), GetAppFont());
            button->id = command;
            row->AddChild(button);
            VecAppend(tb.items, button);
            SetToolbarHoverDropdown(&win, command, MkFunc1(BuildAnnotColorsHoverMenu, &win));
        }
        tb.host->SetLayoutSizedToContent(row);
        tb.host->Relayout();
        auto point = [](VirtCtrl* ctrl) {
            Rect bounds = ctrl->BoundsInWindow();
            return Point{bounds.x + bounds.dx / 2, bounds.y + bounds.dy / 2};
        };
        Point pen = point(tb.items[0]), zoom = point(tb.items[1]);
        Point line = point(tb.items[2]);
        ToolbarHoverDropdownOnMouseMove(&win, &pen);
        utassert(tb.hoverPendingCmdId == 0 && tb.hoverHost == nullptr);
        ToolbarHoverDropdownOnMouseMove(&win, &zoom);
        utassert(tb.hoverPendingCmdId == CmdZoomIn);
        ToolbarHoverDropdownOnMouseMove(&win, &pen);
        utassert(tb.hoverPendingCmdId == 0);
        tb.hoverCmdId = CmdCreateAnnotInk;
        tb.hoverSticky = true;
        ToolbarHoverDropdownOnMouseMove(&win, &zoom);
        utassert(tb.hoverCmdId == CmdCreateAnnotInk && tb.hoverSticky);
        HideToolbarHoverDropdown(&win);
        tb.hoverPendingCmdId = CmdCreateAnnotLine;
        HideToolbarHoverDropdown(&win);
        utassert(tb.hoverPendingCmdId == CmdCreateAnnotLine && tb.hoverHost == nullptr);
        ToolbarHoverDropdownOnMouseMove(&win, &line);
        utassert(tb.hoverPendingCmdId == CmdCreateAnnotLine);
        ToolbarHoverDropdownOnMouseMove(&win, &zoom);
        utassert(tb.hoverPendingCmdId == CmdZoomIn);
        ToolbarHoverDropdownOnMouseMove(&win, &pen);
        utassert(tb.hoverPendingCmdId == 0);
        delete tb.host;
        win.toolbarVirt = nullptr;
    }

    for (int variant = 0; variant < 3; variant++) {
        gSettings->uIFontSize = variant == 2 ? 28 : 18;
        gSettings->interfaceScale = variant == 2 ? 150 : 100;
        str::ReplaceWithCopy(&gSettings->theme, variant == 0 ? StrL("Sumatra Light") : StrL("Modern Green Dark"));
        SetCurrentThemeFromSettings();
        RefreshUiFonts();
        MainWindow win(nullptr);
        win.tabsCtrl = new TabsCtrl();
        ToolbarVirt tb;
        win.toolbarVirt = &tb;
        tb.platformFont = GetAppFont();
        tb.iconSize = UiScalePx(28);
        win.laserPointerColor = MkRgb(244, 67, 54);
        for (int width : {180, 320, 560}) {
            ILayout* pens = BuildInkPenTypes(&win);
            Vec<VirtCtrl*> buttons;
            CollectPaletteControls(pens, buttons);
            utassert(len(buttons) == 5);
            for (bool rtl : {false, true}) {
                ((InkPenRow*)pens->LayoutChildAt(0))->rtl = rtl;
                Size size = pens->Layout(ExpandHeight(width));
                pens->SetBounds({0, 0, size.dx, size.dy});
                int top = buttons[0]->BoundsInWindow().y;
                int selected = 0;
                int clicks = 0;
                for (VirtCtrl* button : buttons) {
                    Rect rect = button->BoundsInWindow();
                    utassert(rect.y == top);
                    utassert(rect.dx > 0 && rect.x >= 0 && rect.Right() <= width);
                    utassert(len(button->tooltip) > 0 && button->HasFlag(vwfFocusable));
                    auto* tile = (InkPenTile*)button;
                    utassert(str::Eq(tile->s, tile->tooltip) && str::Eq(tile->name, tile->s));
                    utassert(tile->pixmap && tile->pixmap->width <= rect.dx);
                    utassert(tile->showLabel == ((InkPenTile*)buttons[0])->showLabel);
                    if (tile->isCurrent) selected++;
                    button->onClick = MkFunc1(PaletteTestClick, &clicks);
                    for (int key : {VK_RETURN, VK_SPACE}) {
                        VirtKeyEvent ev;
                        ev.target = button;
                        ev.vkey = key;
                        utassert(button->OnKeyDown(ev) && ev.didHandle);
                    }
                }
                utassert(selected == 1 && clicks == 10);
            }
            delete pens;
            VecReset(tb.hoverItems);
        }
        for (int command : {CmdCreateAnnotInk, CmdCreateAnnotUnderline}) {
            auto* exact = AllocStruct<PinnedAnnotationTool>();
            exact->tool = str::Dup(PinnedToolName(&win, command));
            exact->color = str::Dup(SerializeColorTemp(AnnotCurrentColor(&win, command)));
            exact->width = command == CmdCreateAnnotInk ? InkPenWidth(&win) : 0;
            VecAppend(*presets, exact);
            for (bool pinned : {true, false}) {
                ToolbarHoverBuildEvent ev;
                ev.cmdId = command;
                BuildAnnotColorsHoverMenu(&win, &ev);
                Vec<VirtCtrl*> buttons;
                CollectPaletteControls(ev.layout, buttons);
                PaletteIconButton* pin = nullptr;
                for (auto* button : buttons) {
                    auto* text = AsVirtButton(button);
                    if (text && (str::Eq(text->s, Tr("Pin this tool")) || str::Eq(text->s, Tr("Unpin this tool"))))
                        pin = (PaletteIconButton*)button;
                }
                utassert(pin != nullptr);
                utassert(pin->isCurrent == pinned);
                utassert(str::Eq(pin->s, pinned ? Tr("Unpin this tool") : Tr("Pin this tool")));
                utassert(str::Eq(pin->name, pin->s) && str::Eq(pin->tooltip, pin->s));
                utassert(pin->pixmap && pin->HasFlag(vwfFocusable));
                Size fit = ev.layout->Layout(ExpandHeight(UiScalePx(400)));
                ev.layout->SetBounds({0, 0, fit.dx, fit.dy});
                Rect pinBounds = pin->BoundsInWindow();
                bool inlineLabel = false;
                for (auto* button : buttons) {
                    auto* text = AsVirtText(button);
                    if (!text || !str::Eq(text->s, Tr("Color"))) continue;
                    Rect label = text->BoundsInWindow();
                    inlineLabel =
                        label.y <= pinBounds.y + pinBounds.dy / 2 && label.Bottom() >= pinBounds.y + pinBounds.dy / 2;
                }
                utassert(inlineLabel);
                if (command == CmdCreateAnnotInk) {
                    bool foundSettings = false;
                    for (auto* button : buttons) {
                        auto* text = AsVirtButton(button);
                        if (!text || !str::Eq(text->s, Tr("Additional tool settings"))) continue;
                        foundSettings = true;
                        auto* extra = (ILayout*)button->userData;
                        utassert(extra && IsCollapsed(extra));
                        extra->SetVisibility(Visibility::Visible);
                        utassert(extra->Layout(ExpandHeight(UiScalePx(400))).dy > 0);
                    }
                    utassert(foundSettings);
                }
                delete ev.layout;
                VecReset(tb.hoverItems);
                if (pinned) VecPop(*presets);
            }
            str::Free(exact->tool);
            str::Free(exact->color);
            free(exact);
        }
        ILayout* pinned = BuildPinnedTools(&win);
        utassert(len(tb.pinnedItems) == 5);
        for (int i = 0; i < 5; i++) {
            auto* icon = AsVirtIconButton(tb.pinnedItems[i]);
            auto* expected = GetCachedPixmapForSvg(InkPenSvg(i, ParseColor((*presets)[i]->color)), tb.iconSize,
                                                   tb.iconSize, TbTextColor(), TbBgColor());
            utassert(icon && icon->pixmap && icon->pixmap == expected);
            if (i) utassert(icon->pixmap != AsVirtIconButton(tb.pinnedItems[i - 1])->pixmap);
        }
        delete pinned;
        VecReset(tb.pinnedItems);
        {
            auto* extra = AllocStruct<PinnedAnnotationTool>();
            extra->tool = str::Dup(GetCommandName(CmdCreateAnnotFreeText));
            VecAppend(*presets, extra);
            MainWindow groupWin(nullptr);
            groupWin.tabsCtrl = new TabsCtrl();
            ToolbarVirt groupTb;
            groupWin.toolbarVirt = &groupTb;
            groupTb.platformFont = tb.platformFont;
            groupTb.iconSize = tb.iconSize;
            VirtHost::CreateArgs args;
            args.className = WStrL(L"SumatraPinnedGroupTest");
            args.isPopup = true;
            args.visible = false;
            args.initialSize = {2000, 100};
            groupTb.host = VirtHost::Create(args);
            groupWin.hwndToolbar = groupTb.host->native;
            BuildToolbarLayout(&groupWin);
            auto contains = [&](auto&& self, ILayout* layout, VirtCtrl* control) -> bool {
                if (layout == control) return true;
                for (int i = 0; i < layout->LayoutChildCount(); i++) {
                    if (self(self, layout->LayoutChildAt(i), control)) return true;
                }
                return false;
            };
            ILayout* annotationGroup = nullptr;
            VirtCtrl* editTool = nullptr;
            for (VirtCtrl* control : groupTb.items) {
                if (control->id == CmdToggleEditPDF) editTool = control;
            }
            for (auto& item : groupTb.mainRow->children) {
                if (contains(contains, item.layout, editTool)) annotationGroup = item.layout;
            }
            utassert(annotationGroup != nullptr && len(groupTb.pinnedItems) == 6);
            for (VirtCtrl* control : groupTb.pinnedItems) {
                utassert(annotationGroup && contains(contains, annotationGroup, control));
            }
            utassert(groupTb.annotationLine == nullptr && groupTb.annotationRow == nullptr);
            utassert(len(groupTb.annotationGroups) > 0);
            int editGroupIndex = -1;
            int expandedGroupIndex = -1;
            for (int i = 0; i < len(groupTb.mainRow->children); i++) {
                if (groupTb.mainRow->children[i].layout == annotationGroup) editGroupIndex = i;
                if (groupTb.mainRow->children[i].layout == groupTb.annotationGroups[0]) expandedGroupIndex = i;
            }
            utassert(expandedGroupIndex == editGroupIndex + 1);
            for (VirtCtrl* control : groupTb.items) {
                if (!control || !control->id) continue;
                for (VirtCtrl* expanded : groupTb.annotationItems) {
                    utassert(!expanded || expanded->id != control->id);
                }
            }
            int collapsedHeight = groupTb.host->layout->MinIntrinsicHeight(2000);
            groupTb.mainRow->firstGroup = editGroupIndex;
            SetPdfAnnotationsToolbarVisible(&groupWin, true);
            utassert(groupTb.mainRow->firstGroup <= editGroupIndex);
            groupTb.mainRow->Restore();
            for (auto* group : groupTb.annotationGroups) {
                group->SetVisibility(Visibility::Visible);
                bool onMainRow = false;
                for (auto& child : groupTb.mainRow->children) onMainRow |= child.layout == group;
                utassert(onMainRow);
            }
            groupTb.annotationExpanded = true;
            groupTb.host->Relayout();
            utassert(groupTb.host->layout->MinIntrinsicHeight(2000) == collapsedHeight);
            int first = groupTb.mainRow->firstGroup;
            while (groupTb.mainRow->Scroll(1)) {
                utassert(groupTb.mainRow->firstGroup > first);
                first = groupTb.mainRow->firstGroup;
            }
            utassert(!groupTb.mainRow->canScrollAfter);
            while (groupTb.mainRow->Scroll(-1)) {
            }
            utassert(!groupTb.mainRow->canScrollBefore);
            CaptureToolbarPalette(groupTb.host, fmt("toolbar-pins-%d.bmp", variant));
            delete groupTb.host;
            groupTb.host = nullptr;
            groupWin.hwndToolbar = nullptr;
            groupWin.pageEdit = nullptr;
            groupWin.chapterEdit = nullptr;
            groupWin.toolbarVirt = nullptr;
            VecPop(*presets);
            str::Free(extra->tool);
            free(extra);
        }
        for (int command : {CmdCreateAnnotInk, CmdToggleLaserPointer}) {
            ToolbarHoverBuildEvent ev;
            ev.win = &win;
            ev.cmdId = command;
            if (command == CmdCreateAnnotInk)
                BuildAnnotColorsHoverMenu(&win, &ev);
            else
                BuildLaserHoverMenu(&win, &ev);
            utassert(ev.layout != nullptr);
            int availableWidth = variant == 0 ? 3840 : 2560;
            auto* scroll = new ToolbarPaletteScroll(ev.layout, {availableWidth, 240}, tb.platformFont);
            scroll->lineDy = PlatformFontLineHeight(tb.platformFont);
            VirtHost::CreateArgs args;
            args.className = WStrL(L"SumatraToolbarHoverMenu");
            args.title = command == CmdCreateAnnotInk ? Tr("Pen types") : Tr("Laser pointer");
            args.isPopup = true;
            args.visible = false;
            args.initialSize = {100, 100};
            auto* host = VirtHost::Create(args);
            utassert(host != nullptr);
            tb.hoverHost = host;
            WindowApplyScaledCaption(host->native);
            Size work{availableWidth, 240};
            Size viewport = ToolbarPaletteViewport(host, work);
            scroll->limit = {ToolbarPaletteWidth(tb.platformFont, viewport.dx), viewport.dy};
            host->onNativeMsg = MkFunc1(PaletteNativeMsg, (ScrollBox*)scroll);
            Size size = host->SetLayoutSizedToContent(scroll);
            utassert(size.dx <= work.dx && size.dy <= work.dy);
            // The first layout installs the native scrollbar, so measure the
            // frame after its visibility has settled.
            Rect window = host->ScreenRect();
            Rect client = host->ClientRect();
            Size frame{window.dx - client.dx, window.dy - client.dy};
            utassert(size.dx - frame.dx <= UiScalePx(560) && size.dy - frame.dy <= 240);
            utassert(size.dx - frame.dx <= std::max(UiScalePx(320), 8 * PlatformFontLineHeight(tb.platformFont)));
            host->SetBounds({0, 0, size.dx, size.dy});
            host->Relayout();
            Vec<VirtCtrl*> controls;
            CollectPaletteControls(ev.layout, controls);
            VirtCtrl* last = nullptr;
            for (VirtCtrl* ctrl : controls) {
                if (ctrl->onClick.IsValid() || AsVirtSlider(ctrl)) {
                    ctrl->SetFlag(vwfFocusable, true);
                    last = ctrl;
                }
                Rect bounds = ctrl->BoundsInWindow();
                utassert(bounds.x >= 0 && bounds.Right() <= host->ClientRect().Right() + 1);
            }
            utassert(last != nullptr);
            utassert(scroll->MaxScrollY() > 0);
            CaptureToolbarPalette(
                host, fmt("%s-%d-top.bmp", command == CmdCreateAnnotInk ? StrL("pen") : StrL("laser"), variant));
            SendMessageW(host->native, WM_VSCROLL, SB_BOTTOM, 0);
            utassert(scroll->scrollY == scroll->MaxScrollY());
            if (command == CmdCreateAnnotInk) {
                // The pin is inline above the colors; the bottom of the panel
                // is now the thickness footer. Focus below still verifies the
                // slider can be brought fully into view on a short monitor.
                VirtText* footer = nullptr;
                for (VirtCtrl* ctrl : controls) {
                    auto* text = AsVirtText(ctrl);
                    if (text && str::Eq(text->s, Tr("Thick"))) footer = text;
                }
                utassert(footer != nullptr);
                if (footer) {
                    Rect bounds = footer->BoundsInWindow();
                    utassert(bounds.y >= 0 && bounds.Bottom() <= host->ClientRect().Bottom());
                }
            }
            CaptureToolbarPalette(
                host, fmt("%s-%d-bottom.bmp", command == CmdCreateAnnotInk ? StrL("pen") : StrL("laser"), variant));
            SendMessageW(host->native, WM_KEYDOWN, VK_PRIOR, 0);
            utassert(scroll->scrollY < scroll->MaxScrollY());
            int previousScroll = scroll->scrollY;
            SendMessageW(host->native, WM_KEYDOWN, VK_NEXT, 0);
            utassert(scroll->scrollY > previousScroll);
            scroll->ScrollTo(0);
            SendMessageW(host->native, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            utassert(scroll->scrollY > 0);
            scroll->ScrollTo(0);
            {
                // These messages represent plain Tab. Keep real modifier keys
                // out of this hidden fixture, and restore only this thread's
                // key state afterward; no physical key or cursor is injected.
                BYTE savedKeys[256]{};
                bool haveKeys = GetKeyboardState(savedKeys) != FALSE;
                ReleaseThreadKeyState();
                for (int i = 0; i < len(controls) + 1 && host->vroot->focused != last; i++)
                    SendMessageW(host->native, WM_KEYDOWN, VK_TAB, 0);
                if (haveKeys) SetKeyboardState(savedKeys);
            }
            utassert(host->vroot->focused == last);
            Rect focused = last->BoundsInWindow();
            utassert(focused.y >= 0 && focused.Bottom() <= host->ClientRect().Bottom());
            HRGN clip = CreateRectRgn(0, 0, 0, 0);
            utassert(GetWindowRgn(host->native, clip) != ERROR);
            utassert(!PtInRegion(clip, 0, 0) && PtInRegion(clip, size.dx / 2, size.dy / 2));
            int diameter = std::min(UiCornerDiameter(DpiGetForHwnd(host->native), 6), std::min(size.dx, size.dy));
            HRGN painted = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
            utassert(EqualRgn(clip, painted));
            DeleteObject(painted);
            host->SetBounds({0, 0, size.dx - 20, size.dy - 20});
            utassert(GetWindowRgn(host->native, clip) != ERROR);
            RECT box{};
            GetRgnBox(clip, &box);
            utassert(box.right <= size.dx - 19 && box.bottom <= size.dy - 19);
            DeleteObject(clip);
            {
                scroll->limit.dy = 1600;
                scroll->ScrollTo(0);
                size = host->SetLayoutSizedToContent(scroll);
                host->SetBounds({0, 0, size.dx, size.dy});
                utassert(scroll->BoundsInWindow().dx == host->ClientRect().dx);
                utassert(host->vroot->bounds.dx == host->ClientRect().dx);
                CaptureToolbarPalette(
                    host, fmt("%s-%d-full.bmp", command == CmdCreateAnnotInk ? StrL("pen") : StrL("laser"), variant));
            }
            delete host;
            tb.hoverHost = nullptr;
            VecReset(tb.hoverItems);
        }
        {
            // A right click opens palette actions without selecting a color
            // or dismissing an annotation-properties popup on capture change.
            auto* popup = new AnnotColorPopup();
            popup->win = &win;
            VirtHost::CreateArgs args;
            args.className = WStrL(L"SumatraAnnotColorPopup");
            args.visible = false;
            args.isPopup = true;
            args.initialSize = {400, 200};
            popup->host = VirtHost::Create(args);
            utassert(popup->host != nullptr);
            if (popup->host) {
                gAnnotColorPopup = popup;
                utassert(BeginAnnotColorPopupMenu(&win, popup->host->native) == win.hwndFrame);
                utassert(popup->menuActive);
                VirtHostNativeMsg changed;
                changed.host = popup->host;
                changed.msg = WM_CAPTURECHANGED;
                changed.lp = 1;
                AnnotColorPopupNativeMsg(popup, &changed);
                utassert(gAnnotColorPopup == popup);
                ToolbarColorSwatch swatch;
                swatch.col = MkRgb(20, 40, 60);
                VirtMouseEvent click;
                click.target = &swatch;
                click.button = 1;
                OnAnnotColorPopupSwatch(popup, &click);
                utassert(!popup->hasPick && gAnnotColorPopup == popup);
                popup->menuActive = false;
                gAnnotColorPopup = nullptr;
                delete popup->host;
            }
            delete popup;
        }
        win.toolbarVirt = nullptr;
    }
}
#endif
