/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "base/Base.h"
#include "base/File.h"
#include "gui/Dpi.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include "gui/win/WinGui.h"

#include "Theme.h"
#include "Settings.h"
#include "AppSettings.h"
#include "DarkMode.h"
#include "SumatraConfig.h"
#include "base/Win.h"
#include "Translations.h"
#include "MarkdownEdit.h"
#include "MarkdownLayout.h"

enum class MarkdownEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be
};

static bool WriteMarkdownAtomic(Str path, Str bytes) {
    WCHAR temporary[MAX_PATH]{};
    if (!GetTempFileNameW(CWStrTemp(path::GetDirTemp(path)), L"smd", 0, temporary)) return false;
    TempStr temp = ToUtf8Temp(WStr(temporary));
    bool ok = file::WriteFile(temp, bytes);
    if (ok) {
        HANDLE handle =
            CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ok = handle != INVALID_HANDLE_VALUE;
        if (ok) {
            ok = FlushFileBuffers(handle) != FALSE;
            CloseHandle(handle);
        }
    }
    if (ok) ok = ReplaceFileW(CWStrTemp(path), temporary, nullptr, 0, nullptr, nullptr) != FALSE;
    if (!ok) file::Delete(temp);
    return ok;
}

static bool DecodeMarkdown(Str bytes, Str& text, MarkdownEncoding& encoding) {
    encoding = MarkdownEncoding::Utf8;
    int skip = 0;
    if (len(bytes) >= 3 && memcmp(bytes.s, "\xef\xbb\xbf", 3) == 0) {
        skip = 3;
        encoding = MarkdownEncoding::Utf8Bom;
    } else if (len(bytes) >= 2 && ((u8)bytes.s[0] == 0xff || (u8)bytes.s[0] == 0xfe)) {
        bool le = (u8)bytes.s[0] == 0xff && (u8)bytes.s[1] == 0xfe;
        bool be = (u8)bytes.s[0] == 0xfe && (u8)bytes.s[1] == 0xff;
        if ((!le && !be) || (len(bytes) - 2) % 2) return false;
        encoding = le ? MarkdownEncoding::Utf16Le : MarkdownEncoding::Utf16Be;
        int count = (len(bytes) - 2) / 2;
        WCHAR* wide = AllocArray<WCHAR>(count + 1);
        defer {
            free(wide);
        };
        for (int i = 0; i < count; i++) {
            u8 a = bytes.s[2 + i * 2], b = bytes.s[3 + i * 2];
            wide[i] = le ? (a | (b << 8)) : ((a << 8) | b);
            if (!wide[i]) return false;
        }
        if (count && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, count, nullptr, 0, nullptr, nullptr))
            return false;
        text = str::Dup(ToUtf8Temp(WStr(wide, count)));
        return true;
    }
    Str utf8(bytes.s ? bytes.s + skip : nullptr, len(bytes) - skip);
    if (len(utf8) && (memchr(utf8.s, 0, len(utf8)) ||
                      !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.s, len(utf8), nullptr, 0)))
        return false;
    text = str::Dup(utf8);
    return true;
}

static Str EncodeMarkdown(Str text, MarkdownEncoding encoding) {
    str::Builder bytes;
    if (encoding == MarkdownEncoding::Utf8Bom) bytes.Append(Str("\xef\xbb\xbf", 3));
    if (encoding == MarkdownEncoding::Utf8 || encoding == MarkdownEncoding::Utf8Bom) {
        bytes.Append(text);
    } else {
        bool le = encoding == MarkdownEncoding::Utf16Le;
        bytes.Append(le ? Str("\xff\xfe", 2) : Str("\xfe\xff", 2));
        WStr wide = ToWStrTemp(text);
        for (int i = 0; i < len(wide); i++) {
            char pair[2] = {(char)(le ? wide.s[i] & 0xff : wide.s[i] >> 8),
                            (char)(le ? wide.s[i] >> 8 : wide.s[i] & 0xff)};
            bytes.Append(Str(pair, 2));
        }
    }
    return bytes.TakeStr();
}

#if IS_DEBUG
bool MarkdownEdit_UnitTests() {
    Str text = StrL("# Notes\nUnicode: αβ — 日本語\n");
    for (auto encoding :
         {MarkdownEncoding::Utf8, MarkdownEncoding::Utf8Bom, MarkdownEncoding::Utf16Le, MarkdownEncoding::Utf16Be}) {
        Str bytes = EncodeMarkdown(text, encoding), decoded;
        MarkdownEncoding actual;
        bool ok = DecodeMarkdown(bytes, decoded, actual) && actual == encoding && str::Eq(text, decoded);
        str::Free(bytes);
        str::Free(decoded);
        if (!ok) return false;
    }
    Str decoded;
    MarkdownEncoding encoding;
    if (DecodeMarkdown(Str("a\0b", 3), decoded, encoding) || DecodeMarkdown(Str("\xff\xfe\0", 3), decoded, encoding) ||
        DecodeMarkdown(Str("\xc0\xaf", 2), decoded, encoding))
        return false;
    bool empty = DecodeMarkdown({}, decoded, encoding) && len(decoded) == 0;
    str::Free(decoded);
    if (!empty) return false;
    if (!ValidMarkdownLayout(80, 10, 10) || !ValidMarkdownLayout(85, 10, 5) || ValidMarkdownLayout(80, 10, 5) ||
        ValidMarkdownLayout(0, 10, 10) || ValidMarkdownLayout(100, -1, 1))
        return false;
    if (MarkdownLayoutCssTemp(0, 10, 10) || !str::Contains(MarkdownLayoutCssTemp(80, 10, 10), StrL("width:80.0000%")))
        return false;
    WCHAR directory[MAX_PATH]{}, temporary[MAX_PATH]{};
    if (!GetTempPathW(dimof(directory), directory) || !GetTempFileNameW(directory, L"smt", 0, temporary)) return false;
    Str path = str::Dup(ToUtf8Temp(WStr(temporary)));
    defer {
        file::Delete(path);
        str::Free(path);
    };
    if (!file::WriteFile(path, StrL("original\n")) || !WriteMarkdownAtomic(path, text)) return false;
    Str persisted = file::ReadFile(path);
    bool same = str::Eq(persisted, text);
    str::Free(persisted);
    if (!same) return false;
    HANDLE lock =
        CreateFileW(temporary, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    bool denied = !WriteMarkdownAtomic(path, StrL("must not replace"));
    CloseHandle(lock);
    persisted = file::ReadFile(path);
    same = str::Eq(persisted, text);
    str::Free(persisted);
    return denied && same;
}
#endif

struct TextViewWnd : WindowBase {
    Edit* edit = nullptr;
    Button* saveButton = nullptr;
    Button* closeButton = nullptr;
    PlatformFont* monoFont = nullptr;
    HWND* hwndPtr = nullptr;
    Str editPath;
    Str originalBytes;
    Str originalView;
    BY_HANDLE_FILE_INFORMATION sourceIdentity{};
    MarkdownEncoding encoding = MarkdownEncoding::Utf8;
    bool useCrLf = false;
    bool mixedEndings = false;
    bool* didSave = nullptr;
    Vec<Button*> formatButtons;
    ~TextViewWnd() override {
        str::Free(editPath);
        str::Free(originalBytes);
        str::Free(originalView);
    }

    bool Create(Str title, Str text);
    void UpdateTheme() override;
    void ApplyDarkMode() override;
    static Str FormatTextForEdit(Str text);
    bool SaveMarkdown();
    bool TryClose();
    void SaveClick() { SaveMarkdown(); }
    void CloseClick() {
        if (TryClose()) ScheduleDelete();
    }
    void InsertOrWrap(Str prefix, Str suffix, Str placeholder);
    void OnBold() { InsertOrWrap(StrL("**"), StrL("**"), StrL("bold text")); }
    void OnItalic() { InsertOrWrap(StrL("*"), StrL("*"), StrL("italic text")); }
    void OnUnderline() { InsertOrWrap(StrL("<u>"), StrL("</u>"), StrL("underlined text")); }
    void OnHeading() { InsertOrWrap(StrL("### "), StrL(""), StrL("Heading")); }
    void OnList() { InsertOrWrap(StrL("- "), StrL(""), StrL("list item")); }
    void OnLink() { InsertOrWrap(StrL("["), StrL("](https://)"), StrL("link text")); }
    void OnTask() { InsertOrWrap(StrL("- [ ] "), StrL(""), StrL("task item")); }
    void OnColor() { InsertOrWrap(StrL("<span style=\"color: #e03e2d;\">"), StrL("</span>"), StrL("colored text")); }
    void OnAlign() { InsertOrWrap(StrL("<div align=\"center\">\n"), StrL("\n</div>"), StrL("centered content")); }
    void OnCode() { InsertOrWrap(StrL("`"), StrL("`"), StrL("code")); }
    void OnHighlight() { InsertOrWrap(StrL("=="), StrL("=="), StrL("highlighted text")); }
    void OnCallout() { InsertOrWrap(StrL("> [!NOTE]\n> "), StrL(""), StrL("callout text")); }
    void OnStrike() { InsertOrWrap(StrL("~~"), StrL("~~"), StrL("strikethrough text")); }
    void EditKey(KeyEvent* ev) {
        if (len(editPath) == 0 || !ev->isCtrl || ev->isAlt) return;
        if (ev->vkey == 'S') {
            SaveMarkdown();
            ev->didHandle = true;
        } else if (ev->vkey == 'B') {
            OnBold();
            ev->didHandle = true;
        } else if (ev->vkey == 'I') {
            OnItalic();
            ev->didHandle = true;
        } else if (ev->vkey == 'U') {
            OnUnderline();
            ev->didHandle = true;
        } else if (ev->vkey == 'K') {
            OnLink();
            ev->didHandle = true;
        }
    }
    void MinimumSize(GetMinMaxInfoEvent* ev) {
        ev->mmi->ptMinTrackSize.x = DpiScale(580);
        ev->mmi->ptMinTrackSize.y = DpiScale(300);
    }
    void EditorDpiChanged(DpiChangedEvent*) { UpdateTheme(); }
};

void TextViewWnd::InsertOrWrap(Str prefix, Str suffix, Str placeholder) {
    if (!edit || !edit->hwnd) return;
    DWORD start = 0, end = 0;
    SendMessageW(edit->hwnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    TempStr fullText = edit->GetTextTemp();
    TempWStr fullWide = ToWStrTemp(fullText);
    int textLen = fullWide.len;
    if ((int)start > textLen) start = (DWORD)textLen;
    if ((int)end > textLen) end = (DWORD)textLen;
    if (start > end) std::swap(start, end);

    WStr selWide = (start < end) ? WStr(fullWide.s + start, (int)(end - start)) : WStr();
    TempStr selUtf8 = ToUtf8Temp(selWide);

    str::Builder b;
    b.Append(prefix);
    if (len(selUtf8) > 0) {
        b.Append(selUtf8);
    } else {
        b.Append(placeholder);
    }
    b.Append(suffix);

    TempStr replacement = ToStrTemp(b);
    TempWStr repWide = ToWStrTemp(replacement);
    SendMessageW(edit->hwnd, EM_REPLACESEL, TRUE, (LPARAM)repWide.s);

    if (len(selUtf8) == 0 && len(placeholder) > 0) {
        TempWStr preWide = ToWStrTemp(prefix);
        TempWStr phWide = ToWStrTemp(placeholder);
        DWORD newStart = start + (DWORD)preWide.len;
        DWORD newEnd = newStart + (DWORD)phWide.len;
        SendMessageW(edit->hwnd, EM_SETSEL, newStart, newEnd);
    }
    SetFocus(edit->hwnd);
}

void TextViewWnd::ApplyDarkMode() {
    DarkModeApplyToWindowAndEraseBg(hwnd);
}

void TextViewWnd::UpdateTheme() {
    WindowBase::UpdateTheme();
    if (editPath) {
        monoFont = GetUserGuiFont(StrL("Consolas"), GetAppFontSizeForDpi(GetDpi()));
        auto* appFont = GetAppFontForDpi(GetDpi());
        if (saveButton) saveButton->SetFont(appFont);
        if (closeButton) closeButton->SetFont(appFont);
        for (auto* btn : formatButtons) {
            if (btn) btn->SetFont(appFont);
        }
    }
    // Re-apply monospaced font after darkmode child theming (may reset font).
    if (edit && monoFont) {
        edit->SetFont(monoFont);
    }
}

// Returns a temp-arena string: ToStr() would be a view into the local Builder
// and dangle as soon as this function returns (crash in HwndSetText/CWStrTemp).
Str TextViewWnd::FormatTextForEdit(Str text) {
    str::Builder crlfText;
    for (int i = 0; i < text.len; i++) {
        char c = text.s[i];
        if (c == '\n' && (i == 0 || text.s[i - 1] != '\r')) {
            crlfText.AppendChar('\r');
        }
        crlfText.AppendChar(c);
    }
    return ToStrTemp(crlfText);
}

bool TextViewWnd::Create(Str title, Str text) {
    {
        CreateCustomArgs args;
        args.title = title;
        args.visible = false;
        args.style = WS_OVERLAPPEDWINDOW;
        if (editPath) args.font = GetAppFont();
        args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }

    Edit::CreateArgs args;
    args.parent = hwnd;
    args.isMultiLine = true;
    args.withBorder = true;
    edit = new Edit();
    edit->Create(args);
    SendMessageW(edit->hwnd, EM_SETREADONLY, editPath ? FALSE : TRUE, 0);
    SendMessageW(edit->hwnd, EM_SETLIMITTEXT, 0, 0);

    HDC hdc = GetDC(hwnd);
    monoFont =
        editPath ? GetUserGuiFont(StrL("Consolas"), GetAppFontSize()) : HdcCreateSimpleFont(hdc, StrL("Consolas"), 14);
    ReleaseDC(hwnd, hdc);
    if (monoFont) {
        edit->SetFont(monoFont);
    }

    // set tab stop to 4 spaces (16 dialog units; default is 32 = 8 spaces)
    DWORD tabStop = 16;
    SendMessageW(edit->hwnd, EM_SETTABSTOPS, 1, (LPARAM)&tabStop);

    edit->SetText(FormatTextForEdit(text));
    SendMessageW(edit->hwnd, EM_SETSEL, 0, 0);
    layout = edit;
    if (editPath) {
        originalView = str::Dup(edit->GetTextTemp());
        auto* root = new VBox();
        root->alignCross = CrossAxisAlign::Stretch;

        auto* toolbar = new HBox();
        toolbar->alignCross = CrossAxisAlign::CrossCenter;
        toolbar->gap = DpiScale(3);

        Button::CreateArgs btnArgs;
        btnArgs.parent = hwnd;
        btnArgs.font = GetAppFont();

#define ADD_MD_FORMAT_BTN(txt, method)                                       \
        do {                                                                 \
            btnArgs.text = Str(txt);                                         \
            auto* b = new Button();                                          \
            b->Create(btnArgs);                                              \
            b->onClick = MkMethod0<TextViewWnd, &TextViewWnd::method>(this); \
            VecAppend(formatButtons, b);                                     \
            toolbar->AddChild(b);                                            \
        } while (0)

        ADD_MD_FORMAT_BTN("B", OnBold);
        ADD_MD_FORMAT_BTN("I", OnItalic);
        ADD_MD_FORMAT_BTN("U", OnUnderline);
        ADD_MD_FORMAT_BTN("H", OnHeading);
        ADD_MD_FORMAT_BTN("List", OnList);
        ADD_MD_FORMAT_BTN("Link", OnLink);
        ADD_MD_FORMAT_BTN("[ ]", OnTask);
        ADD_MD_FORMAT_BTN("Color", OnColor);
        ADD_MD_FORMAT_BTN("Align", OnAlign);
        ADD_MD_FORMAT_BTN("Code", OnCode);
        ADD_MD_FORMAT_BTN("Highlight", OnHighlight);
        ADD_MD_FORMAT_BTN("Callout", OnCallout);
        ADD_MD_FORMAT_BTN("Strike", OnStrike);

#undef ADD_MD_FORMAT_BTN

        root->AddChild(new Padding(toolbar, DpiScaledInsets(6, 6, 2, 6)));
        root->AddChild(edit, 1);
        auto* buttons = new HBox();
        buttons->alignMain = MainAxisAlign::MainEnd;
        buttons->gap = DpiScale(8);
        Button::CreateArgs buttonArgs;
        buttonArgs.parent = hwnd;
        buttonArgs.font = GetFont();
        buttonArgs.text = Tr("Close");
        auto* close = new Button();
        closeButton = close;
        close->Create(buttonArgs);
        close->onClick = MkMethod0<TextViewWnd, &TextViewWnd::CloseClick>(this);
        buttonArgs.text = Tr("Save");
        auto* save = new Button();
        saveButton = save;
        save->Create(buttonArgs);
        save->onClick = MkMethod0<TextViewWnd, &TextViewWnd::SaveClick>(this);
        buttons->AddChild(close);
        buttons->AddChild(save);
        root->AddChild(new Padding(buttons, DpiScaledInsets(6)));
        layout = root;
        onKeyDown = MkMethod1<TextViewWnd, KeyEvent*, &TextViewWnd::EditKey>(this);
        onGetMinMaxInfo = MkMethod1<TextViewWnd, GetMinMaxInfoEvent*, &TextViewWnd::MinimumSize>(this);
        onDpiChanged = MkMethod1<TextViewWnd, DpiChangedEvent*, &TextViewWnd::EditorDpiChanged>(this);
    }

    int winW = DpiScale(800);
    int winH = DpiScale(600);
    SetWindowPos(hwnd, nullptr, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER);
    DoLayout();
    UpdateTheme();
    SetIsVisible(true);
    if (editPath) edit->SetFocus();
    return true;
}

bool TextViewWnd::SaveMarkdown() {
    if (len(editPath) == 0) return false;
    Str view = str::Dup(edit->GetTextTemp());
    defer {
        str::Free(view);
    };
    if (str::Eq(view, originalView)) return true;
    if (mixedEndings &&
        MessageBoxW(hwnd,
                    CWStrTemp(Tr("This file has mixed line endings. Save edited text using consistent line endings?")),
                    CWStrTemp(Tr("Markdown line endings")), MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return false;
    str::Builder normalized;
    for (int i = 0; i < len(view); i++) {
        char c = view.s[i];
        if (c == '\r') {
            if (i + 1 < len(view) && view.s[i + 1] == '\n') i++;
            if (useCrLf) normalized.AppendChar('\r');
            normalized.AppendChar('\n');
        } else if (c == '\n') {
            if (useCrLf) normalized.AppendChar('\r');
            normalized.AppendChar('\n');
        } else
            normalized.AppendChar(c);
    }
    Str bytes = EncodeMarkdown(ToStr(normalized), encoding);
    defer {
        str::Free(bytes);
    };
    HANDLE source = CreateFileW(CWStrTemp(editPath), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        MessageBoxW(hwnd, CWStrTemp(Tr("The source is busy, missing or unavailable. Your edits have not been saved.")),
                    CWStrTemp(Tr("Could not save Markdown")), MB_OK | MB_ICONWARNING);
        return false;
    }
    defer {
        CloseHandle(source);
    };
    Str current = file::ReadFile(editPath);
    BY_HANDLE_FILE_INFORMATION identity{};
    bool same = GetFileInformationByHandle(source, &identity) &&
                identity.dwVolumeSerialNumber == sourceIdentity.dwVolumeSerialNumber &&
                identity.nFileIndexHigh == sourceIdentity.nFileIndexHigh &&
                identity.nFileIndexLow == sourceIdentity.nFileIndexLow && str::Eq(current, originalBytes);
    str::Free(current);
    if (!same &&
        MessageBoxW(hwnd, CWStrTemp(Tr("The file changed outside this editor. Replace it with your edited text?")),
                    CWStrTemp(Tr("Markdown changed")), MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES)
        return false;
    bool ok = WriteMarkdownAtomic(editPath, bytes);
    if (!ok) {
        MessageBoxW(hwnd, CWStrTemp(Tr("The file could not be replaced. Your source and unsaved edits are preserved.")),
                    CWStrTemp(Tr("Could not save Markdown")), MB_OK | MB_ICONERROR);
        return false;
    }
    str::ReplaceWithCopy(&originalBytes, bytes);
    str::ReplaceWithCopy(&originalView, view);
    HANDLE replacement =
        CreateFileW(CWStrTemp(editPath), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (replacement != INVALID_HANDLE_VALUE) {
        GetFileInformationByHandle(replacement, &sourceIdentity);
        CloseHandle(replacement);
    }
    mixedEndings = false;
    if (didSave) *didSave = true;
    return true;
}

bool TextViewWnd::TryClose() {
    if (len(editPath) == 0 || str::Eq(edit->GetTextTemp(), originalView)) return true;
    int answer = MessageBoxW(hwnd, CWStrTemp(Tr("Save your Markdown changes before closing?")),
                             CWStrTemp(Tr("Unsaved Markdown")), MB_YESNOCANCEL | MB_ICONQUESTION);
    return answer == IDNO || (answer == IDYES && SaveMarkdown());
}

static void TeardownTextViewWnd(TextViewWnd* w) {
    if (!w) {
        return;
    }
    if (w->hwndPtr) {
        *w->hwndPtr = nullptr;
    }
    w->ScheduleDelete();
}

static void OnTextViewClose(WindowBase::CloseEvent* ev) {
    auto* window = (TextViewWnd*)ev->e->self;
    ev->e->didHandle = true;
    if (window->TryClose()) TeardownTextViewWnd(window);
}

static void OnTextViewDestroy(WindowBase::DestroyEvent* ev) {
    TeardownTextViewWnd((TextViewWnd*)ev->e->self);
}

HWND ShowTextInWindow(Str title, Str text, HWND* hwndPtr) {
    auto* wnd = new TextViewWnd();
    wnd->hwndPtr = hwndPtr;
    wnd->closeOnEsc = true;
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnTextViewClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnTextViewDestroy);
    if (!wnd->Create(title, text)) {
        delete wnd;
        return nullptr;
    }
    return wnd->hwnd;
}

void ShowTextInWindowDialog(Str title, Str text) {
    auto* wnd = new TextViewWnd();
    wnd->closeOnEsc = true;
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnTextViewClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnTextViewDestroy);
    if (!wnd->Create(title, text)) {
        delete wnd;
        return;
    }
    // returns once the scheduled delete has run and destroyed the window
    RunModalWindow(wnd->hwnd, nullptr);
}

bool ShowMarkdownEditor(HWND owner, Str path) {
    if (file::GetSize(path) < 0 || file::GetSize(path) > 20 * 1024 * 1024) {
        MessageBoxW(owner, CWStrTemp(Tr("This Markdown file is unavailable or exceeds the editor's 20 MiB limit.")),
                    CWStrTemp(Tr("Could not edit Markdown")), MB_OK | MB_ICONWARNING);
        return false;
    }
    Str bytes = file::ReadFile(path), text;
    MarkdownEncoding encoding;
    if (!DecodeMarkdown(bytes, text, encoding)) {
        str::Free(bytes);
        MessageBoxW(
            owner,
            CWStrTemp(Tr("The source uses an unsupported encoding or contains invalid text. It has not been changed.")),
            CWStrTemp(Tr("Could not edit Markdown")), MB_OK | MB_ICONWARNING);
        return false;
    }
    bool saved = false;
    auto* window = new TextViewWnd();
    window->editPath = str::Dup(path);
    HANDLE identity =
        CreateFileW(CWStrTemp(path), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (identity != INVALID_HANDLE_VALUE) {
        GetFileInformationByHandle(identity, &window->sourceIdentity);
        CloseHandle(identity);
    }
    window->originalBytes = bytes;
    window->encoding = encoding;
    window->didSave = &saved;
    int crlf = 0, lf = 0, cr = 0;
    for (int i = 0; i < len(text); i++) {
        if (text.s[i] == '\r') {
            if (i + 1 < len(text) && text.s[i + 1] == '\n') {
                crlf++;
                i++;
            } else
                cr++;
            if (crlf + lf + cr == 1) window->useCrLf = true;
        } else if (text.s[i] == '\n')
            lf++;
    }
    window->mixedEndings = (int)(crlf > 0) + (int)(lf > 0) + (int)(cr > 0) > 1 || cr > 0;
    window->closeOnEsc = true;
    window->closeOnCtrlW = true;
    window->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnTextViewClose);
    window->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnTextViewDestroy);
    bool created = window->Create(fmt("%s — %s", Tr("Edit Markdown"), path::GetBaseNameTemp(path)), text);
    str::Free(text);
    if (!created) {
        delete window;
        return false;
    }
    SetWindowLongPtrW(window->hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
    HwndCenterDialog(window->hwnd, owner);
    RunModalWindow(window->hwnd, owner);
    return saved;
}
