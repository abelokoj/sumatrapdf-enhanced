/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// A dialog for editing advanced settings, driven by the settings metadata
// (gSettingsInfo). Shows a filterable list of settings; clicking a bool
// toggles it, clicking an enum cycles its allowed values, clicking a string /
// color / number setting edits it in-place (Enter confirms, Esc cancels).
// Save writes the settings file and reloads it (so all derived state is
// re-computed); Cancel abandons the changes.

#include "base/Base.h"
#include "base/Win.h"
#include "gui/Dpi.h"
#include "base/UITask.h"
#include "base/SettingsUtil.h"

#include "gui/UIModels.h"
#include "gui/Layout.h"
#include "gui/win/WinGui.h"
#include "gui/PlatformFont.h"
#include "gui/Gfx.h"
#include "gui/GuiColors.h"
#include "gui/VirtCtrl.h"

#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "DisplayMode.h"
#include "DocController.h"
#include "EngineBase.h"
#include "MainWindow.h"
#include "PdfDarkMode.h"
#include "Theme.h"
#include "SvgIcons.h"
#include "Commands.h"
#include "Toolbar.h"
#include "Tabs.h"
#include "FileThumbnails.h"
#include "DarkMode.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "FilterHighlightDraw.h"
#if IS_DEBUG
#include "RenderCache.h"
#include "base/tests/UtAssert.h"
#endif
#include "SumatraDialogs.h"

constexpr const char* kSettingsDocsUrl = "https://www.sumatrapdfreader.org/settings/settings3-7.html";
// smallest client width the user can drag the dialog to (96 dpi pixels)
constexpr int kAdvSettingsMinClientDx = 480;

// a single editable setting; the pending (possibly edited) value is kept
// here and only written back on Save. The field is addressed by its offset
// into gSettings, not a pointer: a settings reload (file watcher, Save)
// frees and re-creates gSettings while the dialog stays open
namespace {
struct SettingItem {
    Str name;    // dotted path, e.g. "FixedPageUI.TextColor", owned
    Str comment; // doc comment describing the setting, owned
    SettingType type = SettingType::Bool;
    int fieldOffset = 0;
    const char** enumValues = nullptr; // non-null for enum (string) settings

    // pending value; strVal (owned) is used for String, Color and Compact
    bool boolVal = false;
    int intVal = 0;
    float floatVal = 0;
    Str strVal;
    const StructInfo* compactInfo = nullptr; // Compact: WindowMargin, PageSpacing, …

    // default value from the settings metadata; defStr (owned) for String/Color
    bool defBool = false;
    int defInt = 0;
    float defFloat = 0;
    Str defStr;

    bool changed = false; // pending value differs from what was loaded (this session)
    bool previewed = false;
    int previewOriginalInt = 0;
    int previewValueInt = 0;
    Str previewOriginal;
    Str previewValue;
    Str previewLabel;
    PlatformFont* previewFont = nullptr;
    int previewLabelWidth = 0;

    ~SettingItem() {
        str::Free(name);
        str::Free(comment);
        str::Free(strVal);
        str::Free(defStr);
        str::Free(previewOriginal);
        str::Free(previewValue);
        str::Free(previewLabel);
    }
};
} // namespace

static bool CompactIsAllInts(const StructInfo* info) {
    if (!info || info->fieldCount == 0) {
        return false;
    }
    for (size_t i = 0; i < info->fieldCount; i++) {
        if (info->fields[i].type != SettingType::Int) {
            return false;
        }
    }
    return true;
}

static TempStr FormatCompactIntsTemp(const StructInfo* info, const u8* base, bool useDefault) {
    str::Builder b;
    for (size_t i = 0; i < info->fieldCount; i++) {
        const FieldInfo& f = info->fields[i];
        if (i > 0) {
            b.AppendChar(' ');
        }
        int val = useDefault ? (int)f.value : *(const int*)(base + f.offset);
        b.Append(fmt("%d", val));
    }
    return ToStrTemp(b);
}

static void ApplyCompactInts(const StructInfo* info, u8* base, Str s) {
    int off = 0;
    for (size_t i = 0; i < info->fieldCount; i++) {
        const FieldInfo& f = info->fields[i];
        if (f.type != SettingType::Int) {
            continue;
        }
        Str rest = Str(s.s + off, len(s) - off);
        off += str::TrimWs(rest);
        int val = (int)f.value;
        if (len(rest) > 0) {
            val = ParseInt(rest);
            off += str::TrimNonWs(rest);
        }
        *(int*)(base + f.offset) = val;
    }
}

static bool StrValsEq(Str a, Str b) {
    if (len(a) == 0 && len(b) == 0) {
        return true;
    }
    return str::Eq(a, b);
}

// true if the pending value differs from the setting's default value
static bool SettingDiffersFromDefault(SettingItem* item) {
    switch (item->type) {
        case SettingType::Bool:
            return item->boolVal != item->defBool;
        case SettingType::Int:
            return item->intVal != item->defInt;
        case SettingType::Float:
            return item->floatVal != item->defFloat;
        default:
            return !StrValsEq(item->strVal, item->defStr);
    }
}

// value of the setting formatted for display; the result is temp-allocated
static TempStr FormatSettingValueTemp(SettingItem* item) {
    switch (item->type) {
        case SettingType::Bool:
            return str::DupTemp(item->boolVal ? StrL("true") : StrL("false"));
        case SettingType::Int:
            return fmt("%d", item->intVal);
        case SettingType::Float:
            return fmt("%g", item->floatVal);
        default:
            return str::DupTemp(item->strVal);
    }
}

static TempStr FormatSettingDefaultTemp(SettingItem* item) {
    switch (item->type) {
        case SettingType::Bool:
            return str::DupTemp(item->defBool ? StrL("true") : StrL("false"));
        case SettingType::Int:
            return fmt("%d", item->defInt);
        case SettingType::Float:
            return fmt("%g", item->defFloat);
        default:
            return str::DupTemp(item->defStr);
    }
}

// drop every preview and render with the saved settings again
static void EndPreviewSettingChange() {
    ClearDocumentColorsFollowThemePreview();
    UpdateDocumentColors();
}

static void SetItemChanged(SettingItem* item) {
    u8* p = SettingFieldPtr(item->fieldOffset);
    switch (item->type) {
        case SettingType::Bool:
            item->changed = item->boolVal != *(bool*)p;
            break;
        case SettingType::Int:
            item->changed = item->intVal != (item->previewed ? item->previewOriginalInt : *(int*)p);
            break;
        case SettingType::Float:
            item->changed = item->floatVal != *(float*)p;
            break;
        case SettingType::Compact:
            item->changed = !StrValsEq(item->strVal, FormatCompactIntsTemp(item->compactInfo, p, false));
            break;
        default: {
            Str curr = item->previewed ? item->previewOriginal : *(Str*)p;
            bool bothEmpty = len(item->strVal) == 0 && len(curr) == 0;
            item->changed = !bothEmpty && !str::Eq(item->strVal, curr);
            break;
        }
    }
}

// one dialog item per editable setting, seeded with the current value
static void CollectSettings(Vec<SettingItem*>& items) {
    Vec<SettingField> fields;
    CollectSettingFields(fields);
    for (const SettingField& sf : fields) {
        const FieldInfo& field = *sf.field;
        u8* fieldPtr = SettingFieldPtr(sf.offset);
        if (field.type == SettingType::Compact) {
            // only all-int compact structs; the "Open Settings File" button
            // covers the rest
            const auto* sub = (const StructInfo*)field.value;
            if (!CompactIsAllInts(sub)) {
                continue;
            }
            auto* item = new SettingItem();
            item->name = str::Dup(sf.path);
            item->comment = str::Dup(sf.comment);
            item->type = field.type;
            item->fieldOffset = sf.offset;
            item->compactInfo = sub;
            item->strVal = str::Dup(FormatCompactIntsTemp(sub, fieldPtr, false));
            item->defStr = str::Dup(FormatCompactIntsTemp(sub, fieldPtr, true));
            VecAppend(items, item);
            continue;
        }

        auto* item = new SettingItem();
        item->name = str::Dup(sf.path);
        item->comment = str::Dup(sf.comment);
        item->type = field.type;
        item->fieldOffset = sf.offset;
        // field.value holds the default: the value itself for Bool/Int,
        // a string pointer for Float/String/Color (null == empty). It's
        // NOT a valid pointer for Bool/Int, so only deref it for the
        // string-backed types.
        switch (field.type) {
            case SettingType::Bool:
                item->boolVal = *(bool*)fieldPtr;
                item->defBool = field.value != 0;
                break;
            case SettingType::Int:
                item->intVal = *(int*)fieldPtr;
                item->defInt = (int)field.value;
                if (str::EqI(item->name, StrL("AnnotationModeIconStyle"))) {
                    static const char* choices[] = {"1", "2", nullptr};
                    item->enumValues = choices;
                }
                if (str::EqI(item->name, StrL("AnnotationToolbarStyle"))) {
                    static const char* choices[] = {"1", "2", nullptr};
                    item->enumValues = choices;
                }
                if (str::EqI(item->name, StrL("AnnotationPageNumberStyle")) ||
                    str::EqI(item->name, StrL("AnnotationThreeColumnPageStyle"))) {
                    static const char* choices[] = {"1", "2", "3", nullptr};
                    item->enumValues = choices;
                }
                break;
            case SettingType::Float:
                item->floatVal = *(float*)fieldPtr;
                str::Parse(Str((const char*)field.value), "%f", &item->defFloat);
                break;
            default:
                item->strVal = str::Dup(*(Str*)fieldPtr);
                item->defStr = str::Dup(Str((const char*)field.value));
                if (field.type == SettingType::String) {
                    item->enumValues = GetSettingsEnumValues(sf.path);
                }
                break;
        }
        VecAppend(items, item);
    }
}

struct ListBoxModelSettings : ListBoxModel {
    Vec<SettingItem*>* items = nullptr; // not owned
    Vec<int> filtered;                  // indexes into items

    ~ListBoxModelSettings() override = default;
    int ItemsCount() override { return len(filtered); }
    Str Item(int i) override { return (*items)[filtered[i]]->name; }
    SettingItem* ItemAt(int i) {
        if (i < 0 || i >= len(filtered)) {
            return nullptr;
        }
        return (*items)[filtered[i]];
    }
};

struct AdvancedSettingsWnd : WindowBase {
    ~AdvancedSettingsWnd() override;

    PlatformFont* fontBold = nullptr;
    MainWindow* win = nullptr;

    // the list, the doc comment, the hints and the buttons are virtual
    // controls; the filter and the in-place editors are real HWNDs
    Edit* editFilter = nullptr;
    VirtListBox* listBox = nullptr;
    ListBoxModelSettings* model = nullptr;     // owned by listBox
    VirtFixedLinesText* commentText = nullptr; // shows the selected setting's doc comment
    // "changed settings: n" under the list, above the setting help; collapsed when n == 0
    ILayout* changedCountRow = nullptr;
    VirtFill* changedCountBg = nullptr;
    VirtText* changedCountText = nullptr;
    Edit* editValue = nullptr;         // in-place value editor, created on demand
    DropDown* dropDownValue = nullptr; // in-place enum editor, created on demand
    Str dropDownOrigVal;               // value before the drop-down opened (owned), for Esc
    int editItemIdx = -1;              // index into items of the setting being edited
    // Create/show/focus of the in-place editor can re-enter CancelEditValue
    // (filter EN_CHANGE, selection change) while the local editor pointer is
    // still in use — that was a heap UAF (CloseEnumEdit delete). While set,
    // CancelEditValue / CloseEnumEdit only mark pendingCancelEdit and return.
    bool editCreating = false;
    bool pendingCancelEdit = false;

    // bottom buttons (layout-owned; Tab order Save → Cancel → Open → Help)
    VirtButton* btnSave = nullptr;
    VirtButton* btnCancel = nullptr;
    VirtButton* btnOpenSettingsFile = nullptr;
    VirtButton* btnHelp = nullptr;

    // filter tokens for list matching + match underlay paint (command-palette style)
    StrVec filterWords;
    Vec<u8> highlighted; // scratch for DrawMaybeHighlightedText

    Vec<SettingItem*> items;
    bool appearancePreviewActive = false;
    Str previewTheme;
    Str previewLastLight;
    Str previewLastDark;
    Str previewCurrentLight;
    Str previewCurrentDark;

    bool Create(MainWindow* win);
    void OnKeyDown(KeyEvent* ev);
    bool HandleEscapeKey(bool isEditingValue, bool isEditingEnum);
    bool HandleTabKey(bool isEditingValue, bool isEditingEnum);
    bool HandleEnterKey(bool isEditingValue, bool isEditingEnum);
    bool HandleUpDownKey(const KeyEvent& ev);
    void OnSize(WindowBase::SizeEvent* ev);
    void OnGetMinMaxInfo(WindowBase::GetMinMaxInfoEvent* ev);

    void QueryChanged();
    int CountChangedSettings() const;
    void UpdateChangedCountLabel();
    void NoteItemEdited(SettingItem* item);
    void DrawListBoxItem(VirtListBox::DrawItemEvent* ev);
    void ActivateItem(int lbIdx);
    void OnItemDoubleClicked();
    void OnSelectionChanged();

    Rect ValueRectForItem(int idx); // in listBox client coords
    void BeginEditValue(int idx);
    void CommitEditValue();
    void CancelEditValue();

    void BeginEditEnum(int idx);
    void OnEnumSelectionChanged();
    void OnEnumDropDownClosed();
    void CheckDropDownClosed();
    void CloseEnumEdit(bool keepValue);
    void KeepFocus();
    void RelayoutInPlaceEditor();
    void OnEditValueKillFocus();
    void OnValueTextChanged();
    bool LiveAppearanceEnabled() const;
    void ScheduleAppearancePreview();
    void ApplyAppearancePreview();
    void OnAppearanceTimer(WindowBase::TimerEvent*);
    void SuspendAppearancePreview();
    void ResumeAppearancePreview();
    void CancelAppearanceChanges();
    void PostedRefocusEditValue();

    void OnOpenSettingsFile(VirtMouseEvent* ev = nullptr);
    void OnHelp(VirtMouseEvent* ev = nullptr);
    void OnCancel(VirtMouseEvent* ev = nullptr);
    void OnSave(VirtMouseEvent* ev = nullptr);

    void ApplyChangesAndSave();
};

static AdvancedSettingsWnd* gAdvancedSettingsWnd = nullptr;

AdvancedSettingsWnd::~AdvancedSettingsWnd() {
    // editFilter, listBox and commentText are added to the layout; VBox owns
    // and deletes its children, and the base WindowBase::~WindowBase() deletes `layout`, so
    // they must not be deleted here (doing so is a double-free). editValue and
    // dropDownValue are created on demand and are not part of the layout, so
    // they are freed explicitly.
    // covers every way the dialog goes away (Save, Cancel, Esc, the close box).
    // On Save the value is in gSettings by now, so dropping the preview
    // renders the same thing; on every other path it undoes the preview.
    CancelAppearanceChanges();
    EndPreviewSettingChange();
    delete editValue;
    delete dropDownValue;
    DeleteVecMembers(items);
    str::Free(dropDownOrigVal);
    str::Free(previewTheme);
    str::Free(previewLastLight);
    str::Free(previewLastDark);
    str::Free(previewCurrentLight);
    str::Free(previewCurrentDark);
}

static void ClearAdvancedSettingsWnd() {
    gAdvancedSettingsWnd = nullptr;
}

// Match setting name against a pre-split filter so "use tabs" hits "UseTabs".
// words is empty when the filter is empty or only whitespace (match all).
// SplitFilterToWords already trims; do not ContainsI against the raw edit text
// ("use " would miss "UseTabs" because of the trailing space).
static bool SettingNameMatchesFilter(Str name, const StrVec& words) {
    if (len(words) == 0) {
        return true;
    }
    // Single token: continuous case-insensitive substring ("use" / "use " → UseTabs)
    if (len(words) == 1) {
        return str::ContainsI(name, words[0]);
    }
    // Multi-word: every word must appear (camelCase-friendly; order-independent)
    return FilterMatches(name, words);
}

// Keep the metadata order within each group, but put customized settings first
// so the values users are most likely to review are immediately visible.
static void CollectFilteredSettings(Vec<SettingItem*>& items, const StrVec& words, Vec<int>& filtered) {
    VecReset(filtered);
    for (int group = 0; group < 2; group++) {
        bool wantNonDefault = group == 0;
        int n = len(items);
        for (int i = 0; i < n; i++) {
            SettingItem* item = items[i];
            if (SettingDiffersFromDefault(item) == wantNonDefault && SettingNameMatchesFilter(item->name, words)) {
                VecAppend(filtered, i);
            }
        }
    }
}

void AdvancedSettingsWnd::QueryChanged() {
    CancelEditValue();
    Str filter = editFilter->GetTextTemp();
    // split trims leading/trailing/internal runs of whitespace into words
    filterWords.Reset();
    SplitFilterToWords(filter, filterWords);
    CollectFilteredSettings(items, filterWords, model->filtered);
    listBox->SetModel(model); // resets selection to -1
    OnSelectionChanged();
}

// show the selected setting's doc comment in the text area below the list
void AdvancedSettingsWnd::OnSelectionChanged() {
    int lbSel = listBox->GetCurrentSelection();
    int selItemIdx = (lbSel >= 0 && lbSel < model->ItemsCount()) ? model->filtered[lbSel] : -1;
    // an in-place editor stays keyed to editItemIdx; when the selection moves to
    // a different setting, dismiss the editor (keeping the value). Guarding on
    // the index (rather than dismissing unconditionally) is important: creating
    // an editor can synchronously re-enter here (focus change / CB_SHOWDROPDOWN)
    // while the selection is still on the item being edited, and dismissing then
    // would free the editor mid-construction.
    if (editItemIdx >= 0 && editItemIdx != selItemIdx) {
        CommitEditValue();
        CloseEnumEdit(true);
    }
    if (!commentText) {
        return;
    }
    SettingItem* item = model->ItemAt(lbSel);
    commentText->SetText(item ? item->comment : Str(""));
}

// Split a list-item row into non-overlapping name (left) and value (right)
// columns so long names and long values (e.g. InverseSearchCmdLine) don't
// draw on top of each other (#5804).
static void AdvSettingsItemColumns(const Rect& rc, Rect& rcName, Rect& rcVal) {
    int pad = DpiScale(4);
    int gap = DpiScale(10);
    int totalW = rc.dx - (2 * pad);
    if (totalW < 1) {
        rcName = rc;
        rcVal = rc;
        return;
    }
    // Value column ~45% (min 100px); name gets the rest. Both are clipped with
    // ellipsis when the dialog is narrow.
    int minVal = DpiScale(100);
    int valW = std::max(totalW * 45 / 100, minVal);
    valW = std::min(valW, totalW * 3 / 5);
    int nameW = totalW - valW - gap;
    if (nameW < DpiScale(72)) {
        nameW = (totalW / 2) - (gap / 2);
        valW = totalW - nameW - gap;
    }
    nameW = std::max(nameW, 1);
    valW = std::max(valW, 1);

    rcName = rc;
    rcName.x += pad;
    rcName.dx = nameW;

    rcVal = rc;
    rcVal.x += rcVal.dx - pad - valW;
    rcVal.dx = valW;
}

enum class SettingPreviewKind {
    None,
    Color,
    Palette,
    Pin,
    Picker,
    Pen,
    Tab,
    File,
    Theme,
    AnnotationModeIcon,
    PageNumber,
    ThreeColumnPage,
    ToolbarStyle
};
static constexpr UINT_PTR kAdvAppearanceTimer = 71;
static constexpr UINT kAdvAppearanceDelay = 400;

static SettingPreviewKind SettingPreview(SettingItem* item) {
    if (item->type == SettingType::Color) return SettingPreviewKind::Color;
    if (str::EqI(item->name, StrL("Annotations.PresetColors")) || str::EqI(item->name, StrL("Annotations.InkColors")) ||
        str::EqI(item->name, StrL("LaserColors")) || str::EqI(item->name, StrL("CustomColors")))
        return SettingPreviewKind::Palette;
    if (str::EqI(item->name, StrL("PinIconStyle"))) return SettingPreviewKind::Pin;
    if (str::EqI(item->name, StrL("ColorPickerIconStyle"))) return SettingPreviewKind::Picker;
    if (str::EqI(item->name, StrL("PenCursorStyle"))) return SettingPreviewKind::Pen;
    if (str::EqI(item->name, StrL("ActiveTabStyle"))) return SettingPreviewKind::Tab;
    if (str::EqI(item->name, StrL("FileTypeIconStyle"))) return SettingPreviewKind::File;
    if (str::EqI(item->name, StrL("AnnotationModeIconStyle"))) return SettingPreviewKind::AnnotationModeIcon;
    if (str::EqI(item->name, StrL("AnnotationPageNumberStyle"))) return SettingPreviewKind::PageNumber;
    if (str::EqI(item->name, StrL("AnnotationThreeColumnPageStyle"))) return SettingPreviewKind::ThreeColumnPage;
    if (str::EqI(item->name, StrL("AnnotationToolbarStyle"))) return SettingPreviewKind::ToolbarStyle;
    if (str::EqI(item->name, StrL("Theme"))) return SettingPreviewKind::Theme;
    return SettingPreviewKind::None;
}

static int AdvPreviewWidth(SettingItem* item, Rect row) {
    auto kind = SettingPreview(item);
    if (kind == SettingPreviewKind::None) return 0;
    int side = std::max(8, row.dy - 2 * DpiScale(4));
    int count = kind == SettingPreviewKind::Palette || kind == SettingPreviewKind::Theme ? 3 : 1;
    if (kind == SettingPreviewKind::Tab) count = 2;
    if (kind == SettingPreviewKind::ThreeColumnPage) count = 2;
    return std::min((side + DpiScale(4)) * count, std::max(0, row.dx / 2));
}

static void SplitAdvPreview(SettingItem* item, Rect& value, Rect& preview) {
    int width = AdvPreviewWidth(item, value);
    preview = {value.Right() - width, value.y, width, value.dy};
    if (HwndIsRtl(gAdvancedSettingsWnd ? gAdvancedSettingsWnd->hwnd : nullptr)) {
        preview.x = value.x;
        value.x += width;
    }
    value.dx -= width;
}

static int SettingStyleValue(SettingItem* item, Str value) {
    int style = item->intVal;
    if (str::Parse(value, "%d%$", &style).s) return style;
    return -1;
}

static bool AppearanceValueValid(SettingItem* item, Str value) {
    auto kind = SettingPreview(item);
    if (kind == SettingPreviewKind::Color) {
        ParsedColor parsed;
        ParseColor(parsed, value);
        return len(value) == 0 || parsed.parsedOk;
    }
    if (kind == SettingPreviewKind::Palette) {
        Vec<Color> colors;
        ParseColorList(value, colors, 0);
        return len(value) == 0 || len(colors) > 0;
    }
    int style = SettingStyleValue(item, value);
    if (kind == SettingPreviewKind::Pin) return style == 3 || style == 4 || style == 10;
    if (kind == SettingPreviewKind::Picker) return style == 1 || style == 2 || style == 5 || style == 6 || style == 8;
    if (kind == SettingPreviewKind::Pen) return style == 9 || style == 11 || style == 12;
    if (kind == SettingPreviewKind::Tab) return style == 1 || style == 3 || style == 4;
    if (kind == SettingPreviewKind::File) return style >= 1 && style <= 3;
    if (kind == SettingPreviewKind::AnnotationModeIcon) return style == 1 || style == 2;
    if (kind == SettingPreviewKind::PageNumber) return style >= 1 && style <= 3;
    if (kind == SettingPreviewKind::ThreeColumnPage) return style >= 1 && style <= 3;
    if (kind == SettingPreviewKind::ToolbarStyle) return style == 1 || style == 2;
    if (kind == SettingPreviewKind::Theme) {
        if (str::EqI(value, StrL("System"))) return true;
        for (int i = 0; i < ThemeGetCount(); i++) {
            if (str::EqI(value, ThemeGetNameAt(i))) return true;
        }
    }
    return str::EqI(item->name, StrL("DocumentColorsFollowTheme")) &&
           (str::EqI(value, StrL("off")) || str::EqI(value, StrL("smart")) || str::EqI(value, StrL("legacy")));
}

static void SetPreviewField(SettingItem* item, Str value, int integer) {
    u8* field = SettingFieldPtr(item->fieldOffset);
    if (item->type == SettingType::Int) {
        *(int*)field = integer;
    } else if (item->type == SettingType::Color) {
        SetColorText(*(ParsedColor*)field, value);
    } else {
        str::ReplaceWithCopy((Str*)field, value);
    }
}

enum AppearanceRefresh : unsigned {
    RefreshNone = 0,
    RefreshCanvas = 1,
    RefreshTabs = 2,
    RefreshIcons = 4,
    RefreshTheme = 8,
    RefreshToolbarLayout = 16,
};

static unsigned PreviewRefresh(SettingItem* item) {
    auto kind = SettingPreview(item);
    if (kind == SettingPreviewKind::File) return RefreshNone;
    if (kind == SettingPreviewKind::PageNumber || kind == SettingPreviewKind::ThreeColumnPage ||
        kind == SettingPreviewKind::ToolbarStyle)
        return RefreshIcons | RefreshToolbarLayout;
    if (kind == SettingPreviewKind::Pin || kind == SettingPreviewKind::Picker ||
        kind == SettingPreviewKind::AnnotationModeIcon)
        return RefreshIcons;
    if (kind == SettingPreviewKind::Tab || str::EqI(item->name, StrL("ActiveTabAccent"))) return RefreshTabs;
    if (kind == SettingPreviewKind::Pen || str::EqI(item->name, StrL("PenCursorAccent")) ||
        str::EqI(item->name, StrL("FixedPageUI.SelectionColor")))
        return RefreshCanvas;
    if (str::StartsWithI(item->name, StrL("Annotations.")) || kind == SettingPreviewKind::Palette) return RefreshIcons;
    return RefreshTheme;
}

static void RepaintAdvanced(unsigned refresh) {
    if (refresh & RefreshTheme) {
        SetCurrentThemeFromSettings();
        SumatraUpdateTheme();
        return;
    }
    if (refresh & RefreshTabs) SetCurrentThemeFromSettings();
    for (MainWindow* window : gWindows) {
        if (refresh & RefreshTabs) UpdateTabWidth(window);
        if (refresh & RefreshIcons) UpdateToolbarAfterThemeChange(window);
        if (refresh & RefreshToolbarLayout) ScheduleUiUpdate(window, kUiForceRelayout | kUiToolbarDirty);
        if (refresh & RefreshCanvas) InvalidateRect(window->hwndCanvas, nullptr, FALSE);
    }
    if (refresh & RefreshIcons) MaybeRedrawHomePage();
}

bool AdvancedSettingsWnd::LiveAppearanceEnabled() const {
    for (SettingItem* item : items) {
        if (str::EqI(item->name, StrL("LiveAppearancePreview"))) return item->boolVal;
    }
    return gSettings && gSettings->liveAppearancePreview;
}

void AdvancedSettingsWnd::SuspendAppearancePreview() {
    for (SettingItem* item : items) {
        if (item->previewed) SetPreviewField(item, item->previewOriginal, item->previewOriginalInt);
    }
    if (previewTheme) {
        str::ReplaceWithCopy(&gSettings->theme, previewTheme);
        str::ReplaceWithCopy(&gSettings->lastLightTheme, previewLastLight);
        str::ReplaceWithCopy(&gSettings->lastDarkTheme, previewLastDark);
    }
}

void AdvancedSettingsWnd::ResumeAppearancePreview() {
    for (SettingItem* item : items) {
        if (item->previewed) SetPreviewField(item, item->previewValue, item->previewValueInt);
    }
    if (previewTheme) {
        str::ReplaceWithCopy(&gSettings->lastLightTheme, previewCurrentLight);
        str::ReplaceWithCopy(&gSettings->lastDarkTheme, previewCurrentDark);
    }
}

void AdvancedSettingsWnd::CancelAppearanceChanges() {
    if (hwnd) KillTimer(hwnd, kAdvAppearanceTimer);
    if (!appearancePreviewActive) return;
    SuspendAppearancePreview();
    appearancePreviewActive = false;
    EndAppearancePreview(this);
    bool hadTheme = len(previewTheme) > 0;
    unsigned refresh = RefreshNone;
    for (SettingItem* item : items) {
        if (item->previewed) refresh |= PreviewRefresh(item);
        item->previewed = false;
        str::Free(item->previewOriginal);
        item->previewOriginal = {};
        str::Free(item->previewValue);
        item->previewValue = {};
    }
    RepaintAdvanced(refresh);
    if (hadTheme) {
        str::ReplaceWithCopy(&gSettings->lastLightTheme, previewLastLight);
        str::ReplaceWithCopy(&gSettings->lastDarkTheme, previewLastDark);
    }
    str::Free(previewTheme);
    previewTheme = {};
    if (hwnd && !deleteScheduled) {
        if (refresh & RefreshTheme)
            UpdateTheme();
        else if (listBox)
            listBox->Invalidate();
        KeepFocus();
    }
}

void AdvancedSettingsWnd::ScheduleAppearancePreview() {
    if (!hwnd) return;
    KillTimer(hwnd, kAdvAppearanceTimer);
    if (!LiveAppearanceEnabled()) {
        CancelAppearanceChanges();
        return;
    }
    SetTimer(hwnd, kAdvAppearanceTimer, kAdvAppearanceDelay, nullptr);
}

void AdvancedSettingsWnd::OnAppearanceTimer(WindowBase::TimerEvent* ev) {
    if (ev->timerId != kAdvAppearanceTimer) return;
    KillTimer(hwnd, kAdvAppearanceTimer);
    ApplyAppearancePreview();
}

void AdvancedSettingsWnd::ApplyAppearancePreview() {
    if (!LiveAppearanceEnabled()) return;
    bool updated = false;
    unsigned refresh = RefreshNone;
    for (int i = 0; i < len(items); i++) {
        SettingItem* item = items[i];
        bool editing = editValue && editItemIdx == i;
        if (!item->changed && !item->previewed && !editing) continue;
        Str value = editing ? editValue->GetTextTemp() : FormatSettingValueTemp(item);
        if (!AppearanceValueValid(item, value)) continue;
        u8* field = SettingFieldPtr(item->fieldOffset);
        int integer = item->type == SettingType::Int ? SettingStyleValue(item, value) : 0;
        bool equal = item->type == SettingType::Int ? *(int*)field == integer : StrValsEq(*(Str*)field, value);
        if (equal) continue;
        if (!appearancePreviewActive) {
            BeginAppearancePreview(this,
                                   MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::CancelAppearanceChanges>(this),
                                   MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::SuspendAppearancePreview>(this),
                                   MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::ResumeAppearancePreview>(this));
            appearancePreviewActive = true;
            str::ReplaceWithCopy(&previewTheme, gSettings->theme);
            str::ReplaceWithCopy(&previewLastLight, gSettings->lastLightTheme);
            str::ReplaceWithCopy(&previewLastDark, gSettings->lastDarkTheme);
        }
        if (!item->previewed) {
            item->previewed = true;
            if (item->type == SettingType::Int)
                item->previewOriginalInt = *(int*)field;
            else
                str::ReplaceWithCopy(&item->previewOriginal, *(Str*)field);
        }
        item->previewValueInt = integer;
        str::ReplaceWithCopy(&item->previewValue, value);
        SetPreviewField(item, value, integer);
        refresh |= PreviewRefresh(item);
        updated = true;
    }
    if (!updated) return;
    RepaintAdvanced(refresh);
    str::ReplaceWithCopy(&previewCurrentLight, gSettings->lastLightTheme);
    str::ReplaceWithCopy(&previewCurrentDark, gSettings->lastDarkTheme);
    if (refresh & RefreshTheme)
        UpdateTheme();
    else if (listBox)
        listBox->Invalidate();
    KeepFocus();
}

static void DrawPageNumberPreview(Gfx* gfx, Rect bounds, int style, Color text, Color background) {
    int pad = std::max(1, bounds.dy / 12);
    int width = std::min(bounds.dx, bounds.dy * 4 / 5);
    Rect page{bounds.x + (bounds.dx - width) / 2, bounds.y, width, bounds.dy};
    int center = page.y + page.dy / 2;
    int gap = style == 3 ? pad : 0;
    Rect top{page.x, page.y, page.dx, (page.dy - gap) / 2};
    Rect bottom{page.x, top.Bottom() + gap, page.dx, page.Bottom() - top.Bottom() - gap};
    int radius = std::max(1, pad * 2);
    Color edge = AccentColor(background, 45);
    if (style == 3) {
        gfx->FillRoundedRect(top, radius, background, edge);
        gfx->FillRoundedRect(bottom, radius, background, edge);
    } else {
        gfx->FillRoundedRect(page, radius, background, edge);
        int inset = style == 1 ? page.dx / 3 : pad;
        gfx->FillRect({page.x + inset, center, page.dx - 2 * inset, 1}, edge);
    }
    int fontSize = std::max(4, page.dy / 3);
    PlatformFont* font = GetDefaultGuiFontOfSize(fontSize);
    gfx->DrawText(StrL("iii"), top, gfxTextCenter | gfxTextVCenter, font, text);
    gfx->DrawText(StrL("96"), bottom, gfxTextCenter | gfxTextVCenter, font, text);
}

static void DrawThreeColumnPagePreview(Gfx* gfx, Rect bounds, int style, Color text, Color background) {
    if (style == 1) {
        DrawPageNumberPreview(gfx, bounds, gSettings ? gSettings->annotationPageNumberStyle : 1, text, background);
        return;
    }
    int width = style == 2 ? bounds.dx : std::min(bounds.dx, std::max(bounds.dy, bounds.dx / 2));
    int height = std::min(bounds.dy, std::max(1, width / 2));
    Rect page{bounds.x + (bounds.dx - width) / 2, bounds.y + (bounds.dy - height) / 2, width, height};
    int center = page.x + page.dx / 2;
    Color edge = AccentColor(background, 45);
    gfx->FillRoundedRect(page, std::max(1, height / 6), background, edge);
    gfx->FillRect({center, page.y + 1, 1, std::max(1, page.dy - 2)}, edge);
    PlatformFont* font = GetDefaultGuiFontOfSize(std::max(4, height / 2));
    Rect current{page.x, page.y, page.dx / 2, page.dy};
    Rect total{center, page.y, page.Right() - center, page.dy};
    gfx->DrawText(StrL("iii"), current, gfxTextCenter | gfxTextVCenter, font, text);
    gfx->DrawText(StrL("96"), total, gfxTextCenter | gfxTextVCenter, font, text);
}

static void DrawToolbarStylePreview(Gfx* gfx, Rect bounds, int style, Color text, Color background) {
    int width = std::min(bounds.dx, bounds.dy * 3 / 4);
    int pad = std::max(1, bounds.dy / 12);
    Rect rail{bounds.x + (bounds.dx - width) / 2, bounds.y, width, bounds.dy};
    Color edge = AccentColor(background, 45);
    int panelHeight = std::max(1, (rail.dy - 2 * pad) / 3);
    if (style == 2) gfx->FillRoundedRect(rail, pad * 2, background, edge);
    for (int row = 0; row < 3; row++) {
        Rect panel{rail.x, rail.y + row * (panelHeight + pad), rail.dx, panelHeight};
        if (style == 1) gfx->FillRoundedRect(panel, pad, background, edge);
        int dot = std::max(1, panelHeight / 3);
        for (int column = 0; column < 2; column++) {
            int x = panel.x + panel.dx * (2 * column + 1) / 4 - dot / 2;
            gfx->FillEllipse({x, panel.y + (panel.dy - dot) / 2, dot, dot}, text);
        }
    }
}

static void DrawSettingPreview(Gfx* gfx, Rect bounds, SettingItem* item, Str value, Color text, Color background) {
    auto kind = SettingPreview(item);
    if (kind == SettingPreviewKind::None || bounds.IsEmpty() || !AppearanceValueValid(item, value)) return;
    int pad = DpiScale(4);
    bounds.dy = std::max(1, bounds.dy - 2 * pad);
    bounds.y += pad;
    bounds.dx = std::max(1, bounds.dx - pad);
    gfx->PushClip(bounds);
    if (kind == SettingPreviewKind::Color) {
        ParsedColor parsed;
        ParseColor(parsed, value);
        Color color = parsed.parsedOk ? parsed.col : kColorUnset;
        u8 r, g, b, alpha;
        UnpackPdfColor(parsed.pdfCol, r, g, b, alpha);
        Rect swatch{bounds.x, bounds.y, std::min(bounds.dx, bounds.dy), bounds.dy};
        if (parsed.parsedOk) DrawDropDownSwatch(gfx, swatch, color, alpha);
    } else if (kind == SettingPreviewKind::Palette) {
        Vec<Color> colors;
        ParseColorList(value, colors, 3);
        int side = std::min(bounds.dy, std::max(1, (bounds.dx - 2 * pad) / 3));
        for (int i = 0; i < len(colors); i++) {
            DrawDropDownSwatch(gfx, {bounds.x + i * (side + pad), bounds.y, side, bounds.dy}, colors[i]);
        }
    } else if (kind == SettingPreviewKind::Theme) {
        Color foreground, control, accent;
        int index = -2;
        if (str::EqI(value, StrL("System"))) index = -1;
        for (int i = 0; i < ThemeGetCount(); i++) {
            if (str::EqI(value, ThemeGetNameAt(i))) index = i;
        }
        if (ThemeGetPaletteAt(index, foreground, control, accent)) {
            gfx->FillRoundedRect(bounds, std::max(2, bounds.dy / 5), control, AccentColor(control, 45));
            int inset = std::max(2, bounds.dy / 5);
            gfx->FillRect({bounds.x + inset, bounds.y + inset, std::max(1, bounds.dx - 2 * inset), 2}, foreground);
            gfx->FillRect({bounds.x + inset, bounds.Bottom() - inset - 2, std::max(1, bounds.dx / 2), 2}, accent);
        }
    } else if (kind == SettingPreviewKind::PageNumber) {
        DrawPageNumberPreview(gfx, bounds, SettingStyleValue(item, value), text, background);
    } else if (kind == SettingPreviewKind::ThreeColumnPage) {
        DrawThreeColumnPagePreview(gfx, bounds, SettingStyleValue(item, value), text, background);
    } else if (kind == SettingPreviewKind::ToolbarStyle) {
        DrawToolbarStylePreview(gfx, bounds, SettingStyleValue(item, value), text, background);
    } else {
        int style = SettingStyleValue(item, value);
        Str svg;
        switch (kind) {
            case SettingPreviewKind::Pin:
                svg = Str(GetPinIconSvg((PinIconStyle)style));
                break;
            case SettingPreviewKind::Picker:
                svg = Str(GetColorPickerIconSvg((ColorPickerIconStyle)style));
                break;
            case SettingPreviewKind::Pen: {
                ParsedColor color = gSettings ? gSettings->penCursorAccent : ParsedColor{};
                Color accent = GetParsedColor(color, MkRgb(0x26, 0x75, 0xe8));
                svg = GetPenCursorSvgTemp((PenCursorStyle)style, accent, text);
                break;
            }
            case SettingPreviewKind::Tab: {
                ParsedColor color = gSettings ? gSettings->activeTabAccent : ParsedColor{};
                Color accent = GetParsedColor(color, MkRgb(0x26, 0x75, 0xe8));
                svg = GetActiveTabPreviewSvgTemp(style, accent, background, text);
                break;
            }
            case SettingPreviewKind::File: {
                Rect page = bounds;
                page.dx = std::min(page.dx, page.dy);
                gfx->DrawPixmap(GetFileTypeIconPreview(style), page);
                break;
            }
            case SettingPreviewKind::AnnotationModeIcon: {
                int size = std::min(bounds.dx, bounds.dy);
                gfx->DrawPixmap(AnnotationModeIconPixmap(size, text, style), {bounds.x, bounds.y, size, size});
                break;
            }
            default:
                break;
        }
        if (svg) gfx->DrawPixmap(GetCachedPixmapForSvg(svg, bounds.dx, bounds.dy, text), bounds);
    }
    gfx->PopClip();
}

#if IS_DEBUG
void AdvancedAppearancePreviewTest() {
    RenderCache* savedCache = gRenderCache;
    if (!savedCache) gRenderCache = new RenderCache();
    defer {
        if (!savedCache) {
            delete gRenderCache;
            gRenderCache = nullptr;
        }
    };
    SettingItem item;
    item.type = SettingType::Color;
    item.name = str::Dup(StrL("FixedPageUI.BackgroundColor"));
    utassert(SettingPreview(&item) == SettingPreviewKind::Color);
    const struct {
        Str name;
        SettingPreviewKind kind;
    } names[] = {
        {StrL("Annotations.PresetColors"), SettingPreviewKind::Palette},
        {StrL("Annotations.InkColors"), SettingPreviewKind::Palette},
        {StrL("LaserColors"), SettingPreviewKind::Palette},
        {StrL("CustomColors"), SettingPreviewKind::Palette},
        {StrL("PinIconStyle"), SettingPreviewKind::Pin},
        {StrL("ColorPickerIconStyle"), SettingPreviewKind::Picker},
        {StrL("PenCursorStyle"), SettingPreviewKind::Pen},
        {StrL("ActiveTabStyle"), SettingPreviewKind::Tab},
        {StrL("FileTypeIconStyle"), SettingPreviewKind::File},
        {StrL("AnnotationModeIconStyle"), SettingPreviewKind::AnnotationModeIcon},
        {StrL("AnnotationPageNumberStyle"), SettingPreviewKind::PageNumber},
        {StrL("AnnotationThreeColumnPageStyle"), SettingPreviewKind::ThreeColumnPage},
        {StrL("AnnotationToolbarStyle"), SettingPreviewKind::ToolbarStyle},
        {StrL("Theme"), SettingPreviewKind::Theme},
        {StrL("InverseSearchCmdLine"), SettingPreviewKind::None},
    };
    item.type = SettingType::String;
    for (auto& entry : names) {
        str::ReplaceWithCopy(&item.name, entry.name);
        utassert(SettingPreview(&item) == entry.kind);
    }
    item.type = SettingType::Int;
    item.intVal = 10;
    utassert(SettingStyleValue(&item, StrL("3")) == 3);
    utassert(SettingStyleValue(&item, StrL("4")) == 4);
    utassert(SettingStyleValue(&item, StrL("3junk")) == -1);
    utassert(SettingStyleValue(&item, StrL("")) == -1);
    str::ReplaceWithCopy(&item.name, StrL("AnnotationPageNumberStyle"));
    for (Str value : {StrL("1"), StrL("2"), StrL("3")}) utassert(AppearanceValueValid(&item, value));
    for (Str value : {StrL("0"), StrL("4"), StrL("1junk"), StrL("1.0"), StrL("")})
        utassert(!AppearanceValueValid(&item, value));
    utassert(PreviewRefresh(&item) == (RefreshIcons | RefreshToolbarLayout));
    Settings* defaults = NewSettings({});
    utassert(defaults->annotationPageNumberStyle == 1);
    utassert(defaults->annotationToolbarStyle == 1);
    utassert(defaults->annotationThreeColumnPageStyle == 1);
    DeleteSettings(defaults);
    for (int value : {-1, 0, 1, 2, 3, 4}) {
        Settings* prefs = NewSettings(fmt("AnnotationPageNumberStyle = %d\n", value));
        utassert(prefs->annotationPageNumberStyle == limitValue(value, 1, 3));
        Str serialized = SerializeSettings(prefs, {});
        Settings* restored = NewSettings(serialized);
        utassert(restored->annotationPageNumberStyle == prefs->annotationPageNumberStyle);
        DeleteSettings(restored);
        str::Free(serialized);
        DeleteSettings(prefs);
    }
    str::ReplaceWithCopy(&item.name, StrL("AnnotationThreeColumnPageStyle"));
    for (Str value : {StrL("1"), StrL("2"), StrL("3")}) utassert(AppearanceValueValid(&item, value));
    for (Str value : {StrL("0"), StrL("4"), StrL("-1"), StrL("1junk"), StrL("1.0"), StrL("")})
        utassert(!AppearanceValueValid(&item, value));
    utassert(PreviewRefresh(&item) == (RefreshIcons | RefreshToolbarLayout));
    for (int value : {-1, 0, 1, 2, 3, 4}) {
        Settings* prefs = NewSettings(fmt("AnnotationThreeColumnPageStyle = %d\n", value));
        utassert(prefs->annotationThreeColumnPageStyle == limitValue(value, 1, 3));
        utassert(prefs->annotationPageNumberStyle == 1 && prefs->annotationToolbarColumns == 1);
        Str serialized = SerializeSettings(prefs, {});
        Settings* restored = NewSettings(serialized);
        utassert(restored->annotationThreeColumnPageStyle == prefs->annotationThreeColumnPageStyle);
        DeleteSettings(restored);
        str::Free(serialized);
        DeleteSettings(prefs);
    }
    str::ReplaceWithCopy(&item.name, StrL("AnnotationToolbarStyle"));
    for (Str value : {StrL("1"), StrL("2")}) utassert(AppearanceValueValid(&item, value));
    for (Str value : {StrL("0"), StrL("3"), StrL("-1"), StrL("1junk"), StrL("1.0"), StrL("")})
        utassert(!AppearanceValueValid(&item, value));
    utassert(PreviewRefresh(&item) == (RefreshIcons | RefreshToolbarLayout));
    for (int value : {-1, 0, 1, 2, 3}) {
        Settings* prefs = NewSettings(fmt("AnnotationToolbarStyle = %d\n", value));
        utassert(prefs->annotationToolbarStyle == limitValue(value, 1, 2));
        Str serialized = SerializeSettings(prefs, {});
        Settings* restored = NewSettings(serialized);
        utassert(restored->annotationToolbarStyle == prefs->annotationToolbarStyle);
        DeleteSettings(restored);
        str::Free(serialized);
        DeleteSettings(prefs);
    }

    int oldDpiX = dpiX, oldDpiY = dpiY;
    for (int dpi : {96, 144, 192}) {
        DpiSet(dpi, dpi);
        for (int width : {100, 220, 400}) {
            item.type = SettingType::Color;
            Rect value{12, 3, width, DpiScale(28)};
            Rect preview;
            SplitAdvPreview(&item, value, preview);
            utassert(preview.dx > 0 && value.dx > 0);
            utassert(value.x == 12);
            utassert(value.Right() == preview.x && preview.Right() == 12 + width);
            utassert(value.x >= 12 && value.Right() <= 12 + width);
        }
    }
    DpiSet(96, 96);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 120;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    utassert(dc && bitmap && bits);
    if (dc && bitmap && bits) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        GfxHdc gfx(dc);
        gfx.FillRect({0, 0, 120, 32}, kColWhite);
        item.type = SettingType::Color;
        DrawSettingPreview(&gfx, {0, 0, 40, 30}, &item, StrL("#123456"), kColBlack, kColWhite);
        utassert(GetPixel(dc, 11, 15) == MkRgb(0x12, 0x34, 0x56));
        DrawSettingPreview(&gfx, {0, 0, 40, 30}, &item, StrL("#00123456"), kColBlack, kColWhite);
        utassert(GetPixel(dc, 11, 15) != MkRgb(0x12, 0x34, 0x56));
        item.type = SettingType::String;
        str::ReplaceWithCopy(&item.name, StrL("Annotations.InkColors"));
        DrawSettingPreview(&gfx, {0, 0, 90, 30}, &item, StrL("#ff0000 #00ff00 #0000ff"), kColBlack, kColWhite);
        utassert(GetPixel(dc, 12, 15) == kColRed);
        utassert(GetPixel(dc, 40, 15) == MkRgb(0, 255, 0));
        utassert(GetPixel(dc, 68, 15) == kColBlue);
        SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    DpiSet(oldDpiX, oldDpiY);
    if (gSettings) {
        AdvancedSettingsWnd window;
        CollectSettings(window.items);
        SettingItem* style = nullptr;
        SettingItem* live = nullptr;
        SettingItem* pageNumber = nullptr;
        SettingItem* threeColumnPage = nullptr;
        SettingItem* toolbarStyle = nullptr;
        for (auto* setting : window.items) {
            if (str::EqI(setting->name, StrL("ActiveTabStyle"))) style = setting;
            if (str::EqI(setting->name, StrL("LiveAppearancePreview"))) live = setting;
            if (str::EqI(setting->name, StrL("AnnotationPageNumberStyle"))) pageNumber = setting;
            if (str::EqI(setting->name, StrL("AnnotationThreeColumnPageStyle"))) threeColumnPage = setting;
            if (str::EqI(setting->name, StrL("AnnotationToolbarStyle"))) toolbarStyle = setting;
        }
        utassert(style && live);
        if (style && live) {
            int original = gSettings->activeTabStyle;
            style->intVal = original == 4 ? 1 : 4;
            SetItemChanged(style);
            live->boolVal = false;
            window.ApplyAppearancePreview();
            utassert(gSettings->activeTabStyle == original);
            live->boolVal = true;
            window.ApplyAppearancePreview();
            utassert(gSettings->activeTabStyle == style->intVal);
            SetItemChanged(style);
            utassert(style->changed);
            Str serialized = SerializeSettings(gSettings, {});
            Settings* saved = NewSettings(serialized);
            utassert(saved->activeTabStyle == original);
            utassert(gSettings->activeTabStyle == style->intVal);
            DeleteSettings(saved);
            str::Free(serialized);
            style->intVal = 99;
            window.ApplyAppearancePreview();
            utassert(gSettings->activeTabStyle != 99);
            window.CancelAppearanceChanges();
            utassert(gSettings->activeTabStyle == original);
        }
        utassert(pageNumber && live);
        if (pageNumber && live) {
            utassert(pageNumber->enumValues && pageNumber->enumValues[2] && !pageNumber->enumValues[3]);
            int original = gSettings->annotationPageNumberStyle;
            pageNumber->intVal = original == 3 ? 1 : 3;
            SetItemChanged(pageNumber);
            live->boolVal = false;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationPageNumberStyle == original);
            live->boolVal = true;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationPageNumberStyle == pageNumber->intVal);
            Str serialized = SerializeSettings(gSettings, {});
            Settings* saved = NewSettings(serialized);
            utassert(saved->annotationPageNumberStyle == original);
            DeleteSettings(saved);
            str::Free(serialized);
            pageNumber->intVal = 2;
            SetItemChanged(pageNumber);
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationPageNumberStyle == 2);
            window.CancelAppearanceChanges();
            utassert(gSettings->annotationPageNumberStyle == original);
            for (Str value : {StrL("0"), StrL("4"), StrL("-1"), StrL("1junk"), StrL("1.0"), StrL("")}) {
                utassert(!SetSettingsValueFromStr(StrL("AnnotationPageNumberStyle"), value));
                utassert(gSettings->annotationPageNumberStyle == original);
            }
            pageNumber->intVal = original;
            SetItemChanged(pageNumber);
        }
        utassert(threeColumnPage && live);
        if (threeColumnPage && live) {
            utassert(threeColumnPage->enumValues && threeColumnPage->enumValues[2] && !threeColumnPage->enumValues[3]);
            int original = gSettings->annotationThreeColumnPageStyle;
            int pageStyle = gSettings->annotationPageNumberStyle;
            int columns = gSettings->annotationToolbarColumns;
            threeColumnPage->intVal = original == 3 ? 1 : 3;
            SetItemChanged(threeColumnPage);
            live->boolVal = false;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationThreeColumnPageStyle == original);
            live->boolVal = true;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationThreeColumnPageStyle == threeColumnPage->intVal);
            utassert(gSettings->annotationPageNumberStyle == pageStyle &&
                     gSettings->annotationToolbarColumns == columns);
            Str serialized = SerializeSettings(gSettings, {});
            Settings* saved = NewSettings(serialized);
            utassert(saved->annotationThreeColumnPageStyle == original);
            DeleteSettings(saved);
            str::Free(serialized);
            threeColumnPage->intVal = 99;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationThreeColumnPageStyle != 99);
            window.CancelAppearanceChanges();
            utassert(gSettings->annotationThreeColumnPageStyle == original);
            for (Str value : {StrL("0"), StrL("4"), StrL("-1"), StrL("1junk"), StrL("1.0"), StrL("")}) {
                utassert(!SetSettingsValueFromStr(StrL("AnnotationThreeColumnPageStyle"), value));
                utassert(gSettings->annotationThreeColumnPageStyle == original);
            }
            threeColumnPage->intVal = original;
            SetItemChanged(threeColumnPage);
        }
        utassert(toolbarStyle && live);
        if (toolbarStyle && live) {
            utassert(toolbarStyle->enumValues && toolbarStyle->enumValues[1] && !toolbarStyle->enumValues[2]);
            int original = gSettings->annotationToolbarStyle;
            toolbarStyle->intVal = original == 1 ? 2 : 1;
            SetItemChanged(toolbarStyle);
            live->boolVal = false;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationToolbarStyle == original);
            live->boolVal = true;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationToolbarStyle == toolbarStyle->intVal);
            Str serialized = SerializeSettings(gSettings, {});
            Settings* saved = NewSettings(serialized);
            utassert(saved->annotationToolbarStyle == original);
            DeleteSettings(saved);
            str::Free(serialized);
            toolbarStyle->intVal = 99;
            window.ApplyAppearancePreview();
            utassert(gSettings->annotationToolbarStyle != 99);
            window.CancelAppearanceChanges();
            utassert(gSettings->annotationToolbarStyle == original);
            for (Str value : {StrL("0"), StrL("3"), StrL("-1"), StrL("1junk"), StrL("1.0"), StrL("")}) {
                utassert(!SetSettingsValueFromStr(StrL("AnnotationToolbarStyle"), value));
                utassert(gSettings->annotationToolbarStyle == original);
            }
        }
    }
}
#endif

void AdvancedSettingsWnd::DrawListBoxItem(VirtListBox::DrawItemEvent* ev) {
    VirtListBox* lb = ev->listBox;
    auto* m = (ListBoxModelSettings*)lb->model;
    SettingItem* item = m->ItemAt(ev->itemIndex);
    if (!item) {
        return;
    }

    Gfx* gfx = ev->gfx;
    Rect rc = ev->itemRect;

    Color colBg = lb->GetColor(kColListBg);
    Color colText = lb->GetColor(kColListText);
    if (IsSpecialColor(colBg)) {
        colBg = GetSysColor(COLOR_WINDOW);
    }
    if (IsSpecialColor(colText)) {
        colText = GetSysColor(COLOR_WINDOWTEXT);
    }
    if (ev->selected) {
        colBg = AccentColor(colBg, 30);
    }

    gfx->FillRect(rc, colBg);

    PlatformFont* fontNormal = GetFont() ? GetFont() : GetAppFont();

    // bold name => changed this session; bold value => differs from default.
    // together they show both "not the default" and "edited since opening".
    PlatformFont* nameFont = (item->changed && fontBold) ? fontBold : fontNormal;
    PlatformFont* valFont = (SettingDiffersFromDefault(item) && fontBold) ? fontBold : fontNormal;

    Rect rcName{};
    Rect rcVal{};
    AdvSettingsItemColumns(rc, rcName, rcVal);

    bool isRtl = HwndIsRtl(lb->GetHwnd());
    // yellow/accent underlays on matched filter tokens (same as command palette)
    u32 nameFmt = gfxTextEllipsis | gfxTextVCenter;
    nameFmt |= isRtl ? (gfxTextRight | gfxTextRtl) : gfxTextLeft;
    DrawMaybeHighlightedText(gfx, rcName, item->name, filterWords, highlighted, colBg, isRtl, false, nameFmt, nameFont,
                             colText);

    TempStr val = FormatSettingValueTemp(item);
    if (editValue && editItemIdx >= 0 && items[editItemIdx] == item) val = editValue->GetTextTemp();
    Rect preview;
    SplitAdvPreview(item, rcVal, preview);
    bool editing = editValue && editItemIdx >= 0 && items[editItemIdx] == item;
    if (!preview.IsEmpty() && !isRtl && !editing) {
        if (item->previewFont != valFont || !str::Eq(item->previewLabel, val)) {
            item->previewFont = valFont;
            str::ReplaceWithCopy(&item->previewLabel, val);
            item->previewLabelWidth = PlatformFontMeasureText(valFont, val).dx;
        }
        preview.x = std::min(preview.x, rcVal.x + item->previewLabelWidth + DpiScale(8));
    }
    DrawSettingPreview(gfx, preview, item, val, colText, colBg);
    u32 valFmt = gfxTextEllipsis | gfxTextVCenter | (isRtl ? gfxTextRight | gfxTextRtl : gfxTextLeft);
    gfx->DrawText(val, rcVal, valFmt, valFont, colText);
}

// in-place editor sits in the value column (same split as DrawListBoxItem)
// in window (dialog client) coords, which is where the in-place editors go
Rect AdvancedSettingsWnd::ValueRectForItem(int idx) {
    int lbIdx = -1;
    int n = model->ItemsCount();
    for (int i = 0; i < n; i++) {
        if (model->filtered[i] == idx) {
            lbIdx = i;
            break;
        }
    }
    if (lbIdx < 0) {
        return {};
    }
    Rect rc = listBox->ItemRect(lbIdx);
    if (rc.IsEmpty()) {
        return {};
    }
    Rect rcName{};
    Rect rcVal{};
    AdvSettingsItemColumns(rc, rcName, rcVal);
    Rect preview;
    SplitAdvPreview(items[idx], rcVal, preview);
    return rcVal;
}

void AdvancedSettingsWnd::BeginEditValue(int idx) {
    CancelEditValue();
    SettingItem* item = items[idx];
    Rect r = ValueRectForItem(idx);
    if (r.IsEmpty()) {
        return;
    }

    Edit::CreateArgs args;
    args.parent = hwnd;
    args.isMultiLine = false;
    args.withBorder = true;
    args.isRtl = IsUIRtl();
    args.font = GetFont();
    args.text = FormatSettingValueTemp(item);
    auto* c = new Edit();
    c->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
    // Suppress re-entrant CancelEditValue while Create/show/focus pump messages.
    editCreating = true;
    pendingCancelEdit = false;
    HWND ok = c->Create(args);
    if (!ok) {
        editCreating = false;
        delete c;
        if (pendingCancelEdit) {
            pendingCancelEdit = false;
            CancelEditValue();
        }
        return;
    }
    editValue = c;
    editItemIdx = idx;
    c->onKillFocus = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::OnEditValueKillFocus>(this);
    c->onTextChanged = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::OnValueTextChanged>(this);
    SetWindowPos(c->hwnd, HWND_TOP, r.x, r.y, r.dx, r.dy, SWP_SHOWWINDOW);
    EditSelectAll(c);
    EditSetFocus(c);
    editCreating = false;
    if (pendingCancelEdit) {
        pendingCancelEdit = false;
        CancelEditValue();
    }
}

void AdvancedSettingsWnd::CancelEditValue() {
    if (editCreating) {
        // Called re-entrantly from Create/show/focus of BeginEdit*; finish
        // construction first, then cancel (avoids UAF on the editor being built).
        pendingCancelEdit = true;
        return;
    }
    CloseEnumEdit(true);
    if (!editValue) {
        return;
    }
    auto* tmp = editValue;
    editValue = nullptr;
    editItemIdx = -1;
    delete tmp;
    SetFocusTo(listBox);
    ScheduleAppearancePreview();
}

static void DrawAnnotModeChoice(DropDown::PreviewItemEvent* ev) {
    int index = ev->itemIndex < 0 ? CbGetCurrentSelection(ev->dropDown) : ev->itemIndex;
    int size = std::min(ev->bounds.dx, ev->bounds.dy);
    ev->gfx->DrawPixmap(AnnotationModeIconPixmap(size, ev->foreground, index + 1), ev->bounds);
}

static void DrawPageNumberChoice(DropDown::PreviewItemEvent* ev) {
    int index = ev->itemIndex < 0 ? CbGetCurrentSelection(ev->dropDown) : ev->itemIndex;
    DrawPageNumberPreview(ev->gfx, ev->bounds, index + 1, ev->foreground, ev->background);
}

static void DrawToolbarStyleChoice(DropDown::PreviewItemEvent* ev) {
    int index = ev->itemIndex < 0 ? CbGetCurrentSelection(ev->dropDown) : ev->itemIndex;
    DrawToolbarStylePreview(ev->gfx, ev->bounds, index + 1, ev->foreground, ev->background);
}

static void DrawThreeColumnPageChoice(DropDown::PreviewItemEvent* ev) {
    int index = ev->itemIndex < 0 ? CbGetCurrentSelection(ev->dropDown) : ev->itemIndex;
    DrawThreeColumnPagePreview(ev->gfx, ev->bounds, index + 1, ev->foreground, ev->background);
}

// show a drop-down with the allowed values over the item's value rect
void AdvancedSettingsWnd::BeginEditEnum(int idx) {
    CancelEditValue();
    SettingItem* item = items[idx];
    Rect r = ValueRectForItem(idx);
    if (r.IsEmpty()) {
        return;
    }
    DropDown::CreateArgs args;
    args.parent = hwnd;
    args.font = GetFont();
    bool annotationIcon = str::EqI(item->name, StrL("AnnotationModeIconStyle"));
    bool pageNumber = str::EqI(item->name, StrL("AnnotationPageNumberStyle"));
    bool threeColumnPage = str::EqI(item->name, StrL("AnnotationThreeColumnPageStyle"));
    bool toolbarStyle = str::EqI(item->name, StrL("AnnotationToolbarStyle"));
    args.previewItems = annotationIcon || pageNumber || threeColumnPage || toolbarStyle;
    auto* c = new DropDown();
    if (annotationIcon) c->onPreviewItem = MkFunc1Void(DrawAnnotModeChoice);
    if (pageNumber) c->onPreviewItem = MkFunc1Void(DrawPageNumberChoice);
    if (threeColumnPage) c->onPreviewItem = MkFunc1Void(DrawThreeColumnPageChoice);
    if (toolbarStyle) c->onPreviewItem = MkFunc1Void(DrawToolbarStyleChoice);
    // Suppress re-entrant CancelEditValue / CloseEnumEdit while Create and
    // CB_SHOWDROPDOWN pump messages (filter EN_CHANGE was freeing c mid-flight).
    editCreating = true;
    pendingCancelEdit = false;
    HWND ok = c->Create(args);
    if (!ok) {
        editCreating = false;
        delete c;
        if (pendingCancelEdit) {
            pendingCancelEdit = false;
            CancelEditValue();
        }
        return;
    }
    StrVec vals;
    int currSel = 0;
    for (int i = 0; item->enumValues[i]; i++) {
        Str label = Str(item->enumValues[i]);
        if (annotationIcon) label = i == 0 ? Tr("Solid pencil (default)") : Tr("Fine-tip pen");
        if (toolbarStyle) label = i == 0 ? Tr("Grouped (default)") : Tr("Continuous rail");
        if (pageNumber) {
            if (i == 0) label = Tr("1A - Short separator (default)");
            if (i == 1) label = Tr("1B - Divided box");
            if (i == 2) label = Tr("1C - Two boxes");
        }
        if (threeColumnPage) {
            if (i == 0) label = Tr("Current stacked (default)");
            if (i == 1) label = Tr("Full-width page row");
            if (i == 2) label = Tr("Compact horizontal page field");
        }
        vals.Append(label);
        if (str::EqI(FormatSettingValueTemp(item), Str(item->enumValues[i]))) {
            currSel = i;
        }
    }
    c->SetItems(vals);
    CbSetCurrentSelection(c, currSel);
    c->onSelectionChanged = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::OnEnumSelectionChanged>(this);
    c->onCloseUp = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::OnEnumDropDownClosed>(this);
    dropDownValue = c;
    editItemIdx = idx;
    str::ReplaceWithCopy(&dropDownOrigVal, FormatSettingValueTemp(item));
    SetWindowPos(c->hwnd, HWND_TOP, r.x, r.y, r.dx, r.dy, SWP_SHOWWINDOW);
    HwndSetFocus(c->hwnd);
    SendMessageW(c->hwnd, CB_SHOWDROPDOWN, TRUE, 0);
    editCreating = false;
    if (pendingCancelEdit) {
        pendingCancelEdit = false;
        CancelEditValue();
    }
}

// Previewing a value (or the combo list closing) can activate the main
// window. Put the user back in this dialog; if the list is still open,
// leave the caret on the combo.
void AdvancedSettingsWnd::KeepFocus() {
    if (gAdvancedSettingsWnd != this || !hwnd) {
        return;
    }
    HwndToForeground(hwnd);
    HWND focused = ::GetFocus();
    if (focused == hwnd || ::IsChild(hwnd, focused)) {
        return;
    }
    if (dropDownValue && dropDownValue->hwnd) {
        HwndSetFocus(dropDownValue->hwnd);
        return;
    }
    HwndSetFocus(hwnd);
}

void AdvancedSettingsWnd::RelayoutInPlaceEditor() {
    if (editItemIdx < 0) {
        return;
    }
    Rect r = ValueRectForItem(editItemIdx);
    if (r.IsEmpty()) {
        return;
    }
    HWND h = nullptr;
    if (editValue) {
        h = editValue->hwnd;
    } else if (dropDownValue) {
        h = dropDownValue->hwnd;
    }
    if (h) {
        SetWindowPos(h, HWND_TOP, r.x, r.y, r.dx, r.dy, SWP_NOACTIVATE);
    }
}

void AdvancedSettingsWnd::PostedRefocusEditValue() {
    if (gAdvancedSettingsWnd != this || !editValue || !editValue->hwnd) {
        return;
    }
    if (GetFocus() == editValue->hwnd) {
        return;
    }
    HwndSetFocus(editValue->hwnd);
}

void AdvancedSettingsWnd::OnValueTextChanged() {
    if (listBox) listBox->Invalidate();
    ScheduleAppearancePreview();
}

void AdvancedSettingsWnd::OnEditValueKillFocus() {
    if (!editValue) {
        return;
    }
    HWND next = GetFocus();
    if (next == editValue->hwnd) {
        return;
    }
    if (!next || HwndIsOnScreenKeyboard(next)) {
        auto fn = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::PostedRefocusEditValue>(this);
        uitask::Post(fn, "AdvSettingsRefocusEdit");
        return;
    }
    if (hwnd && (next == hwnd || IsChild(hwnd, next))) {
        return;
    }
    CommitEditValue();
}

void AdvancedSettingsWnd::OnEnumSelectionChanged() {
    if (!dropDownValue) {
        return;
    }
    SettingItem* item = items[editItemIdx];
    int sel = CbGetCurrentSelection(dropDownValue);
    if (sel >= 0) {
        if (item->type == SettingType::Int)
            item->intVal = ParseInt(Str(item->enumValues[sel]));
        else
            str::ReplaceWithCopy(&item->strVal, Str(item->enumValues[sel]));
        NoteItemEdited(item);
        // browsing the list with the arrow keys previews each value in turn
        ScheduleAppearancePreview();
        listBox->Invalidate();
    }
    KeepFocus();
    // selecting with the mouse closes the list: dispose of the control then.
    // can't do it here (we're inside its notification), so check afterwards;
    // during keyboard browsing the list stays open and the control stays up
    auto fn = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::CheckDropDownClosed>(this);
    uitask::Post(fn, "AdvSettingsCheckDropDownClosed");
}

void AdvancedSettingsWnd::OnEnumDropDownClosed() {
    // CBN_CLOSEUP is when Windows hands activation to whoever it thinks
    // owned the list (often the main window). Take it back now, not after
    // a posted task that runs too late.
    KeepFocus();
    auto fn = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::CheckDropDownClosed>(this);
    uitask::Post(fn, "AdvSettingsCheckDropDownClosed");
}

void AdvancedSettingsWnd::CheckDropDownClosed() {
    if (gAdvancedSettingsWnd != this || !dropDownValue) {
        return;
    }
    bool droppedDown = SendMessageW(dropDownValue->hwnd, CB_GETDROPPEDSTATE, 0, 0) != 0;
    if (!droppedDown) {
        CloseEnumEdit(true);
    }
}

void AdvancedSettingsWnd::CloseEnumEdit(bool keepValue) {
    if (editCreating) {
        pendingCancelEdit = true;
        return;
    }
    if (!dropDownValue) {
        return;
    }
    auto* tmp = dropDownValue;
    dropDownValue = nullptr;
    if (!keepValue) {
        SettingItem* item = items[editItemIdx];
        if (item->type == SettingType::Int)
            item->intVal = ParseInt(dropDownOrigVal);
        else
            str::ReplaceWithCopy(&item->strVal, dropDownOrigVal);
        NoteItemEdited(item);
        ScheduleAppearancePreview();
    }
    editItemIdx = -1;
    delete tmp;
    listBox->Invalidate();
    KeepFocus();
    SetFocusTo(listBox);
    // destroying the combo can activate the main window after we return
    auto fn = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::KeepFocus>(this);
    uitask::Post(fn, "AdvSettingsKeepFocus");
}

void AdvancedSettingsWnd::CommitEditValue() {
    if (!editValue) {
        return;
    }
    SettingItem* item = items[editItemIdx];
    Str s = editValue->GetTextTemp();
    if (SettingPreview(item) != SettingPreviewKind::None && !AppearanceValueValid(item, s)) {
        EditSetFocus(editValue);
        return;
    }
    switch (item->type) {
        case SettingType::Int:
            item->intVal = ParseInt(s);
            break;
        case SettingType::Float: {
            float f = item->floatVal;
            str::Parse(s, "%f", &f);
            item->floatVal = f;
            break;
        }
        default:
            str::ReplaceWithCopy(&item->strVal, s);
            break;
    }
    NoteItemEdited(item);
    ScheduleAppearancePreview();
    CancelEditValue();
    listBox->Invalidate();
}

// activate a setting: toggle a bool, or begin editing an enum / value. A single
// click only selects (so a stray click doesn't change anything); activation
// happens on double-click or Enter.
void AdvancedSettingsWnd::ActivateItem(int lbIdx) {
    SettingItem* item = model->ItemAt(lbIdx);
    if (!item) {
        return;
    }
    if (item->type == SettingType::Bool) {
        item->boolVal = !item->boolVal;
        NoteItemEdited(item);
        listBox->Invalidate();
        return;
    }
    int idx = model->filtered[lbIdx];
    if (item->enumValues) {
        BeginEditEnum(idx);
        return;
    }
    BeginEditValue(idx);
}

void AdvancedSettingsWnd::OnItemDoubleClicked() {
    ActivateItem(listBox->GetCurrentSelection());
}

void AdvancedSettingsWnd::ApplyChangesAndSave() {
    CancelAppearanceChanges();
    // snapshot settings that need explicit apply (tabs, menu bar ...) before we
    // overwrite them, so we can act on what actually changed after the reload
    SettingsApplyState before = GetSettingsApplyState();
    bool didChange = false;
    for (SettingItem* item : items) {
        if (!item->changed) {
            continue;
        }
        didChange = true;
        u8* p = SettingFieldPtr(item->fieldOffset);
        switch (item->type) {
            case SettingType::Bool:
                *(bool*)p = item->boolVal;
                break;
            case SettingType::Int:
                *(int*)p = item->intVal;
                break;
            case SettingType::Float:
                *(float*)p = item->floatVal;
                break;
            case SettingType::Compact:
                ApplyCompactInts(item->compactInfo, p, item->strVal);
                break;
            default:
                str::ReplaceWithCopy((Str*)p, item->strVal);
                break;
        }
        // SaveSettings() re-generates these strings from their parsed
        // representations, which would clobber the edit unless the parsed
        // representation is updated as well
        if (str::EqI(item->name, StrL("DefaultDisplayMode"))) {
            gSettings->defaultDisplayModeEnum = DisplayModeFromString(item->strVal, DisplayMode::Automatic);
        } else if (str::EqI(item->name, StrL("DefaultZoom"))) {
            gSettings->defaultZoomFloat = ZoomFromString(item->strVal, kZoomActualSize);
        } else if (str::EqI(item->name, StrL("ImageUI.DefaultZoom"))) {
            gSettings->imageUI.defaultZoomFloat = ZoomFromString(item->strVal, 0);
        } else if (str::EqI(item->name, StrL("ComicBookUI.DefaultZoom"))) {
            gSettings->comicBookUI.defaultZoomFloat = ZoomFromString(item->strVal, 0);
        }
    }
    if (!didChange) {
        return;
    }
    ScheduleSaveSettings();
    // reload so that all state derived from settings (theme, fonts, parsed
    // colors, custom commands, accelerators ...) is re-computed and applied
    ForceReloadSettings();
    // apply changes that a reload doesn't pick up on its own (menu bar, tabs,
    // anti-alias ...) and re-layout the open windows
    ApplyChangedSettingsAndRelayout(before);
}

// opens the settings file in the editor associated with .txt files
void OpenSettingsFileInEditor() {
    if (!CanAccessDisk()) {
        return;
    }
    TempStr path = GetSettingsPathTemp();
    LaunchFileIfExists(path);
}

void AdvancedSettingsWnd::OnOpenSettingsFile(VirtMouseEvent*) {
    OpenSettingsFileInEditor();
}

void AdvancedSettingsWnd::OnHelp(VirtMouseEvent*) {
    SumatraLaunchBrowser(Str(kSettingsDocsUrl));
}

void AdvancedSettingsWnd::OnCancel(VirtMouseEvent*) {
    CancelAppearanceChanges();
    ScheduleDelete();
}

void AdvancedSettingsWnd::OnSave(VirtMouseEvent*) {
    CommitEditValue();
    if (editValue) return;
    // queue the dialog teardown first: ApplyChangesAndSave() may post a tabs
    // transition that closes/recreates windows (including this dialog's owner),
    // and uitask runs FIFO, so the delete must be enqueued before it
    ScheduleDelete();
    ApplyChangesAndSave();
}

// Esc cancels an in-place edit; it only closes the dialog when nothing is unsaved.
bool AdvancedSettingsWnd::HandleEscapeKey(bool isEditingValue, bool isEditingEnum) {
    if (isEditingValue) {
        CancelEditValue();
    } else if (isEditingEnum) {
        CloseEnumEdit(false);
    } else if (CountChangedSettings() == 0) {
        ScheduleDelete();
    }
    return true;
}

bool AdvancedSettingsWnd::HandleTabKey(bool isEditingValue, bool isEditingEnum) {
    // leave in-place editors before moving focus (Enter would commit too)
    if (isEditingValue) {
        CommitEditValue();
    } else if (isEditingEnum) {
        CloseEnumEdit(true);
    }
    // the ring is the layout order: filter → list → Save → Cancel → Open → Help
    TabNavigate(IsShiftPressed());
    return true;
}

bool AdvancedSettingsWnd::HandleEnterKey(bool isEditingValue, bool isEditingEnum) {
    if (isEditingValue) {
        CommitEditValue();
        return true;
    }
    if (isEditingEnum) {
        CloseEnumEdit(true);
        return true;
    }
    // focused button: Enter = click (OnKeyDown would otherwise steal
    // Enter for list-item activation)
    LRESULT res = 0;
    if (vroot && vroot->focused && VirtTreeOnMessage(hwnd, vroot, WM_KEYDOWN, VK_RETURN, 0, res)) {
        return true;
    }
    // Activate the selected row (bool: toggle true↔false; else edit).
    // Do not require list focus: after filter/arrow navigation the
    // selection can be valid while focus is still on the filter.
    int lbIdx = listBox->GetCurrentSelection();
    if (lbIdx < 0) {
        return false;
    }
    ActivateItem(lbIdx);
    return true;
}

// up/down in the filter edit moves the list selection (wrapping)
bool AdvancedSettingsWnd::HandleUpDownKey(const KeyEvent& ev) {
    int dir = 0;
    if (ev.vkey == VK_UP) {
        dir = -1;
    } else if (ev.vkey == VK_DOWN) {
        dir = 1;
    }
    if (dir == 0 || ev.hwnd != editFilter->hwnd) {
        return false;
    }
    int n = listBox->ItemsCount();
    if (n > 0) {
        int sel = (listBox->GetCurrentSelection() + dir + n) % n;
        listBox->SetCurrentSelection(sel);
        OnSelectionChanged(); // programmatic selection: no LBN_SELCHANGE
    }
    return true;
}

void AdvancedSettingsWnd::OnKeyDown(KeyEvent* ev) {
    // a single click only selects; activation (toggle / edit) is on double-click
    // (OnItemDoubleClicked) or Enter
    bool isEditingValue = editValue && ev->hwnd == editValue->hwnd;
    bool isEditingEnum = dropDownValue && ev->hwnd == dropDownValue->hwnd;
    if (ev->vkey == VK_ESCAPE) {
        ev->didHandle = HandleEscapeKey(isEditingValue, isEditingEnum);
        return;
    }
    if (ev->vkey == VK_TAB) {
        ev->didHandle = HandleTabKey(isEditingValue, isEditingEnum);
        return;
    }
    if (ev->vkey == VK_RETURN) {
        ev->didHandle = HandleEnterKey(isEditingValue, isEditingEnum);
        return;
    }
    if (HandleUpDownKey(*ev)) {
        ev->didHandle = true;
    }
}

// clicking the window's close box sends WM_CLOSE. We must schedule our own
// teardown here: the framework's default WM_CLOSE handler calls WindowBase::Destroy(),
// which removes the WindowBase from the hwnd->WindowBase list *before* DestroyWindow(), so the
// following WM_DESTROY never reaches onDestroy - leaving gAdvancedSettingsWnd
// dangling and blocking reopen. CloseEvent::didHandle defaults to true, so
// returning from here skips that default Destroy().
static void OnClose(WindowBase::CloseEvent* /*ev*/) {
    if (gAdvancedSettingsWnd) {
        gAdvancedSettingsWnd->ScheduleDelete();
    }
}

static void OnDestroy(WindowBase::DestroyEvent* /*ev*/) {
    if (gAdvancedSettingsWnd) {
        gAdvancedSettingsWnd->ScheduleDelete();
    }
}

// last client size chosen by the user (or initial layout); reused when the
// dialog is reopened in the same process so a widened window sticks (#5804)
static int gAdvSettingsLastClientDx = 0;
static int gAdvSettingsLastClientDy = 0;

// re-layout the controls when the (resizable) window is resized
void AdvancedSettingsWnd::OnSize(WindowBase::SizeEvent* ev) {
    // a WS_CAPTION/WS_THICKFRAME window gets WM_SIZE during CreateCustom,
    // before the child controls exist; ignore layout until they're created
    if (!layout || !listBox) {
        return;
    }
    int dx = ev->size.dx;
    int dy = ev->size.dy;
    if (dx == 0 || dy == 0) {
        return;
    }
    gAdvSettingsLastClientDx = dx;
    gAdvSettingsLastClientDy = dy;
    DoLayout({dx, dy});
    RelayoutInPlaceEditor();
    HwndInvalidate(hwnd);
}

void AdvancedSettingsWnd::OnGetMinMaxInfo(WindowBase::GetMinMaxInfoEvent* ev) {
    if (!ev->mmi) {
        return;
    }
    int minClient = DpiScale(kAdvSettingsMinClientDx);
    int chromeX = 0;
    if (hwnd) {
        Rect wr = HwndWindowRect(hwnd);
        Rect cr = HwndClientRect(hwnd);
        if (cr.dx > 0 && wr.dx > cr.dx) {
            chromeX = wr.dx - cr.dx;
        }
    }
    ev->mmi->ptMinTrackSize.x = minClient + chromeX;
}

int AdvancedSettingsWnd::CountChangedSettings() const {
    int n = 0;
    for (SettingItem* item : items) {
        if (item->changed) {
            n++;
        }
    }
    return n;
}

// "changed settings: n" sits under the list, above the setting help.
// Hidden until at least one value has been edited since the dialog opened.
void AdvancedSettingsWnd::UpdateChangedCountLabel() {
    if (!changedCountRow || !changedCountText) {
        return;
    }
    int n = CountChangedSettings();
    bool show = n > 0;
    Visibility vis = show ? Visibility::Visible : Visibility::Collapse;
    if (show) {
        changedCountText->SetText(fmt(Tr("Unsaved changes: %d").s, n));
    }
    changedCountRow->SetVisibility(vis);
    changedCountText->SetIsVisible(show);
    if (changedCountBg) {
        changedCountBg->SetIsVisible(show);
    }
    if (hwnd && layout) {
        DoLayout();
        HwndRepaintNow(hwnd);
    }
}

void AdvancedSettingsWnd::NoteItemEdited(SettingItem* item) {
    SetItemChanged(item);
    UpdateChangedCountLabel();
    ScheduleAppearancePreview();
}

// center the dialog over the main window frame
static void PositionDialog(HWND hwnd, HWND hwndRelative) {
    Rect rRelative = HwndWindowRect(hwndRelative);
    Rect r = HwndWindowRect(hwnd);
    int x = rRelative.x + (rRelative.dx / 2) - (r.dx / 2);
    int y = rRelative.y + (rRelative.dy / 2) - (r.dy / 2);
    r = {x, y, r.dx, r.dy};
    Rect r2 = ShiftRectToWorkArea(r, hwndRelative, true);
    SetWindowPos(hwnd, nullptr, r2.x, r2.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

// the buttons are virtual controls, so they are styled here rather than by the
// system: a filled box with a border, brighter on hover (like the other dialogs)
bool AdvancedSettingsWnd::Create(MainWindow* mainWin) {
    win = mainWin;
    // OnSize repositions in-place editors after DoLayout; skip the generic path
    autoLayout = false;
    CancelAppearancePreview();
    CollectSettings(items);

    {
        CreateCustomArgs args;
        args.title = Tr("Advanced Settings");
        args.visible = false;
        args.style = WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME;
        args.font = GetFont();
        args.icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(GetAppIconID()));
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }
    onTimer = MkMethod1<AdvancedSettingsWnd, WindowBase::TimerEvent*, &AdvancedSettingsWnd::OnAppearanceTimer>(this);
    fontBold = GetBoldPlatformFont(GetFont() ? GetFont() : GetAppFont());

    auto colBg = ThemeWindowControlBackgroundColor();
    auto colTxt = ThemeWindowTextColor();
    SetColors(colTxt, colBg);
    bool isRtl = IsUIRtl();

    auto* vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;

    {
        Edit::CreateArgs args;
        args.parent = hwnd;
        args.isMultiLine = false;
        args.withBorder = true;
        args.cueText = Tr("enter search term to filter settings");
        args.font = GetFont();
        args.isRtl = isRtl;
        auto* c = new Edit();
        c->SetColors(colTxt, colBg);
        c->maxDx = 150;
        HWND ok = c->Create(args);
        ReportIf(!ok);
        c->onTextChanged = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::QueryChanged>(this);
        editFilter = c;
        vbox->AddChild(c);
    }

    {
        auto* c = new VirtListBox();
        c->dpi = GetDpi();
        c->font = font;
        c->padding = DpiScaledInsets(4, 0);
        c->onDrawItem =
            MkMethod1<AdvancedSettingsWnd, VirtListBox::DrawItemEvent*, &AdvancedSettingsWnd::DrawListBoxItem>(this);
        listBox = c;
        model = new ListBoxModelSettings();
        model->items = &items;
        CollectFilteredSettings(items, filterWords, model->filtered);
        c->onSelectionChanged = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::OnSelectionChanged>(this);
        c->onDoubleClick = MkMethod0<AdvancedSettingsWnd, &AdvancedSettingsWnd::OnItemDoubleClicked>(this);
        c->SetModel(model);
        vbox->AddChild(c, 1);
    }

    // under the list, above the selected setting's help: how many values were
    // edited this session. Hidden until at least one has been changed.
    {
        auto* overlay = new Overlay();
        changedCountBg = new VirtFill();
        changedCountBg->SetColor(kColFillBg, kColYellow);
        changedCountBg->SetIsVisible(false);
        overlay->AddChild(changedCountBg);

        auto* hbox = new HBox();
        hbox->alignMain = MainAxisAlign::MainCenter;
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        changedCountText = NewVirtText({
            .s = {},
            .font = fontBold,
            .textColor = kColRed,
            .isRtl = isRtl,
        });
        changedCountText->SetIsVisible(false);
        hbox->AddChild(new Padding(changedCountText, DpiScaledInsets(4, 8)));
        overlay->AddChild(hbox, CrossAxisAlign::Stretch, CrossAxisAlign::CrossCenter);

        overlay->SetVisibility(Visibility::Collapse);
        changedCountRow = overlay;
        vbox->AddChild(overlay);
    }

    // selected setting's doc comment (the help area)
    {
        auto* c = new VirtFixedLinesText();
        c->font = font;
        c->borderCol = ThemeEdgeColor();
        // don't let the doc text touch the edges of the dialog
        c->padding = DpiScaledInsets(4);
        commentText = c;
        vbox->AddChild(new Padding(c, DpiScaledInsets(4, 2)));
    }

    // one centered hint above the buttons
    {
        auto* hbox = new HBox();
        hbox->alignMain = MainAxisAlign::MainCenter;
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        auto* c = NewVirtText({
            .s = Tr("Bold: differs from default"),
            .font = font,
            .isRtl = isRtl,
        });
        hbox->AddChild(new Padding(c, DpiScaledInsets(1, 8)));
        vbox->AddChild(hbox);
    }

    {
        // left: Save, Cancel, Open Settings File — right: Help
        auto* hbox = new HBox();
        hbox->alignMain = MainAxisAlign::SpaceBetween;
        hbox->alignCross = CrossAxisAlign::CrossCenter;
        hbox->gap = font->averageCharWidth;
        auto pad = Insets{4, 0, 4, 0};

        auto* left = new HBox();
        left->alignMain = MainAxisAlign::MainStart;
        left->alignCross = CrossAxisAlign::CrossCenter;
        left->gap = font->averageCharWidth;
        btnSave = NewThemedButton(hwnd, Tr("Save"), font, true);
        btnSave->onClick = MkMethod1<AdvancedSettingsWnd, VirtMouseEvent*, &AdvancedSettingsWnd::OnSave>(this);
        left->AddChild(new Padding(btnSave, pad));
        btnCancel = NewThemedButton(hwnd, Tr("Cancel"), font, false);
        btnCancel->onClick = MkMethod1<AdvancedSettingsWnd, VirtMouseEvent*, &AdvancedSettingsWnd::OnCancel>(this);
        left->AddChild(new Padding(btnCancel, pad));
        btnOpenSettingsFile = NewThemedButton(hwnd, Tr("Open Settings File"), font, false);
        btnOpenSettingsFile->onClick =
            MkMethod1<AdvancedSettingsWnd, VirtMouseEvent*, &AdvancedSettingsWnd::OnOpenSettingsFile>(this);
        left->AddChild(new Padding(btnOpenSettingsFile, pad));
        hbox->AddChild(left);

        btnHelp = NewThemedButton(hwnd, Tr("Help"), font, false);
        btnHelp->onClick = MkMethod1<AdvancedSettingsWnd, VirtMouseEvent*, &AdvancedSettingsWnd::OnHelp>(this);
        hbox->AddChild(new Padding(btnHelp, pad));
        vbox->AddChild(hbox);
    }

    DarkModeApplyToPopupWindow(hwnd);

    auto* padding = PadWindowScroll(vbox, listBox, DpiScaledInsets(4, 8));
    layout = padding;

    auto rc = HwndClientRect(win->hwndFrame);
    // Default is wide enough that long setting names (e.g. InverseSearchCmdLine)
    // and long values don't crowd each other; reuse last size if the user
    // resized earlier this session (#5804), but never below kAdvSettingsMinClientDx.
    int minDx = DpiScale(kAdvSettingsMinClientDx);
    int dy = gAdvSettingsLastClientDy > 0 ? gAdvSettingsLastClientDy : limitValue(rc.dy - 72, 480, 900);
    int dx = gAdvSettingsLastClientDx > 0 ? gAdvSettingsLastClientDx : limitValue(rc.dx - 128, 760, 1100);
    dx = std::max(dx, minDx);
    LayoutAndSizeToContent(layout, dx, dy, hwnd);
    // pick up the virtual controls so we paint them and they get their input
    DoLayout(HwndClientRect(hwnd).Size());
    gAdvSettingsLastClientDx = HwndClientRect(hwnd).dx;
    gAdvSettingsLastClientDy = HwndClientRect(hwnd).dy;
    PositionDialog(hwnd, win->hwndFrame);

    SetIsVisible(true);
    EditSetFocus(editFilter);
    return true;
}

struct AdvSettingsRowRec {
    int idx;
    Rect rect;
};

// set only while the probe below is painting into its scratch bitmap
static Vec<AdvSettingsRowRec>* gAdvSettingsRowRec = nullptr;

static void RecordAdvSettingsRow(VirtListBox::DrawItemEvent* ev) {
    if (gAdvSettingsRowRec) {
        VecAppend(*gAdvSettingsRowRec, {ev->itemIndex, ev->itemRect});
    }
}

// The rects the list hands its row drawer. Asking VirtListBox for them
// (ItemRect) would report what the layout intends, not what the paint does --
// and the bug this is here for was a paint that disagreed with the layout. So
// the real tree is painted into a scratch bitmap with the row drawer swapped
// for a recorder.
static void RecordAdvSettingsRows(AdvancedSettingsWnd* wnd, Vec<AdvSettingsRowRec>& out) {
    Rect client = HwndClientRect(wnd->hwnd);
    if (client.IsEmpty() || !wnd->vroot) {
        return;
    }
    HDC hdcWin = GetDC(wnd->hwnd);
    if (!hdcWin) {
        return;
    }
    HDC memDC = CreateCompatibleDC(hdcWin);
    HBITMAP bmp = CreateCompatibleBitmap(hdcWin, client.dx, client.dy);
    if (memDC && bmp) {
        HGDIOBJ oldBmp = SelectObject(memDC, bmp);
        auto prevDrawItem = wnd->listBox->onDrawItem;
        gAdvSettingsRowRec = &out;
        wnd->listBox->onDrawItem = MkFunc1Void<VirtListBox::DrawItemEvent*>(RecordAdvSettingsRow);
        {
            Gfx* gfx = GfxCreate(memDC);
            wnd->vroot->Paint(gfx, client);
            delete gfx;
        }
        wnd->listBox->onDrawItem = prevDrawItem;
        gAdvSettingsRowRec = nullptr;
        SelectObject(memDC, oldBmp);
    }
    if (bmp) {
        DeleteObject(bmp);
    }
    if (memDC) {
        DeleteDC(memDC);
    }
    ReleaseDC(wnd->hwnd, hdcWin);
}

// What the settings list actually draws: the list's content rect in dialog
// client coords, the row height, and the rect of every row the paint touched.
// `action` is "geom" to report, or "scroll" to first scroll by `arg` rows. Rows
// are always whole and always tile the usable viewport; a test can check that
// on the real pixels without hard-coding a layout that depends on window size,
// dpi and theme. Used by tests/issue-5882.ts.
TempStr AdvSettingsRowsResultTemp(Str action, int arg, int* exitCodeOut) {
    str::Builder out;
    auto finish = [&](int code) -> TempStr {
        if (exitCodeOut) {
            *exitCodeOut = code;
        }
        return ToStrTemp(out);
    };

    AdvancedSettingsWnd* wnd = gAdvancedSettingsWnd;
    if (!wnd || !wnd->hwnd) {
        out.Append(StrL("NOTREADY no-dialog\n"));
        return finish(2);
    }
    if (str::Eq(action, StrL("previews"))) {
        for (SettingItem* item : wnd->items) {
            auto kind = SettingPreview(item);
            if (kind == SettingPreviewKind::None) continue;
            out.Append(fmt("preview=%s kind=%d value=%s\n", item->name, (int)kind, FormatSettingValueTemp(item)));
        }
        return finish(0);
    }
    if (str::Eq(action, StrL("names"))) {
        for (SettingItem* item : wnd->items) {
            out.Append(item->name);
            out.AppendChar('\n');
        }
        return finish(0);
    }
    if (str::Eq(action, StrL("nondefault"))) {
        int n = 0;
        for (SettingItem* item : wnd->items) {
            if (!SettingDiffersFromDefault(item)) {
                continue;
            }
            n++;
            out.Append(
                fmt("%s=%s default=%s\n", item->name, FormatSettingValueTemp(item), FormatSettingDefaultTemp(item)));
        }
        out.Append(fmt("count=%d\n", n));
        return finish(0);
    }
    if (str::Eq(action, StrL("changed"))) {
        int n = wnd->CountChangedSettings();
        bool banner = wnd->changedCountRow && wnd->changedCountRow->GetVisibility() == Visibility::Visible;
        out.Append(fmt("changed=%d banner=%d\n", n, banner ? 1 : 0));
        return finish(0);
    }
    if (str::Eq(action, StrL("toggle"))) {
        int nBool = 0;
        SettingItem* target = nullptr;
        for (SettingItem* item : wnd->items) {
            if (item->type != SettingType::Bool) {
                continue;
            }
            if (nBool == arg) {
                target = item;
                break;
            }
            nBool++;
        }
        if (!target) {
            out.Append(StrL("ERROR no-bool\n"));
            return finish(1);
        }
        target->boolVal = !target->boolVal;
        wnd->NoteItemEdited(target);
        if (wnd->listBox) {
            wnd->listBox->Invalidate();
        }
        int n = wnd->CountChangedSettings();
        bool banner = wnd->changedCountRow && wnd->changedCountRow->GetVisibility() == Visibility::Visible;
        out.Append(fmt("toggled=%s changed=%d banner=%d\n", target->name, n, banner ? 1 : 0));
        return finish(0);
    }
    if (str::Eq(action, StrL("esc"))) {
        bool editingValue = wnd->editValue != nullptr;
        bool editingEnum = wnd->dropDownValue != nullptr;
        wnd->HandleEscapeKey(editingValue, editingEnum);
        bool closing = !gAdvancedSettingsWnd || gAdvancedSettingsWnd->deleteScheduled;
        out.Append(fmt("closed=%d\n", closing ? 1 : 0));
        return finish(0);
    }
    if (str::Eq(action, StrL("save"))) {
        wnd->OnSave(nullptr);
        out.Append(StrL("saved=1\n"));
        return finish(0);
    }
    if (str::Eq(action, StrL("state"))) {
        out.Append(fmt("editing=%d\n", wnd->editValue ? 1 : 0));
        return finish(0);
    }
    if (str::Eq(action, StrL("edit"))) {
        int idx = -1;
        if (wnd->model) {
            int n = wnd->model->ItemsCount();
            for (int i = 0; i < n; i++) {
                SettingItem* item = wnd->model->ItemAt(i);
                if (item && !item->enumValues && item->type != SettingType::Bool) {
                    idx = wnd->model->filtered[i];
                    break;
                }
            }
        }
        if (idx < 0) {
            out.Append(StrL("ERROR no-edit-item\n"));
            return finish(1);
        }
        wnd->BeginEditValue(idx);
        out.Append(fmt("editing=%d\n", wnd->editValue ? 1 : 0));
        return finish(0);
    }
    if (str::Eq(action, StrL("killfocus"))) {
        if (wnd->editValue && wnd->editValue->hwnd) {
            HWND edit = wnd->editValue->hwnd;
            int id = GetDlgCtrlID(edit);
            SendMessageW(wnd->hwnd, WM_COMMAND, MAKEWPARAM(id, EN_KILLFOCUS), (LPARAM)edit);
        }
        out.Append(fmt("editing=%d\n", wnd->editValue ? 1 : 0));
        return finish(0);
    }
    if (str::Eq(action, StrL("resize"))) {
        WindowBase::SizeEvent ev;
        ev.size = HwndClientRect(wnd->hwnd).Size();
        wnd->OnSize(&ev);
        out.Append(fmt("editing=%d\n", wnd->editValue ? 1 : 0));
        return finish(0);
    }
    if (!wnd->listBox) {
        out.Append(StrL("NOTREADY no-dialog\n"));
        return finish(2);
    }
    VirtListBox* lb = wnd->listBox;
    if (lb->bounds.IsEmpty()) {
        out.Append(StrL("NOTREADY no-layout\n"));
        return finish(2);
    }
    int itemDy = lb->GetItemHeight();
    if (str::Eq(action, StrL("scroll"))) {
        lb->ScrollBy(arg * itemDy);
        HwndRepaintNow(wnd->hwnd);
    } else if (!str::Eq(action, StrL("geom"))) {
        out.Append(fmt("ERROR unknown-action action=%s\n", action));
        return finish(1);
    }

    // the content rect is where the rows live; UsableDy() rounds it down to
    // whole rows, so the strip below the last one is background and must stay
    // that way however far the list is scrolled
    Rect cr = lb->ContentRectInWindow();
    out.Append(fmt("OK content=%d,%d,%d,%d itemDy=%d usableDy=%d scrollY=%d maxScrollY=%d items=%d\n", cr.x, cr.y,
                   cr.dx, cr.dy, itemDy, lb->UsableDy(), lb->scrollY, lb->MaxScrollY(), lb->ItemsCount()));
    Vec<AdvSettingsRowRec> drawn;
    RecordAdvSettingsRows(wnd, drawn);
    for (const AdvSettingsRowRec& rec : drawn) {
        Rect r = rec.rect;
        out.Append(fmt("row=%d rect=%d,%d,%d,%d\n", rec.idx, r.x, r.y, r.dx, r.dy));
    }
    return finish(0);
}

void ShowAdvancedSettingsDialog(MainWindow* win) {
    if (!HasPermission(Perm::SavePreferences)) {
        return;
    }
    if (gAdvancedSettingsWnd) {
        HwndSetFocus(gAdvancedSettingsWnd->hwnd);
        return;
    }
    auto* wnd = new AdvancedSettingsWnd();
    wnd->onBeforeDelete = MkFunc0Void(ClearAdvancedSettingsWnd);
    wnd->onClose = MkFunc1Void<WindowBase::CloseEvent*>(OnClose);
    wnd->onDestroy = MkFunc1Void<WindowBase::DestroyEvent*>(OnDestroy);
    wnd->onSize = MkMethod1<AdvancedSettingsWnd, WindowBase::SizeEvent*, &AdvancedSettingsWnd::OnSize>(wnd);
    wnd->onGetMinMaxInfo =
        MkMethod1<AdvancedSettingsWnd, WindowBase::GetMinMaxInfoEvent*, &AdvancedSettingsWnd::OnGetMinMaxInfo>(wnd);
    wnd->onKeyDown = MkMethod1<AdvancedSettingsWnd, KeyEvent*, &AdvancedSettingsWnd::OnKeyDown>(wnd);
    wnd->SetFont(GetAppFont());
    bool ok = wnd->Create(win);
    if (!ok) {
        delete wnd;
        return;
    }
    gAdvancedSettingsWnd = wnd;
}
