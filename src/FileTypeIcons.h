/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

bool ValidFileTypeIconStyle(int style);
Str FileTypeIconStyleName(int style);
TempStr FileTypeIconCacheDir();
int FileTypeIconDownloadBytes(int style);
bool FileTypeIconPackAvailable(int style, Str cacheRoot = {});
bool DownloadFileTypeIcons(int style, Str* error, Str cacheRoot = {});
TempStr FileTypeDefaultIcon(Str ext, Str exePath, int style, Str cacheRoot = {});

#if IS_DEBUG
void FileTypeIconsTest();
void FileTypeIconRegistryTest();
#endif
