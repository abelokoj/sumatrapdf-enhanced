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
int MarkdownMathFontIndex(Str text, Str math);
TempStr MarkdownFontsCacheDir();
bool MarkdownFontsAvailable(Str text, Str math);
bool DownloadMarkdownFonts(Str text, Str math, Str* error, Str cacheRoot = {});
Str MarkdownFontResource(Str url);
Str MarkdownSuppliedCss(bool dark);
bool ReadingFontAvailable(Str name);
bool DownloadReadingFont(Str name, Str* error, Str cacheRoot = {});
TempStr ResolveReadingFont(Str name);
void MarkdownFonts_UnitTests();
