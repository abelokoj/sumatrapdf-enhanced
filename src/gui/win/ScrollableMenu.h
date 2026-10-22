/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

extern void (*gUiInstallScrollbar)(HWND hwnd);
extern int (*gUiScrollbarWidth)(int dpi);
extern int (*gUiCornerRadius)(int dpi, int designRadius);

struct ScrollMenuDecoration {
    UINT command = 0;
    Color swatches[2] = {kColorUnset, kColorUnset};
    int swatchCount = 0;
    UINT actionCommand = 0;
    Pixmap* actionIcon = nullptr;
    int actionIconSize = 0;
    bool actionSlash = false;
    Str actionLabel;
    Str tooltip;
};

struct ScrollMenuItem {
    WStr label;
    WStr accessibleLabel;
    Str tooltip;
    Str actionLabel;
    UINT command = 0;
    UINT actionCommand = 0;
    Pixmap* actionIcon = nullptr;
    int actionIconSize = 0;
    Color swatches[2] = {kColorUnset, kColorUnset};
    int swatchCount = 0;
    bool actionSlash = false;
    HMENU submenu = nullptr;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
    bool literal = false;
};

enum class ScrollMenuPick {
    Item,
    Action
};

struct ScrollMenuSearch {
    Str label;
    Str hint;
    Str empty;
};

struct ScrollMenu {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND search = nullptr;
    HWND empty = nullptr;
    const ScrollMenuSearch* searchText = nullptr;
    Vec<int> visibleItems;
    int searchDy = 0;
    PlatformFont* font = nullptr;
    Vec<ScrollMenuItem> items;
    Color background = 0;
    Color foreground = 0;
    Color hotBackground = 0;
    HBRUSH brush = nullptr;
    int rowDy = 0;
    int padding = 0;
    int width = 0;
    int height = 0;
    int picked = -1;
    int hoveredAction = -1;
    int pressedAction = -1;
    bool actionFocused = false;
    bool pickedAction = false;
    bool rtl = false;
    bool done = false;
    Tooltip* tip = nullptr;

    ~ScrollMenu() {
        delete tip;
        if (hwnd) DestroyWindow(hwnd);
        for (auto& item : items) {
            wstr::Free(item.label);
            wstr::Free(item.accessibleLabel);
            str::Free(item.tooltip);
            str::Free(item.actionLabel);
        }
        if (brush) DeleteObject(brush);
    }

    void Pick(int index, ScrollMenuPick kind = ScrollMenuPick::Item) {
        if (index < 0 || index >= len(items)) return;
        auto& item = items[index];
        bool action = kind == ScrollMenuPick::Action;
        if (item.separator || (action ? !item.actionCommand : !item.enabled)) return;
        picked = index;
        pickedAction = action;
        done = true;
    }
};

static ScrollMenu* gActiveScrollMenu = nullptr;

static int ScrollMenuItemIndex(const ScrollMenu& popup, int row) {
    if (!popup.searchText) return row;
    return row >= 0 && row < len(popup.visibleItems) ? popup.visibleItems[row] : -1;
}

static void FilterScrollMenu(ScrollMenu& popup) {
    if (!popup.search) return;
    TempStr query = HwndGetTextTemp(popup.search);
    SendMessageW(popup.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(popup.list, LB_RESETCONTENT, 0, 0);
    VecClear(popup.visibleItems);
    popup.hoveredAction = popup.pressedAction = -1;
    popup.actionFocused = false;
    if (popup.tip) popup.tip->Delete();
    for (int i = 0; i < len(popup.items); i++) {
        auto& item = popup.items[i];
        if (len(query) && !str::ContainsI(ToUtf8Temp(item.label), query)) continue;
        VecAppend(popup.visibleItems, i);
        WStr name = len(item.accessibleLabel) ? item.accessibleLabel : item.label;
        SendMessageW(popup.list, LB_ADDSTRING, 0, (LPARAM)name.s);
    }
    SendMessageW(popup.list, LB_SETCURSEL, len(popup.visibleItems) ? 0 : -1, 0);
    SendMessageW(popup.list, WM_SETREDRAW, TRUE, 0);
    ShowWindow(popup.empty, len(popup.visibleItems) ? SW_HIDE : SW_SHOW);
    ShowWindow(popup.list, len(popup.visibleItems) ? SW_SHOW : SW_HIDE);
    HwndInvalidate(popup.list);
}

static int ScrollMenuLeadingDx(const ScrollMenu& popup, const ScrollMenuItem& item) {
    int swatch = std::max(8, popup.rowDy * 2 / 5);
    return std::max(popup.rowDy, item.swatchCount * swatch +
                                     std::max(0, item.swatchCount - 1) * std::max(2, popup.padding / 2) +
                                     popup.padding * 2);
}

static Rect ScrollMenuActionRect(const ScrollMenu& popup, Rect row) {
    if (!popup.rtl) row.x = row.Right() - popup.rowDy;
    row.dx = popup.rowDy;
    return row;
}

static Rect ScrollMenuSwatchRect(const ScrollMenu& popup, const ScrollMenuItem& item, Rect row, int index) {
    int swatch = std::max(8, popup.rowDy * 2 / 5);
    int gap = std::max(2, popup.padding / 2);
    int width = item.swatchCount * swatch + (item.swatchCount - 1) * gap;
    int leading = ScrollMenuLeadingDx(popup, item);
    int x = popup.rtl ? row.Right() - leading + (leading - width) / 2 : row.x + (leading - width) / 2;
    return {x + index * (swatch + gap), row.y + (popup.rowDy - swatch) / 2, swatch, swatch};
}

#if IS_DEBUG
static inline TempStr ScrollMenuStateTemp() {
    auto* popup = gActiveScrollMenu;
    if (!popup || !popup->hwnd || !popup->list) return {};
    str::Builder out;
    Rect client = HwndClientRect(popup->list);
    int selected = (int)SendMessageW(popup->list, LB_GETCURSEL, 0, 0);
    out.Append(fmt("scrollMenu hwnd=%lld list=%lld count=%d rowDy=%d selected=%d actionFocused=%d\n",
                   (i64)(LONG_PTR)popup->hwnd, (i64)(LONG_PTR)popup->list, len(popup->items), popup->rowDy, selected,
                   popup->actionFocused ? 1 : 0));
    for (int index = 0; index < len(popup->items); index++) {
        auto& item = popup->items[index];
        RECT native{};
        SendMessageW(popup->list, LB_GETITEMRECT, index, (LPARAM)&native);
        Rect row = ToRect(native);
        Rect action = item.actionCommand ? ScrollMenuActionRect(*popup, row) : Rect{};
        out.Append(
            fmt("scrollMenuRow index=%d command=%u action=%u enabled=%d visible=%d rect=%d,%d,%d,%d "
                "actionRect=%d,%d,%d,%d label=%s\n",
                index, item.command, item.actionCommand, item.enabled ? 1 : 0, row.Intersect(client).IsEmpty() ? 0 : 1,
                row.x, row.y, row.dx, row.dy, action.x, action.y, action.dx, action.dy, item.label));
        for (int swatch = 0; swatch < item.swatchCount; swatch++) {
            Rect rect = ScrollMenuSwatchRect(*popup, item, row, swatch);
            out.Append(fmt("scrollMenuSwatch index=%d swatch=%d color=%08x rect=%d,%d,%d,%d\n", index, swatch,
                           item.swatches[swatch], rect.x, rect.y, rect.dx, rect.dy));
        }
    }
    return ToStrTemp(out);
}
#endif

static void MeasureScrollMenu(ScrollMenu& popup, HWND owner, HMENU menu, int visibleRows, int maxWidth,
                              const Vec<ScrollMenuDecoration>* decorations = nullptr) {
    HDC dc = GetDC(owner);
    HGDIOBJ oldFont = SelectObject(dc, popup.font->GetHFont());
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    popup.padding = std::max(4, (int)metrics.tmHeight / 3);
    popup.rowDy = metrics.tmHeight + popup.padding * 2;
    int longest = 0;
    for (int i = 0; i < GetMenuItemCount(menu); i++) {
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;
        GetMenuItemInfoW(menu, i, TRUE, &info);
        auto label = (WCHAR*)AllocZero(info.cch + 1, sizeof(WCHAR));
        info.dwTypeData = label;
        info.cch++;
        GetMenuItemInfoW(menu, i, TRUE, &info);
        ScrollMenuItem item;
        item.label = WStr(label);
        item.command = info.wID;
        item.submenu = info.hSubMenu;
        item.enabled = !(info.fState & (MFS_DISABLED | MFS_GRAYED));
        item.checked = (info.fState & MFS_CHECKED) != 0;
        item.separator = (info.fType & MFT_SEPARATOR) != 0;
        item.literal = (info.fType & MFT_OWNERDRAW) != 0;
        if (decorations) {
            for (const auto& detail : *decorations) {
                if (detail.command != item.command) continue;
                item.swatchCount = std::clamp(detail.swatchCount, 0, 2);
                for (int swatch = 0; swatch < item.swatchCount; swatch++)
                    item.swatches[swatch] = detail.swatches[swatch];
                item.actionCommand = detail.actionCommand;
                item.actionIcon = detail.actionIcon;
                item.actionIconSize = detail.actionIconSize;
                item.actionSlash = detail.actionSlash;
                item.actionLabel = str::Dup(detail.actionLabel);
                item.tooltip = str::Dup(detail.tooltip);
                TempStr accessible = fmt("%s. %s", item.label, item.tooltip);
                if (item.actionCommand) accessible = fmt("%s. %s (Delete)", accessible, item.actionLabel);
                item.accessibleLabel = wstr::Dup(ToWStrTemp(accessible));
                break;
            }
        }
        VecAppend(popup.items, item);
        SIZE size{};
        GetTextExtentPoint32W(dc, label, len(item.label), &size);
        longest = std::max(longest, (int)size.cx + ScrollMenuLeadingDx(popup, item));
    }
    SelectObject(dc, oldFont);
    ReleaseDC(owner, dc);
    int dpi = DpiGetForHwnd(owner);
    int scrollbarDx = gUiScrollbarWidth ? gUiScrollbarWidth(dpi) : DpiScaleByDpi(dpi, 17);
    int rows = std::min(len(popup.items), std::clamp(visibleRows, 1, 50));
    popup.width = std::min(maxWidth, longest + popup.rowDy + popup.padding * 2 + scrollbarDx + 4);
    popup.width = std::max(popup.width, popup.rowDy * 4);
    popup.searchDy = popup.searchText ? popup.rowDy + popup.padding * 2 : 0;
    if (popup.searchText) popup.width = std::max(popup.width, std::min(maxWidth, popup.rowDy * 8));
    popup.height = rows * popup.rowDy + popup.searchDy + 4;
}

static Color ScrollMenuBlend(Color color, Color background) {
    int alpha = GetAlpha(color);
    return MkRgb((u8)((GetRed(color) * alpha + GetRed(background) * (255 - alpha) + 127) / 255),
                 (u8)((GetGreen(color) * alpha + GetGreen(background) * (255 - alpha) + 127) / 255),
                 (u8)((GetBlue(color) * alpha + GetBlue(background) * (255 - alpha) + 127) / 255));
}

static void PaintScrollMenuSwatch(GfxHdc& gfx, Rect rect, Color color, Color background, Color edge) {
    int tile = std::max(2, rect.dx / 4);
    Color alternate = AccentColor(background, 35);
    gfx.PushClip(rect);
    for (int y = 0; y < rect.dy; y += tile) {
        for (int x = 0; x < rect.dx; x += tile) {
            Color bg = ((x / tile + y / tile) % 2) ? alternate : background;
            gfx.FillRect({rect.x + x, rect.y + y, tile, tile}, color == kColorUnset ? bg : ScrollMenuBlend(color, bg));
        }
    }
    gfx.PopClip();
    gfx.FillRoundedRect(rect, std::max(2, tile / 2), kColorTransparent, edge);
    if (color == kColorUnset) gfx.DrawLineAA({rect.x + 2, rect.Bottom() - 2}, {rect.Right() - 2, rect.y + 2}, edge);
}

static void PaintScrollMenuItem(ScrollMenu& popup, DRAWITEMSTRUCT* draw) {
    int index = ScrollMenuItemIndex(popup, (int)draw->itemID);
    if (index < 0 || index >= len(popup.items)) return;
    auto& item = popup.items[index];
    bool hot = item.enabled && (draw->itemState & ODS_SELECTED);
    Color bg = hot ? popup.hotBackground : popup.background;
    HdcFillRect(draw->hDC, ToRect(draw->rcItem), bg);
    if (item.separator) {
        Rect line = ToRect(draw->rcItem);
        line.x += popup.padding;
        line.dx -= popup.padding * 2;
        line.y += line.dy / 2;
        line.dy = 1;
        HdcFillRect(draw->hDC, line, AccentColor(bg, 40));
        return;
    }
    HDC dc = draw->hDC;
    HGDIOBJ oldFont = SelectObject(dc, popup.font->GetHFont());
    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, item.enabled ? popup.foreground : AccentColor(popup.background, 100));
    RECT text = draw->rcItem;
    if (popup.rtl) {
        text.right -= ScrollMenuLeadingDx(popup, item);
        text.left += popup.rowDy;
    } else {
        text.left += ScrollMenuLeadingDx(popup, item);
        text.right -= popup.rowDy;
    }
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (item.literal) flags |= DT_NOPREFIX;
    flags |= popup.rtl ? DT_RIGHT | DT_RTLREADING : DT_LEFT;
    DrawTextW(dc, item.label.s, len(item.label), &text, flags);
    RECT glyph = draw->rcItem;
    if (popup.rtl)
        glyph.left = glyph.right - popup.rowDy;
    else
        glyph.right = glyph.left + popup.rowDy;
    if (item.swatchCount) {
        GfxHdc gfx(dc);
        for (int i = 0; i < item.swatchCount; i++)
            PaintScrollMenuSwatch(gfx, ScrollMenuSwatchRect(popup, item, ToRect(draw->rcItem), i), item.swatches[i], bg,
                                  popup.foreground);
    } else if (item.checked) {
        SetTextColor(dc, IsLightColor(bg) ? RGB(0, 112, 64) : RGB(109, 233, 165));
        DrawTextW(dc, L"\u2714", 1, &glyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    if (item.actionCommand) {
        Rect action = ScrollMenuActionRect(popup, ToRect(draw->rcItem));
        bool focus = hot && popup.actionFocused;
        bool hover = popup.hoveredAction == index;
        Rect button = action;
        button.Inflate(-popup.padding / 2, -popup.padding / 2);
        GfxHdc gfx(dc);
        if (focus || hover)
            gfx.FillRoundedRect(button, popup.padding, AccentColor(popup.hotBackground, 25),
                                focus ? popup.foreground : kColorTransparent);
        int size = std::max(1, std::min(item.actionIconSize, action.dx - popup.padding * 2));
        Rect icon{action.x + (action.dx - size) / 2, action.y + (action.dy - size) / 2, size, size};
        if (item.actionIcon) gfx.DrawPixmap(item.actionIcon, icon);
        if (item.actionSlash)
            gfx.DrawLineAA({icon.x + 1, icon.Bottom() - 1}, {icon.Right() - 1, icon.y + 1}, popup.foreground,
                           std::max(1.f, popup.padding / 4.f));
    } else if (item.submenu) {
        glyph = draw->rcItem;
        if (popup.rtl)
            glyph.right = glyph.left + popup.rowDy;
        else
            glyph.left = glyph.right - popup.rowDy;
        SetTextColor(dc, popup.foreground);
        DrawTextW(dc, popup.rtl ? L"\u2039" : L"\u203a", 1, &glyph, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    if ((draw->itemState & ODS_FOCUS) && !popup.actionFocused) DrawFocusRect(dc, &text);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
}

static LRESULT CALLBACK ScrollMenuListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* popup = (ScrollMenu*)data;
    if (msg == WM_KEYDOWN) {
        int selected = ScrollMenuItemIndex(*popup, (int)SendMessageW(hwnd, LB_GETCURSEL, 0, 0));
        bool actionAvailable = selected >= 0 && selected < len(popup->items) && popup->items[selected].actionCommand;
        if (popup->search && wp == VK_TAB) {
            SetFocus(popup->search);
            return 0;
        }
        if (wp == VK_TAB && actionAvailable) {
            popup->actionFocused = !popup->actionFocused;
            HwndInvalidate(hwnd);
            return 0;
        }
        if (wp == VK_DELETE && actionAvailable) {
            popup->Pick(selected, ScrollMenuPick::Action);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            popup->done = true;
            return 0;
        }
        if (wp == (popup->rtl ? VK_RIGHT : VK_LEFT)) {
            if (popup->actionFocused) {
                popup->actionFocused = false;
                HwndInvalidate(hwnd);
            } else {
                popup->done = true;
            }
            return 0;
        }
        if (wp == (popup->rtl ? VK_LEFT : VK_RIGHT) && actionAvailable) {
            popup->actionFocused = true;
            HwndInvalidate(hwnd);
            return 0;
        }
        if (wp == VK_RETURN || wp == VK_SPACE || wp == (popup->rtl ? VK_LEFT : VK_RIGHT)) {
            popup->Pick(selected,
                        popup->actionFocused && actionAvailable ? ScrollMenuPick::Action : ScrollMenuPick::Item);
            return 0;
        }
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END || wp == VK_PRIOR || wp == VK_NEXT)
            popup->actionFocused = false;
    }
    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
        DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
        int index = !HIWORD(hit) ? ScrollMenuItemIndex(*popup, LOWORD(hit)) : -1;
        Rect row;
        if (index >= 0) {
            RECT nativeRow{};
            SendMessageW(hwnd, LB_GETITEMRECT, LOWORD(hit), (LPARAM)&nativeRow);
            row = ToRect(nativeRow);
        }
        Point point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        bool actionHit =
            index >= 0 && popup->items[index].actionCommand && ScrollMenuActionRect(*popup, row).Contains(point);
        if (msg == WM_MOUSEMOVE) {
            int hover = actionHit ? index : -1;
            if (popup->hoveredAction != hover) {
                popup->hoveredAction = hover;
                HwndInvalidate(hwnd);
            }
            if (popup->tip) {
                Str text =
                    index >= 0 ? (actionHit ? popup->items[index].actionLabel : popup->items[index].tooltip) : Str{};
                if (len(text))
                    popup->tip->SetSingle(text, actionHit ? ScrollMenuActionRect(*popup, row) : row, true);
                else
                    popup->tip->Delete();
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
        } else if (msg == WM_LBUTTONDOWN) {
            popup->pressedAction = actionHit ? index : -1;
            popup->actionFocused = actionHit;
        } else if (actionHit && popup->pressedAction == index) {
            popup->Pick(index, ScrollMenuPick::Action);
            return 0;
        }
    } else if (msg == WM_MOUSELEAVE) {
        popup->hoveredAction = -1;
        if (popup->tip) popup->tip->Delete();
        HwndInvalidate(hwnd);
    } else if (msg == WM_SETCURSOR && popup->hoveredAction >= 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_LBUTTONUP) {
        DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
        if (!HIWORD(hit)) popup->Pick(ScrollMenuItemIndex(*popup, LOWORD(hit)));
    }
    return result;
}

static LRESULT CALLBACK ScrollMenuSearchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* popup = (ScrollMenu*)data;
    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
            popup->done = true;
            return 0;
        }
        if (wp == VK_RETURN) {
            popup->Pick(ScrollMenuItemIndex(*popup, (int)SendMessageW(popup->list, LB_GETCURSEL, 0, 0)));
            return 0;
        }
        if ((wp == VK_DOWN || wp == VK_UP || wp == VK_TAB) && len(popup->visibleItems)) {
            SetFocus(popup->list);
            if (wp == VK_UP) SendMessageW(popup->list, LB_SETCURSEL, len(popup->visibleItems) - 1, 0);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK ScrollMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* popup = (ScrollMenu*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        popup = (ScrollMenu*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)popup);
    }
    if (!popup) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_DRAWITEM:
            PaintScrollMenuItem(*popup, (DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_COMMAND:
            if ((HWND)lp == popup->search && HIWORD(wp) == EN_CHANGE) FilterScrollMenu(*popup);
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor((HDC)wp, popup->foreground);
            SetBkColor((HDC)wp, popup->background);
            return (LRESULT)popup->brush;
        case WM_ERASEBKGND:
            HdcFillRect((HDC)wp, HwndClientRect(hwnd), AccentColor(popup->background, 35));
            return TRUE;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) popup->done = true;
            break;
        case WM_CLOSE:
            popup->done = true;
            return 0;
        case WM_DESTROY:
            popup->hwnd = nullptr;
            popup->done = true;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool CreateScrollMenu(ScrollMenu& popup, HWND owner, Rect bounds) {
    const WCHAR* name = L"SumatraScrollableMenu";
    WNDCLASSW cls{};
    cls.lpfnWndProc = ScrollMenuProc;
    cls.hInstance = GetInstance();
    cls.lpszClassName = name;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    popup.brush = CreateSolidBrush(popup.background);
    popup.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, name, L"", WS_POPUP, bounds.x, bounds.y, bounds.dx, bounds.dy, owner,
                                 nullptr, GetInstance(), &popup);
    if (!popup.hwnd) return false;
    DWORD style =
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
    int listY = 2 + popup.searchDy;
    if (popup.searchText) {
        int pad = popup.padding;
        HDC dc = GetDC(popup.hwnd);
        HGDIOBJ oldFont = SelectObject(dc, popup.font->GetHFont());
        WStr label = ToWStrTemp(popup.searchText->label);
        SIZE size{};
        GetTextExtentPoint32W(dc, label.s, len(label), &size);
        SelectObject(dc, oldFont);
        ReleaseDC(popup.hwnd, dc);
        int labelDx = std::min((int)size.cx + pad, bounds.dx / 2);
        int editDx = bounds.dx - labelDx - pad * 3;
        int labelX = popup.rtl ? bounds.dx - pad - labelDx : pad;
        int editX = popup.rtl ? pad : pad * 2 + labelDx;
        HWND caption = CreateWindowExW(popup.rtl ? WS_EX_RTLREADING : 0, WC_STATICW, CWStrTemp(label),
                                       WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, labelX, pad + 2, labelDx, popup.rowDy,
                                       popup.hwnd, (HMENU)3, GetInstance(), nullptr);
        popup.search = CreateWindowExW(popup.rtl ? WS_EX_RTLREADING : 0, WC_EDITW, L"",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, editX, pad + 2,
                                       editDx, popup.rowDy, popup.hwnd, (HMENU)2, GetInstance(), nullptr);
        if (!caption || !popup.search) return false;
        SendMessageW(caption, WM_SETFONT, (WPARAM)popup.font->GetHFont(), FALSE);
        SendMessageW(popup.search, WM_SETFONT, (WPARAM)popup.font->GetHFont(), FALSE);
        SendMessageW(popup.search, EM_SETCUEBANNER, TRUE, (LPARAM)CWStrTemp(popup.searchText->hint));
        SetWindowSubclass(popup.search, ScrollMenuSearchProc, 1, (DWORD_PTR)&popup);
        popup.empty = CreateWindowExW(popup.rtl ? WS_EX_RTLREADING : 0, WC_STATICW, CWStrTemp(popup.searchText->empty),
                                      WS_CHILD | SS_CENTER | SS_CENTERIMAGE, pad, listY, bounds.dx - pad * 2,
                                      popup.rowDy, popup.hwnd, (HMENU)4, GetInstance(), nullptr);
        if (!popup.empty) return false;
        SendMessageW(popup.empty, WM_SETFONT, (WPARAM)popup.font->GetHFont(), FALSE);
    }
    popup.list = CreateWindowExW(popup.rtl ? WS_EX_RTLREADING : 0, L"LISTBOX", L"", style, 2, listY, bounds.dx - 4,
                                 bounds.dy - listY - 2, popup.hwnd, (HMENU)1, GetInstance(), nullptr);
    if (!popup.list) return false;
    SendMessageW(popup.list, WM_SETFONT, (WPARAM)popup.font->GetHFont(), FALSE);
    SendMessageW(popup.list, LB_SETITEMHEIGHT, 0, popup.rowDy);
    bool decorated = false;
    for (auto& item : popup.items) {
        WStr accessibleName = len(item.accessibleLabel) ? item.accessibleLabel : item.label;
        LRESULT index = SendMessageW(popup.list, LB_ADDSTRING, 0, (LPARAM)accessibleName.s);
        if (index >= 0) SendMessageW(popup.list, LB_SETITEMDATA, index, item.command);
        decorated |= len(item.tooltip) || item.actionCommand;
    }
    if (gUiInstallScrollbar) gUiInstallScrollbar(popup.list);
    SetWindowSubclass(popup.list, ScrollMenuListProc, 1, (DWORD_PTR)&popup);
    if (decorated) {
        popup.tip = new Tooltip();
        Tooltip::CreateArgs args;
        args.parent = popup.list;
        args.font = popup.font;
        args.isRtl = popup.rtl;
        popup.tip->Create(args);
    }
    int radius = gUiCornerRadius ? gUiCornerRadius(DpiGetForHwnd(owner), 6) : DpiScaleByDpi(DpiGetForHwnd(owner), 6);
    HRGN region = CreateRoundRectRgn(0, 0, bounds.dx + 1, bounds.dy + 1, radius * 2, radius * 2);
    if (region && !SetWindowRgn(popup.hwnd, region, FALSE)) DeleteObject(region);
    for (int i = 0; i < len(popup.items); i++) {
        if (popup.items[i].enabled && !popup.items[i].separator) {
            SendMessageW(popup.list, LB_SETCURSEL, i, 0);
            break;
        }
    }
    if (popup.search) FilterScrollMenu(popup);
    return true;
}

static bool ScrollMenuContainsWindow(HWND popup, HWND target) {
    // Overlay scrollbars are owned top-level windows, not child controls.
    for (HWND window = target; window; window = GetWindow(window, GW_OWNER)) {
        if (window == popup || IsChild(popup, window)) return true;
    }
    return false;
}

static UINT TrackScrollMenu(HWND owner, HMENU menu, Point anchor, PlatformFont* font, int visibleRows, int maxWidth,
                            Color background, Color foreground, Color hotBackground, bool rtl, bool rightAlign = false,
                            const Vec<ScrollMenuDecoration>* decorations = nullptr,
                            const ScrollMenuSearch* searchText = nullptr) {
    ScrollMenu popup;
    ScrollMenu* previous = gActiveScrollMenu;
    gActiveScrollMenu = &popup;
    defer {
        gActiveScrollMenu = previous;
    };
    popup.searchText = searchText;
    popup.font = font;
    popup.background = background;
    popup.foreground = foreground;
    popup.hotBackground = hotBackground;
    popup.rtl = rtl;
    MeasureScrollMenu(popup, owner, menu, visibleRows, maxWidth, decorations);
    if (len(popup.items) == 0) return 0;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({anchor.x, anchor.y}, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT work = monitor.rcWork;
    int rows = std::max(1, ((int)(work.bottom - work.top) - popup.searchDy - 4) / popup.rowDy);
    popup.height = std::min(popup.height, rows * popup.rowDy + popup.searchDy + 4);
    popup.width = std::min(popup.width, (int)(work.right - work.left));
    int x = rightAlign ? anchor.x - popup.width : anchor.x;
    x = std::clamp(x, (int)work.left, (int)work.right - popup.width);
    int y = std::clamp(anchor.y, (int)work.top, (int)work.bottom - popup.height);
    if (!CreateScrollMenu(popup, owner, {x, y, popup.width, popup.height})) return 0;
    HWND oldFocus = GetFocus();
    ShowWindow(popup.hwnd, SW_SHOW);
    SetFocus(popup.search ? popup.search : popup.list);
    MSG message{};
    bool quit = false;
    while (!popup.done && IsWindow(owner)) {
        int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            quit = result == 0;
            break;
        }
        bool mouseDown = message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN ||
                         message.message == WM_MBUTTONDOWN || message.message == WM_NCLBUTTONDOWN;
        if (mouseDown && !ScrollMenuContainsWindow(popup.hwnd, message.hwnd)) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    POINT nextAnchor{x + popup.width, y};
    if (popup.picked >= 0) {
        RECT row{};
        int pickedRow = popup.searchText ? VecFind(popup.visibleItems, popup.picked) : popup.picked;
        SendMessageW(popup.list, LB_GETITEMRECT, pickedRow, (LPARAM)&row);
        nextAnchor.y += row.top;
    }
    delete popup.tip;
    popup.tip = nullptr;
    DestroyWindow(popup.hwnd);
    popup.hwnd = nullptr;
    if (IsWindow(oldFocus)) SetFocus(oldFocus);
    if (quit) PostQuitMessage((int)message.wParam);
    if (popup.picked < 0 || !IsWindow(owner)) return 0;
    auto& item = popup.items[popup.picked];
    if (popup.pickedAction) return item.actionCommand;
    if (!item.submenu) return item.command;
    return TrackScrollMenu(owner, item.submenu, {rtl ? x : nextAnchor.x, nextAnchor.y}, font, visibleRows, maxWidth,
                           background, foreground, hotBackground, rtl, rtl, decorations);
}
