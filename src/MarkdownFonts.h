/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

enum class MarkdownFontKind {
    Text,
    Math
};
int MarkdownFontCount(MarkdownFontKind);
Str MarkdownFontName(MarkdownFontKind, int index);
int MarkdownFontIndex(MarkdownFontKind, Str name);
void MarkdownFontChoices(MarkdownFontKind, StrVec& names);
TempStr MarkdownFontCss(Str fontName, Str mathFontName);
TempStr MarkdownFontPickerHtml();
Str MarkdownFontResource(Str url);
Str MarkdownSuppliedCss(bool dark);
void MarkdownFonts_UnitTests();
