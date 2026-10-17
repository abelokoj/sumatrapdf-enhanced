/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Crypto.h"
#include "base/File.h"
#include "base/GdiPlusUtil.h"
#include "base/GuessFileType.h"
#include "base/Pixmap.h"
#include "gui/Dpi.h"

#include "Settings.h"
#include "AppSettings.h"
#include "ImageReader.h"

#include "AppTools.h"
#include "FileThumbnails.h"

constexpr int kMaxThumbnailDx = kThumbnailDx * 250 / 100;
constexpr int kMaxThumbnailDy = kThumbnailFullPageDy * 250 / 100;
constexpr i64 kMaxThumbnailPixels = (i64)kMaxThumbnailDx * kMaxThumbnailDy;
constexpr i64 kMaxThumbnailPngBytes = 4 * kMaxThumbnailPixels + 1024;

ThumbnailMode GetThumbnailMode() {
    ThumbnailMode mode =
        gSettings && gSettings->homePageFullPageThumbnails ? ThumbnailMode::FullPage : ThumbnailMode::Cropped;
    static ThumbnailMode cachedMode = ThumbnailMode::Cropped;
    if (mode != cachedMode) {
        // FileState owns one image; a mode change must discard the resident previews.
        cachedMode = mode;
        if (gSettings && gSettings->fileStates) {
            for (FileState* fs : *gSettings->fileStates) {
                FreePixmap(fs->thumbnail);
                fs->thumbnail = nullptr;
            }
        }
    }
    return mode;
}

Size GetThumbnailRenderSize(ThumbnailMode mode) {
    int height = mode == ThumbnailMode::FullPage ? kThumbnailFullPageDy : kThumbnailDy;
    return {std::min(UiScalePx(kMaxThumbnailDx), kMaxThumbnailDx),
            std::min(UiScalePx(height * 250 / 100), height * 250 / 100)};
}

Size GetThumbnailRenderSize() {
    return GetThumbnailRenderSize(GetThumbnailMode());
}

float ThumbnailZoom(RectF& pageRect, Size size, ThumbnailMode mode) {
    if (pageRect.IsEmpty() || size.dx <= 0 || size.dy <= 0) return 0;
    float zoom = (float)size.dx / pageRect.dx;
    if (mode == ThumbnailMode::FullPage) return std::min(zoom, (float)size.dy / pageRect.dy);
    pageRect.dy = std::min(pageRect.dy, (float)size.dy / zoom);
    return zoom;
}

TempStr GetThumbnailPathTemp(Str filePath, ThumbnailMode mode) {
    // create a fingerprint of a (normalized) path for the file name
    // I'd have liked to also include the file's last modification time
    // in the fingerprint (much quicker than hashing the entire file's
    // content), but that's too expensive for files on slow drives
    u8 digest[16]{};
    // Null/empty paths show up when a FileState has no path (corrupt settings
    // or a race while closing); never hash an empty path (crash e.g. 35043).
    if (len(filePath) == 0) {
        return {};
    }
    TempStr path = str::DupTemp(filePath);
    if (path::HasVariableDriveLetter(path)) {
        // ignore the drive letter, if it might change
        path.s[0] = '?';
    }
    CalcMD5Digest(path, digest);
    TempStr fingerPrint = str::MemToHexTemp(Str((const char*)digest, dimofi(digest)));

    TempStr thumbsDir = GetThumbnailCacheDirTemp();
    if (len(thumbsDir) == 0) {
        return {};
    }

    Str suffix = mode == ThumbnailMode::FullPage ? StrL(".full.png") : StrL(".png");
    TempStr res = path::JoinTemp(thumbsDir, str::JoinTemp(fingerPrint, suffix));
    return res;
}

TempStr GetThumbnailPathTemp(Str filePath) {
    return GetThumbnailPathTemp(filePath, GetThumbnailMode());
}

TempStr GetThumbnailCacheDirTemp() {
    TempStr thumbsDir = GetPathInAppDataDirTemp(StrL("sumatrapdfcache"));
    return thumbsDir;
}

// Keep the directory while background thumbnail saves may still be in flight.
void EmptyThumbnailCacheDirectory() {
    TempStr thumbsDir = GetThumbnailCacheDirTemp();
    dir::Empty(thumbsDir);
}

void DeleteThumbnailForFile(Str filePath) {
    for (ThumbnailMode mode : {ThumbnailMode::Cropped, ThumbnailMode::FullPage}) {
        TempStr thumbPath = GetThumbnailPathTemp(filePath, mode);
        if (len(thumbPath) == 0) continue;
        bool ok = file::Delete(thumbPath);
        const auto* status = ok ? "ok" : "failed";
        logf("DeleteThumbnailForFile: file::Remove('%s') %s\n", thumbPath, Str(status));
    }
}

static bool PixmapIsEmpty(const Pixmap* px) {
    return !px || px->width <= 0 || px->height <= 0 || !px->data;
}

Pixmap* LoadThumbnail(FileState* fs) {
    GetThumbnailMode();
    return fs ? fs->thumbnail : nullptr;
}

bool ThumbnailIsReady(const Pixmap* thumbnail, Size size, ThumbnailMode mode) {
    if (PixmapIsEmpty(thumbnail)) return false;
    if (mode == ThumbnailMode::FullPage) {
        return thumbnail->width >= size.dx - 1 || thumbnail->height >= size.dy - 1;
    }
    return thumbnail->width >= size.dx;
}

bool HasThumbnail(FileState* fs) {
    ThumbnailMode mode = GetThumbnailMode();
    return fs && ThumbnailIsReady(fs->thumbnail, GetThumbnailRenderSize(mode), mode);
}

static bool ThumbnailPngValid(Str data) {
    FileTypeInfo info = GuessFileInfoFromData(data);
    bool valid = info.ft == FileType::Png && info.hasImageSize && info.imageDx <= kMaxThumbnailDx &&
                 info.imageDy <= kMaxThumbnailDy && (i64)info.imageDx * info.imageDy <= kMaxThumbnailPixels;
    FreeFileTypeInfo(&info);
    return valid;
}

Pixmap* LoadCachedThumbnail(Str cachePath, Str filePath) {
    if (len(cachePath) == 0 || len(filePath) == 0) return nullptr;
    i64 fileSize = file::GetSize(cachePath);
    if (fileSize <= 0 || fileSize > kMaxThumbnailPngBytes) return nullptr;
    u8 header[24];
    if (file::ReadN(cachePath, header, sizeof(header)) != (int)sizeof(header) ||
        !ThumbnailPngValid(Str((const char*)header, sizeof(header)))) {
        return nullptr;
    }
    FILETIME cacheTime = file::GetModificationTime(cachePath);
    FILETIME sourceTime = file::GetModificationTime(filePath);
    if (FileTimeDiffInSecs(sourceTime, cacheTime) > 0) return nullptr;

    Str data = file::ReadFile(cachePath);
    defer {
        str::Free(data);
    };
    if (len(data) == 0 || len(data) > kMaxThumbnailPngBytes || !ThumbnailPngValid(data)) return nullptr;
    Pixmap* thumbnail = PixmapToBgr(PixmapFromData(data));
    if (PixmapIsEmpty(thumbnail)) {
        FreePixmap(thumbnail);
        return nullptr;
    }
    return thumbnail;
}

// takes ownership of bmp
void SetThumbnail(FileState* fs, Pixmap* bmp, ThumbnailMode mode) {
    ReportIf(bmp && PixmapIsEmpty(bmp));
    if (mode != GetThumbnailMode() || !fs || len(fs->filePath) == 0 || PixmapIsEmpty(bmp)) {
        FreePixmap(bmp);
        return;
    }
    FreePixmap(fs->thumbnail);
    fs->thumbnail = bmp;
}

void SaveThumbnail(Str thumbnailPath, Pixmap* thumbnail) {
    if (len(thumbnailPath) == 0 || PixmapIsEmpty(thumbnail)) {
        return;
    }
    // failing to create the cache dir is environmental (antivirus, ACLs, disk full,
    // a file occupying the name) rather than a bug, so log and skip the thumbnail -
    // same as the other dir::CreateForFile callers. err == 0 means the create
    // reported success but the directory was gone when we looked.
    int err = 0;
    if (!dir::CreateForFile(thumbnailPath, &err)) {
        logf("SaveThumbnail: dir::CreateForFile('%s') failed, err=%d\n", thumbnailPath, err);
        return;
    }
    ReportIf(!str::EndsWithI(thumbnailPath, StrL(".png")));

    // the engine renders pages to an 8-bit palette DIB when it can, and those
    // pixels can only be read through the platform bitmap
    Pixmap* converted = nullptr;
    defer {
        FreePixmap(converted);
    };
    if (thumbnail->format == PixmapFormat::Native) {
        converted = PixmapCopyAs32bppDIB(thumbnail);
        if (!converted) {
            logf("SaveThumbnail: PixmapCopyAs32bppDIB() failed for '%s'\n", thumbnailPath);
            return;
        }
        thumbnail = converted;
    }
    // Wrap (don't take ownership) so we can encode the in-memory thumbnail as PNG.
    Gdiplus::Bitmap* bmp = WrapPixmapGdiplus(thumbnail);
    if (!bmp) {
        return;
    }
    CLSID tmpClsid = GetGdiPlusEncoderClsid(L"image/png");
    WCHAR* pathW = CWStrTemp(thumbnailPath);
    Gdiplus::Status st = bmp->Save(pathW, &tmpClsid, nullptr);
    delete bmp;
    if (st != Gdiplus::Ok) {
        // gdi+ creates the file before it encodes, so a failure leaves a 0-byte
        // png behind, which reads back as a blank thumbnail (issue #5932)
        logf("SaveThumbnail: Save('%s') failed with %d\n", thumbnailPath, (int)st);
        file::Delete(thumbnailPath);
    }
}

void RemoveThumbnail(FileState* fs) {
    if (!fs || len(fs->filePath) == 0) {
        return;
    }
    DeleteThumbnailForFile(fs->filePath);
    FreePixmap(fs->thumbnail);
    fs->thumbnail = nullptr;
}

#if IS_DEBUG
bool FileThumbnails_UnitTests() {
    if (!gSettings) return false;
    bool ok = true;
    int savedDpiX = dpiX;
    int savedDpiY = dpiY;
    int savedScale = gSettings->interfaceScale;
    for (int dpi : {96, 144, 192, 384}) {
        DpiSet(dpi, dpi);
        for (int scale : {50, 100, 150, 250}) {
            gSettings->interfaceScale = scale;
            for (ThumbnailMode mode : {ThumbnailMode::Cropped, ThumbnailMode::FullPage}) {
                Size target = GetThumbnailRenderSize(mode);
                ok &= target.dx > 0 && target.dx <= 530 && target.dy > 0 && target.dy <= 750;
                ok &= (i64)target.dx * target.dy <= kMaxThumbnailPixels;
            }
        }
    }
    DpiSet(savedDpiX, savedDpiY);
    gSettings->interfaceScale = savedScale;
    u8 header[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 2, 18, 0, 0, 2, 238};
    ok &= ThumbnailPngValid(Str((char*)header, sizeof(header)));
    header[18] = 8;
    ok &= !ThumbnailPngValid(Str((char*)header, sizeof(header)));
    header[18] = 2;
    header[20] = 0x7f;
    ok &= !ThumbnailPngValid(Str((char*)header, sizeof(header)));
    ok &= !ThumbnailPngValid(Str((char*)header, 23));
    ok &= !ThumbnailPngValid(StrL("not a cached PNG"));
    TempStr source = GetTempFilePathTemp(StrL("thumbnail"));
    TempStr cached = str::JoinTemp(source, StrL(".png"));
    defer {
        file::Delete(cached);
        file::Delete(source);
    };
    ok &= file::WriteFile(source, StrL("thumbnail source"));
    Pixmap* sample = AllocPixmap(3, 4);
    if (!sample) return false;
    memset(sample->data, 255, sample->stride * sample->height);
    SaveThumbnail(cached, sample);
    FreePixmap(sample);
    Pixmap* loaded = LoadCachedThumbnail(cached, source);
    ok &= loaded && loaded->width == 3 && loaded->height == 4;
    FreePixmap(loaded);
    FILETIME cacheTime = file::GetModificationTime(cached);
    ULARGE_INTEGER newer;
    newer.LowPart = cacheTime.dwLowDateTime;
    newer.HighPart = cacheTime.dwHighDateTime;
    newer.QuadPart += 10ULL * 10000000;
    FILETIME sourceTime{newer.LowPart, newer.HighPart};
    ok &= file::SetModificationTime(source, sourceTime);
    loaded = LoadCachedThumbnail(cached, source);
    ok &= !loaded;
    FreePixmap(loaded);
    Size size{530, 750};
    RectF portrait{0, 0, 600, 1200};
    float fullZoom = ThumbnailZoom(portrait, size, ThumbnailMode::FullPage);
    ok &= portrait.dy == 1200 && fullZoom == 0.625f;
    RectF cropped{0, 0, 600, 1200};
    float croppedZoom = ThumbnailZoom(cropped, {530, 375}, ThumbnailMode::Cropped);
    ok &= cropped.dy < 1200 && fabsf(cropped.dy * croppedZoom - 375) < 0.01f;
    RectF landscape{0, 0, 1200, 600};
    fullZoom = ThumbnailZoom(landscape, size, ThumbnailMode::FullPage);
    ok &= landscape.dy == 600 && fabsf(landscape.dx * fullZoom - 530) < 0.01f;
    RectF empty;
    ok &= ThumbnailZoom(empty, size, ThumbnailMode::FullPage) == 0;
    Str file = StrL("C:\\Reading\\Thumbnail-modes.pdf");
    TempStr cropPath = GetThumbnailPathTemp(file, ThumbnailMode::Cropped);
    TempStr fullPath = GetThumbnailPathTemp(file, ThumbnailMode::FullPage);
    ok &= len(cropPath) > 0 && len(fullPath) > 0 && !str::Eq(cropPath, fullPath);
    ok &= str::EndsWithI(fullPath, StrL(".full.png"));

    if (!gSettings || !gSettings->fileStates || len(*gSettings->fileStates) == 0) return false;
    FileState* fs = (*gSettings->fileStates)[0];
    bool savedMode = gSettings->homePageFullPageThumbnails;
    gSettings->homePageFullPageThumbnails = false;
    GetThumbnailMode();
    FreePixmap(fs->thumbnail);
    fs->thumbnail = AllocPixmap(1, 1);
    ok &= fs->thumbnail != nullptr;
    gSettings->homePageFullPageThumbnails = true;
    ok &= GetThumbnailMode() == ThumbnailMode::FullPage && !fs->thumbnail;
    Pixmap* marker = AllocPixmap(1, 1);
    ok &= marker != nullptr;
    fs->thumbnail = marker;
    SetThumbnail(fs, AllocPixmap(212, 150), ThumbnailMode::Cropped);
    ok &= fs->thumbnail == marker;
    gSettings->homePageFullPageThumbnails = false;
    ok &= GetThumbnailMode() == ThumbnailMode::Cropped && !fs->thumbnail;
    gSettings->homePageFullPageThumbnails = savedMode;
    GetThumbnailMode();
    return ok;
}
#endif
