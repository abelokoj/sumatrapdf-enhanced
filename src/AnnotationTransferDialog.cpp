/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/File.h"
#include "base/GuessFileType.h"
#include "base/UITask.h"
#include "gui/Dpi.h"
#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/PlatformFont.h"
#include "gui/win/WinGui.h"
#include "gui/Gfx.h"
#include "gui/VirtCtrl.h"
#include <commdlg.h>
#include "Settings.h"
#include "AppSettings.h"
#include "DocController.h"
#include "Annotation.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "DisplayModel.h"
#include "MainWindow.h"
#include "Theme.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "DarkMode.h"
#include "AnnotationTransfer.h"
#include "AnnotationTransferDialog.h"

struct TransferLabel : VirtText {
    TransferLabel(Str text, PlatformFont* font) : VirtText(text, font) {}
    Size Layout(Constraints bc) override {
        int width = bc.HasBoundedWidth() ? std::max(1, bc.max.dx) : font->averageCharWidth * 72;
        return bc.Constrain(PlatformFontMeasureText(font, s, width));
    }
    int MinIntrinsicWidth(int) override { return font->averageCharWidth * 24; }
    int MinIntrinsicHeight(int width) override { return Layout(ExpandHeight(width)).dy; }
    void Paint(VirtPaintCtx& ctx) override {
        ctx.gfx->DrawText(s, ctx.content, gfxTextWrap, font, ThemeWindowTextColor());
    }
};

enum class TransferAction {
    Export,
    Analyze,
    Import
};
struct TransferJob {
    HWND hwnd = nullptr;
    EngineBase* engine = nullptr;
    AnnotationTransferPlan* plan = nullptr;
    TransferAction action = TransferAction::Analyze;
    Str path, error;
    HANDLE cancel = nullptr;
    bool success = false;
    int imported = 0, skipped = 0;
    ~TransferJob() {
        if (engine) engine->Release();
        delete plan;
        str::Free(path);
        str::Free(error);
        if (cancel) CloseHandle(cancel);
    }
};

struct TransferWnd : WindowBase {
    MainWindow* owner = nullptr;
    HWND ownerHwnd = nullptr;
    bool restoreOwner = false;
    bool importing = false;
    bool closing = false;
    bool complete = false;
    EngineBase* engine = nullptr;
    AnnotationTransferPlan* plan = nullptr;
    TransferJob* job = nullptr;
    Str input, savedPath;
    VirtText* description = nullptr;
    VirtText* status = nullptr;
    VirtText* source = nullptr;
    VirtText* target = nullptr;
    VirtListBox* list = nullptr;
    ListBoxModelStrings* model = nullptr;
    Edit* page = nullptr;
    VirtButton* apply = nullptr;
    VirtButton* skip = nullptr;
    VirtButton* save = nullptr;
    VirtButton* cancel = nullptr;
    ~TransferWnd() override {
        if (job) SetEvent(job->cancel);
        if (restoreOwner && IsWindow(ownerHwnd)) EnableWindow(ownerHwnd, TRUE);
        if (engine) engine->Release();
        delete plan;
        str::Free(input);
        str::Free(savedPath);
    }
    bool Create(MainWindow*, EngineBase*, bool);
    void Begin(TransferAction, Str);
    void FillRows();
    void Summary();
    void OnSelect();
    void OnMap(VirtMouseEvent* = nullptr);
    void OnSkip(VirtMouseEvent* = nullptr);
    void OnSave(VirtMouseEvent* = nullptr);
    void OnCancel(VirtMouseEvent* = nullptr);
    void OnKey(KeyEvent*);
    bool ApplyPage();
};
static TransferWnd* gTransferWnd = nullptr;

static TempStr PickTransferFile(HWND owner, TransferAction action, Str name = {}) {
    WCHAR pathBuffer[32768]{};
    if (len(name)) {
        auto suggested = ToWStrTemp(name);
        if (len(suggested) < dimof(pathBuffer)) memcpy(pathBuffer, suggested.s, (len(suggested) + 1) * sizeof(WCHAR));
    }
    OPENFILENAMEW args{};
    args.lStructSize = sizeof(args);
    args.hwndOwner = owner;
    args.lpstrFile = pathBuffer;
    args.nMaxFile = dimof(pathBuffer);
    args.lpstrFilter = action == TransferAction::Import
                           ? L"PDF document\0*.pdf\0"
                           : L"Annotation transfer package\0*.sannot.pdf\0All files\0*.*\0";
    args.lpstrDefExt = action == TransferAction::Export ? L"sannot.pdf" : L"pdf";
    args.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    bool save = action != TransferAction::Analyze;
    args.Flags |= save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
    auto title = ToWStrTemp(action == TransferAction::Export    ? Tr("Export annotations")
                            : action == TransferAction::Analyze ? Tr("Choose annotations to import")
                                                                : Tr("Save annotated copy"));
    args.lpstrTitle = title.s;
    bool picked = save ? GetSaveFileNameW(&args) : GetOpenFileNameW(&args);
    return picked ? ToUtf8Temp(pathBuffer) : TempStr{};
}

static void ClearTransferWnd() {
    gTransferWnd = nullptr;
}
static void CloseTransferWnd(WindowBase::CloseEvent*) {
    if (gTransferWnd) gTransferWnd->OnCancel();
}
static void DestroyTransferWnd(WindowBase::DestroyEvent*) {
    if (gTransferWnd) gTransferWnd->ScheduleDelete();
}

static void FinishTransfer(TransferJob* job) {
    auto* w = gTransferWnd;
    if (!w || w->hwnd != job->hwnd || w->job != job) {
        delete job;
        return;
    }
    w->job = nullptr;
    if (w->closing) {
        w->ScheduleDelete();
        delete job;
        return;
    }
    if (!job->success) {
        if (job->action == TransferAction::Import && !w->plan) {
            w->plan = job->plan;
            job->plan = nullptr;
        }
        w->status->SetText(len(job->error) ? job->error : Tr("Annotations could not be transferred."));
        w->save->SetText(w->plan ? Tr("Save annotated copy...") : Tr("Try again..."));
        w->save->SetIsEnabled(true);
    } else if (job->action == TransferAction::Analyze) {
        w->plan = job->plan;
        job->plan = nullptr;
        w->source->SetText(fmt(Tr("From: %s").s, path::GetBaseNameTemp(w->plan->sourcePath)));
        w->target->SetText(fmt(Tr("To: %s").s, path::GetBaseNameTemp(w->plan->targetPath)));
        w->FillRows();
        w->Summary();
        w->save->SetText(Tr("Save annotated copy..."));
        w->save->SetIsEnabled(true);
    } else {
        w->complete = true;
        str::ReplaceWithCopy(&w->savedPath, job->path);
        if (job->action == TransferAction::Export) {
            w->status->SetText(fmt(Tr("Exported to %s. Open the revised PDF and choose Import annotations.").s,
                                   path::GetBaseNameTemp(job->path)));
            w->save->SetText(Tr("Close"));
        } else {
            w->plan = job->plan;
            job->plan = nullptr;
            w->status->SetText(fmt(Tr("Saved %d annotations; %d skipped. Your original PDFs are unchanged.").s,
                                   job->imported, job->skipped));
            w->save->SetText(Tr("Open annotated copy"));
        }
        w->save->SetIsEnabled(true);
        w->cancel->SetText(Tr("Close"));
    }
    if (!w->plan && job->action == TransferAction::Import) {
        w->plan = job->plan;
        job->plan = nullptr;
    }
    if (w->list) w->list->SetIsEnabled(w->plan && !w->complete);
    w->OnSelect();
    w->DoLayout();
    delete job;
}
static void RunTransfer(TransferJob* job) {
    if (job->action == TransferAction::Export) {
        job->success = ExportAnnotationTransfer(job->engine, job->path, &job->error, job->cancel);
    } else if (job->action == TransferAction::Analyze) {
        job->plan = AnalyzeAnnotationTransfer(job->engine, job->path, &job->error, job->cancel);
        job->success = job->plan != nullptr;
    } else {
        job->success =
            ImportAnnotationTransfer(job->plan, job->path, &job->imported, &job->skipped, &job->error, job->cancel);
    }
    uitask::Post(MkFunc0(FinishTransfer, job), "FinishAnnotationTransfer");
}
void TransferWnd::Begin(TransferAction action, Str path) {
    if (job) return;
    auto* work = new TransferJob();
    work->hwnd = hwnd;
    work->action = action;
    work->path = str::Dup(path);
    work->cancel = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!work->cancel) {
        delete work;
        status->SetText(Tr("The operation could not start. Please try again."));
        save->SetIsEnabled(true);
        return;
    }
    if (action == TransferAction::Import) {
        work->plan = plan;
        plan = nullptr;
    } else {
        work->engine = engine;
        engine->AddRef();
    }
    job = work;
    save->SetIsEnabled(false);
    if (list) list->SetIsEnabled(false);
    if (page) page->SetIsEnabled(false);
    if (apply) apply->SetIsEnabled(false);
    if (skip) skip->SetIsEnabled(false);
    status->SetText(action == TransferAction::Analyze  ? Tr("Matching annotated pages with the revised PDF...")
                    : action == TransferAction::Export ? Tr("Exporting annotations...")
                                                       : Tr("Saving annotated copy..."));
    DoLayout();
    RunAsync(MkFunc0(RunTransfer, work), StrL("AnnotationTransfer"));
}

void TransferWnd::FillRows() {
    if (!plan || !list) return;
    int selected = list->GetCurrentSelection();
    model->strings.Reset();
    for (auto& match : plan->pages) {
        auto destination = match.targetPage > 0 ? fmt(Tr("new page %d").s, match.targetPage) : Tr("skipped");
        auto matchKind = match.confidence == 1 ? Tr("text match") : Tr("review placement");
        if (!match.targetPage && match.suggestedPage)
            destination = fmt(Tr("skipped; suggested new page %d").s, match.suggestedPage);
        model->strings.Append(fmt(Tr("Old page %d: %s | %d annotations | %s (%d%%)").s, match.sourcePage, destination,
                                  match.annotationCount, matchKind, (int)(match.confidence * 100 + 0.5f)));
    }
    list->SetModel(model);
    if (selected < 0 && len(plan->pages)) selected = 0;
    list->SetCurrentSelection(selected);
    OnSelect();
}
void TransferWnd::Summary() {
    if (!plan) return;
    int count = 0, skipped = 0;
    for (auto& match : plan->pages) {
        if (match.targetPage > 0)
            count += match.annotationCount;
        else
            skipped += match.annotationCount;
    }
    status->SetText(
        fmt(Tr("Ready to copy %d annotations; %d skipped. Review uncertain pages or enter a page manually.").s, count,
            skipped));
    save->SetIsEnabled(count > 0);
    DoLayout();
}
void TransferWnd::OnSelect() {
    if (!page) return;
    int index = list ? list->GetCurrentSelection() : -1;
    bool enabled = plan && index >= 0 && index < len(plan->pages) && !job && !complete;
    page->SetIsEnabled(enabled);
    apply->SetIsEnabled(enabled);
    skip->SetIsEnabled(enabled);
    page->SetText(enabled ? fmt("%d", plan->pages[index].targetPage) : Str{});
}
bool TransferWnd::ApplyPage() {
    if (!plan || job || complete) return false;
    int index = list->GetCurrentSelection();
    if (index < 0 || index >= len(plan->pages)) return true;
    auto value = page->GetTextTemp();
    int targetPage = 0;
    bool valid = len(value) > 0;
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c < '0' || c > '9' || targetPage > plan->targetPageCount / 10 + 1) {
            valid = false;
            break;
        }
        targetPage = targetPage * 10 + c - '0';
    }
    if (!valid || targetPage > plan->targetPageCount) {
        status->SetText(fmt(Tr("Enter a page from 1 to %d, or 0 to skip this source page.").s, plan->targetPageCount));
        EditSetFocus(page);
        EditSelectAll(page);
        DoLayout();
        return false;
    }
    plan->pages[index].targetPage = targetPage;
    FillRows();
    Summary();
    return true;
}
void TransferWnd::OnMap(VirtMouseEvent*) {
    ApplyPage();
}
void TransferWnd::OnSkip(VirtMouseEvent*) {
    if (!plan || job || complete) return;
    page->SetText(StrL("0"));
    ApplyPage();
}
void TransferWnd::OnKey(KeyEvent* event) {
    if (page && event->hwnd == page->hwnd && event->vkey == VK_RETURN) {
        event->didHandle = true;
        ApplyPage();
    }
}
void TransferWnd::OnSave(VirtMouseEvent*) {
    if (job) return;
    if (complete) {
        if (importing && len(savedPath) && IsMainWindowValidAndNotClosing(owner)) {
            auto* args = new LoadArgs(savedPath, owner);
            args->forceReuse = false;
            StartLoadDocument(args);
        }
        OnCancel();
        return;
    }
    if (!importing || !plan) {
        auto action = importing ? TransferAction::Analyze : TransferAction::Export;
        auto name = fmt("%s-annotations.sannot.pdf", path::GetBaseNameTemp(path::GetPathNoExtTemp(input)));
        auto path = PickTransferFile(hwnd, action, importing ? Str{} : name);
        if (len(path)) Begin(action, path);
        return;
    }
    if (!ApplyPage()) return;
    int count = 0;
    for (auto& match : plan->pages)
        if (match.targetPage > 0) count += match.annotationCount;
    if (!count) return;
    auto name = fmt("%s-annotated.pdf", path::GetBaseNameTemp(path::GetPathNoExtTemp(plan->targetPath)));
    auto path = PickTransferFile(hwnd, TransferAction::Import, name);
    if (!len(path)) return;
    if (path::IsSame(path, plan->targetPath) || path::IsSame(path, plan->sourcePath) ||
        path::IsSame(path, plan->packagePath)) {
        status->SetText(Tr("Choose a different filename to keep both original PDFs and the annotation package."));
        DoLayout();
        return;
    }
    Begin(TransferAction::Import, path);
}
void TransferWnd::OnCancel(VirtMouseEvent*) {
    if (job) {
        closing = true;
        SetEvent(job->cancel);
        status->SetText(Tr("Canceling safely..."));
        cancel->SetIsEnabled(false);
        DoLayout();
        return;
    }
    ScheduleDelete();
}

bool TransferWnd::Create(MainWindow* win, EngineBase* pdf, bool isImport) {
    owner = win;
    ownerHwnd = win->hwndFrame;
    importing = isImport;
    engine = pdf;
    engine->AddRef();
    input = str::Dup(engine->FilePath());
    CreateCustomArgs args;
    args.owner = ownerHwnd;
    args.title = importing ? Tr("Import annotations") : Tr("Export annotations");
    args.visible = false;
    args.style = WS_POPUPWINDOW | WS_CAPTION;
    args.font = font;
    args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
    CreateCustom(args);
    if (!hwnd) return false;
    auto* outer = new VBox();
    outer->alignCross = CrossAxisAlign::Stretch;
    outer->gap = font->averageCharWidth;
    auto* box = new VBox();
    auto* scroll = new ScrollBox(box);
    outer->AddChild(scroll, 1);
    box->alignCross = CrossAxisAlign::Stretch;
    box->gap = font->averageCharWidth;
    description = new TransferLabel(importing ? Tr("Transfer notes, highlights and drawings into this PDF.")
                                              : Tr("Export your annotations for another version of this PDF."),
                                    font);
    box->AddChild(description);
    if (importing) {
        source = NewVirtText({.s = Tr("From: annotation package"), .font = font, .pathEllipsis = true});
        target =
            NewVirtText({.s = fmt(Tr("To: %s").s, path::GetBaseNameTemp(input)), .font = font, .pathEllipsis = true});
        box->AddChild(source);
        box->AddChild(target);
        auto* help = new TransferLabel(Tr("Only confident page matches are included automatically. Drawings retain "
                                          "their page position; check changed layouts."),
                                       font);
        box->AddChild(help);
        list = new VirtListBox();
        list->font = font;
        list->dpi = GetDpi();
        list->idealSizeLines = 7;
        list->idealSizeDx = font->averageCharWidth * 72;
        model = new ListBoxModelStrings();
        list->SetModel(model);
        list->onSelectionChanged = MkMethod0<TransferWnd, &TransferWnd::OnSelect>(this);
        box->AddChild(list);
        auto* row = new Wrap();
        row->alignCross = CrossAxisAlign::CrossCenter;
        row->colGap = font->averageCharWidth;
        row->rowGap = font->averageCharWidth;
        row->AddChild(NewVirtText({.s = Tr("Target page:"), .font = font}));
        page = new Edit();
        Edit::CreateArgs editArgs;
        editArgs.parent = hwnd;
        editArgs.font = font;
        editArgs.withBorder = true;
        editArgs.idealWidthChars = 5;
        page->Create(editArgs);
        EditSetNumbersOnly(page, true);
        row->AddChild(page);
        apply = NewThemedButton(hwnd, Tr("Apply page"), font, false);
        apply->onClick = MkMethod1<TransferWnd, VirtMouseEvent*, &TransferWnd::OnMap>(this);
        row->AddChild(apply);
        skip = NewThemedButton(hwnd, Tr("Skip page"), font, false);
        skip->onClick = MkMethod1<TransferWnd, VirtMouseEvent*, &TransferWnd::OnSkip>(this);
        row->AddChild(skip);
        outer->AddChild(row);
    }
    status = new TransferLabel(Tr("Choose a file to continue."), font);
    box->AddChild(status);
    auto* buttons = new HBox();
    buttons->alignMain = MainAxisAlign::MainEnd;
    buttons->alignCross = CrossAxisAlign::CrossCenter;
    buttons->gap = font->averageCharWidth;
    cancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
    cancel->onClick = MkMethod1<TransferWnd, VirtMouseEvent*, &TransferWnd::OnCancel>(this);
    buttons->AddChild(cancel);
    save = NewThemedButton(hwnd, importing ? Tr("Choose annotations...") : Tr("Export annotations..."), font, true);
    save->onClick = MkMethod1<TransferWnd, VirtMouseEvent*, &TransferWnd::OnSave>(this);
    buttons->AddChild(save);
    outer->AddChild(buttons);
    layout = new Padding(outer, DpiScaledInsets(10, 12));
    UpdateTheme();
    Rect work = GetWorkAreaRect(HwndWindowRect(ownerHwnd), ownerHwnd);
    int dx = std::min(font->averageCharWidth * (importing ? 78 : 62), std::max(1, work.dx - DpiScale(32)));
    Size ideal = layout->Layout(ExpandHeight(dx));
    int dy = std::min(ideal.dy, std::max(1, work.dy - DpiScale(96)));
    ResizeHwndToClientArea(hwnd, dx, dy, false);
    DoLayout();
    HwndCenterDialog(hwnd, ownerHwnd);
    OnSelect();
    SetIsVisible(true);
    restoreOwner = IsWindowEnabled(ownerHwnd);
    if (restoreOwner) EnableWindow(ownerHwnd, FALSE);
    return true;
}

static void ShowTransfer(MainWindow* owner, bool importing) {
    if (gTransferWnd) {
        HwndSetFocus(gTransferWnd->hwnd);
        return;
    }
    if (!IsMainWindowValidAndNotClosing(owner) || !owner->IsDocLoaded()) return;
    auto* display = owner->AsFixed();
    auto* engine = display ? display->GetEngine() : nullptr;
    if (!engine || !EngineMupdfSupportsAnnotations(engine)) return;
    auto* wnd = new TransferWnd();
    wnd->closeOnEsc = true;
    wnd->onBeforeDelete = MkFunc0Void(ClearTransferWnd);
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(CloseTransferWnd);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(DestroyTransferWnd);
    wnd->onKeyDown = MkMethod1<TransferWnd, KeyEvent*, &TransferWnd::OnKey>(wnd);
    wnd->SetFont(GetAppFont());
    if (!wnd->Create(owner, engine, importing)) {
        delete wnd;
        return;
    }
    gTransferWnd = wnd;
    wnd->SetFocusTo(wnd->save);
}
void ExportAnnotationsDialog(MainWindow* owner) {
    ShowTransfer(owner, false);
}
void ImportAnnotationsDialog(MainWindow* owner) {
    ShowTransfer(owner, true);
}
void CloseAnnotationTransferDialog(MainWindow* owner) {
    if (gTransferWnd && (!owner || gTransferWnd->owner == owner)) gTransferWnd->OnCancel();
}
