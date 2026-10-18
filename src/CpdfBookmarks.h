/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

bool CanEditPdfBookmarks(MainWindow*);
void ShowCpdfBookmarks(MainWindow*);
void AddBookmarkForCurrentPage(MainWindow*);
void RenameSelectedBookmark(MainWindow*);
void DeleteSelectedBookmark(MainWindow*);
void SaveBookmarksAsync(MainWindow*);
#if defined(DEBUG)
bool CpdfBookmarks_UnitTests();
#endif
