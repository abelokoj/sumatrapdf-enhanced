/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include <commctrl.h>
#include <richedit.h>
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/VirtHost.h"
#include "base/Win.h"
#include "base/Timer.h"

#include "Settings.h"
#include "AppSettings.h"
#include "Theme.h"
#include "OverlayScrollbar.h"
#if IS_DEBUG
#include "base/GdiPlusUtil.h"
#include "base/tests/UtAssert.h"
#include "DarkMode.h"
#endif

constexpr const WCHAR* kOverlayScrollbarClass = L"SUMATRA_OVERLAY_SCROLLBAR";

static bool gScrollbarClassRegistered = false;

bool gOverlayScrollbarSuppressThick = false;
// if true, draw small filled triangles instead of chevrons
static bool gThickArrows = true;

// all live overlay scrollbars, for global mouse tracking
static Vec<OverlayScrollbar*> gAllScrollbars;
static UINT_PTR gMouseTrackTimer = 0;
#if IS_DEBUG
static int gScrollbarAllocations = 0;
static int gScrollbarPaints = 0;
#endif
static Point gLastMousePos = {-1, -1};
static constexpr UINT_PTR kMouseTrackTimerID = 100;
static constexpr int kMouseTrackIntervalMs = 50;
static constexpr UINT kScrollbarPaintMessage = WM_APP + 1;
static constexpr UINT_PTR kNativeScrollbarSubclass = 0x736272;
static constexpr UINT_PTR kNativeScrollbarTimer = 0x736273;
static constexpr WCHAR kNativeScrollbarProperty[] = L"SumatraAppScrollbar";
static constexpr WCHAR kNativeHScrollbarProperty[] = L"SumatraAppHScrollbar";
static void SyncNativeScrollbar(OverlayScrollbar* sb, bool force = false);
static void EraseNativeScrollEdges(HWND hwnd, HDC target = nullptr);

static Rect NativeScrollbarClip(HWND hwnd) {
    Rect clip = HwndWindowRect(hwnd);
    HWND parent = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) ? GetParent(hwnd) : nullptr;
    while (parent) {
        clip = clip.Intersect(HwndMapRectToWindow(HwndClientRect(parent), parent, nullptr));
        if (!(GetWindowLongPtrW(parent, GWL_STYLE) & WS_CHILD)) break;
        parent = GetParent(parent);
    }
    return clip;
}

static const char* kAppScrollbarCss = R"(
::-webkit-scrollbar { width: var(--scrollbar-size); height: var(--scrollbar-size); }
::-webkit-scrollbar-track, ::-webkit-scrollbar-corner { background: var(--scrollbar-track); }
::-webkit-scrollbar-thumb { background: #8b8b8b; border: clamp(1px, calc(var(--scrollbar-size) / 8), 3px) solid var(--scrollbar-track); border-radius: 999px; }
::-webkit-scrollbar-thumb:hover, ::-webkit-scrollbar-thumb:active {
  background: var(--scrollbar-hover); box-shadow: inset 0 0 0 1px var(--scrollbar-hover-edge); }
::-webkit-scrollbar-button:single-button:hover, ::-webkit-scrollbar-button:single-button:active {
  background-color: var(--scrollbar-hover); }
::-webkit-scrollbar-button:single-button { display: block; width: var(--scrollbar-size); height: var(--scrollbar-size);
  background-color: var(--scrollbar-track); background-repeat: no-repeat; background-position: center; background-size: 66.667%; }
::-webkit-scrollbar-button:vertical:decrement { background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'%3E%3Cpath fill='%238b8b8b' d='M.85 7.4L4.58 2.36Q5 1.8 5.42 2.36L9.15 7.4Q9.6 8 8.85 8H1.15Q.4 8 .85 7.4Z'/%3E%3C/svg%3E"); }
::-webkit-scrollbar-button:vertical:increment { background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'%3E%3Cpath fill='%238b8b8b' d='M.85 2.6L4.58 7.64Q5 8.2 5.42 7.64L9.15 2.6Q9.6 2 8.85 2H1.15Q.4 2 .85 2.6Z'/%3E%3C/svg%3E"); }
::-webkit-scrollbar-button:horizontal:decrement { background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'%3E%3Cpath fill='%238b8b8b' d='M7.4 .85L2.36 4.58Q1.8 5 2.36 5.42L7.4 9.15Q8 9.6 8 8.85V1.15Q8 .4 7.4 .85Z'/%3E%3C/svg%3E"); }
::-webkit-scrollbar-button:horizontal:increment { background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'%3E%3Cpath fill='%238b8b8b' d='M2.6 .85L7.64 4.58Q8.2 5 7.64 5.42L2.6 9.15Q2 9.6 2 8.85V1.15Q2 .4 2.6 .85Z'/%3E%3C/svg%3E"); }
)";

Str AppScrollbarCss() {
    return Str(kAppScrollbarCss);
}

// Derive scrollbar colors from current theme
static Color ThemeTrackColor() {
    if (ThemeUsesHighContrastColors()) return GetSysColor(COLOR_SCROLLBAR);
    Color bg = ThemeControlBackgroundColor();
    return bg;
}

static Color ThemeThumbColor() {
    if (ThemeUsesHighContrastColors()) return GetSysColor(COLOR_BTNTEXT);
    return MkRgb(139, 139, 139);
}

static Color ThemeThumbHoverColor() {
    if (ThemeUsesHighContrastColors()) return GetSysColor(COLOR_HIGHLIGHT);
    return UiScrollbarHoverColor(ThemeTrackColor());
}

TempStr AppScrollbarStyleTemp() {
    Color track = ThemeTrackColor();
    auto cssColor = [](Color color) {
        return fmt("#%02x%02x%02x", GetRValue(color), GetGValue(color), GetBValue(color));
    };
    return fmt(":root{--scrollbar-size:%dpx;--scrollbar-track:%s;--scrollbar-hover:%s;--scrollbar-hover-edge:%s;}%s",
               GetAppScrollbarWidth(96), cssColor(track), cssColor(UiScrollbarHoverColor(track)),
               cssColor(UiScrollbarHoverEdge(track)), AppScrollbarCss());
}

static constexpr int kMinThumbSize = 20;

using State = OverlayScrollbar::State;

static bool IsThick(OverlayScrollbar* sb) {
    return sb->state == State::SmartThick || sb->state == State::AlwaysThick;
}

static bool IsVisible(OverlayScrollbar* sb) {
    return sb->state == State::SmartThin || sb->state == State::SmartThick || sb->state == State::AlwaysThick;
}

// scrollbar is active: shown or auto-hidden but ready to appear
static bool IsActive(OverlayScrollbar* sb) {
    return sb->state != State::Hidden;
}

static int ScaledWidth(OverlayScrollbar* sb) {
    sb->thickWidth = GetAppScrollbarWidth(DpiGetForHwnd(sb->hwndOwner));
    sb->thinWidth = sb->thickWidth;
    return sb->thickWidth;
}

static bool IsVert(OverlayScrollbar* sb) {
    return sb->type == OverlayScrollbar::Type::Vert;
}

static Rect GetBarRect(OverlayScrollbar* sb) {
    Rect rc = HwndClientRect(sb->hwnd);
    if (sb->nativeAdapter) {
        int width = std::min(IsVert(sb) ? rc.dx : rc.dy, GetAppScrollbarWidth(DpiGetForHwnd(sb->hwndOwner)));
        if (IsVert(sb)) {
            rc.x += (rc.dx - width) / 2;
            rc.dx = width;
        } else {
            rc.y += (rc.dy - width) / 2;
            rc.dy = width;
        }
    }
    return rc;
}

// Get the track rect in client coords of the scrollbar window
static Rect GetTrackRect(OverlayScrollbar* sb) {
    Rect rc = GetBarRect(sb);
    int arrowSize = 0;
    int gap = 0;
    if (IsVisible(sb)) {
        arrowSize = IsVert(sb) ? rc.dx : rc.dy;
        gap = UiScalePxForDpi(DpiGetForHwnd(sb->hwndOwner), 2);
    }
    int total = std::min(arrowSize + gap, (IsVert(sb) ? rc.dy : rc.dx) / 2);
    if (IsVert(sb)) {
        return {rc.x, rc.y + total, rc.dx, rc.dy - (2 * total)};
    }
    return {rc.x + total, rc.y, rc.dx - (2 * total), rc.dy};
}

// Calculate thumb rect within the track
static Rect GetThumbRect(OverlayScrollbar* sb) {
    Rect track = GetTrackRect(sb);
    int range = sb->nMax - sb->nMin + 1;
    if (range <= 0 || (int)sb->nPage >= range) {
        return track;
    }

    int trackLen = IsVert(sb) ? track.dy : track.dx;
    int thumbLen = MulDiv(trackLen, (int)sb->nPage, range);
    thumbLen =
        setMinMax(thumbLen, std::min(trackLen, UiScalePxForDpi(DpiGetForHwnd(sb->hwndOwner), kMinThumbSize)), trackLen);

    int scrollableTrack = trackLen - thumbLen;
    int scrollableRange = range - (int)sb->nPage;
    int pos = sb->isDragging ? sb->nTrackPos : sb->nPos;
    int thumbOffset = 0;
    if (scrollableRange > 0) {
        thumbOffset = MulDiv(pos - sb->nMin, scrollableTrack, scrollableRange);
    }
    thumbOffset = setMinMax(thumbOffset, 0, scrollableTrack);

    if (IsVert(sb)) {
        return {track.x, track.y + thumbOffset, track.dx, thumbLen};
    }
    return {track.x + thumbOffset, track.y, thumbLen, track.dy};
}

static Rect GetArrowTopRect(OverlayScrollbar* sb) {
    Rect rc = GetBarRect(sb);
    int arrowSize = std::min(IsVert(sb) ? rc.dx : rc.dy, (IsVert(sb) ? rc.dy : rc.dx) / 2);
    if (IsVert(sb)) {
        return {rc.x, rc.y, rc.dx, arrowSize};
    }
    return {rc.x, rc.y, arrowSize, rc.dy};
}

static Rect GetArrowBottomRect(OverlayScrollbar* sb) {
    Rect rc = GetBarRect(sb);
    int arrowSize = std::min(IsVert(sb) ? rc.dx : rc.dy, (IsVert(sb) ? rc.dy : rc.dx) / 2);
    if (IsVert(sb)) {
        return {rc.x, rc.Bottom() - arrowSize, rc.dx, arrowSize};
    }
    return {rc.Right() - arrowSize, rc.y, arrowSize, rc.dy};
}

static void ScrollRichEditTo(HWND hwnd, int target) {
    SCROLLINFO info{sizeof(info), SIF_ALL};
    GetScrollInfo(hwnd, SB_VERT, &info);
    // Rich Edit lays out long documents lazily, so nMax can still describe
    // only the first part of the text. EM_LINESCROLL clamps at the last line.
    target = std::max(info.nMin, target);
    int current = (int)SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    int count = (int)SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
    int low = 0, high = std::max(0, count - 1);
    // EM_SETSCROLLPOS truncates coordinates to 16 bits. Locate the actual
    // wrapped line using 32-bit character positions, including mixed heights.
    while (low < high) {
        int middle = low + (high - low + 1) / 2;
        LRESULT character = SendMessageW(hwnd, EM_LINEINDEX, middle, 0);
        POINT pos{};
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pos, character);
        if (pos.y + info.nPos <= target)
            low = middle;
        else
            high = middle - 1;
    }
    SendMessageW(hwnd, EM_LINESCROLL, 0, low - current);
}

static void SendScrollMsg(OverlayScrollbar* sb, UINT scrollMsg, WPARAM wp) {
    if (sb->nativeAdapter) {
        sb->sendingNative = true;
        defer {
            sb->sendingNative = false;
        };
        WCHAR klass[32]{};
        GetClassNameW(sb->hwndOwner, klass, dimofi(klass));
        bool track = LOWORD(wp) == SB_THUMBTRACK || LOWORD(wp) == SB_THUMBPOSITION;
        if (track && IsVert(sb) && _wcsicmp(klass, L"RICHEDIT50W") == 0) {
            ScrollRichEditTo(sb->hwndOwner, sb->nTrackPos);
        } else if (track && IsVert(sb) && _wcsicmp(klass, L"EDIT") == 0) {
            int first = (int)SendMessageW(sb->hwndOwner, EM_GETFIRSTVISIBLELINE, 0, 0);
            SendMessageW(sb->hwndOwner, EM_LINESCROLL, 0, sb->nTrackPos - first);
        } else if (track && IsVert(sb) && _wcsicmp(klass, L"LISTBOX") == 0) {
            SendMessageW(sb->hwndOwner, LB_SETTOPINDEX, sb->nTrackPos, 0);
        } else {
            SendMessageW(sb->hwndOwner, scrollMsg, wp, 0);
        }
        SyncNativeScrollbar(sb);
        return;
    }
    SendMessageW(sb->hwndOwner, scrollMsg, wp, 0);
}

static UINT ScrollMsgForType(OverlayScrollbar* sb) {
    return IsVert(sb) ? WM_VSCROLL : WM_HSCROLL;
}

// The band the scrollbar occupies when thick, in screen coordinates. The mouse
// being in it is what turns a smart scrollbar thick, so it is always the thick
// width, whatever width the scrollbar is drawn at right now
static Rect GetScrollbarScreenRect(OverlayScrollbar* sb) {
    Rect ownerRc = HwndWindowRect(sb->hwndOwner);
    if (sb->nativeAdapter) {
        ownerRc = HwndMapRectToWindow(HwndClientRect(sb->hwndOwner), sb->hwndOwner, nullptr);
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_VSCROLL) {
            ownerRc.dx += DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_HSCROLL) {
            ownerRc.dy += DpiGetSystemMetrics(SM_CYHSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
    }
    int scrollW = ScaledWidth(sb);
    if (IsVert(sb)) {
        return {ownerRc.x + ownerRc.dx - scrollW, ownerRc.y, scrollW, ownerRc.dy};
    }
    return {ownerRc.x, ownerRc.y + ownerRc.dy - scrollW, ownerRc.dx, scrollW};
}

// Check if hwnd is the same as or an ancestor of child
static bool IsOrIsParentOf(HWND hwnd, HWND child) {
    while (child) {
        if (child == hwnd) {
            return true;
        }
        child = GetParent(child);
    }
    return false;
}

// Sit just above the owner frame, so the bar is seen but doesn't cover the
// windows floating over the canvas (the annotation property row, the command
// palette). SetWindowPos inserts *after* the window it is given, so that is
// the one directly above the frame, not the frame itself (issue #6093).
static HWND ScrollbarZOrderAfter(OverlayScrollbar* sb) {
    HWND root = sb->hwndOwner ? GetAncestor(sb->hwndOwner, GA_ROOT) : nullptr;
    if (!root) {
        return HWND_TOP;
    }
    HWND above = GetWindow(root, GW_HWNDPREV);
    // skip ourselves: we are what is directly above the frame once shown
    while (above == sb->hwnd) {
        above = GetWindow(above, GW_HWNDPREV);
    }
    return above ? above : HWND_TOP;
}

static void SetScrollbarPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
    if (SetWindowPos(hwnd, after, x, y, w, h, flags) || (flags & SWP_NOZORDER)) return;
    DWORD error = GetLastError();
    // A protected or closing window above the owner must not prevent showing our bar.
    if (error == ERROR_ACCESS_DENIED || error == ERROR_INVALID_WINDOW_HANDLE)
        SetWindowPos(hwnd, nullptr, x, y, w, h, flags | SWP_NOZORDER);
}

static void ShowScrollbarHwnd(OverlayScrollbar* sb) {
    SetScrollbarPos(sb->hwnd, ScrollbarZOrderAfter(sb), 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
}

// Cursor is over the owner canvas, not over another top-level window covering it
static bool MouseOverOwnerSurface(OverlayScrollbar* sb, Point pt) {
    Rect ownerRc = HwndWindowRect(sb->hwndOwner);
    if (!ownerRc.Contains(pt)) {
        return false;
    }
    POINT p{pt.x, pt.y};
    HWND hit = WindowFromPoint(p);
    if (!hit) {
        return false;
    }
    if (hit == sb->hwnd) {
        return true;
    }
    HWND ownerRoot = GetAncestor(sb->hwndOwner, GA_ROOT);
    HWND hitRoot = GetAncestor(hit, GA_ROOT);
    if (ownerRoot && hitRoot && hitRoot != ownerRoot) {
        return false;
    }
    return true;
}

static void FreeScrollbarSurface(OverlayScrollbar* sb) {
    if (sb->paintDc) {
        SelectObject(sb->paintDc, sb->paintOldBitmap);
        DeleteObject(sb->paintBitmap);
        DeleteDC(sb->paintDc);
    }
    sb->paintDc = nullptr;
    sb->paintBitmap = sb->paintOldBitmap = nullptr;
    sb->paintBits = nullptr;
    sb->paintSize = {};
}

static bool EnsureScrollbarSurface(OverlayScrollbar* sb, int w, int h) {
    if (sb->paintDc && sb->paintSize == Size(w, h)) return true;
    FreeScrollbarSurface(sb);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(dc);
        return false;
    }
    sb->paintOldBitmap = (HBITMAP)SelectObject(dc, bitmap);
    sb->paintDc = dc;
    sb->paintBitmap = bitmap;
    sb->paintBits = bits;
    sb->paintSize = {w, h};
#if IS_DEBUG
    gScrollbarAllocations++;
#endif
    return true;
}

enum class ArrowDirection {
    Up,
    Down,
    Left,
    Right
};

// Trim each triangle corner with a quadratic curve, keeping the glyph inside its button.
static void PaintRoundedArrow(Gdiplus::Graphics& gfx, Rect bounds, ArrowDirection direction, Gdiplus::Color color) {
    float size = std::min(bounds.dx, bounds.dy) / 3.f;
    if (size <= 0) return;
    Gdiplus::PointF points[] = {{0, -.7f}, {-1, .7f}, {1, .7f}};
    float cx = bounds.x + bounds.dx / 2.f, cy = bounds.y + bounds.dy / 2.f;
    for (auto& p : points) {
        float x = p.X, y = p.Y;
        if (direction == ArrowDirection::Down) y = -y;
        if (direction == ArrowDirection::Left) {
            x = p.Y;
            y = p.X;
        }
        if (direction == ArrowDirection::Right) {
            x = -p.Y;
            y = p.X;
        }
        p = {cx + x * size, cy + y * size};
    }
    Gdiplus::PointF entry[3], exit[3];
    float cut = size * .28f;
    for (int i = 0; i < 3; i++) {
        auto toward = [&](int other) {
            float dx = points[other].X - points[i].X, dy = points[other].Y - points[i].Y;
            float fraction = cut / sqrtf(dx * dx + dy * dy);
            return Gdiplus::PointF{points[i].X + dx * fraction, points[i].Y + dy * fraction};
        };
        entry[i] = toward((i + 2) % 3);
        exit[i] = toward((i + 1) % 3);
    }
    Gdiplus::GraphicsPath path;
    for (int i = 0; i < 3; i++) {
        auto control = [&](Gdiplus::PointF p) {
            return Gdiplus::PointF{p.X + (points[i].X - p.X) * 2.f / 3.f, p.Y + (points[i].Y - p.Y) * 2.f / 3.f};
        };
        path.AddBezier(entry[i], control(entry[i]), control(exit[i]), exit[i]);
        path.AddLine(exit[i], entry[(i + 1) % 3]);
    }
    path.CloseFigure();
    Gdiplus::SolidBrush brush(color);
    gfx.FillPath(&brush, &path);
}

// Update the layered window with the current appearance
static void PaintScrollbar(OverlayScrollbar* sb) {
    if (!sb->hwnd || !IsVisible(sb) || !HwndIsVisible(sb->hwnd)) {
        return;
    }

    Rect wrc = HwndWindowRect(sb->hwnd);
    int w = wrc.dx;
    int h = wrc.dy;
    if (w <= 0 || h <= 0) {
        return;
    }

    if (!EnsureScrollbarSurface(sb, w, h)) return;
#if IS_DEBUG
    gScrollbarPaints++;
#endif
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = sb->paintDc;
    void* bits = sb->paintBits;
    memset(bits, 0, (size_t)w * h * 4);

    u8 alpha = 255;

    auto fillRect = [&](Rect r, Color color) {
        DWORD pixel = PremultiplyPixel(color, alpha);
        DWORD* pixels = (DWORD*)bits;
        int x0 = std::max(r.x, 0);
        int y0 = std::max(r.y, 0);
        int x1 = std::min(r.x + r.dx, w);
        int y1 = std::min(r.y + r.dy, h);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                pixels[(y * w) + x] = pixel;
            }
        }
    };

    if (IsVisible(sb)) {
        fillRect(Rect(0, 0, w, h), ThemeTrackColor());
    }

    {
        Gdiplus::Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Gdiplus::Graphics gfx(&surface);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Rect thumbRc = GetThumbRect(sb);
        bool active = sb->mouseOverBar || sb->isDragging;
        Color thumbCol = active ? ThemeThumbHoverColor() : ThemeThumbColor();

        int thumbInset = UiScalePxForDpi(DpiGetForHwnd(sb->hwndOwner), 2);
        if (IsVisible(sb)) {
            if (IsVert(sb))
                thumbRc.SubLR(std::min(thumbInset, thumbRc.dx / 3), std::min(thumbInset, thumbRc.dx / 3));
            else
                thumbRc.SubTB(std::min(thumbInset, thumbRc.dy / 3), std::min(thumbInset, thumbRc.dy / 3));
        }
        if (!thumbRc.IsEmpty()) {
            int diameter = std::min(thumbRc.dx, thumbRc.dy);
            Gdiplus::GraphicsPath path;
            path.AddArc(thumbRc.x, thumbRc.y, diameter, diameter, 180, 90);
            path.AddArc(thumbRc.Right() - diameter, thumbRc.y, diameter, diameter, 270, 90);
            path.AddArc(thumbRc.Right() - diameter, thumbRc.Bottom() - diameter, diameter, diameter, 0, 90);
            path.AddArc(thumbRc.x, thumbRc.Bottom() - diameter, diameter, diameter, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush thumbBrush(
                Gdiplus::Color(alpha, GetRValue(thumbCol), GetGValue(thumbCol), GetBValue(thumbCol)));
            gfx.FillPath(&thumbBrush, &path);
            if (active && !ThemeUsesHighContrastColors()) {
                Color edge = UiScrollbarHoverEdge(ThemeTrackColor());
                Gdiplus::Pen pen(Gdiplus::Color(alpha, GetRValue(edge), GetGValue(edge), GetBValue(edge)), 1.0f);
                gfx.DrawPath(&pen, &path);
            }
        }

        if (IsVisible(sb)) {
            Color arrowCol = active ? ThemeThumbHoverColor() : ThemeThumbColor();
            u8 ar = (u8)MulDiv(GetRValue(arrowCol), alpha, 255);
            u8 ag = (u8)MulDiv(GetGValue(arrowCol), alpha, 255);
            u8 ab = (u8)MulDiv(GetBValue(arrowCol), alpha, 255);
            Gdiplus::Color gdipArrowCol(alpha, ar, ag, ab);

            Rect arrowTop = GetArrowTopRect(sb);
            Rect arrowBot = GetArrowBottomRect(sb);

            if (gThickArrows) {
                PaintRoundedArrow(gfx, arrowTop, IsVert(sb) ? ArrowDirection::Up : ArrowDirection::Left, gdipArrowCol);
                PaintRoundedArrow(gfx, arrowBot, IsVert(sb) ? ArrowDirection::Down : ArrowDirection::Right,
                                  gdipArrowCol);
            } else {
                // chevron lines
                Gdiplus::Pen pen(gdipArrowCol, std::max(1.f, std::min(arrowTop.dx, arrowTop.dy) / 10.f));
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                int inset = IsVert(sb) ? arrowTop.dx / 5 : arrowTop.dy / 5;

                if (IsVert(sb)) {
                    int sz = arrowTop.dx / 5;
                    float cx = (float)(arrowTop.x + (arrowTop.dx / 2));
                    float cy = (float)(arrowTop.y + (arrowTop.dy / 2)) + ((float)inset / 2);
                    Gdiplus::PointF upPts[3] = {
                        {cx - (float)sz, cy + ((float)sz / 2.0f)},
                        {cx, cy - ((float)sz / 2.0f)},
                        {cx + (float)sz, cy + ((float)sz / 2.0f)},
                    };
                    gfx.DrawLines(&pen, upPts, 3);

                    cx = (float)(arrowBot.x + (arrowBot.dx / 2));
                    cy = (float)(arrowBot.y + (arrowBot.dy / 2)) - ((float)inset / 2);
                    Gdiplus::PointF downPts[3] = {
                        {cx - (float)sz, cy - ((float)sz / 2.0f)},
                        {cx, cy + ((float)sz / 2.0f)},
                        {cx + (float)sz, cy - ((float)sz / 2.0f)},
                    };
                    gfx.DrawLines(&pen, downPts, 3);
                } else {
                    int sz = arrowTop.dy / 5;
                    float cx = (float)(arrowTop.x + (arrowTop.dx / 2)) + ((float)inset / 2);
                    float cy = (float)(arrowTop.y + (arrowTop.dy / 2));
                    Gdiplus::PointF leftPts[3] = {
                        {cx + ((float)sz / 2.0f), cy - (float)sz},
                        {cx - ((float)sz / 2.0f), cy},
                        {cx + ((float)sz / 2.0f), cy + (float)sz},
                    };
                    gfx.DrawLines(&pen, leftPts, 3);

                    cx = (float)(arrowBot.x + (arrowBot.dx / 2)) - ((float)inset / 2);
                    cy = (float)(arrowBot.y + (arrowBot.dy / 2));
                    Gdiplus::PointF rightPts[3] = {
                        {cx - ((float)sz / 2.0f), cy - (float)sz},
                        {cx + ((float)sz / 2.0f), cy},
                        {cx - ((float)sz / 2.0f), cy + (float)sz},
                    };
                    gfx.DrawLines(&pen, rightPts, 3);
                }
            }
        }
    }

    POINT ptSrc = {0, 0};
    SIZE szWnd = {w, h};
    POINT ptDst = {wrc.x, wrc.y};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(sb->hwnd, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    ReleaseDC(nullptr, hdcScreen);
}

// Make the layered window fully transparent without hiding it.
// ShowWindow(SW_HIDE) can steal activation from other windows (e.g. command palette).
static void MakeLayeredWindowTransparent(HWND hwnd) {
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 0;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
}

static void SetState(OverlayScrollbar* sb, State newState) {
    if (sb->state == newState) {
        return;
    }
    bool wasVisible = IsVisible(sb);
    sb->state = newState;
    bool nowVisible = IsVisible(sb);

    OverlayScrollbarUpdatePos(sb);
    if (nowVisible) {
        if (!wasVisible) {
            ShowScrollbarHwnd(sb);
        }
        PaintScrollbar(sb);
    } else {
        sb->mouseOverThumb = false;
        if (newState == State::Hidden) {
            OverlayScrollbarHide(sb);
        } else {
            // Smart auto-hide keeps the active layered window available for mouse proximity.
            MakeLayeredWindowTransparent(sb->hwnd);
        }
    }

    if (wasVisible != nowVisible) {
        for (auto* other : gAllScrollbars) {
            if (other == sb || other->hwndOwner != sb->hwndOwner || !IsVisible(other)) continue;
            OverlayScrollbarUpdatePos(other);
            PaintScrollbar(other);
        }
    }

    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    if (newState == State::SmartThin) {
        SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showAfterScrollMs, nullptr);
    }
}

static void ShowScrollbarWindow(OverlayScrollbar* sb, bool thick) {
    // Don't revert to thin while user is dragging the thumb
    if (sb->isDragging && !thick) {
        return;
    }
    if (sb->mode == OverlayScrollbar::Mode::Thick) {
        SetState(sb, State::AlwaysThick);
    } else {
        SetState(sb, thick ? State::SmartThick : State::SmartThin);
    }
}

static void HideScrollbarWindow(OverlayScrollbar* sb) {
    // Don't hide while user is dragging the thumb
    if (sb->isDragging) {
        return;
    }
    if (sb->mode == OverlayScrollbar::Mode::Thick) {
        return; // never hide in Thick mode
    }
    SetState(sb, State::SmartInvisible);
}

// Restart the thin-bar auto-hide countdown (showAfterScrollMs). SetState only
// arms the timer on a state transition, so continuous scroll while already
// SmartThin would otherwise let the earlier mouse-stop / first-reveal timer
// fire and hide the bar mid-scroll.
static void RestartSmartThinAutoHide(OverlayScrollbar* sb) {
    if (!sb->hwnd || sb->state != State::SmartThin) {
        return;
    }
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
    SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->showAfterScrollMs, nullptr);
}

// ---- Global mouse tracking ----

static void CALLBACK MouseTrackTimerProc(HWND /*hwnd*/, UINT /*msg*/, UINT_PTR /*idEvent*/, DWORD /*time*/) {
    // e.g. splitter drag uses SetCapture(); don't react to cursor proximity then
    if (GetCapture()) {
        return;
    }

    Point pt = GetCursorPosition();

    bool mouseMoved = (pt.x != gLastMousePos.x || pt.y != gLastMousePos.y);
    gLastMousePos = pt;

    HWND hwndForeground = GetForegroundWindow();

    for (auto* sb : gAllScrollbars) {
        if (!sb->hwnd || !sb->hwndOwner) {
            continue;
        }

        if (!IsActive(sb)) {
            continue;
        }

        // Only process scrollbars whose owner is in the active window hierarchy
        bool ownerActive = IsOrIsParentOf(hwndForeground, sb->hwndOwner);
        if (!ownerActive) {
            // If we were showing, hide
            if (IsVisible(sb)) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
            }
            continue;
        }

        // Check if mouse is over the owner window's client area, and not over a
        // different top-level window covering it (edit annotations, etc.)
        bool overOwner = MouseOverOwnerSurface(sb, pt);

        // Is the mouse over the band the thick scrollbar occupies? Being merely
        // near it isn't enough: the thick bar used to pop out while the mouse
        // was still over the page, which is distracting while reading
        Rect sbRect = GetScrollbarScreenRect(sb);
        bool overScrollbar = sbRect.Contains(pt) && overOwner;

        if (sb->mouseOverBar != overScrollbar) {
            sb->mouseOverBar = overScrollbar;
            if (!overScrollbar) sb->mouseOverThumb = false;
            PaintScrollbar(sb);
        }

        if (sb->isDragging) {
            // Don't change state while dragging
            continue;
        }

        if (gOverlayScrollbarSuppressThick) {
            continue;
        }

        if (overScrollbar) {
            // Mouse is over the scrollbar area - show thick
            if (!IsThick(sb)) {
                ShowScrollbarWindow(sb, true);
            }
            // Update thumb hover state
            Point clientPt = HwndScreenToClient(sb->hwnd, pt);
            Rect thumbRc = GetThumbRect(sb);
            bool wasOver = sb->mouseOverThumb;
            sb->mouseOverThumb = thumbRc.Contains(clientPt);
            if (wasOver != sb->mouseOverThumb) {
                PaintScrollbar(sb);
            }
        } else if (overOwner && mouseMoved) {
            // Mouse is over owner and moving, but not over the scrollbar - show thin
            // IsThick() means transitioning from thick to thin
            if (IsThick(sb) || sb->state != State::SmartThin) {
                ShowScrollbarWindow(sb, false);
            }
            // Reset the auto-hide timer since mouse is moving
            KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
            SetTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide, sb->hideAfterMouseStopMs, nullptr);
        } else if (IsThick(sb) && !overOwner) {
            // Mouse left the owner area while thick - transition to hidden
            HideScrollbarWindow(sb);
        }
        // If mouse is over owner but not moving, the existing auto-hide timer handles it
    }
}

static void StartMouseTracking() {
    if (gMouseTrackTimer) {
        return;
    }
    gLastMousePos = {-1, -1};
    gMouseTrackTimer = SetTimer(nullptr, kMouseTrackTimerID, kMouseTrackIntervalMs, MouseTrackTimerProc);
}

static void StopMouseTracking() {
    if (gMouseTrackTimer) {
        KillTimer(nullptr, gMouseTrackTimer);
        gMouseTrackTimer = 0;
    }
}

// ---- WndProc for scrollbar window (handles clicks, drag, wheel) ----

static void RequestScrollbarPaint(OverlayScrollbar* sb) {
    if (!sb || !sb->hwnd || sb->paintPending) return;
    sb->paintPending = true;
    if (!PostMessageW(sb->hwnd, kScrollbarPaintMessage, 0, 0)) {
        sb->paintPending = false;
        PaintScrollbar(sb);
    }
}

static void FinishScrollbarInput(OverlayScrollbar* sb) {
    sb->repeatScrollCode = SB_ENDSCROLL;
    KillTimer(sb->hwnd, OverlayScrollbar::kTimerRepeatScroll);
    if (!sb->isDragging) return;
    sb->isDragging = false;
    sb->nPos = sb->nTrackPos;
    RequestScrollbarPaint(sb);
    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBPOSITION, sb->nPos));
}

static LRESULT CALLBACK WndProcOverlayScrollbar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayScrollbar* sb = (OverlayScrollbar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!sb) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    switch (msg) {
        case kScrollbarPaintMessage: {
            if (!sb->paintPending) return 0;
            sb->paintPending = false;
            PaintScrollbar(sb);
            return 0;
        }
        case WM_TIMER:
            if (wp == OverlayScrollbar::kTimerAutoHide) {
                if (!sb->isDragging) {
                    HideScrollbarWindow(sb);
                }
                return 0;
            }
            if (wp == OverlayScrollbar::kTimerRepeatScroll) {
                if (sb->repeatScrollCode == SB_ENDSCROLL) {
                    KillTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll);
                    return 0;
                }
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(sb->repeatScrollCode, 0));
                if (sb->repeatIsInitial) {
                    // switch from initial delay to repeat rate
                    sb->repeatIsInitial = false;
                    UINT repeatMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &repeatMs, 0);
                    // SPI_GETKEYBOARDSPEED returns 0-31, map to ~33-500ms (same as OS key repeat)
                    repeatMs = 400 - (repeatMs * 12);
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, repeatMs, nullptr);
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            if (!sb->mouseOverBar) {
                sb->mouseOverBar = true;
                if (sb->nativeAdapter) EraseNativeScrollEdges(sb->hwndOwner);
                PaintScrollbar(sb);
            }

            if (sb->isDragging) {
                int ptInTrack = IsVert(sb) ? my : mx;
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                Rect thumb = GetThumbRect(sb);
                int thumbLen = IsVert(sb) ? thumb.dy : thumb.dx;
                int trackLen = IsVert(sb) ? track.dy : track.dx;
                int scrollableTrack = trackLen - thumbLen;
                int scrollableRange = range - (int)sb->nPage;

                int dragDelta = ptInTrack - sb->dragStartY;
                int newPos = sb->dragStartPos;
                if (scrollableTrack > 0 && scrollableRange > 0) {
                    newPos = sb->dragStartPos + MulDiv(dragDelta, scrollableRange, scrollableTrack);
                }
                newPos = setMinMax(newPos, sb->nMin, sb->nMax - (int)sb->nPage + 1);
                if (newPos == sb->nTrackPos) return 0;
                sb->nTrackPos = newPos;
                RequestScrollbarPaint(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBTRACK, newPos));
                return 0;
            }

            // Also update hover immediately when the cursor enters the bar.
            if (IsVisible(sb)) {
                Rect thumbRc = GetThumbRect(sb);
                bool wasOver = sb->mouseOverThumb;
                sb->mouseOverThumb = thumbRc.Contains(Point(mx, my));
                if (wasOver != sb->mouseOverThumb) {
                    PaintScrollbar(sb);
                }
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            sb->mouseOverBar = false;
            sb->mouseOverThumb = false;
            if (sb->nativeAdapter) EraseNativeScrollEdges(sb->hwndOwner);
            PaintScrollbar(sb);
            return 0;

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lp);
            int my = GET_Y_LPARAM(lp);
            SetCapture(hwnd);

            if (IsVisible(sb)) {
                Rect arrowTop = GetArrowTopRect(sb);
                Rect arrowBot = GetArrowBottomRect(sb);
                Point pt(mx, my);

                if (arrowTop.Contains(pt)) {
                    // SB_LINEUP == SB_LINELEFT, but spell out which axis we mean
                    UINT code = IsVert(sb) ? SB_LINEUP : SB_LINELEFT; // NOLINT(bugprone-branch-clone)
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    sb->repeatScrollCode = code;
                    sb->repeatIsInitial = true;
                    UINT delayMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                    delayMs = 250 + (delayMs * 250); // 0-3 maps to 250-1000ms
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                    return 0;
                }
                if (arrowBot.Contains(pt)) {
                    // SB_LINEDOWN == SB_LINERIGHT, but spell out which axis we mean
                    UINT code = IsVert(sb) ? SB_LINEDOWN : SB_LINERIGHT; // NOLINT(bugprone-branch-clone)
                    SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                    sb->repeatScrollCode = code;
                    sb->repeatIsInitial = true;
                    UINT delayMs = 0;
                    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                    delayMs = 250 + (delayMs * 250);
                    SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                    return 0;
                }
            }

            // Shift+click: jump thumb center to click position
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                Rect track = GetTrackRect(sb);
                int range = sb->nMax - sb->nMin + 1;
                int trackLen = IsVert(sb) ? track.dy : track.dx;
                Rect thumb = GetThumbRect(sb);
                int thumbLen = IsVert(sb) ? thumb.dy : thumb.dx;
                int scrollableTrack = trackLen - thumbLen;
                int scrollableRange = range - (int)sb->nPage;
                int clickInTrack = (IsVert(sb) ? my : mx) - (IsVert(sb) ? track.y : track.x);
                int thumbOffset = clickInTrack - (thumbLen / 2);
                thumbOffset = setMinMax(thumbOffset, 0, scrollableTrack);
                int newPos = sb->nMin;
                if (scrollableTrack > 0 && scrollableRange > 0) {
                    newPos = sb->nMin + MulDiv(thumbOffset, scrollableRange, scrollableTrack);
                }
                sb->nTrackPos = newPos;
                PaintScrollbar(sb);
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(SB_THUMBTRACK, newPos));
                ReleaseCapture();
                return 0;
            }

            Rect thumbRc = GetThumbRect(sb);
            Point pt(mx, my);
            if (thumbRc.Contains(pt)) {
                sb->isDragging = true;
                sb->dragStartY = IsVert(sb) ? my : mx;
                sb->dragStartPos = sb->nPos;
                sb->nTrackPos = sb->nPos;
                return 0;
            }

            Rect track = GetTrackRect(sb);
            if (track.Contains(pt)) {
                int clickPos = IsVert(sb) ? my : mx;
                int thumbMid = IsVert(sb) ? (thumbRc.y + (thumbRc.dy / 2)) : (thumbRc.x + (thumbRc.dx / 2));
                UINT code;
                if (clickPos < thumbMid) {
                    // SB_PAGEUP == SB_PAGELEFT (same for DOWN/RIGHT); spell out the axis
                    code = IsVert(sb) ? SB_PAGEUP : SB_PAGELEFT; // NOLINT(bugprone-branch-clone)
                } else {
                    code = IsVert(sb) ? SB_PAGEDOWN : SB_PAGERIGHT; // NOLINT(bugprone-branch-clone)
                }
                SendScrollMsg(sb, ScrollMsgForType(sb), MAKEWPARAM(code, 0));
                sb->repeatScrollCode = code;
                sb->repeatIsInitial = true;
                UINT delayMs = 0;
                SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delayMs, 0);
                delayMs = 250 + (delayMs * 250);
                SetTimer(hwnd, OverlayScrollbar::kTimerRepeatScroll, delayMs, nullptr);
                return 0;
            }
            ReleaseCapture();
            return 0;
        }

        case WM_CAPTURECHANGED:
            FinishScrollbarInput(sb);
            return 0;

        case WM_CANCELMODE:
        case WM_LBUTTONUP:
            FinishScrollbarInput(sb);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            SendMessageW(sb->hwndOwner, msg, wp, lp);
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_NCHITTEST: {
            // pass through rightmost 2px of vertical scrollbar for frame resize
            if (IsVert(sb)) {
                int x = GET_X_LPARAM(lp);
                Rect rc = HwndWindowRect(hwnd);
                if ((rc.x + rc.dx - x) <= 2) {
                    return HTTRANSPARENT;
                }
            }
            LRESULT def = DefWindowProcW(hwnd, msg, wp, lp);
            if (def == HTNOWHERE) {
                return HTCLIENT;
            }
            return def;
        }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterScrollbarClass() {
    if (gScrollbarClassRegistered) {
        return;
    }
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProcOverlayScrollbar;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = GetCachedCursor(IDC_ARROW);
    wcex.lpszClassName = kOverlayScrollbarClass;
    RegisterClassExW(&wcex);
    gScrollbarClassRegistered = true;
}

OverlayScrollbar* OverlayScrollbarCreate(HWND hwndOwner, OverlayScrollbar::Type type, OverlayScrollbar::Mode mode) {
    RegisterScrollbarClass();

    auto* sb = new OverlayScrollbar();
    sb->hwndOwner = hwndOwner;
    sb->type = type;
    sb->mode = mode;
    ScaledWidth(sb);
    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    DWORD style = WS_POPUP;

    // use the top-level ancestor as owner so the scrollbar stays above its
    // own window but doesn't cover other application windows
    HWND hwndTopLevel = GetAncestor(hwndOwner, GA_ROOT);
    sb->hwnd = CreateWindowExW(exStyle, kOverlayScrollbarClass, nullptr, style, 0, 0, 1, 1, hwndTopLevel, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(sb->hwnd, GWLP_USERDATA, (LONG_PTR)sb);

    // Register for global mouse tracking
    VecAppend(gAllScrollbars, sb);
    StartMouseTracking();

    return sb;
}

void OverlayScrollbarDestroy(OverlayScrollbar* sb) {
    if (!sb) {
        return;
    }

    // Unregister from global mouse tracking
    VecRemove(gAllScrollbars, sb);
    if (len(gAllScrollbars) == 0) {
        StopMouseTracking();
    }

    if (sb->hwnd) {
        KillTimer(sb->hwnd, OverlayScrollbar::kTimerAutoHide);
        DestroyWindow(sb->hwnd);
    }
    FreeScrollbarSurface(sb);
    delete sb;
}

// Same API as SetScrollInfo / GetScrollInfo
void OverlayScrollbarSetInfo(OverlayScrollbar* sb, const SCROLLINFO* si, bool redraw) {
    if (!sb) {
        return;
    }
    bool changed = false;
    if (si->fMask & SIF_RANGE) {
        if (sb->nMin != si->nMin || sb->nMax != si->nMax) {
            changed = true;
        }
        sb->nMin = si->nMin;
        sb->nMax = si->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        if (sb->nPage != si->nPage) {
            changed = true;
        }
        sb->nPage = si->nPage;
    }
    if (si->fMask & SIF_POS) {
        if (sb->nPos != si->nPos) {
            changed = true;
        }
        sb->nPos = si->nPos;
    }

    if (redraw && changed) {
        // Scroll moved: re-reveal the thin smart bar if auto-hidden, repaint the
        // thumb, and keep the auto-hide timer alive while scrolling continues
        // (wheel / keyboard / smooth-scroll ticks — not only mouse motion).
        if (IsVisible(sb)) {
            // Merge bursts of wheel/drag input into one paint of the latest position.
            RequestScrollbarPaint(sb);
            if (sb->state == State::SmartThin) {
                RestartSmartThinAutoHide(sb);
            }
        } else {
            ShowScrollbarWindow(sb, false);
        }
    }
}

// Show the thin smart overlay after scroll activity (mouse wheel, keys, etc.).
// Unlike mouse-move tracking, this does not require cursor motion (#5859).
void OverlayScrollbarNotifyScroll(OverlayScrollbar* sb) {
    if (!sb || !IsActive(sb) || sb->isDragging) {
        return;
    }
    if (sb->mode == OverlayScrollbar::Mode::Thick) {
        return;
    }
    // Leave thick-from-proximity alone; only (re)show the thin indicator.
    if (IsThick(sb)) {
        return;
    }
    if (sb->state != State::SmartThin) {
        ShowScrollbarWindow(sb, false);
    } else {
        RestartSmartThinAutoHide(sb);
    }
}

void OverlayScrollbarGetInfo(OverlayScrollbar* sb, SCROLLINFO* si) {
    if (!sb) {
        return;
    }
    if (si->fMask & SIF_RANGE) {
        si->nMin = sb->nMin;
        si->nMax = sb->nMax;
    }
    if (si->fMask & SIF_PAGE) {
        si->nPage = sb->nPage;
    }
    if (si->fMask & SIF_POS) {
        si->nPos = sb->nPos;
    }
    if (si->fMask & SIF_TRACKPOS) {
        si->nTrackPos = sb->nTrackPos;
    }
}

// Call when owner window moves/resizes
void OverlayScrollbarUpdatePos(OverlayScrollbar* sb) {
    if (!sb || !sb->hwnd || !sb->hwndOwner) {
        return;
    }

    Rect ownerRc = HwndWindowRect(sb->hwndOwner);
    Rect ownerWindow = ownerRc;
    if (sb->nativeAdapter) {
        ownerRc = HwndMapRectToWindow(HwndClientRect(sb->hwndOwner), sb->hwndOwner, nullptr);
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_VSCROLL) {
            ownerRc.dx += DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
        if (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & WS_HSCROLL) {
            ownerRc.dy += DpiGetSystemMetrics(SM_CYHSCROLL, DpiGetForHwnd(sb->hwndOwner));
        }
    }

    int scrollW = ScaledWidth(sb);
    int x, y, w, h;

    // Keep visible bars from covering each other's end arrows.
    bool siblingVisible = false;
    for (auto* other : gAllScrollbars) {
        if (other != sb && other->hwndOwner == sb->hwndOwner && IsVisible(other)) {
            siblingVisible = true;
            break;
        }
    }
    int siblingInset = 0;
    if (IsVisible(sb) && siblingVisible) {
        siblingInset = scrollW;
    }

    if (IsVert(sb)) {
        x = ownerRc.x + ownerRc.dx - scrollW;
        y = ownerRc.y;
        w = scrollW;
        h = ownerRc.dy - siblingInset;
    } else {
        x = ownerRc.x;
        y = ownerRc.y + ownerRc.dy - scrollW;
        w = ownerRc.dx - siblingInset;
        h = scrollW;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE);
    if (IsVisible(sb)) {
        exStyle &= ~WS_EX_TRANSPARENT;
    } else {
        exStyle |= WS_EX_TRANSPARENT;
    }
    if (exStyle != GetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE)) SetWindowLongPtrW(sb->hwnd, GWL_EXSTYLE, exStyle);

    // SWP_NOOWNERZORDER: raising an owned popup otherwise raises the owner
    // frame over other top-level windows (command palette, annotations).
    UINT swpFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    HWND insertAfter = nullptr;
    // re-show the window if the state says it should be visible
    // (RelayoutFrame hides overlay scrollbar windows with SW_HIDE
    // to prevent them from appearing at stale positions)
    if (IsVisible(sb) && !HwndIsVisible(sb->hwnd)) {
        swpFlags |= SWP_SHOWWINDOW;
        insertAfter = ScrollbarZOrderAfter(sb);
    } else {
        swpFlags |= SWP_NOZORDER;
    }
    Rect previousBounds = HwndWindowRect(sb->hwnd);
    if (!sb->nativeAdapter || previousBounds != Rect{x, y, w, h} || (swpFlags & SWP_SHOWWINDOW)) {
        SetScrollbarPos(sb->hwnd, insertAfter, x, y, w, h, swpFlags);
    }
    if (sb->nativeAdapter) {
        Rect visible = ownerRc.Intersect(NativeScrollbarClip(sb->hwndOwner));
        Rect stripe{x, y, w, h};
        visible = visible.Intersect(stripe);
        HRGN region = CreateRectRgn(visible.x - x, visible.y - y, visible.Right() - x, visible.Bottom() - y);
        HRGN ownerRegion = CreateRectRgn(0, 0, 0, 0);
        if (GetWindowRgn(sb->hwndOwner, ownerRegion) != ERROR) {
            OffsetRgn(ownerRegion, ownerWindow.x - x, ownerWindow.y - y);
            CombineRgn(region, region, ownerRegion, RGN_AND);
        }
        DeleteObject(ownerRegion);
        HRGN previous = CreateRectRgn(0, 0, 0, 0);
        bool same = GetWindowRgn(sb->hwnd, previous) != ERROR && EqualRgn(previous, region);
        DeleteObject(previous);
        if (same || !SetWindowRgn(sb->hwnd, region, true)) DeleteObject(region);
    }
}

// Hide the scrollbar window without stealing activation from other windows.
// Uses SWP_HIDEWINDOW | SWP_NOACTIVATE instead of ShowWindow(SW_HIDE).
void OverlayScrollbarHide(OverlayScrollbar* sb) {
    if (!sb || !sb->hwnd) {
        return;
    }
    SetWindowPos(sb->hwnd, nullptr, 0, 0, 0, 0,
                 SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

// Show/hide
void OverlayScrollbarShow(OverlayScrollbar* sb, bool show) {
    if (!sb) {
        return;
    }
    if (!show) {
        if (!IsActive(sb)) {
            return;
        }
        SetState(sb, State::Hidden);
        return;
    }

    // Already painted thin/thick — nothing to do. SmartInvisible still has an
    // IsWindowVisible layered HWND (fully transparent), so do not treat that as
    // shown: keyboard scroll / UpdateScrollbars must re-reveal it (#5850).
    if (IsVisible(sb) && HwndIsVisible(sb->hwnd)) {
        return;
    }
    if (!IsActive(sb) || sb->state == State::SmartInvisible) {
        ShowScrollbarWindow(sb, false);
        return;
    }
    // re-show if window was temporarily hidden (e.g. during relayout)
    OverlayScrollbarUpdatePos(sb);
    ShowScrollbarHwnd(sb);
    PaintScrollbar(sb);
}

// Change the scrollbar mode (Smart vs Thick)
void OverlayScrollbarSetMode(OverlayScrollbar* sb, OverlayScrollbar::Mode mode) {
    if (!sb || sb->mode == mode) {
        return;
    }
    sb->mode = mode;
    if (!IsActive(sb)) {
        return;
    }
    // transition to the appropriate state for the new mode
    if (mode == OverlayScrollbar::Mode::Thick) {
        SetState(sb, State::AlwaysThick);
    } else {
        // Smart mode: start as thin, will auto-hide
        SetState(sb, State::SmartThin);
    }
}

// returns true if scrollbar is visible (thin, thick, or always thick)
bool IsOverlayScrollbarVisible(OverlayScrollbar* sb) {
    return sb && IsVisible(sb);
}

int AppScrollbarTrackPos(HWND hwnd, int fallback, int bar) {
    auto* sb = (OverlayScrollbar*)GetPropW(hwnd, bar == SB_HORZ ? kNativeHScrollbarProperty : kNativeScrollbarProperty);
    return sb && (sb->isDragging || sb->sendingNative) ? sb->nTrackPos : fallback;
}

int AppScrollbarInset(HWND hwnd) {
    if (!GetPropW(hwnd, kNativeScrollbarProperty)) return 0;
    int dpi = DpiGetForHwnd(hwnd);
    int native = DpiGetSystemMetrics(SM_CXVSCROLL, dpi);
    return std::max(0, GetAppScrollbarWidth(dpi) - native);
}

#if IS_DEBUG
static int nativeScrollbarRefreshCount = 0;
#endif

static void SyncNativeScrollbar(OverlayScrollbar* sb, bool force) {
    if (sb->syncingNative) return;
    sb->syncingNative = true;
    defer {
        sb->syncingNative = false;
    };
    SCROLLINFO info{sizeof(info), SIF_ALL};
    int bar = IsVert(sb) ? SB_VERT : SB_HORZ;
    LONG_PTR style = IsVert(sb) ? WS_VSCROLL : WS_HSCROLL;
    bool range = GetScrollInfo(sb->hwndOwner, bar, &info) && info.nMax - info.nMin + 1 > (int)info.nPage;
    bool show = range && HwndIsVisible(sb->hwndOwner) && (GetWindowLongPtrW(sb->hwndOwner, GWL_STYLE) & style);
    Rect bounds = HwndWindowRect(sb->hwndOwner);
    Rect clip = NativeScrollbarClip(sb->hwndOwner);
    int width = GetAppScrollbarWidth(DpiGetForHwnd(sb->hwndOwner));
    Color track = ThemeTrackColor(), thumb = ThemeThumbColor();
    const SCROLLINFO& before = sb->nativeInfo;
    if (!force && sb->nativeSyncValid && before.nMin == info.nMin && before.nMax == info.nMax &&
        before.nPage == info.nPage && before.nPos == info.nPos && sb->nativeBounds == bounds &&
        sb->nativeClip == clip && sb->nativeWidth == width && sb->nativeShown == show && sb->nativeTrack == track &&
        sb->nativeThumb == thumb) {
        return;
    }
#if IS_DEBUG
    nativeScrollbarRefreshCount++;
#endif
    bool repaint = sb->nativeSyncValid && (sb->nativeTrack != track || sb->nativeThumb != thumb ||
                                           sb->nativeWidth != width || sb->nativeBounds.Size() != bounds.Size());
    sb->nativeSyncValid = true;
    sb->nativeInfo = info;
    sb->nativeBounds = bounds;
    sb->nativeClip = clip;
    sb->nativeWidth = width;
    sb->nativeShown = show;
    sb->nativeTrack = track;
    sb->nativeThumb = thumb;
    OverlayScrollbarSetInfo(sb, &info, show);
    OverlayScrollbarShow(sb, show);
    if (show) {
        OverlayScrollbarUpdatePos(sb);
        if (repaint) PaintScrollbar(sb);
    }
}

static void EraseNativeScrollEdges(HWND hwnd, HDC target) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(style & (WS_VSCROLL | WS_HSCROLL)) || (!target && !IsWindowVisible(hwnd))) return;
    int dpi = DpiGetForHwnd(hwnd);
    int width = GetAppScrollbarWidth(dpi);
    int nativeV = DpiGetSystemMetrics(SM_CXVSCROLL, dpi);
    int nativeH = DpiGetSystemMetrics(SM_CYHSCROLL, dpi);

    Rect client = HwndMapRectToWindow(HwndClientRect(hwnd), hwnd, nullptr);
    Rect window = HwndWindowRect(hwnd);
    client.Offset(-window.x, -window.y);
    HDC dc = target ? target : GetWindowDC(hwnd);
    if (!dc) return;
    HBRUSH brush = CreateSolidBrush(ThemeTrackColor());
    if (style & WS_VSCROLL) {
        int left = client.Right() - std::max(0, width - nativeV) - 1;
        RECT stripe = ToRECT(Rect{left, client.y, std::abs(nativeV - width) + 1, client.dy});
        FillRect(dc, &stripe, brush);
    }
    if (style & WS_HSCROLL) {
        int top = client.Bottom() - std::max(0, width - nativeH) - 1;
        RECT stripe = ToRECT(Rect{client.x, top, client.dx, std::abs(nativeH - width) + 1});
        FillRect(dc, &stripe, brush);
    }
    DeleteObject(brush);
    if (!target) ReleaseDC(hwnd, dc);
}

static void SyncNativeScrollbars(HWND hwnd, bool force = false) {
    auto* vertical = (OverlayScrollbar*)GetPropW(hwnd, kNativeScrollbarProperty);
    if (vertical) SyncNativeScrollbar(vertical, force);
    auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
    if (!horizontal && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_HSCROLL)) {
        horizontal = OverlayScrollbarCreate(hwnd, OverlayScrollbar::Type::Horz, OverlayScrollbar::Mode::Thick);
        horizontal->nativeAdapter = true;
        if (!horizontal->hwnd || !SetPropW(hwnd, kNativeHScrollbarProperty, horizontal)) {
            OverlayScrollbarDestroy(horizontal);
            horizontal = nullptr;
        }
    }
    if (horizontal) SyncNativeScrollbar(horizontal, force);
    if (vertical && horizontal) {
        OverlayScrollbar* bars[] = {vertical, horizontal};
        for (auto* sb : bars) {
            if (!IsVisible(sb)) continue;
            Size previous = HwndWindowRect(sb->hwnd).Size();
            OverlayScrollbarUpdatePos(sb);
            if (previous != HwndWindowRect(sb->hwnd).Size()) PaintScrollbar(sb);
        }
    }
}

static LRESULT CALLBACK NativeScrollbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto* sb = (OverlayScrollbar*)data;
    if (msg == WM_NCDESTROY) {
        KillTimer(hwnd, kNativeScrollbarTimer);
        RemovePropW(hwnd, kNativeScrollbarProperty);
        auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
        RemovePropW(hwnd, kNativeHScrollbarProperty);
        RemoveWindowSubclass(hwnd, NativeScrollbarProc, id);
        OverlayScrollbarDestroy(sb);
        OverlayScrollbarDestroy(horizontal);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_TIMER && wp == kNativeScrollbarTimer) {
        auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
        if (!HwndIsVisible(hwnd) && !sb->nativeShown && (!horizontal || !horizontal->nativeShown)) return 0;
        SyncNativeScrollbars(hwnd);
        return 0;
    }
    LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    if ((OverlayScrollbar*)GetPropW(hwnd, kNativeScrollbarProperty) != sb) return result;
    if (msg == WM_SIZE || msg == WM_SETTEXT || msg == WM_VSCROLL || msg == WM_HSCROLL || msg == WM_MOUSEWHEEL ||
        msg == WM_MOUSEHWHEEL || msg == WM_KEYDOWN || msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW ||
        msg == WM_NCPAINT || msg == EM_SETSCROLLPOS || msg == LB_SETTOPINDEX || msg == EM_LINESCROLL ||
        msg == WM_STYLECHANGED) {
        SyncNativeScrollbars(hwnd);
        EraseNativeScrollEdges(hwnd);
    }
    if (msg == WM_PAINT || msg == WM_NCMOUSEMOVE || msg == WM_NCMOUSELEAVE) EraseNativeScrollEdges(hwnd);
    return result;
}

void InstallAppScrollbar(HWND hwnd) {
    if (!hwnd) return;
    if (GetPropW(hwnd, kNativeScrollbarProperty)) {
        SyncNativeScrollbars(hwnd);
        return;
    }
    auto* sb = OverlayScrollbarCreate(hwnd, OverlayScrollbar::Type::Vert, OverlayScrollbar::Mode::Thick);
    sb->nativeAdapter = true;
    if (!sb->hwnd || !SetPropW(hwnd, kNativeScrollbarProperty, sb) ||
        !SetWindowSubclass(hwnd, NativeScrollbarProc, kNativeScrollbarSubclass, (DWORD_PTR)sb)) {
        RemovePropW(hwnd, kNativeScrollbarProperty);
        OverlayScrollbarDestroy(sb);
        return;
    }
    SetTimer(hwnd, kNativeScrollbarTimer, 200, nullptr);
    SyncNativeScrollbars(hwnd);
    EraseNativeScrollEdges(hwnd);
}

void RemoveAppScrollbar(HWND hwnd) {
    auto* sb = (OverlayScrollbar*)GetPropW(hwnd, kNativeScrollbarProperty);
    if (!sb) return;
    KillTimer(hwnd, kNativeScrollbarTimer);
    RemovePropW(hwnd, kNativeScrollbarProperty);
    auto* horizontal = (OverlayScrollbar*)GetPropW(hwnd, kNativeHScrollbarProperty);
    RemovePropW(hwnd, kNativeHScrollbarProperty);
    RemoveWindowSubclass(hwnd, NativeScrollbarProc, kNativeScrollbarSubclass);
    OverlayScrollbarDestroy(sb);
    OverlayScrollbarDestroy(horizontal);
}

#if IS_DEBUG
bool OverlayScrollbar_UnitTestsAppearance() {
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -10000, -10000, 600, 600, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    utassert(owner != nullptr);
    if (!owner) return false;
    for (auto type : {OverlayScrollbar::Type::Vert, OverlayScrollbar::Type::Horz}) {
        auto* sb = OverlayScrollbarCreate(owner, type, OverlayScrollbar::Mode::Thick);
        utassert(sb && sb->hwnd);
        if (!sb || !sb->hwnd) continue;
        sb->state = State::AlwaysThick;
        sb->nMax = 999;
        sb->nPage = 250;
        sb->nPos = 375;
        for (int width : {8, 16, 24, 48, 80}) {
            int w = IsVert(sb) ? width : 600;
            int h = IsVert(sb) ? 600 : width;
            SetWindowPos(sb->hwnd, nullptr, -10000, -10000, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(sb->hwnd, SW_SHOWNOACTIVATE);
            PaintScrollbar(sb);
            auto* pixels = (DWORD*)sb->paintBits;
            utassert(pixels != nullptr);
            if (!pixels) continue;
            DWORD track = PremultiplyPixel(ThemeTrackColor(), 255);
            for (Rect arrow : {GetArrowTopRect(sb), GetArrowBottomRect(sb)}) {
                int count = 0;
                for (int y = arrow.y; y < arrow.Bottom(); y++) {
                    for (int x = arrow.x; x < arrow.Right(); x++) count += pixels[y * w + x] != track;
                }
                utassert(count > width * width / 20);
                utassert(count < width * width / 2);
            }
            // Rounded apexes must leave the original sharp tip unpainted.
            if (width >= 48) {
                int tip = (int)floorf(width / 2.f - width / 3.f * .7f);
                utassert(pixels[IsVert(sb) ? tip * w + width / 2 : width / 2 * w + tip] == track);
                utassert(pixels[IsVert(sb) ? (h - 1 - tip) * w + width / 2 : width / 2 * w + w - 1 - tip] == track);
            }
            Rect thumb = GetThumbRect(sb);
            int inset = std::min(UiScalePxForDpi(DpiGetForHwnd(owner), 2), width / 3);
            if (IsVert(sb))
                thumb.SubLR(inset, inset);
            else
                thumb.SubTB(inset, inset);
            utassert(pixels[thumb.y * w + thumb.x] == track);
            utassert(pixels[(thumb.y + thumb.dy / 2) * w + thumb.x + thumb.dx / 2] != track);
            WCHAR captureDir[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"SUMATRA_SCROLLBAR_CAPTURE_DIR", captureDir, dimof(captureDir))) {
                TempStr file = fmt("%s/scrollbar-%s-%d.png", ToUtf8Temp(WStr(captureDir)),
                                   IsVert(sb) ? StrL("vertical") : StrL("horizontal"), width);
                Gdiplus::Bitmap bitmap(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)sb->paintBits);
                CLSID encoder = GetGdiPlusEncoderClsid(L"image/png");
                utassert(bitmap.Save(CWStrTemp(ToWStrTemp(file)), &encoder) == Gdiplus::Ok);
            }
        }
        OverlayScrollbarDestroy(sb);
    }
    DestroyWindow(owner);
    return true;
}

bool OverlayScrollbar_UnitTestsPerf() {
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -10000, -10000, 400, 800, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    utassert(owner != nullptr);
    if (!owner) return false;
    OverlayScrollbar* sb = OverlayScrollbarCreate(owner, OverlayScrollbar::Type::Vert, OverlayScrollbar::Mode::Thick);
    utassert(sb && sb->hwnd);
    if (!sb || !sb->hwnd) {
        OverlayScrollbarDestroy(sb);
        DestroyWindow(owner);
        return false;
    }
    sb->state = State::AlwaysThick;
    sb->nMax = 10000;
    sb->nPage = 100;
    SetWindowPos(sb->hwnd, nullptr, -10000, -10000, 30, 600, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(sb->hwnd, SW_SHOWNOACTIVATE);
    int allocations = gScrollbarAllocations;
    TimeStamp started = TimeGet();
    for (int i = 0; i < 200; i++) {
        sb->nPos = i;
        sb->mouseOverBar = i % 2;
        PaintScrollbar(sb);
    }
    int added = gScrollbarAllocations - allocations;
    printf("Scrollbar paint: 200 frames, %d surfaces, %.3f ms\n", added, TimeSinceInMs(started));
    utassert(added == 1);
    int paints = gScrollbarPaints;
    SCROLLINFO info{sizeof(info), SIF_POS};
    for (int i = 0; i < 100; i++) {
        info.nPos = i;
        OverlayScrollbarSetInfo(sb, &info, true);
    }
    utassert(gScrollbarPaints == paints);
    utassert(sb->paintPending);
    SendMessageW(sb->hwnd, kScrollbarPaintMessage, 0, 0);
    utassert(gScrollbarPaints == paints + 1);
    utassert(!sb->paintPending);
    utassert(sb->nPos == 99);
    SetWindowPos(sb->hwnd, nullptr, -10000, -10000, 45, 700, SWP_NOZORDER | SWP_NOACTIVATE);
    PaintScrollbar(sb);
    utassert(gScrollbarAllocations == allocations + 2);
    utassert(sb->paintSize == Size(45, 700));
    FreeScrollbarSurface(sb);
    utassert(!sb->paintDc && !sb->paintBitmap && !sb->paintBits);
    PaintScrollbar(sb);
    utassert(gScrollbarAllocations == allocations + 3);
    sb->mode = OverlayScrollbar::Mode::Smart;
    SetState(sb, State::SmartThin);
    RequestScrollbarPaint(sb);
    SetState(sb, State::SmartInvisible);
    paints = gScrollbarPaints;
    SendMessageW(sb->hwnd, kScrollbarPaintMessage, 0, 0);
    utassert(gScrollbarPaints == paints);
    utassert(!sb->paintPending);
    OverlayScrollbarDestroy(sb);
    DestroyWindow(owner);
    return true;
}

static LRESULT CALLBACK ScrollbarInputTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    if (msg == WM_VSCROLL || msg == WM_HSCROLL) {
        (*(int*)data)++;
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void TestScrollbarInput(OverlayScrollbar* sb) {
    int scrolls = 0;
    HWND owner = sb->hwndOwner;
    utassert(SetWindowSubclass(owner, ScrollbarInputTestProc, 1, (DWORD_PTR)&scrolls));
    sb->nPos = 400;
    Rect up = GetArrowTopRect(sb);
    SendMessageW(sb->hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(up.x + up.dx / 2, up.y + up.dy / 2));
    utassert(scrolls == 1);
    SendMessageW(sb->hwnd, WM_TIMER, OverlayScrollbar::kTimerRepeatScroll, 0);
    utassert(scrolls == 2);
    SendMessageW(sb->hwnd, WM_LBUTTONUP, 0, 0);

    Rect down = GetArrowBottomRect(sb);
    LPARAM bottom = MAKELPARAM(down.x + down.dx / 2, down.y + down.dy / 2);
    SendMessageW(sb->hwnd, WM_LBUTTONDOWN, 0, bottom);
    int before = scrolls;
    SendMessageW(sb->hwnd, WM_CANCELMODE, 0, 0);
    SendMessageW(sb->hwnd, WM_TIMER, OverlayScrollbar::kTimerRepeatScroll, 0);
    utassert(scrolls == before);
    utassert(GetCapture() != sb->hwnd);
    SendMessageW(sb->hwnd, WM_LBUTTONUP, 0, 0);

    Rect thumb = GetThumbRect(sb);
    LPARAM center = MAKELPARAM(thumb.x + thumb.dx / 2, thumb.y + thumb.dy / 2);
    SendMessageW(sb->hwnd, WM_LBUTTONDOWN, 0, center);
    utassert(sb->isDragging);
    before = scrolls;
    for (int i = 0; i < 20; i++) SendMessageW(sb->hwnd, WM_MOUSEMOVE, MK_LBUTTON, center);
    utassert(scrolls == before);
    SetCapture(owner);
    utassert(!sb->isDragging);
    ReleaseCapture();
    SendMessageW(sb->hwnd, WM_LBUTTONUP, 0, 0);
    RemoveWindowSubclass(owner, ScrollbarInputTestProc, 1);
}

bool OverlayScrollbar_UnitTestsNative() {
    Settings* saved = gSettings;
    gSettings = NewSettings({});
    gSettings->scrollbarWidth = 28;
    HWND emptyOwner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, -10000, -10000, 300, 200, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
    utassert(emptyOwner != nullptr);
    if (emptyOwner) {
        ShowWindow(emptyOwner, SW_SHOWNOACTIVATE);
        for (auto mode : {OverlayScrollbar::Mode::Thick, OverlayScrollbar::Mode::Smart}) {
            auto* bar = OverlayScrollbarCreate(emptyOwner, OverlayScrollbar::Type::Vert, mode);
            utassert(bar && bar->hwnd);
            if (bar && bar->hwnd) {
                SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
                info.nMax = 999;
                info.nPage = 100;
                OverlayScrollbarSetInfo(bar, &info, false);
                OverlayScrollbarShow(bar, true);
                utassert(HwndIsVisible(bar->hwnd));
                TestScrollbarInput(bar);
                HWND active = GetActiveWindow();
                OverlayScrollbarShow(bar, false);
                utassert(!IsActive(bar));
                utassert(!HwndIsVisible(bar->hwnd));
                utassert(GetActiveWindow() == active);
                OverlayScrollbarUpdatePos(bar);
                utassert(!HwndIsVisible(bar->hwnd));
                OverlayScrollbarShow(bar, true);
                utassert(HwndIsVisible(bar->hwnd));
                OverlayScrollbarShow(bar, false);
                utassert(!HwndIsVisible(bar->hwnd));
            }
            OverlayScrollbarDestroy(bar);
        }
        DestroyWindow(emptyOwner);
    }
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 300, 200, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 10, 10, 250,
                                100, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    bool ok = parent && edit;
    if (edit) {
        str::Builder text;
        for (int i = 0; i < 2000; i++) text.Append(StrL("Practice word\r\n"));
        SendMessageW(edit, EM_SETLIMITTEXT, 100000, 0);
        SetWindowTextW(edit, CWStrTemp(ToWStrTemp(ToStrTemp(text))));
        InstallAppScrollbar(edit);
        auto* sb = (OverlayScrollbar*)GetPropW(edit, kNativeScrollbarProperty);
        ok &= sb && sb->nativeAdapter && !HwndIsVisible(sb->hwnd);
        utassert(sb && sb->nativeAdapter && !HwndIsVisible(sb->hwnd));
        if (sb) {
            HDC surface = CreateCompatibleDC(nullptr);
            HDC desktop = GetDC(nullptr);
            HBITMAP bitmap = CreateCompatibleBitmap(desktop, 250, 100);
            ReleaseDC(nullptr, desktop);
            HGDIOBJ oldBitmap = SelectObject(surface, bitmap);
            Rect client = HwndClientRect(edit);
            int innerEdge = client.Right() -
                            std::max(0, GetAppScrollbarWidth(DpiGetForHwnd(edit)) -
                                            DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(edit))) -
                            1;
            SetPixel(surface, innerEdge, client.dy / 2, (ThemeTrackColor() ^ 0x00ffffff) & 0x00ffffff);
            SetPixel(surface, client.Right() - 1, client.dy / 2, (ThemeTrackColor() ^ 0x00ffffff) & 0x00ffffff);
            EraseNativeScrollEdges(edit, surface);
            utassert(GetPixel(surface, client.Right() - 1, client.dy / 2) == (ThemeTrackColor() & 0x00ffffff));
            utassert(GetPixel(surface, innerEdge, client.dy / 2) == (ThemeTrackColor() & 0x00ffffff));
            SelectObject(surface, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(surface);
            SyncNativeScrollbars(edit);
            int refreshes = nativeScrollbarRefreshCount;
            WINDOWPOS unchanged{edit,
                                nullptr,
                                10,
                                10,
                                250,
                                100,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW};
            for (int i = 0; i < 100; i++) {
                SendMessageW(edit, WM_WINDOWPOSCHANGED, 0, (LPARAM)&unchanged);
            }
            utassert(nativeScrollbarRefreshCount == refreshes);
            if (!ThemeUsesHighContrastColors()) {
                utassert(ThemeThumbColor() == MkRgb(139, 139, 139));
                utassert(ThemeThumbHoverColor() == UiScrollbarHoverColor(ThemeTrackColor()));
                utassert(GetRValue(ThemeThumbHoverColor()) > GetRValue(ThemeThumbColor()));
            }
            gSettings->scrollbarWidth = 8;
            utassert(ScaledWidth(sb) == GetAppScrollbarWidth(DpiGetForHwnd(edit)));
            SetWindowPos(sb->hwnd, nullptr, 0, 0, 8, 200, SWP_NOZORDER | SWP_NOACTIVATE);
            sb->state = State::SmartThin;
            utassert(ScaledWidth(sb) == GetAppScrollbarWidth(DpiGetForHwnd(edit)));
            Rect smartTrack = GetTrackRect(sb);
            sb->state = State::AlwaysThick;
            utassert(smartTrack == GetTrackRect(sb));
            sb->state = State::Hidden;
            gSettings->scrollbarWidth = 28;
            utassert(!GetPropW(edit, kNativeHScrollbarProperty));
            SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) | WS_HSCROLL);
            SCROLLINFO horizontalInfo{sizeof(horizontalInfo), SIF_RANGE | SIF_PAGE | SIF_POS};
            horizontalInfo.nMax = 200000;
            horizontalInfo.nPage = 100;
            horizontalInfo.nPos = 100000;
            SetScrollInfo(edit, SB_HORZ, &horizontalInfo, false);
            SyncNativeScrollbars(edit);
            auto* horizontal = (OverlayScrollbar*)GetPropW(edit, kNativeHScrollbarProperty);
            utassert(horizontal && horizontal->type == OverlayScrollbar::Type::Horz);
            if (horizontal) {
                horizontal->sendingNative = true;
                horizontal->nTrackPos = 170000;
                utassert(AppScrollbarTrackPos(edit, 7, SB_HORZ) == 170000);
                utassert(AppScrollbarTrackPos(edit, 7) == 7);
                horizontal->sendingNative = false;
                utassert(AppScrollbarTrackPos(edit, 7, SB_HORZ) == 7);
                horizontal->state = State::AlwaysThick;
                SetWindowPos(horizontal->hwnd, nullptr, 0, 0, 200, GetAppScrollbarWidth(DpiGetForHwnd(edit)),
                             SWP_NOZORDER | SWP_NOACTIVATE);
                Rect track = GetTrackRect(horizontal);
                Rect thumb = GetThumbRect(horizontal);
                utassert(track.Intersect(thumb) == thumb);
                utassert(GetArrowTopRect(horizontal).Right() <= track.x);
                utassert(GetArrowBottomRect(horizontal).x >= track.Right());
                SetWindowPos(horizontal->hwnd, nullptr, 0, 0, 8, 20, SWP_NOZORDER | SWP_NOACTIVATE);
                track = GetTrackRect(horizontal);
                thumb = GetThumbRect(horizontal);
                utassert(track.dx >= 0 && thumb.dx >= 0 && thumb.dx <= track.dx);
                horizontal->state = State::Hidden;
            }
            sb->nTrackPos = 1200;
            SendScrollMsg(sb, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, sb->nTrackPos));
            ok &= SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0) == 1200;
            utassert(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0) == 1200);
            sb->sendingNative = true;
            sb->nTrackPos = 150000;
            ok &= AppScrollbarTrackPos(edit, 7) == 150000;
            sb->sendingNative = false;
            ok &= AppScrollbarTrackPos(edit, 7) == 7;
            ok &= AppScrollbarInset(edit) == std::max(0, GetAppScrollbarWidth(DpiGetForHwnd(edit)) -
                                                             DpiGetSystemMetrics(SM_CXVSCROLL, DpiGetForHwnd(edit)));
            MoveWindow(edit, 10, 170, 250, 100, false);
            OverlayScrollbarUpdatePos(sb);
            HRGN region = CreateRectRgn(0, 0, 0, 0);
            RECT clipped{};
            ok &= GetWindowRgn(sb->hwnd, region) != ERROR;
            utassert(GetWindowRgn(sb->hwnd, region) != ERROR);
            GetRgnBox(region, &clipped);
            ok &= clipped.bottom <= 30;
            utassert(clipped.bottom <= 30);
            DeleteObject(region);
        }
    }
    HWND combo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 10, 10, 180, 160,
                                 parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    ok &= combo != nullptr;
    if (combo) {
        for (int i = 0; i < 40; i++) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Dictionary pack");
        COMBOBOXINFO info{sizeof(info)};
        bool haveInfo = GetComboBoxInfo(combo, &info);
        utassert(haveInfo);
        if (haveInfo) {
            DarkModeApplyToChildControls(parent);
            auto* sb = (OverlayScrollbar*)GetPropW(info.hwndList, kNativeScrollbarProperty);
            utassert(sb && sb->nativeAdapter && !HwndIsVisible(sb->hwnd));
            ok &= sb && sb->nativeAdapter && !HwndIsVisible(sb->hwnd);
        }
    }
    HMODULE richModule = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (richModule) {
        HWND rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 10,
                                    10, 250, 100, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        ok &= rich != nullptr;
        if (rich) {
            str::Builder text;
            for (int i = 0; i < 10000; i++) text.Append(StrL("Dictionary definition\r\n"));
            SendMessageW(rich, EM_EXLIMITTEXT, 0, 1000000);
            SetWindowTextW(rich, CWStrTemp(ToWStrTemp(ToStrTemp(text))));
            InstallAppScrollbar(rich);
            auto* sb = (OverlayScrollbar*)GetPropW(rich, kNativeScrollbarProperty);
            ok &= sb && !HwndIsVisible(sb->hwnd);
            if (sb) {
                sb->nTrackPos = 80000;
                SendScrollMsg(sb, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, sb->nTrackPos));
                int firstLine = (int)SendMessageW(rich, EM_GETFIRSTVISIBLELINE, 0, 0);
                ok &= firstLine > 1000;
                utassert(firstLine > 1000);
            }
        }
    }
    if (parent) DestroyWindow(parent);
    if (richModule) FreeLibrary(richModule);
    DeleteSettings(gSettings);
    gSettings = saved;
    return ok;
}
#endif
