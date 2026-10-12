/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Pixmap.h"
#include "gui/Dpi.h"
#include "base/Win.h"

#include "gui/UIModels.h"

#include "Settings.h"
#include "AppSettings.h"
#include "Theme.h"
#include "DocController.h"
#include "EngineBase.h"
#include "TextSelection.h"
#include "Commands.h"
#include "Translations.h"
enum class TouchSelHandle;
enum class SelectionDragEdge;
#include "Selection.h"
#include "RefHover.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif

static int gClassRegistered = 0;

static bool PopupClientToPagePt(RefHoverState* s, int clientX, int clientY, PointF& ptOut) {
    if (!s || !s->hitEngine || s->displayed.destPage <= 0 || s->renderInFlight) {
        return false;
    }
    float zoom = s->displayed.baseZoom * s->displayed.userZoom;
    if (zoom <= 0.f) {
        return false;
    }
    int border = DpiScale(kRefHoverBorder);
    if (clientX < border || clientY < border) {
        return false;
    }
    RectF region = s->displayed.region;
    float y = (float)(clientY - border) / zoom;
    if (y >= region.dy) {
        y -= region.dy;
        region = s->displayed.continuationRegion;
    }
    float x = (float)(clientX - border) / zoom;
    if (region.IsEmpty() || y >= region.dy || x >= region.dx) return false;
    ptOut.x = region.x + x;
    ptOut.y = region.y + y;
    return true;
}

static IPageDestination* LaunchLinkAtPagePt(RefHoverState* s, PointF pagePt) {
    if (!s || !s->hitEngine || s->displayed.destPage <= 0) {
        return nullptr;
    }
    IPageElement* el = s->hitEngine->GetElementAtPos(s->displayed.destPage, pagePt);
    if (!el || !el->Is(kindPageElementDest)) {
        return nullptr;
    }
    IPageDestination* dest = el->AsLink();
    if (!RefHoverIsLaunchLink(dest)) {
        return nullptr;
    }
    return dest;
}

static IPageDestination* LaunchLinkAtPopupPt(RefHoverState* s, int clientX, int clientY) {
    PointF pagePt;
    if (!PopupClientToPagePt(s, clientX, clientY, pagePt)) {
        return nullptr;
    }
    return LaunchLinkAtPagePt(s, pagePt);
}

bool RefHoverHasSelection(RefHoverState* s) {
    return s && s->selection && s->selection->result.len > 0;
}

void RefHoverClearSelection(RefHoverState* s) {
    if (!s) return;
    s->selecting = false;
    s->selectingWords = false;
    if (s->hwndPopup && GetCapture() == s->hwndPopup) ReleaseCapture();
    delete s->selection;
    s->selection = nullptr;
}

static bool RefHoverCopySelection(RefHoverState* s) {
    if (!RefHoverHasSelection(s)) return false;
    Str text = s->selection->ExtractText(StrL("\r\n"));
    bool copied = CopyTextToClipboard(text);
    str::Free(text);
    return copied;
}

static void RefHoverSelectVisible(RefHoverState* s) {
    if (!s || !s->hitEngine || s->displayed.destPage <= 0 || s->renderInFlight) return;
    int border = DpiScale(kRefHoverBorder);
    Rect client = HwndClientRect(s->hwndPopup);
    float zoom = s->displayed.baseZoom * s->displayed.userZoom;
    if (zoom <= 0.f) return;
    float visibleDx = std::max(0, client.dx - 2 * border) / zoom;
    float visibleDy = std::max(0, client.dy - 2 * border) / zoom;
    RectF primary = s->displayed.region;
    primary.dx = std::min(primary.dx, visibleDx);
    primary.dy = std::min(primary.dy, visibleDy);
    RectF continuation = s->displayed.continuationRegion;
    continuation.dx = std::min(continuation.dx, visibleDx);
    continuation.dy = std::min(continuation.dy, std::max(0.f, visibleDy - s->displayed.region.dy));
    int n = 0;
    Rect* coords = nullptr;
    s->hitEngine->GetTextForPage(s->displayed.destPage, &n, &coords);
    int first = -1, last = -1;
    for (int i = 0; coords && i < n; i++) {
        RectF glyph = ToRectF(coords[i]);
        if (glyph.IsEmpty() || (glyph.Intersect(primary).IsEmpty() && glyph.Intersect(continuation).IsEmpty()))
            continue;
        if (first < 0) first = i;
        last = i;
    }
    if (first < 0) return;
    RefHoverClearSelection(s);
    s->selection = new TextSelection(s->hitEngine);
    s->selection->StartAt(s->displayed.destPage, first);
    s->selection->SelectUpTo(s->displayed.destPage, last + 1);
    KillTimer(s->hwndCanvas, kRefHoverHideTimerID);
    HwndInvalidate(s->hwndPopup, false);
}

static void RefHoverPaintSelection(HDC hdc, RefHoverState* s) {
    if (!RefHoverHasSelection(s) || s->renderInFlight) return;
    ParsedColor* color = GetPrefsColor(gSettings->fixedPageUI.selectionColor);
    Gdiplus::SolidBrush brush(Gdiplus::Color(SelectionColorAlpha(color), GetRValue(color->col), GetGValue(color->col),
                                             GetBValue(color->col)));
    Gdiplus::Graphics graphics(hdc);
    float zoom = s->displayed.baseZoom * s->displayed.userZoom;
    int border = DpiScale(kRefHoverBorder);
    Rect client = HwndClientRect(s->hwndPopup);
    graphics.SetClip(
        Gdiplus::Rect(border, border, std::max(0, client.dx - 2 * border), std::max(0, client.dy - 2 * border)));
    auto paint = [&](RectF region, float offsetY) {
        if (region.IsEmpty()) return;
        auto& selected = s->selection->result;
        for (int i = 0; i < selected.len; i++) {
            if (selected.pages[i] != s->displayed.destPage) continue;
            RectF clipped = ToRectF(selected.rects[i]).Intersect(region);
            if (clipped.IsEmpty()) continue;
            graphics.FillRectangle(&brush, border + (clipped.x - region.x) * zoom,
                                   border + offsetY + (clipped.y - region.y) * zoom, clipped.dx * zoom,
                                   clipped.dy * zoom);
        }
    };
    paint(s->displayed.region, 0);
    paint(s->displayed.continuationRegion, s->displayed.region.dy * zoom);
}

static void RefHoverExtendSelection(RefHoverState* s, Point point) {
    PointF page;
    if (!s->selection || !PopupClientToPagePt(s, point.x, point.y, page)) return;
    if (s->selectingWords)
        s->selection->SelectWordsUpTo(s->displayed.destPage, page.x, page.y);
    else
        s->selection->SelectUpTo(s->displayed.destPage, page.x, page.y);
    HwndInvalidate(s->hwndPopup, false);
}

static void RefHoverRoundPopup(HWND hwnd) {
    Size size = HwndWindowRect(hwnd).Size();
    if (size.dx <= 0 || size.dy <= 0) return;
    int diameter = std::min(2 * GetAppCornerRadius(DpiGetForHwnd(hwnd), 6), std::min(size.dx, size.dy));
    HRGN region = CreateRoundRectRgn(0, 0, size.dx + 1, size.dy + 1, diameter, diameter);
    if (!region) return;
    HRGN previous = CreateRectRgn(0, 0, 0, 0);
    bool same = GetWindowRgn(hwnd, previous) != ERROR && EqualRgn(previous, region);
    DeleteObject(previous);
    if (same || !SetWindowRgn(hwnd, region, TRUE)) DeleteObject(region);
}

static void RefHoverDrawEdge(HDC hdc, HWND hwnd) {
    constexpr float edgePoints = 0.5f;
    int dpi = DpiGetForHwnd(hwnd);
    float width = edgePoints * (float)dpi / 72.f;
    Rect client = HwndClientRect(hwnd);
    float inset = width / 2.f;
    float dx = (float)client.dx - 1.f - width;
    float dy = (float)client.dy - 1.f - width;
    if (dx <= 0.f || dy <= 0.f) return;

    float diameter = std::min(2.f * GetAppCornerRadius(dpi, 6), std::min(dx, dy));
    Gdiplus::Graphics gfx(hdc);
    gfx.SetPageUnit(Gdiplus::UnitPixel);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen edge(Gdiplus::Color(255, 255, 0, 0), width);
    if (diameter <= 0.f) {
        gfx.DrawRectangle(&edge, inset, inset, dx, dy);
        return;
    }

    Gdiplus::GraphicsPath path;
    path.AddArc(inset, inset, diameter, diameter, 180.f, 90.f);
    path.AddArc(inset + dx - diameter, inset, diameter, diameter, 270.f, 90.f);
    path.AddArc(inset + dx - diameter, inset + dy - diameter, diameter, diameter, 0.f, 90.f);
    path.AddArc(inset, inset + dy - diameter, diameter, diameter, 90.f, 90.f);
    path.CloseFigure();
    gfx.DrawPath(&edge, &path);
}

static LRESULT CALLBACK RefHoverWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DpiScope dpi(hwnd);
    RefHoverState* state = (RefHoverState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCALCSIZE) {
        return 0;
    }
    if (msg == WM_NCHITTEST) {
        return RefHoverEdgeHit(HwndWindowRect(hwnd), {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, DpiScale(8));
    }
    if (msg == WM_GETMINMAXINFO) {
        auto info = (MINMAXINFO*)lp;
        info->ptMinTrackSize = {DpiScale(160), DpiScale(100)};
        return 0;
    }
    if (msg == WM_ENTERSIZEMOVE && state) {
        state->resizing = true;
        KillTimer(state->hwndCanvas, kRefHoverHideTimerID);
        return 0;
    }
    if (msg == WM_EXITSIZEMOVE && state) {
        state->resizing = false;
        RefHoverResizePopup(state);
        SetTimer(state->hwndCanvas, kRefHoverHideTimerID, 300, nullptr);
        return 0;
    }
    if (msg == WM_SIZE) {
        RefHoverRoundPopup(hwnd);
        if (state && state->resizing) RefHoverResizePopup(state);
        return 0;
    }
    if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && state) {
        RefHoverHide(state, state->hwndCanvas);
        return 0;
    }
    if (msg == WM_SETCURSOR) {
        if (LOWORD(lp) != HTCLIENT) {
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        RefHoverState* s = (RefHoverState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        POINT p;
        if (s && GetCursorPos(&p)) {
            ScreenToClient(hwnd, &p);
            if (!s->selecting && LaunchLinkAtPopupPt(s, p.x, p.y)) {
                SetCursorCached(IDC_HAND);
                return TRUE;
            }
            PointF page;
            if (PopupClientToPagePt(s, p.x, p.y, page) &&
                (s->selecting || (s->hitEngine && s->hitEngine->GetTextForPage(s->displayed.destPage).len > 0))) {
                SetCursorCached(IDC_IBEAM);
                return TRUE;
            }
        }
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        Color background =
            state && state->pageBackground != kColorUnset ? state->pageBackground : ThemeWindowControlBackgroundColor();
        HBRUSH hbg = CreateSolidBrush(background);
        HdcFillRect(hdc, HwndClientRect(hwnd), hbg);
        DeleteObject(hbg);

        RefHoverState* s = (RefHoverState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (s && s->bmp) {
            Size bmpSize = Size(s->bmp->width, s->bmp->height);
            int border = DpiScale(kRefHoverBorder);
            HDC bmpDC = CreateCompatibleDC(hdc);
            HGDIOBJ oldBmp = bmpDC ? SelectObject(bmpDC, s->bmp->hbmp) : nullptr;
            if (oldBmp) {
                BitBlt(hdc, border, border, bmpSize.dx, bmpSize.dy, bmpDC, 0, 0, SRCCOPY);
                SelectObject(bmpDC, oldBmp);
            }
            if (bmpDC) {
                DeleteDC(bmpDC);
            }
        }

        RefHoverPaintSelection(hdc, state);
        RefHoverDrawEdge(hdc, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    // the cursor moved onto the popup: the wheel is for it, no modifier needed
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        RefHoverState* s = (RefHoverState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (s && s->hitEngine) {
            RefHoverOnWheel(s, s->hitEngine, msg, wp);
        }
        return 0;
    }
    if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) && state) {
        Point point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        PointF page;
        if (!PopupClientToPagePt(state, point.x, point.y, page)) return 0;
        bool extend = (wp & MK_SHIFT) && RefHoverHasSelection(state);
        if (!extend) {
            RefHoverClearSelection(state);
            state->selection = new TextSelection(state->hitEngine);
            state->selection->StartAt(state->displayed.destPage, page.x, page.y);
        }
        state->selectingWords = msg == WM_LBUTTONDBLCLK;
        state->selectionDragged = extend || state->selectingWords;
        if (state->selectingWords)
            state->selection->SelectWordAt(state->displayed.destPage, page.x, page.y);
        else
            state->selection->SelectUpTo(state->displayed.destPage, page.x, page.y);
        state->selectionPress = point;
        state->selecting = true;
        KillTimer(state->hwndCanvas, kRefHoverHideTimerID);
        SetFocus(hwnd);
        SetCapture(hwnd);
        HwndInvalidate(hwnd, false);
        return 0;
    }
    if (msg == WM_MOUSEMOVE && state && state->selecting) {
        Point point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (abs(point.x - state->selectionPress.x) >= DpiGetSystemMetrics(SM_CXDRAG) ||
            abs(point.y - state->selectionPress.y) >= DpiGetSystemMetrics(SM_CYDRAG)) {
            state->selectionDragged = true;
            RefHoverExtendSelection(state, point);
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP && state && state->selecting) {
        Point point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (state->selectionDragged) RefHoverExtendSelection(state, point);
        state->selecting = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (!state->selectionDragged) {
            IPageDestination* link = LaunchLinkAtPopupPt(state, point.x, point.y);
            if (link) RefHoverHandlePopupClick(state, link);
        }
        if (!RefHoverHasSelection(state)) RefHoverScheduleHide(state, state->hwndCanvas, 300);
        return 0;
    }
    if ((msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE) && state) {
        state->selecting = false;
        return 0;
    }
    if (msg == WM_COPY && state) {
        RefHoverCopySelection(state);
        return 0;
    }
    if ((msg == WM_COMMAND || msg == WM_KEYDOWN) && state) {
        int command = msg == WM_COMMAND ? LOWORD(wp) : 0;
        if (msg == WM_KEYDOWN) {
            if (wp == VK_ESCAPE) {
                RefHoverHide(state, state->hwndCanvas);
                return 0;
            }
            if (IsCtrlPressed() && wp == 'C') command = CmdCopySelection;
            if (IsCtrlPressed() && wp == 'A') command = CmdSelectAll;
        }
        if (command == CmdCopySelection) {
            RefHoverCopySelection(state);
            return 0;
        }
        if (command == CmdSelectAll) {
            RefHoverSelectVisible(state);
            return 0;
        }
    }
    if (msg == WM_CONTEXTMENU && state) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (RefHoverHasSelection(state) ? 0 : MF_GRAYED), CmdCopySelection,
                    CWStrTemp(Tr("Copy")));
        AppendMenuW(menu, MF_STRING, CmdSelectAll, CWStrTemp(Tr("Select All")));
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (point.x == -1 && point.y == -1) {
            point = {DpiScale(20), DpiScale(20)};
            ClientToScreen(hwnd, &point);
        }
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (command) SendMessageW(hwnd, WM_COMMAND, command, 0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static bool RegisterClassIfNeeded() {
    if (gClassRegistered != 0) {
        return gClassRegistered > 0;
    }
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = RefHoverWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kRefHoverClass;
    wc.hCursor = GetCachedCursor(IDC_ARROW);
    ATOM atom = RegisterClassW(&wc);
    gClassRegistered = atom ? 1 : -1;
    return gClassRegistered > 0;
}

bool RefHoverPopupCreate(RefHoverState* s, HWND hwndCanvas) {
    if (!RegisterClassIfNeeded()) {
        return false;
    }
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kRefHoverClass, nullptr, WS_POPUP | WS_THICKFRAME, 0, 0, 10, 10,
                                hwndCanvas, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        return false;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
    s->hwndPopup = hwnd;
    s->hwndCanvas = hwndCanvas;
    return true;
}

void RefHoverShowPopup(RefHoverState* s, Point screenPt) {
    if (!s || !s->hwndPopup || !s->bmp) {
        return;
    }
    Size bmpSize = Size(s->bmp->width, s->bmp->height);
    int border = DpiScale(kRefHoverBorder);
    int popupW = bmpSize.dx + (2 * border);
    int popupH = bmpSize.dy + (2 * border);

    HMONITOR hmon = MonitorFromPoint({screenPt.x, screenPt.y}, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hmon, &mi);

    int leftBound = mi.rcWork.left;
    int rightBound = mi.rcWork.right;
    int topBound = mi.rcWork.top;
    int bottomBound = mi.rcWork.bottom;
    Rect pr = s->pending.pageScreenRect;
    if (pr.dy > 0) {
        topBound = std::max(pr.y, topBound);
        bottomBound = std::min(pr.y + pr.dy, bottomBound);
    }
    int boundW = rightBound - leftBound;
    int boundH = bottomBound - topBound;
    popupW = std::min(popupW, boundW);
    popupH = std::min(popupH, boundH);

    int pageCenterX = (pr.dx > 0) ? (pr.x + (pr.dx / 2)) : screenPt.x;
    int anchorX = pageCenterX;
    if (pr.dx > 0) {
        int colWidth = pr.dx / 2;
        if (popupW <= colWidth) {
            anchorX = (screenPt.x >= pageCenterX) ? (pr.x + (pr.dx * 3 / 4)) : (pr.x + (pr.dx / 4));
        }
    }
    int x = anchorX - (popupW / 2);
    int cursorPad = DpiScale(kRefHoverCursorPad);
    int spaceBelow = bottomBound - (screenPt.y + cursorPad);
    int spaceAbove = (screenPt.y - cursorPad) - topBound;
    int y;
    if (spaceBelow >= popupH) {
        y = screenPt.y + cursorPad;
    } else if (spaceAbove >= popupH) {
        y = screenPt.y - popupH - cursorPad;
    } else if (spaceBelow >= spaceAbove) {
        if (spaceBelow > 0) {
            popupH = spaceBelow;
        }
        y = screenPt.y + cursorPad;
    } else {
        if (spaceAbove > 0) {
            popupH = spaceAbove;
        }
        y = screenPt.y - popupH - cursorPad;
    }
    x = std::max(x, leftBound);
    if (x + popupW > rightBound) {
        x = rightBound - popupW;
    }
    y = std::max(y, topBound);
    if (y + popupH > bottomBound) {
        popupH = bottomBound - y;
    }

    if (popupW <= 0 || popupH <= 0) {
        ShowWindow(s->hwndPopup, SW_HIDE);
        return;
    }

    SetWindowPos(s->hwndPopup, HWND_TOPMOST, x, y, popupW, popupH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    HwndInvalidate(s->hwndPopup, true);
}

bool RefHoverRerenderDisplayedRegion(RefHoverState* s, EngineBase* engine, int page, RectF region) {
    if (!s || !engine || page <= 0) {
        return false;
    }
    float zoom = s->displayed.baseZoom * s->displayed.userZoom;
    if (zoom <= 0.f) {
        return false;
    }
    if (s->displayed.destPage != page) RefHoverClearSelection(s);
    s->displayed.destPage = page;
    s->displayed.region = region;
    s->displayed.continuationRegion = {};
    RefHoverState::RenderRequest req;
    req.pageNo = page;
    req.zoom = zoom;
    req.region = region;
    RefHoverRequestRender(s, engine, req);
    return true;
}

bool RefHoverResizePopup(RefHoverState* s) {
    if (!s || !s->hwndPopup || !s->bmp || !s->hitEngine || s->displayed.destPage <= 0) {
        return false;
    }
    Rect client = HwndClientRect(s->hwndPopup);
    int border = DpiScale(kRefHoverBorder);
    Size content{client.dx - border * 2, client.dy - border * 2};
    RectF page = s->hitEngine->PageMediabox(s->displayed.destPage);
    RectF region =
        RefHoverSizedRegion(page, s->displayed.region, content, s->displayed.baseZoom * s->displayed.userZoom);
    if (region.dx <= 0.f || region.dy <= 0.f || region == s->displayed.region) {
        return false;
    }
    return RefHoverRerenderDisplayedRegion(s, s->hitEngine, s->displayed.destPage, region);
}

// Re-render the popup at adjusted zoom in response to a mouse-wheel event.
// Popup window keeps its initial size; only the rendered content scales.
// Positive delta zooms in, negative zooms out. Returns true if the zoom
// changed and a re-render happened.
bool RefHoverWheelZoom(RefHoverState* s, EngineBase* engine, int wheelDelta) {
    if (!s || !s->hwndPopup || s->displayed.destPage <= 0 || !engine) {
        return false;
    }
    float factor = (wheelDelta > 0) ? kRefHoverUserZoomStep : (1.f / kRefHoverUserZoomStep);
    float newZoom = s->displayed.userZoom * factor;
    newZoom = ClampF(newZoom, kRefHoverMinUserZoom, kRefHoverMaxUserZoom);
    if (newZoom == s->displayed.userZoom) {
        return false;
    }
    s->displayed.userZoom = newZoom;

    Rect rc = HwndClientRect(s->hwndPopup);
    int border = DpiScale(kRefHoverBorder);
    float clientW = (float)(rc.dx - (2 * border));
    float clientH = (float)(rc.dy - (2 * border));
    float zoom = s->displayed.baseZoom * s->displayed.userZoom;
    if (zoom <= 0.f || clientW <= 0.f || clientH <= 0.f) {
        return false;
    }

    RectF mediabox = engine->PageMediabox(s->displayed.destPage);
    RectF region = s->displayed.region;
    region.dx = clientW / zoom;
    region.dy = clientH / zoom;
    if (region.x + region.dx > mediabox.dx) {
        region.dx = mediabox.dx - region.x;
    }
    if (region.y + region.dy > mediabox.dy) {
        region.dy = mediabox.dy - region.y;
    }
    if (region.dx <= 0.f || region.dy <= 0.f) {
        return false;
    }

    return RefHoverRerenderDisplayedRegion(s, engine, s->displayed.destPage, region);
}

// Scroll the popup's rendered region by a wheel notch. Positive delta scrolls
// toward earlier content (up); negative scrolls toward later content (down).
// Rolls over to the previous / next page when the viewport hits a page edge
// (continuous scrolling). Popup window keeps its initial size; only the
// rendered region's Y (and possibly page number) changes.
bool RefHoverWheelScroll(RefHoverState* s, EngineBase* engine, int wheelDelta) {
    if (!s || !s->hwndPopup || s->displayed.destPage <= 0 || !engine) {
        return false;
    }
    float zoom = s->displayed.baseZoom * s->displayed.userZoom;
    if (zoom <= 0.f) {
        return false;
    }
    int pageCount = engine->PageCount();
    int page = s->displayed.destPage;
    RectF region = s->displayed.region;
    RectF mediabox = engine->PageMediabox(page);
    if (mediabox.dx <= 0.f || mediabox.dy <= 0.f) {
        return false;
    }

    float scrollStep = (float)DpiScale(kRefHoverScrollStepPx);
    float scrollPt = scrollStep * ((float)wheelDelta / (float)WHEEL_DELTA) / zoom;
    float newY = region.y - scrollPt;

    if (newY < 0.f) {
        if (page > 1) {
            float overflow = -newY;
            page--;
            mediabox = engine->PageMediabox(page);
            newY = mediabox.dy - region.dy - overflow;
            newY = std::max(newY, 0.f);
        } else {
            newY = 0.f;
        }
    } else if (newY + region.dy > mediabox.dy) {
        if (page < pageCount) {
            float overflow = (newY + region.dy) - mediabox.dy;
            page++;
            mediabox = engine->PageMediabox(page);
            newY = overflow;
            if (newY + region.dy > mediabox.dy) {
                newY = mediabox.dy - region.dy;
            }
            newY = std::max(newY, 0.f);
        } else {
            newY = mediabox.dy - region.dy;
            newY = std::max(newY, 0.f);
        }
    }

    if (page == s->displayed.destPage && newY == region.y) {
        return false;
    }
    region.y = newY;
    region.dy = std::min(region.dy, mediabox.dy);
    if (region.x + region.dx > mediabox.dx) {
        region.x = mediabox.dx - region.dx;
        if (region.x < 0.f) {
            region.x = 0.f;
            region.dx = mediabox.dx;
        }
    }

    return RefHoverRerenderDisplayedRegion(s, engine, page, region);
}

// A wheel message meant for the popup: Ctrl + wheel zooms, any other wheel
// scrolls. So does a horizontal wheel, which is how mouse software often sends
// Shift + wheel (issue #6252); right scrolls down, as Shift + wheel down does.
void RefHoverOnWheel(RefHoverState* s, EngineBase* engine, UINT msg, WPARAM wp) {
    int delta = GET_WHEEL_DELTA_WPARAM(wp);
    if (msg == WM_MOUSEHWHEEL) {
        RefHoverWheelScroll(s, engine, -delta);
        return;
    }
    bool isCtrl = (LOWORD(wp) & MK_CONTROL) || IsCtrlPressed();
    if (isCtrl) {
        RefHoverWheelZoom(s, engine, delta);
        return;
    }
    RefHoverWheelScroll(s, engine, delta);
}

#if IS_DEBUG
void RefHoverPopup_UnitTests() {
    HWND canvas =
        CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 600, 500, nullptr, nullptr, GetInstance(), nullptr);
    RefHoverState state;
    utassert(RefHoverPopupCreate(&state, canvas));
    SetWindowPos(state.hwndPopup, nullptr, 100, 100, 400, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    Rect window = HwndWindowRect(state.hwndPopup);
    HRGN corners = CreateRectRgn(0, 0, 0, 0);
    utassert(GetWindowRgn(state.hwndPopup, corners) != ERROR);
    utassert(!PtInRegion(corners, 0, 0));
    utassert(PtInRegion(corners, window.dx / 2, window.dy / 2));
    DeleteObject(corners);
    utassert((GetWindowLongPtrW(state.hwndPopup, GWL_STYLE) & WS_THICKFRAME) != 0);
    LRESULT hit = SendMessageW(state.hwndPopup, WM_NCHITTEST, 0, MAKELPARAM(window.Right() - 1, window.Bottom() - 1));
    utassert(hit == HTBOTTOMRIGHT);
    MINMAXINFO info{};
    SendMessageW(state.hwndPopup, WM_GETMINMAXINFO, 0, (LPARAM)&info);
    utassert(info.ptMinTrackSize.x >= DpiScale(160) && info.ptMinTrackSize.y >= DpiScale(100));
    SendMessageW(state.hwndPopup, WM_ENTERSIZEMOVE, 0, 0);
    utassert(state.resizing);
    state.displayed.destPage = 2;
    int generation = state.renderGen;
    RefHoverScheduleHide(&state, canvas, 300);
    utassert(state.displayed.destPage == 2 && state.renderGen == generation);
    SendMessageW(state.hwndPopup, WM_EXITSIZEMOVE, 0, 0);
    utassert(!state.resizing);
    DestroyWindow(state.hwndPopup);
    KillTimer(canvas, kRefHoverHideTimerID);
    DestroyWindow(canvas);
}
#endif
