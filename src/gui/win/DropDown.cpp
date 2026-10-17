/* Copyright 2024 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/Win.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "gui/Dpi.h"

#include "gui/UIModels.h"

#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/win/WinGui.h"
#include "gui/VirtHost.h"

//- DropDown

// https://docs.microsoft.com/en-us/windows/win32/controls/combo-boxes

static Kind kindDropDown = "dropdown";

// Native combo boxes redraw their edit and list after every inserted item.
// Populate them in one transaction, retaining a deliberately hidden control.
struct DropDownUpdate {
    HWND hwnd;
    bool visible;
    explicit DropDownUpdate(HWND window)
        : hwnd(window), visible((GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0) {
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    }
    ~DropDownUpdate() {
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        if (!visible) ShowWindow(hwnd, SW_HIDE);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }
};

DropDown::DropDown() {
    kind = kindDropDown;
}

DropDown::~DropDown() {
    str::Free(pendingText);
}

static void SetDropDownItems(HWND hwnd, StrVec& items) {
    CbResetContent(hwnd);
    int n = len(items);
    for (int i = 0; i < n; i++) {
        CbAddString(hwnd, items[i]);
    }
}

// Same result as SetDropDownItems() but without CB_RESETCONTENT, which on an
// editable combo also empties the edit control. Deleting an item the list has
// selected empties it too, so let go of the selection first - that way exactly
// one step here can disturb the edit, and the caller undoes it.
static void ReplaceDropDownItems(HWND hwnd, StrVec& items) {
    CbSetCurrentSelection(hwnd, -1);
    for (int i = CbGetItemsCount(hwnd) - 1; i >= 0; i--) {
        CbDeleteString(hwnd, i);
    }
    int n = len(items);
    for (int i = 0; i < n; i++) {
        CbInsertString(hwnd, i, items[i]);
    }
}

// On WM_SIZE a combo box re-sets its edit's text (to the same string) and
// leaves all of it selected. Any relayout around us - the find bar's "n / m"
// slot widening as the count comes in - would then wipe out what the user is
// typing, so put the caret back where it was (issue #6068).
static void PrepareDropDownList(DropDown* drop) {
    drop->EnsureItems();
    COMBOBOXINFO info{sizeof(info)};
    // Prepare the scrollbar before native popup activation. Creating its windows
    // during CB_SHOWDROPDOWN's show notifications can dismiss the native list.
    if (gUiInstallScrollbar && GetComboBoxInfo(drop->hwnd, &info)) gUiInstallScrollbar(info.hwndList);
}

void DropDown::EnablePreviewItems() {
    ReportIf(hwnd);
    previewItems = true;
    pendingCreate.previewItems = true;
}

Rect DropDown::PreviewBounds(Rect row) const {
    int pad = DpiScale(3);
    int size = std::max(1, std::min(row.dy - 2 * pad, std::max(DpiScale(20), PlatformFontLineHeight(font))));
    bool rtl = hwnd ? HwndIsRtl(hwnd) : pendingCreate.isRtl;
    return {rtl ? row.Right() - pad - size : row.x + pad, row.y + (row.dy - size) / 2, size, size};
}

Rect DropDown::PreviewTextBounds(Rect row) const {
    if (!previewItems) return row;
    Rect mark = PreviewBounds(row);
    bool rtl = hwnd ? HwndIsRtl(hwnd) : pendingCreate.isRtl;
    int inset = mark.dx + DpiScale(9);
    if (!rtl) row.x += inset;
    row.dx = std::max(0, row.dx - inset);
    return row;
}

void DropDown::PaintPreview(Gfx* gfx, Rect row, int index, Str text, Color foreground, Color background) {
    if (!previewItems || !onPreviewItem.IsValid() || row.IsEmpty()) return;
    PreviewItemEvent ev{this, gfx, PreviewBounds(row), index, text, foreground, background, !IsEnabled()};
    onPreviewItem.Call(&ev);
}

static void SetPreviewMargins(DropDown* drop) {
    HWND edit = CbEditHwnd(drop->hwnd);
    EditSetDefaultMargins(edit);
    if (!edit || !drop->previewItems) return;
    DWORD margins = (DWORD)SendMessageW(edit, EM_GETMARGINS, 0, 0);
    Rect row = HwndClientRect(edit);
    int inset = drop->PreviewBounds(row).dx + DpiScale(9);
    bool rtl = HwndIsRtl(drop->hwnd);
    EditSetMargins(edit, rtl ? LOWORD(margins) : inset, rtl ? inset : HIWORD(margins));
}

static void PaintEditPreview(DropDown* drop, HWND edit, HDC dc) {
    if (!drop->previewItems || !dc) return;
    GfxHdc gfx(dc);
    Color bg = IsSpecialColor(drop->bgColor) ? GetSysColor(COLOR_WINDOW) : drop->bgColor;
    Color fg = IsSpecialColor(drop->textColor) ? GetSysColor(COLOR_WINDOWTEXT) : drop->textColor;
    Rect row = HwndClientRect(edit);
    gfx.FillRect(drop->PreviewBounds(row), bg);
    drop->PaintPreview(&gfx, row, CbGetCurrentSelection(drop), drop->GetTextTemp(), fg, bg);
}

static LRESULT CALLBACK DropDownEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* drop = (DropDown*)data;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, DropDownEditProc, id);
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_PAINT) {
        HDC dc = GetDC(hwnd);
        PaintEditPreview(drop, hwnd, dc);
        if (dc) ReleaseDC(hwnd, dc);
    } else if (msg == WM_PRINTCLIENT) {
        PaintEditPreview(drop, hwnd, (HDC)wp);
    }
    return result;
}

static LRESULT CALLBACK DropDownProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* drop = (DropDown*)data;
    if (drop && ((msg == CB_SHOWDROPDOWN && wp) || msg == WM_LBUTTONDOWN || msg == WM_MOUSEWHEEL ||
                 (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END || wp == VK_F4))))
        PrepareDropDownList(drop);
    if (drop && drop->sectioned && msg == WM_KEYDOWN) {
        int selected = CbGetCurrentSelection(drop);
        if (selected >= 0 && !VecContains(drop->sectionHeaders, selected)) drop->lastFontSelection = selected;
    }
    if (msg == WM_SETFONT) {
        LRESULT res = DefSubclassProc(hwnd, msg, wp, lp);
        SetPreviewMargins(drop);
        if (drop->previewItems) {
            int height = PlatformFontLineHeight(drop->font) + DpiScale(6);
            CbSetItemHeight(hwnd, 0, height);
            CbSetItemHeight(hwnd, -1, height);
        }
        return res;
    }
    if (msg != WM_SIZE) {
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    int start = 0, end = 0;
    CbEditGetSelection(hwnd, start, end);
    LRESULT res = DefSubclassProc(hwnd, msg, wp, lp);
    SetPreviewMargins(drop);
    CbEditSelectText(hwnd, start, end);
    return res;
}

void DropDown::OnCommand(ControlBase::CommandEvent* ev) {
    if (suppressNotify) {
        return;
    }
    auto code = HIWORD(ev->wparam);
    if (code == CBN_DROPDOWN) PrepareDropDownList(this);
    if (code == CBN_EDITCHANGE && itemsPending) pendingSelection = -1;
    if (previewItems && (code == CBN_EDITCHANGE || code == CBN_SELCHANGE)) {
        if (HWND edit = CbEditHwnd(hwnd)) InvalidateRect(edit, nullptr, FALSE);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (code == CBN_EDITCHANGE && onTextChanged.IsValid()) {
        onTextChanged.Call();
        ev->didHandle = true;
        return;
    }
    if (code == CBN_SELCHANGE && sectioned) {
        int selected = CbGetCurrentSelection(this);
        int direction = selected < lastFontSelection ? -1 : 1;
        while (selected >= 0 && selected < len(items) && VecContains(sectionHeaders, selected)) selected += direction;
        if (selected < 0 || selected >= len(items)) selected = lastFontSelection;
        CbSetCurrentSelection(this, selected);
        lastFontSelection = selected;
    }
    if ((code == CBN_SELCHANGE) && onSelectionChanged.IsValid()) {
        onSelectionChanged.Call();
        // must leave didHandle false or else the drop-down list will not close
    }
    if (code == CBN_CLOSEUP && onCloseUp.IsValid()) {
        onCloseUp.Call();
    }
}

static int ColorSwatchItemDy(PlatformFont* font) {
    return PlatformFontMeasureText(font, StrL("Mg")).dy + DpiScale(6);
}

static void PaintCheckerSwatch(Gfx* gfx, Rect r) {
    gfx->FillRect(r, MkRgb(240, 240, 240));
    int s = std::max(r.dx / 4, 2);
    Color dark = MkRgb(180, 180, 180);
    for (int y = 0; y < r.dy; y += s) {
        for (int x = 0; x < r.dx; x += s) {
            if (((x / s) + (y / s)) & 1) {
                int dx = std::min(s, r.dx - x);
                int dy = std::min(s, r.dy - y);
                gfx->FillRect({r.x + x, r.y + y, dx, dy}, dark);
            }
        }
    }
}

void DrawDropDownSwatch(Gfx* gfx, Rect r, Color color, u8 alpha) {
    if (r.IsEmpty()) return;
    PaintCheckerSwatch(gfx, r);
    if (!ColorSkipsPaint(color) && alpha) {
        Color rgb = MkRgb(GetRed(color), GetGreen(color), GetBlue(color));
        if (alpha == 255)
            gfx->FillRect(r, rgb);
        else
            gfx->FillRects(&r, 1, rgb, alpha);
    }
    gfx->DrawRect(r, MkRgb(110, 110, 110), 1);
}

static void DrawPreviewItem(DropDown* drop, DRAWITEMSTRUCT* item) {
    GfxHdc gfx(item->hDC);
    Rect row = ToRect(item->rcItem);
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    Color bg = selected                        ? GetSysColor(COLOR_HIGHLIGHT)
               : IsSpecialColor(drop->bgColor) ? GetSysColor(COLOR_WINDOW)
                                               : drop->bgColor;
    Color fg = selected                          ? GetSysColor(COLOR_HIGHLIGHTTEXT)
               : IsSpecialColor(drop->textColor) ? GetSysColor(COLOR_WINDOWTEXT)
                                                 : drop->textColor;
    if (item->itemState & ODS_DISABLED) fg = GetSysColor(COLOR_GRAYTEXT);
    gfx.FillRect(row, bg);
    int index = item->itemID == (UINT)-1 ? -1 : (int)item->itemID;
    if (drop->itemsPending && index >= 0) index = drop->pendingSelection;
    Str text = index >= 0 && index < len(drop->items) ? drop->items[index] : drop->GetTextTemp();
    drop->PaintPreview(&gfx, row, index, text, fg, bg);
    Rect label = drop->PreviewTextBounds(row);
    label.dx = std::max(0, label.dx - DpiScale(3));
    bool rtl = HwndIsRtl(drop->hwnd);
    gfx.DrawText(text, label,
                 gfxTextVCenter | gfxTextSingleLine | gfxTextEllipsis | (rtl ? gfxTextRight | gfxTextRtl : gfxTextLeft),
                 drop->font, fg);
    if (item->itemState & ODS_FOCUS) gfx.DrawFocusRect(row);
}

static void DrawFontSectionItem(DropDown* w, DRAWITEMSTRUCT* item) {
    if (item->itemID == (UINT)-1 || item->itemID >= (UINT)len(w->items)) return;
    bool heading = VecContains(w->sectionHeaders, (int)item->itemID);
    bool selected = !heading && (item->itemState & ODS_SELECTED);
    COLORREF background = selected ? IsSpecialColor(gColsListBox[kColListSelFocused]) ? GetSysColor(COLOR_HIGHLIGHT)
                                                                                      : gColsListBox[kColListSelFocused]
                                   : (IsSpecialColor(w->bgColor) ? gColsListBox[kColListBg] : w->bgColor);
    COLORREF foreground = selected ? gColsListBox[kColListText]
                                   : (IsSpecialColor(w->textColor) ? gColsListBox[kColListText] : w->textColor);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(item->hDC, &item->rcItem, brush);
    DeleteObject(brush);
    RECT rect = item->rcItem;
    if (heading) {
        HPEN pen = CreatePen(PS_SOLID, 1, gColsBtn[kColBtnTextDisabled]);
        HGDIOBJ previous = SelectObject(item->hDC, pen);
        MoveToEx(item->hDC, rect.left + DpiScale(6), rect.top, nullptr);
        LineTo(item->hDC, rect.right - DpiScale(6), rect.top);
        SelectObject(item->hDC, previous);
        DeleteObject(pen);
        foreground = gColsBtn[kColBtnTextDisabled];
    }
    rect.left += DpiScale(heading ? 6 : 14);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, foreground);
    auto* font = heading ? GetBoldPlatformFont(w->font) : w->font;
    HGDIOBJ previous = font ? SelectObject(item->hDC, font->GetHFont()) : nullptr;
    DrawTextW(item->hDC, CWStrTemp(w->items[(int)item->itemID]), -1, &rect,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (previous) SelectObject(item->hDC, previous);
}

static void DrawColorSwatchItem(DropDown* w, DRAWITEMSTRUCT* dis) {
    if (dis->itemID == (UINT)-1) {
        return;
    }
    // Combo owner-draw is a native list DC; GfxHdc paints immediately and
    // does not need Direct2D BindDC (which fails on some 24-bit combo DCs).
    GfxHdc gfx(dis->hDC);
    Rect rc = ToRect(dis->rcItem);
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    Color bg =
        selected ? GetSysColor(COLOR_HIGHLIGHT) : (IsSpecialColor(w->bgColor) ? GetSysColor(COLOR_WINDOW) : w->bgColor);
    Color txt = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                         : (IsSpecialColor(w->textColor) ? GetSysColor(COLOR_WINDOWTEXT) : w->textColor);
    gfx.FillRect(rc, bg);

    int pad = DpiScale(3);
    int sw = std::max(rc.dy - (2 * pad), 8);
    Rect swatch{rc.x + pad, rc.y + ((rc.dy - sw) / 2), sw, sw};
    Color col = kColorTransparent;
    if ((int)dis->itemID < len(w->itemColors)) {
        col = w->itemColors[(int)dis->itemID];
    }
    if (col == kColorTransparent || ColorSkipsPaint(col)) {
        PaintCheckerSwatch(&gfx, swatch);
    } else {
        gfx.FillRect(swatch, col);
    }
    gfx.DrawRect(swatch, MkRgb(80, 80, 80), 1);

    Str name;
    if ((int)dis->itemID < len(w->items)) {
        name = w->items[(int)dis->itemID];
    }
    if (name && w->font) {
        int gap = DpiScale(6);
        Rect textRc{swatch.x + swatch.dx + gap, rc.y, rc.Right() - (swatch.x + swatch.dx + gap + pad), rc.dy};
        gfx.DrawText(name, textRc, gfxTextVCenter | gfxTextSingleLine | gfxTextEllipsis, w->font, txt);
    }
    if (dis->itemState & ODS_FOCUS) {
        gfx.DrawFocusRect(rc);
    }
}

void DropDown::OnMessageReflect(ControlBase::MessageReflectEvent* ev) {
    if ((colorSwatches || sectioned || previewItems) && ev->msg == WM_MEASUREITEM) {
        auto* mis = (MEASUREITEMSTRUCT*)ev->lparam;
        if (mis->CtlType == ODT_COMBOBOX || mis->CtlType == ODT_LISTBOX) {
            mis->itemHeight = (UINT)ColorSwatchItemDy(font);
            ev->result = TRUE;
        }
        return;
    }
    if ((colorSwatches || sectioned || previewItems) && ev->msg == WM_DRAWITEM) {
        auto* dis = (DRAWITEMSTRUCT*)ev->lparam;
        if (dis->CtlType == ODT_COMBOBOX || dis->CtlType == ODT_LISTBOX) {
            if (previewItems)
                DrawPreviewItem(this, dis);
            else if (sectioned)
                DrawFontSectionItem(this, dis);
            else
                DrawColorSwatchItem(this, dis);
            ev->result = TRUE;
        }
        return;
    }
    if (ev->msg == WM_CTLCOLOREDIT || ev->msg == WM_CTLCOLORSTATIC || ev->msg == WM_CTLCOLORLISTBOX) {
        HDC hdc = (HDC)ev->wparam;
        if (!IsSpecialColor(textColor)) {
            SetTextColor(hdc, textColor);
        }
        if (!IsSpecialColor(bgColor)) {
            SetBkColor(hdc, bgColor);
            SetBkMode(hdc, OPAQUE);
        }
        ev->result = (LRESULT)BackgroundBrush();
    }
}

HWND DropDown::Create(const CreateArgs& args) {
    bool deferred = windowPending;
    windowPending = false;
    onCommand = MkMethod1<DropDown, ControlBase::CommandEvent*, &DropDown::OnCommand>(this);
    onMessageReflect = MkMethod1<DropDown, ControlBase::MessageReflectEvent*, &DropDown::OnMessageReflect>(this);
    colorSwatches = args.colorSwatches;
    previewItems = args.previewItems;
    sectioned = args.sectioned;
    deferItems = args.deferItems;
    CreateControlArgs cargs;
    cargs.parent = args.parent;
    cargs.isRtl = args.isRtl;
    cargs.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    if (args.isEditable) {
        cargs.style |= CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL;
    } else {
        cargs.style |= CBS_DROPDOWNLIST | WS_VSCROLL;
    }
    if (colorSwatches || sectioned || previewItems) {
        cargs.style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        static UINT nextId = 0x500;
        cargs.ctrlId = (HMENU)(INT_PTR)nextId++;
    }
    cargs.className = WC_COMBOBOX;
    cargs.font = args.font;
    cargs.visible = args.visible;
    cargs.pos = args.pos;

    ControlBase::CreateControl(cargs);
    if (!hwnd) {
        return nullptr;
    }

    // SetDropDownItems(hwnd, items);
    SetPreviewMargins(this);
    if (previewItems) {
        if (HWND edit = CbEditHwnd(hwnd))
            SetWindowSubclass(edit, DropDownEditProc, (UINT_PTR)DropDownEditProc, (DWORD_PTR)this);
    }
    SetWindowSubclass(hwnd, DropDownProc, 0, (DWORD_PTR)this);
    CbSetCurrentSelection(hwnd, -1);
    CbSetMinVisible(hwnd, 10);
    if (colorSwatches || sectioned || previewItems) {
        int dy = ColorSwatchItemDy(font);
        CbSetItemHeight(hwnd, 0, dy);
        CbSetItemHeight(hwnd, -1, dy);
    }

    if (!deferred && args.pos.IsEmpty()) SizeToIdealSize(this);
    return hwnd;
}

// Keep the complete value model without creating an offscreen native combo.
// Create() retains its HWND-returning contract for existing callers.
void DropDown::DeferCreate(const CreateArgs& args) {
    ReportIf(hwnd || windowPending);
    pendingCreate = args;
    font = args.font ? args.font : GetDefaultGuiFont();
    colorSwatches = args.colorSwatches;
    previewItems = args.previewItems;
    sectioned = args.sectioned;
    deferItems = args.deferItems;
    windowPending = true;
}

HWND DropDown::EnsureCreated() {
    if (!windowPending) return hwnd;
    int selection = pendingSelection;
    pendingCreate.font = font;
    if (!Create(pendingCreate)) {
        windowPending = true;
        return nullptr;
    }
    HwndBase::SetIsEnabled(pendingEnabled);
    if (selection >= 0)
        CbSetCurrentSelection(this, selection);
    else
        HwndBase::SetText(pendingText);
    if (!deferItems) EnsureItems();
    str::Free(pendingText);
    pendingText = {};
    return hwnd;
}

bool DropDown::IsFocusable() const {
    if (windowPending) return IsVisible() && pendingEnabled && ::IsWindowVisible(pendingCreate.parent);
    return ControlBase::IsFocusable();
}

void DropDown::PrepareFocus() {
    EnsureCreated();
}

// Editable combos keep the keyboard focus on the inner Edit. Focusing the
// ComboBox HWND itself uses the main accelerator table, so a leftover F from
// Ctrl+F can fire CmdToggleFullscreen and take focus away.
void DropDown::SetFocus() {
    PrepareFocus();
    HWND edit = CbEditHwnd(hwnd);
    HwndSetFocus(edit ? edit : hwnd);
}

bool DropDown::IsFocused() const {
    if (!hwnd) {
        return false;
    }
    if (HwndIsFocused(hwnd)) {
        return true;
    }
    HWND edit = CbEditHwnd(hwnd);
    return edit && HwndIsFocused(edit);
}

void DropDown::SetItems(StrVec& newItems) {
    items.Reset();
    VecReset(itemColors);
    int n = len(newItems);
    for (int i = 0; i < n; i++) {
        Str s = newItems[i];
        items.Append(s);
    }
    itemsPending = deferItems || windowPending;
    pendingSelection = -1;
    if (!hwnd) {
        str::Free(pendingText);
        pendingText = {};
        return;
    }
    DropDownUpdate update(hwnd);
    if (itemsPending)
        CbResetContent(hwnd);
    else
        SetDropDownItems(hwnd, items);
    CbSetCurrentSelection(hwnd, -1);
}

void DropDown::SetText(Str text) {
    pendingSelection = -1;
    if (windowPending) {
        str::ReplaceWithCopy(&pendingText, text);
        return;
    }
    HwndBase::SetText(text);
    if (previewItems && hwnd) {
        InvalidateRect(hwnd, nullptr, FALSE);
        if (HWND edit = CbEditHwnd(hwnd)) InvalidateRect(edit, nullptr, FALSE);
    }
}

TempStr DropDown::GetTextTemp() {
    return windowPending ? str::DupTemp(pendingText) : HwndBase::GetTextTemp();
}

void DropDown::SetIsEnabled(bool enabled) const {
    pendingEnabled = enabled;
    if (hwnd) HwndBase::SetIsEnabled(enabled);
}

bool DropDown::IsEnabled() const {
    return windowPending ? pendingEnabled : HwndBase::IsEnabled();
}

void DropDown::EnsureItems() {
    if (windowPending && !EnsureCreated()) return;
    if (!itemsPending) return;
    DropDownUpdate update(hwnd);
    Str text = str::Dup(GetTextTemp());
    int start = 0, end = 0;
    CbEditGetSelection(hwnd, start, end);
    bool previous = suppressNotify;
    suppressNotify = true;
    int selection = pendingSelection;
    itemsPending = false;
    SetDropDownItems(hwnd, items);
    if (selection >= 0)
        CbSetCurrentSelection(hwnd, selection);
    else
        HwndBase::SetText(text);
    CbEditSelectText(hwnd, start, end);
    str::Free(text);
    suppressNotify = previous;
}

// Rebuild the list under a user who is still typing, e.g. the find history
// growing while find-as-you-type runs. Do not go through SetItems: both the
// CB_RESETCONTENT it starts with and the CbSetCurrentSelection(-1) it ends
// with empty a CBS_DROPDOWN edit.
void DropDown::SetItemsKeepText(StrVec& newItems) {
    EnsureItems();
    DropDownUpdate update(hwnd);
    Str cur = str::Dup(GetTextTemp());
    int selStart = 0, selEnd = 0;
    CbEditGetSelection(hwnd, selStart, selEnd);
    bool prev = suppressNotify;
    suppressNotify = true;

    items.Reset();
    VecReset(itemColors);
    int nItems = len(newItems);
    for (int i = 0; i < nItems; i++) {
        items.Append(newItems[i]);
    }
    ReplaceDropDownItems(hwnd, items);

    // dropping the list selection empties the edit; put the text back. The
    // text is what the box means now - the list index it came from is stale
    // once the list is rebuilt.
    if (!str::Eq(GetTextTemp(), cur)) {
        SetText(cur);
        int n = len(cur);
        if (selStart > n) {
            selStart = n;
        }
        if (selEnd > n) {
            selEnd = n;
        }
        CbEditSelectText(hwnd, selStart, selEnd);
    }
    str::Free(cur);
    suppressNotify = prev;
}

static void DropDownItemsFromStringArray(StrVec& items, SeqStrings strings) {
    for (Str s = SeqStrFirst(strings); len(s) > 0; s = SeqStrNext(s)) {
        items.Append(s);
    }
}

void DropDown::SetItemsSeqStrings(SeqStrings items) {
    StrVec strings;
    DropDownItemsFromStringArray(strings, items);
    SetItems(strings);
}

Size DropDown::GetIdealSize() {
    Size s1 = PlatformFontMeasureText(font, StrL("Minimal"));

    int n = len(items);
    for (int i = 0; i < n; i++) {
        Str s = items[i];
        Size s2 = PlatformFontMeasureText(font, s);
        s1.dx = std::max(s1.dx, s2.dx);
        s1.dy = std::max(s1.dy, s2.dy);
    }
    // TODO: not sure if I want scrollbar. Only needed if a lot of items
    int dxPad = DpiGetSystemMetrics(SM_CXVSCROLL);
    int textPadding = DpiScale(8);
    DWORD margins = deferredMargins;
    if (HWND edit = CbEditHwnd(hwnd)) {
        margins = (DWORD)SendMessageW(edit, EM_GETMARGINS, 0, 0);
    }
    textPadding = std::max(textPadding, (int)LOWORD(margins) + (int)HIWORD(margins));
    int dx = s1.dx + dxPad + textPadding;
    if (idealDx > 0 && dx < idealDx) {
        dx = idealDx;
    }
    if (maxDx > 0 && dx > maxDx) {
        dx = maxDx;
    }
    if (colorSwatches || sectioned || previewItems) {
        dx += s1.dy + DpiScale(10);
    }
    // TODO: 5 is a guessed number.
    int dyPad = DpiScale(4);
    int dy = s1.dy + dyPad;
    Rect rc = HwndWindowRect(hwnd);
    dy = std::max(hwnd ? rc.dy : deferredHeight, dy);
    return {dx, dy};
}

void DropDown::SetCursorId(LPWSTR id) {
    cursorId = id;
}

// the base/Win.h combo box helpers, taking the control instead of its HWND
static HWND HwndOf(DropDown* dd) {
    return dd ? dd->hwnd : nullptr;
}

void CbSetCueBanner(DropDown* dd, Str s) {
    CbSetCueBanner(HwndOf(dd), s);
}

int CbGetTextLen(DropDown* dd) {
    return CbGetTextLen(HwndOf(dd));
}

bool CbIsDropped(DropDown* dd) {
    return CbIsDropped(HwndOf(dd));
}

int CbGetCurrentSelection(DropDown* dd) {
    if (dd && dd->itemsPending) return dd->pendingSelection;
    return CbGetCurrentSelection(HwndOf(dd));
}

void CbSetCurrentSelection(DropDown* dd, int n) {
    if (!dd) {
        return;
    }
    if (dd->itemsPending) {
        if (dd->windowPending) {
            dd->pendingSelection = n;
            str::ReplaceWithCopy(&dd->pendingText, n >= 0 && n < len(dd->items) ? dd->items[n] : Str{});
            return;
        }
        bool previous = dd->suppressNotify;
        dd->suppressNotify = true;
        dd->pendingSelection = n;
        if ((GetWindowLongPtrW(dd->hwnd, GWL_STYLE) & CBS_DROPDOWNLIST) == CBS_DROPDOWN) {
            // The edit can display the selected model value without inserting
            // a temporary native row. Populate the list only before interaction.
            dd->HwndBase::SetText(n >= 0 && n < len(dd->items) ? dd->items[n] : Str{});
        } else {
            CbResetContent(dd->hwnd);
            if (n >= 0 && n < len(dd->items)) {
                CbAddString(dd->hwnd, dd->items[n]);
                CbSetCurrentSelection(dd->hwnd, 0);
            }
        }
        dd->suppressNotify = previous;
        return;
    }
    if (n < 0) {
        CbSetCurrentSelection(dd->hwnd, -1);
        return;
    }
    ReportIf(n >= len(dd->items));
    CbSetCurrentSelection(dd->hwnd, n);
}

HWND CbEditHwnd(DropDown* dd) {
    return CbEditHwnd(HwndOf(dd));
}

void CbEditSelectAll(DropDown* dd) {
    CbEditSelectAll(HwndOf(dd));
}

void CbEditSelectText(DropDown* dd, int start, int end) {
    CbEditSelectText(HwndOf(dd), start, end);
}

void CbEditGetSelection(DropDown* dd, int& start, int& end) {
    CbEditGetSelection(HwndOf(dd), start, end);
}

void CbEditSetModified(DropDown* dd, bool on) {
    CbEditSetModified(HwndOf(dd), on);
}

bool CbEditIsModified(DropDown* dd) {
    return CbEditIsModified(HwndOf(dd));
}

#if IS_DEBUG
struct PreviewTrace {
    int calls = 0;
    int index = -1;
    bool expectedText = false;
};
static PreviewTrace* gPreviewTrace = nullptr;
static void RecordDropDownPreview(DropDown::PreviewItemEvent* ev) {
    gPreviewTrace->calls++;
    gPreviewTrace->index = ev->itemIndex;
    gPreviewTrace->expectedText = str::Eq(ev->text, StrL("Third"));
    DrawDropDownSwatch(ev->gfx, ev->bounds, kColRed);
}

void DropDown_UnitTestsPreviews() {
    HWND parent =
        CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 400, 240, nullptr, nullptr, GetInstance(), nullptr);
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader = {sizeof(BITMAPINFOHEADER), 220, -48, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    utassert(parent && bitmap && pixels);
    if (!parent || !bitmap || !pixels) return;
    HGDIOBJ previous = SelectObject(dc, bitmap);
    for (bool rtl : {false, true}) {
        DropDown drop;
        DropDown::CreateArgs args;
        args.parent = parent;
        args.font = GetPlatformFont((HFONT)GetStockObject(DEFAULT_GUI_FONT));
        args.isRtl = rtl;
        args.deferItems = true;
        drop.DeferCreate(args);
        drop.EnablePreviewItems();
        drop.onPreviewItem = MkFunc1Void<DropDown::PreviewItemEvent*>(RecordDropDownPreview);
        StrVec labels;
        for (const char* label : {"First", "Second", "Third"}) labels.Append(Str(label));
        drop.SetItems(labels);
        CbSetCurrentSelection(&drop, 2);
        utassert(drop.EnsureCreated());
        PreviewTrace trace;
        gPreviewTrace = &trace;
        DRAWITEMSTRUCT item{};
        item.itemID = 0;
        item.hDC = dc;
        item.rcItem = {0, 0, 220, 32};
        DrawPreviewItem(&drop, &item);
        utassert(trace.index == 2 && trace.expectedText);
        drop.EnsureItems();
        item.itemID = 2;
        DrawPreviewItem(&drop, &item);
        utassert(trace.calls == 2 && trace.index == 2 && trace.expectedText);
        Rect row{0, 0, 220, 32};
        Rect mark = drop.PreviewBounds(row), text = drop.PreviewTextBounds(row);
        utassert(rtl ? text.Right() <= mark.x : text.x >= mark.Right());
        utassert(mark.dy > 0 && mark.dy <= row.dy && text.dx > 0);
        drop.SetIsEnabled(false);
        DrawPreviewItem(&drop, &item);
        utassert(trace.calls == 3);
        DestroyWindow(drop.hwnd);
    }
    GfxHdc gfx(dc);
    gfx.FillRect({0, 0, 220, 48}, kColWhite);
    DrawDropDownSwatch(&gfx, {2, 2, 24, 24}, kColRed);
    DrawDropDownSwatch(&gfx, {32, 2, 24, 24}, kColRed, 0);
    DrawDropDownSwatch(&gfx, {62, 2, 24, 24}, kColRed, 128);
    GdiFlush();
    auto* bytes = (u8*)pixels;
    auto* opaque = bytes + (10 * 220 + 10) * 4;
    auto* clear = bytes + (10 * 220 + 40) * 4;
    auto* translucent = bytes + (10 * 220 + 70) * 4;
    utassert(opaque[0] == 0 && opaque[1] == 0 && opaque[2] == 255);
    utassert(clear[0] == clear[1] && clear[1] == clear[2] && clear[0] >= 180);
    utassert(translucent[2] > translucent[1] && translucent[1] > 0 && translucent[1] < 180);
    gPreviewTrace = nullptr;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(parent);
}

void DropDown_UnitTestsDeferred() {
    HWND parent =
        CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 400, 240, nullptr, nullptr, GetInstance(), nullptr);
    for (int variant = 0; variant < 4; variant++) {
        bool editable = (variant & 1) != 0;
        bool deferred = variant >= 2;
        DropDown drop;
        DropDown::CreateArgs args;
        args.parent = parent;
        args.font = GetPlatformFont((HFONT)GetStockObject(DEFAULT_GUI_FONT));
        args.isEditable = editable;
        args.deferItems = true;
        if (deferred)
            drop.DeferCreate(args);
        else
            drop.Create(args);
        StrVec items;
        for (const char* label : {"First", "Second", "Third"}) items.Append(Str(label));
        drop.SetItems(items);
        CbSetCurrentSelection(&drop, 2);
        utassert(drop.itemsPending && CbGetCurrentSelection(&drop) == 2);
        if (deferred) {
            utassert(!drop.hwnd && str::Eq(drop.GetTextTemp(), StrL("Third")));
            drop.SetIsEnabled(false);
            utassert(!drop.IsEnabled() && !drop.hwnd);
            if (editable) drop.SetText(StrL("Custom value"));
            utassert(drop.EnsureCreated());
            utassert(!drop.IsEnabled());
            utassert(str::Eq(drop.GetTextTemp(), editable ? StrL("Custom value") : StrL("Third")));
            drop.SetIsEnabled(true);
        }
        utassert(CbGetItemsCount(drop.hwnd) == (editable ? 0 : 1));
        utassert(str::Eq(drop.GetTextTemp(), deferred && editable ? StrL("Custom value") : StrL("Third")));
        if (editable) drop.SetText(StrL("Custom value"));
        drop.EnsureItems();
        utassert(!drop.itemsPending && CbGetItemsCount(drop.hwnd) == 3);
        utassert(str::Eq(drop.GetTextTemp(), editable ? StrL("Custom value") : StrL("Third")));
        utassert(CbGetCurrentSelection(&drop) == (editable ? -1 : 2));
        utassert(!IsWindowVisible(parent));
        DestroyWindow(drop.hwnd);
    }
    DestroyWindow(parent);
}
#endif
