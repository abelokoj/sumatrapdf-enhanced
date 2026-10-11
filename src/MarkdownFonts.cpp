/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "Settings.h"
#include "AppSettings.h"
#include "Translations.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "MarkdownFonts.h"

struct MarkdownFontFace {
    const char* file;
    const WCHAR* resource;
    int weight;
    const char* style;
};
struct MarkdownFontFamily {
    const char* name;
    const char* cssName;
    const MarkdownFontFace* faces;
    int count;
};
constexpr const char* kMarkdownFontHost = "https://sumatrapdf.markdown/_enhanced_fonts/";

static const MarkdownFontFace kLora[] = {
    {"Lora-Regular.otf", L"ENHANCED_MD_FONT_1", 400, "normal"},
    {"Lora-Italic.otf", L"ENHANCED_MD_FONT_2", 400, "italic"},
    {"Lora-Medium.otf", L"ENHANCED_MD_FONT_3", 500, "normal"},
    {"Lora-MediumItalic.otf", L"ENHANCED_MD_FONT_4", 500, "italic"},
    {"Lora-SemiBold.otf", L"ENHANCED_MD_FONT_5", 600, "normal"},
    {"Lora-SemiBoldItalic.otf", L"ENHANCED_MD_FONT_6", 600, "italic"},
    {"Lora-Bold.otf", L"ENHANCED_MD_FONT_7", 700, "normal"},
    {"Lora-BoldItalic.otf", L"ENHANCED_MD_FONT_8", 700, "italic"},
};

static const MarkdownFontFace kLibertinus[] = {
    {"LibertinusSerif-Regular.otf", L"ENHANCED_MD_FONT_9", 400, "normal"},
    {"LibertinusSerif-Italic.otf", L"ENHANCED_MD_FONT_10", 400, "italic"},
    {"LibertinusSerif-Semibold.otf", L"ENHANCED_MD_FONT_11", 600, "normal"},
    {"LibertinusSerif-SemiboldItalic.otf", L"ENHANCED_MD_FONT_12", 600, "italic"},
    {"LibertinusSerif-Bold.otf", L"ENHANCED_MD_FONT_13", 700, "normal"},
    {"LibertinusSerif-BoldItalic.otf", L"ENHANCED_MD_FONT_14", 700, "italic"},
};

static const MarkdownFontFace kNewCM10[] = {
    {"EnhancedWebNewCM10-Regular.woff2", L"ENHANCED_MD_FONT_15", 400, "normal"},
    {"EnhancedWebNewCM10-Italic.woff2", L"ENHANCED_MD_FONT_16", 400, "italic"},
    {"EnhancedWebNewCM10-Bold.woff2", L"ENHANCED_MD_FONT_17", 700, "normal"},
    {"EnhancedWebNewCM10-BoldItalic.woff2", L"ENHANCED_MD_FONT_18", 700, "italic"},
};

static const MarkdownFontFace kNewCM10Book[] = {
    {"EnhancedWebNewCM10-Book.woff2", L"ENHANCED_MD_FONT_19", 400, "normal"},
    {"EnhancedWebNewCM10-BookItalic.woff2", L"ENHANCED_MD_FONT_20", 400, "italic"},
    {"EnhancedWebNewCM10-Bold.woff2", L"ENHANCED_MD_FONT_17", 700, "normal"},
    {"EnhancedWebNewCM10-BoldItalic.woff2", L"ENHANCED_MD_FONT_18", 700, "italic"},
};

static const MarkdownFontFace kNewCMSans10[] = {
    {"EnhancedWebNewCMSans10-Regular.woff2", L"ENHANCED_MD_FONT_21", 400, "normal"},
    {"EnhancedWebNewCMSans10-Oblique.woff2", L"ENHANCED_MD_FONT_22", 400, "italic"},
    {"EnhancedWebNewCMSans10-Bold.woff2", L"ENHANCED_MD_FONT_23", 700, "normal"},
    {"EnhancedWebNewCMSans10-BoldOblique.woff2", L"ENHANCED_MD_FONT_24", 700, "italic"},
};

static const MarkdownFontFace kNewCMSans10Book[] = {
    {"EnhancedWebNewCMSans10-Book.woff2", L"ENHANCED_MD_FONT_25", 400, "normal"},
    {"EnhancedWebNewCMSans10-BookOblique.woff2", L"ENHANCED_MD_FONT_26", 400, "italic"},
    {"EnhancedWebNewCMSans10-Bold.woff2", L"ENHANCED_MD_FONT_23", 700, "normal"},
    {"EnhancedWebNewCMSans10-BoldOblique.woff2", L"ENHANCED_MD_FONT_24", 700, "italic"},
};

static const MarkdownFontFace kNewCMMono10[] = {
    {"EnhancedWebNewCMMono10-Regular.woff2", L"ENHANCED_MD_FONT_27", 400, "normal"},
    {"EnhancedWebNewCMMono10-Italic.woff2", L"ENHANCED_MD_FONT_28", 400, "italic"},
    {"EnhancedWebNewCMMono10-Bold.woff2", L"ENHANCED_MD_FONT_29", 700, "normal"},
    {"EnhancedWebNewCMMono10-BoldOblique.woff2", L"ENHANCED_MD_FONT_30", 700, "italic"},
};

static const MarkdownFontFace kNewCMMono10Book[] = {
    {"EnhancedWebNewCMMono10-Book.woff2", L"ENHANCED_MD_FONT_31", 400, "normal"},
    {"EnhancedWebNewCMMono10-BookItalic.woff2", L"ENHANCED_MD_FONT_32", 400, "italic"},
    {"EnhancedWebNewCMMono10-Bold.woff2", L"ENHANCED_MD_FONT_29", 700, "normal"},
    {"EnhancedWebNewCMMono10-BoldOblique.woff2", L"ENHANCED_MD_FONT_30", 700, "italic"},
};

static const MarkdownFontFace kNewCMUncial10[] = {
    {"EnhancedWebNewCMUncial10-Regular.woff2", L"ENHANCED_MD_FONT_33", 400, "normal"},
    {"EnhancedWebNewCMUncial10-Bold.woff2", L"ENHANCED_MD_FONT_34", 700, "normal"},
};

static const MarkdownFontFace kLibertinusMath[] = {
    {"LibertinusMath-Regular.otf", L"ENHANCED_MD_FONT_35", 400, "normal"},
};

static const MarkdownFontFace kNewCMMath[] = {
    {"EnhancedWebNewCMMath-Regular.woff2", L"ENHANCED_MD_FONT_36", 400, "normal"},
    {"EnhancedWebNewCMMath-Bold.woff2", L"ENHANCED_MD_FONT_37", 700, "normal"},
};

static const MarkdownFontFace kNewCMMathBook[] = {
    {"EnhancedWebNewCMMath-Book.woff2", L"ENHANCED_MD_FONT_38", 400, "normal"},
    {"EnhancedWebNewCMMath-Bold.woff2", L"ENHANCED_MD_FONT_37", 700, "normal"},
};

static const MarkdownFontFace kNewCMSansMath[] = {
    {"EnhancedWebNewCMSansMath-Regular.woff2", L"ENHANCED_MD_FONT_39", 400, "normal"},
};

static const MarkdownFontFamily kMarkdownTextFonts[] = {
    {"", "", nullptr, 0},
    {"Lora", "EnhancedMDLora", kLora, dimofi(kLora)},
    {"Libertinus Serif", "EnhancedMDLibertinus", kLibertinus, dimofi(kLibertinus)},
    {"New Computer Modern", "EnhancedMDNewCM10", kNewCM10, dimofi(kNewCM10)},
    {"New Computer Modern Book", "EnhancedMDNewCM10Book", kNewCM10Book, dimofi(kNewCM10Book)},
    {"New Computer Modern Sans", "EnhancedMDNewCMSans10", kNewCMSans10, dimofi(kNewCMSans10)},
    {"New Computer Modern Sans Book", "EnhancedMDNewCMSans10Book", kNewCMSans10Book, dimofi(kNewCMSans10Book)},
    {"New Computer Modern Mono", "EnhancedMDNewCMMono10", kNewCMMono10, dimofi(kNewCMMono10)},
    {"New Computer Modern Mono Book", "EnhancedMDNewCMMono10Book", kNewCMMono10Book, dimofi(kNewCMMono10Book)},
    {"New Computer Modern Uncial", "EnhancedMDNewCMUncial10", kNewCMUncial10, dimofi(kNewCMUncial10)},
};

static const MarkdownFontFamily kMarkdownMathFonts[] = {
    {"", "", nullptr, 0},
    {"Libertinus Math", "EnhancedMDLibertinusMath", kLibertinusMath, dimofi(kLibertinusMath)},
    {"New Computer Modern Math", "EnhancedMDNewCMMath", kNewCMMath, dimofi(kNewCMMath)},
    {"New Computer Modern Math Book", "EnhancedMDNewCMMathBook", kNewCMMathBook, dimofi(kNewCMMathBook)},
    {"New Computer Modern Sans Math", "EnhancedMDNewCMSansMath", kNewCMSansMath, dimofi(kNewCMSansMath)},
};

int MarkdownFontCount(MarkdownFontKind kind) {
    return kind == MarkdownFontKind::Text ? dimofi(kMarkdownTextFonts) : dimofi(kMarkdownMathFonts);
}
static const MarkdownFontFamily* FontFamily(MarkdownFontKind kind, int index) {
    if (index < 0 || index >= MarkdownFontCount(kind)) return nullptr;
    return kind == MarkdownFontKind::Text ? &kMarkdownTextFonts[index] : &kMarkdownMathFonts[index];
}
Str MarkdownFontName(MarkdownFontKind kind, int index) {
    auto* font = FontFamily(kind, index);
    return font ? Str(font->name) : Str{};
}
int MarkdownFontIndex(MarkdownFontKind kind, Str name) {
    if (!len(name)) return 0;
    for (int i = 1; i < MarkdownFontCount(kind); i++) {
        if (str::EqI(name, MarkdownFontName(kind, i))) return i;
    }
    return 0;
}
void MarkdownFontChoices(MarkdownFontKind kind, StrVec& names) {
    names.Append(kind == MarkdownFontKind::Text ? Tr("Follow interface font") : Tr("Match text font"));
    for (int i = 1; i < MarkdownFontCount(kind); i++) names.Append(MarkdownFontName(kind, i));
}
static Str FontResource(const WCHAR* name) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, name, RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    const char* data = loaded ? (const char*)LockResource(loaded) : nullptr;
    return data ? Str(data, (int)SizeofResource(module, resource)) : Str{};
}
Str MarkdownSuppliedCss(bool dark) {
    return FontResource(dark ? L"ENHANCED_MARKDOWN_DARK_CSS" : L"ENHANCED_MARKDOWN_LIGHT_CSS");
}
Str MarkdownFontResource(Str url) {
    if (!str::TrimPrefix(url, Str(kMarkdownFontHost))) return {};
    for (MarkdownFontKind kind : {MarkdownFontKind::Text, MarkdownFontKind::Math}) {
        for (int i = 1; i < MarkdownFontCount(kind); i++) {
            auto* font = FontFamily(kind, i);
            for (int j = 0; j < font->count; j++) {
                auto& face = font->faces[j];
                if (str::Eq(url, Str(face.file))) return FontResource(face.resource);
            }
        }
    }
    return {};
}
static void AppendFontCss(str::Builder& css, const MarkdownFontFamily* font) {
    for (int i = 0; i < font->count; i++) {
        auto& face = font->faces[i];
        css.Append(
            fmt("@font-face{font-family:'%s';font-weight:%d;font-style:%s;font-display:swap;"
                "src:url('%s%s') format('%s');}\n",
                Str(font->cssName), face.weight, Str(face.style), Str(kMarkdownFontHost), Str(face.file),
                str::EndsWith(Str(face.file), StrL(".woff2")) ? StrL("woff2") : StrL("opentype")));
    }
}
TempStr MarkdownFontCss(Str fontName, Str mathFontName) {
    int textIndex = MarkdownFontIndex(MarkdownFontKind::Text, fontName);
    auto* font = FontFamily(MarkdownFontKind::Text, textIndex);
    int mathIndex = MarkdownFontIndex(MarkdownFontKind::Math, mathFontName);
    if (!mathIndex) {
        mathIndex = textIndex >= 3 ? 2 : 1;
        if (textIndex == 4) mathIndex = 3;
        if (textIndex == 5 || textIndex == 6) mathIndex = 4;
    }
    auto* math = FontFamily(MarkdownFontKind::Math, mathIndex);
    str::Builder css;
    AppendFontCss(css, font);
    AppendFontCss(css, math);
    Str family = font->count ? fmt("'%s','%s',serif", Str(font->cssName), Str(math->cssName))
                             : StrL("var(--enhanced-ui-font-family)");
    css.Append(
        fmt(":root{--enhanced-markdown-font-family:%s;--enhanced-markdown-math-font-family:'%s',math;"
            "--font-serif:var(--enhanced-markdown-font-family);"
            "--base-font-size:var(--enhanced-ui-font-size)!important;}\n"
            "math{font-family:var(--enhanced-markdown-math-font-family);}",
            family, Str(math->cssName)));
    return ToStrTemp(css);
}
static TempStr EscapeFontLabel(Str label) {
    str::Builder html;
    for (int i = 0; i < len(label); i++) {
        char c = label.s[i];
        switch (c) {
            case '&':
                html.Append(StrL("&amp;"));
                break;
            case '<':
                html.Append(StrL("&lt;"));
                break;
            case '>':
                html.Append(StrL("&gt;"));
                break;
            case '\'':
                html.Append(StrL("&#39;"));
                break;
            case '"':
                html.Append(StrL("&quot;"));
                break;
            default:
                html.AppendChar(c);
                break;
        }
    }
    return ToStrTemp(html);
}
TempStr MarkdownFontPickerHtml() {
    int selected = gSettings ? MarkdownFontIndex(MarkdownFontKind::Text, gSettings->markdownUI.fontName) : 0;
    StrVec names;
    MarkdownFontChoices(MarkdownFontKind::Text, names);
    str::Builder html;
    html.Append(
        fmt("<div id='enhanced-markdown-controls'><label for='enhanced-markdown-font'>%s</label>"
            "<select id='enhanced-markdown-font' aria-label='%s'>",
            EscapeFontLabel(Tr("Font")), EscapeFontLabel(Tr("Markdown font"))));
    for (int i = 0; i < len(names); i++) {
        html.Append(fmt("<option value='%d'%s>%s</option>", i, i == selected ? StrL(" selected") : Str{},
                        EscapeFontLabel(names[i])));
    }
    html.Append(
        StrL("</select></div><script>(function(){var select=document.getElementById('enhanced-markdown-font');"
             "select.onchange=function(){var i=parseInt(this.value,10);"
             "document.getElementById('enhanced-markdown-font-css').href='"));
    html.Append(Str(kMarkdownFontHost));
    html.Append(
        StrL("style-'+i+'.css?v='+Date.now();"
             "if(window.__sumatra__&&window.__sumatra__.notify){window.__sumatra__.notify('markdownFont',i);}"
             "else{window.location.href='sumatra-markdown-font:'+i;}"
             "};})();</script>"));
    return ToStrTemp(html);
}
#if IS_DEBUG
void MarkdownFonts_UnitTests() {
    StrVec names;
    MarkdownFontChoices(MarkdownFontKind::Text, names);
    utassert(len(names) == MarkdownFontCount(MarkdownFontKind::Text));
    for (MarkdownFontKind kind : {MarkdownFontKind::Text, MarkdownFontKind::Math}) {
        for (int i = 1; i < MarkdownFontCount(kind); i++) {
            auto* font = FontFamily(kind, i);
            utassert(MarkdownFontIndex(kind, Str(font->name)) == i);
            for (int j = 0; j < font->count; j++) {
                auto& face = font->faces[j];
                Str data = MarkdownFontResource(
                    fmt("%s%s", Str(kMarkdownFontHost), Str(face.file),
                        str::EndsWith(Str(face.file), StrL(".woff2")) ? StrL("woff2") : StrL("opentype")));
                utassert(len(data) > 1000 &&
                         (str::StartsWith(data, StrL("OTTO")) || str::StartsWith(data, StrL("wOF2"))));
            }
        }
    }
    utassert(len(MarkdownFontResource(StrL("https://example.com/LibertinusSerif-Regular.otf"))) == 0);
    utassert(len(MarkdownFontResource(fmt("%s../Settings.h", Str(kMarkdownFontHost)))) == 0);
    utassert(MarkdownFontIndex(MarkdownFontKind::Text, StrL("Unknown font")) == 0);
    TempStr css = MarkdownFontCss(StrL("Libertinus Serif"), {});
    utassert(str::Contains(css, StrL("font-weight:600;font-style:italic")));
    utassert(str::Contains(css, StrL("EnhancedMDLibertinusMath")));
    utassert(str::Contains(css, StrL("'EnhancedMDLibertinus','EnhancedMDLibertinusMath',serif")));
    utassert(str::Contains(MarkdownFontCss(StrL("New Computer Modern Book"), {}), StrL("EnhancedMDNewCMMathBook")));
    utassert(str::Contains(MarkdownFontCss(StrL("New Computer Modern Sans"), {}), StrL("EnhancedMDNewCMSansMath")));
    utassert(str::Eq(EscapeFontLabel(StrL("A<&'\"")), StrL("A&lt;&amp;&#39;&quot;")));
    css = MarkdownFontCss(StrL("New Computer Modern Book"), StrL("New Computer Modern Sans Math"));
    utassert(str::Contains(css, StrL("EnhancedWebNewCM10-Book.woff2")));
    utassert(str::Contains(css, StrL("EnhancedWebNewCMSansMath-Regular.woff2")));
    utassert(str::Contains(css, StrL("--base-font-size:var(--enhanced-ui-font-size)!important")));
    utassert(str::Contains(MarkdownFontPickerHtml(), StrL("<select id='enhanced-markdown-font'")));
    utassert(str::Contains(MarkdownSuppliedCss(false), StrL("--color-bg: #ffffff")));
    utassert(str::Contains(MarkdownSuppliedCss(true), StrL("--color-bg: #1e1e1e")));
}
#endif
