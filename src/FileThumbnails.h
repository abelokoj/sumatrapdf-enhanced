/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// thumbnails are 150px high and have a ratio of sqrt(2) : 1
constexpr int kThumbnailDx = 212;
constexpr int kThumbnailDy = 150;
constexpr int kThumbnailFullPageDy = 300;

enum class ThumbnailMode {
    Cropped,
    FullPage
};

struct Pixmap;
struct FileState;

Pixmap* LoadThumbnail(FileState* fs);
bool HasThumbnail(FileState* fs);
bool ThumbnailIsReady(const Pixmap*, Size, ThumbnailMode);
Pixmap* LoadCachedThumbnail(Str cachePath, Str filePath);
void SetThumbnail(FileState* fs, Pixmap* bmp, ThumbnailMode);
void SaveThumbnail(Str cachePath, Pixmap*);
void RemoveThumbnail(FileState* fs);

TempStr GetThumbnailCacheDirTemp();
TempStr GetThumbnailPathTemp(Str filePath);
TempStr GetThumbnailPathTemp(Str filePath, ThumbnailMode);
void DeleteThumbnailForFile(Str path);
void EmptyThumbnailCacheDirectory();

ThumbnailMode GetThumbnailMode();
Size GetThumbnailRenderSize();
Size GetThumbnailRenderSize(ThumbnailMode);
float ThumbnailZoom(RectF& pageRect, Size, ThumbnailMode);
void RequestHomeThumbnail(FileState* fs);

#if IS_DEBUG
bool FileThumbnails_UnitTests();
#endif
