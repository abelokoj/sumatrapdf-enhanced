/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/ByteReaderWriter.h"
#include "base/Crypto.h"
#include "base/File.h"
#include "base/Http.h"
#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "SumatraPDF.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "FileTypeIcons.h"

struct FileTypeIconPack {
    int style;
    const char* name;
    const char* url;
    int bytes;
    const char* sha256;
};

struct FileTypeIconAsset {
    int style;
    const char* ext;
    int bytes;
    const char* sha256;
};

#include "FileTypeIconPacks.h"

static const char* kIconExtensions[] = {
    "pdf", "xps", "oxps", "cbz",  "cbr",  "cb7",  "cbt",  "djvu", "chm",  "mobi", "epub", "md",  "markdown",
    "svg", "azw", "azw3", "azw4", "fb2",  "fb2z", "prc",  "tif",  "tiff", "jp2",  "png",  "jpg", "jpeg",
    "tga", "gif", "avif", "heic", "heif", "jfif", "webp", "jxl",  "bmp",  "ico",  "jxr",  "hdp", "wdp"};

constexpr int kFirstFileTypeIcon = 1000;
constexpr int kMaxIconPackSize = 8 * 1024 * 1024;
constexpr int kMaxIconSize = 512 * 1024;
static Mutex gIconDownloadLock;
static Mutex gIconVerifyLock;
static StrVec gVerifiedIconPaths;
static Vec<FILETIME> gVerifiedIconTimes;

bool ValidFileTypeIconStyle(int style) {
    return style >= 1 && style <= 3;
}

Str FileTypeIconStyleName(int style) {
    if (style == 1) return StrL("Folded pages");
    if (style == 2) return StrL("Color covers");
    return StrL("Outlined pages");
}

static const FileTypeIconPack* IconPack(int style) {
    for (const auto& pack : kFileTypeIconPacks) {
        if (pack.style == style) return &pack;
    }
    return nullptr;
}

static int ExtensionIndex(Str ext) {
    str::TrimPrefix(ext, StrL("."));
    for (int i = 0; i < dimofi(kIconExtensions); i++) {
        if (str::EqI(ext, Str(kIconExtensions[i]))) return i;
    }
    return -1;
}

static const FileTypeIconAsset* IconAsset(int style, Str ext) {
    for (const auto& asset : kFileTypeIconAssets) {
        if (asset.style == style && str::Eq(ext, Str(asset.ext))) return &asset;
    }
    return nullptr;
}

TempStr FileTypeIconCacheDir() {
    return path::ToOSTemp(path::JoinTemp(GetAppDataDirTemp(), StrL("file-icons")));
}

static TempStr PackDir(const FileTypeIconPack* pack, Str root) {
    if (!len(root)) root = FileTypeIconCacheDir();
    return path::ToOSTemp(path::JoinTemp(root, Str(pack->sha256)));
}

static TempStr IconPath(const FileTypeIconPack* pack, const FileTypeIconAsset* asset, Str root) {
    return path::JoinTemp(PackDir(pack, root), fmt("%s.ico", Str(asset->ext)));
}

int FileTypeIconDownloadBytes(int style) {
    const auto* pack = IconPack(style);
    return pack ? pack->bytes : 0;
}

static bool ValidDigest(Str data, int bytes, const char* sha256) {
    if (len(data) != bytes) return false;
    u8 hash[32]{};
    CalcSHA2Digest(data, hash);
    return str::Eq(str::MemToHexTemp(Str((char*)hash, sizeof(hash))), Str(sha256));
}

static bool ValidIcon(Str data) {
    ByteReader reader(data);
    if (!reader.CanRead(0, 6) || reader.UInt16LE(0) != 0 || reader.UInt16LE(2) != 1) return false;
    int count = reader.UInt16LE(4);
    if (count < 1 || count > 32 || !reader.CanRead(6, count * 16)) return false;
    for (int i = 0; i < count; i++) {
        u32 size = reader.UInt32LE(6 + i * 16 + 8);
        u32 offset = reader.UInt32LE(6 + i * 16 + 12);
        if (!size || offset < (u32)(6 + count * 16) || offset > (u32)len(data) || size > (u32)len(data) - offset)
            return false;
    }
    return true;
}

static bool CachedIcon(const FileTypeIconPack* pack, const FileTypeIconAsset* asset, Str root) {
    TempStr path = IconPath(pack, asset, root);
    if (file::GetSize(path) != asset->bytes) return false;
    FILETIME modified = file::GetModificationTime(path);
    AutoUnlockMutex lock(&gIconVerifyLock);
    int index = gVerifiedIconPaths.Find(path);
    if (index >= 0 && CompareFileTime(&modified, &gVerifiedIconTimes[index]) == 0) return true;
    Str data = file::ReadFile(path);
    bool valid = ValidDigest(data, asset->bytes, asset->sha256) && ValidIcon(data);
    str::Free(data);
    if (!valid) return false;
    if (index < 0) {
        gVerifiedIconPaths.Append(path);
        VecAppend(gVerifiedIconTimes, modified);
    } else {
        gVerifiedIconTimes[index] = modified;
    }
    return true;
}

bool FileTypeIconPackAvailable(int style, Str cacheRoot) {
    if (!ValidFileTypeIconStyle(style)) return false;
    if (style == 3) return true;
    if (!HasPermission(Perm::DiskAccess)) return false;
    const auto* pack = IconPack(style);
    if (!pack) return false;
    for (const char* ext : kIconExtensions) {
        const auto* asset = IconAsset(style, Str(ext));
        if (!asset || !CachedIcon(pack, asset, cacheRoot)) return false;
    }
    return true;
}

struct IconEntry {
    Str ext;
    Str data;
};

static bool ReadIconPack(Str data, Vec<IconEntry>& entries) {
    VecReset(entries);
    if (len(data) < 12 || len(data) > kMaxIconPackSize || memcmp(data.s, "SFEICON1", 8)) return false;
    ByteReader reader(data);
    int count = (int)reader.UInt32LE(8);
    if (count != dimofi(kIconExtensions)) return false;
    int offset = 12;
    for (int i = 0; i < count; i++) {
        if (!reader.CanRead(offset, 6)) return false;
        int nameSize = reader.UInt16LE(offset);
        u32 iconSize = reader.UInt32LE(offset + 2);
        offset += 6;
        if (nameSize < 1 || nameSize > 8 || iconSize > kMaxIconSize || !reader.CanRead(offset, nameSize)) return false;
        Str ext(data.s + offset, nameSize);
        offset += nameSize;
        if (!str::Eq(ext, Str(kIconExtensions[i])) || !reader.CanRead(offset, (int)iconSize)) return false;
        Str icon(data.s + offset, (int)iconSize);
        if (!ValidIcon(icon)) return false;
        VecAppend(entries, IconEntry{ext, icon});
        offset += (int)iconSize;
    }
    return offset == len(data);
}

static bool IconDownloadError(Str* error, Str message) {
    if (error) str::ReplaceWithCopy(error, message);
    return false;
}

static bool ValidIconPack(Str data, const FileTypeIconPack* pack, Vec<IconEntry>& entries) {
    if (!ValidDigest(data, pack->bytes, pack->sha256) || !ReadIconPack(data, entries)) return false;
    for (const auto& entry : entries) {
        const auto* asset = IconAsset(pack->style, entry.ext);
        if (!asset || !ValidDigest(entry.data, asset->bytes, asset->sha256)) return false;
    }
    return true;
}

static void RemoveIconStaging(Str staging) {
    // Only the fixed extension filenames we wrote are eligible for cleanup.
    for (const char* ext : kIconExtensions) file::Delete(path::JoinTemp(staging, fmt("%s.ico", Str(ext))));
    RemoveDirectoryW(CWStrTemp(staging));
}

static bool PublishIconPack(const FileTypeIconPack* pack, const Vec<IconEntry>& entries, Str cacheRoot) {
    TempStr destination = PackDir(pack, cacheRoot);
    TempStr staging = path::JoinTemp(
        cacheRoot, fmt("%s.%d.%llu.pending", Str(pack->name), (int)GetCurrentProcessId(), (u64)GetTickCount64()));
    if (!dir::Create(staging)) return false;
    bool ok = true;
    for (const auto& entry : entries) {
        if (!file::WriteFile(path::JoinTemp(staging, fmt("%s.ico", entry.ext)), entry.data)) {
            ok = false;
            break;
        }
    }
    TempStr previous = str::JoinTemp(staging, StrL(".old"));
    bool movedPrevious = false;
    if (ok && dir::Exists(destination)) {
        if (FileTypeIconPackAvailable(pack->style, cacheRoot)) {
            RemoveIconStaging(staging);
            return true;
        }
        movedPrevious = MoveFileExW(CWStrTemp(destination), CWStrTemp(previous), MOVEFILE_WRITE_THROUGH) != 0;
        ok = movedPrevious;
    }
    if (ok) {
        ok = MoveFileExW(CWStrTemp(staging), CWStrTemp(destination), MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok && movedPrevious) MoveFileExW(CWStrTemp(previous), CWStrTemp(destination), MOVEFILE_WRITE_THROUGH);
        if (!ok) ok = FileTypeIconPackAvailable(pack->style, cacheRoot);
    }
    if (ok && movedPrevious) RemoveIconStaging(previous);
    RemoveIconStaging(staging);
    return ok;
}

bool DownloadFileTypeIcons(int style, Str* error, Str cacheRoot) {
    if (error) str::ReplaceWithCopy(error, {});
    if (!ValidFileTypeIconStyle(style)) return IconDownloadError(error, StrL("Unknown file icon style."));
    if (style == 3) return true;
    if (!HasPermission(Perm::DiskAccess)) return IconDownloadError(error, StrL("File icon downloads are disabled."));
    if (!len(cacheRoot)) cacheRoot = FileTypeIconCacheDir();
    if (FileTypeIconPackAvailable(style, cacheRoot)) return true;
    if (!HasPermission(Perm::InternetAccess)) return IconDownloadError(error, StrL("Internet access is disabled."));
    AutoUnlockMutex lock(&gIconDownloadLock);
    if (FileTypeIconPackAvailable(style, cacheRoot)) return true;
    const auto* pack = IconPack(style);
    if (!pack || pack->bytes <= 0 || pack->bytes > kMaxIconPackSize)
        return IconDownloadError(error, StrL("The file icon pack is not available."));
    int directoryError = 0;
    if (!dir::CreateAll(cacheRoot, &directoryError))
        return IconDownloadError(error, fmt("Cannot create the icon folder (Windows error %d).", directoryError));
    TempStr pending = path::JoinTemp(cacheRoot, fmt("%s.%d.pending", Str(pack->name), (int)GetCurrentProcessId()));
    bool ok = HttpGetToFile(Str(pack->url), pending, {}, pack->bytes);
    Str data = ok ? file::ReadFile(pending) : Str{};
    file::Delete(pending);
    Vec<IconEntry> entries;
    ok = ok && ValidIconPack(data, pack, entries);
    if (!ok) {
        str::Free(data);
        return IconDownloadError(error, StrL("The icon download failed or its checksum did not match. Try again."));
    }
    ok = PublishIconPack(pack, entries, cacheRoot);
    str::Free(data);
    if (!ok || !FileTypeIconPackAvailable(style, cacheRoot))
        return IconDownloadError(error, StrL("Cannot save the downloaded file icons. Try again."));
    return true;
}

TempStr FileTypeDefaultIcon(Str ext, Str exePath, int style, Str cacheRoot) {
    int index = ExtensionIndex(ext);
    if (index < 0) return {};
    const auto* pack = IconPack(style);
    const auto* asset = pack ? IconAsset(style, Str(kIconExtensions[index])) : nullptr;
    if (pack && asset && HasPermission(Perm::DiskAccess) && CachedIcon(pack, asset, cacheRoot))
        return fmt("\"%s\",0", IconPath(pack, asset, cacheRoot));
    return fmt("\"%s\",-%d", exePath, kFirstFileTypeIcon + index);
}

#if IS_DEBUG
void FileTypeIconsTest() {
    utassert(dimofi(kIconExtensions) == 39);
    utassert(!ValidFileTypeIconStyle(0));
    utassert(!ValidFileTypeIconStyle(4));
    utassert(FileTypeIconPackAvailable(3));
    for (int i = 0; i < dimofi(kIconExtensions); i++) {
        Str ext(kIconExtensions[i]);
        utassert(ExtensionIndex(ext) == i);
        utassert(ExtensionIndex(fmt(".%s", ext)) == i);
        utassert(str::Eq(FileTypeDefaultIcon(ext, StrL("C:\\reader.exe"), 3),
                         fmt("\"C:\\reader.exe\",-%d", kFirstFileTypeIcon + i)));
        utassert(IconAsset(1, ext));
        utassert(IconAsset(2, ext));
        for (int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
            HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kFirstFileTypeIcon + i),
                                           IMAGE_ICON, size, size, 0);
            utassert(icon);
            if (!icon) continue;
            ICONINFO info{};
            utassert(GetIconInfo(icon, &info));
            BITMAP bitmap{};
            utassert(info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap));
            utassert(bitmap.bmWidth == size && bitmap.bmHeight == size && bitmap.bmBitsPixel == 32);
            if (info.hbmColor) DeleteObject(info.hbmColor);
            if (info.hbmMask) DeleteObject(info.hbmMask);
            DestroyIcon(icon);
        }
    }
    utassert(!len(FileTypeDefaultIcon(StrL(".unsupported"), StrL("reader.exe"), 3)));
    Vec<IconEntry> entries;
    utassert(!ReadIconPack({}, entries));
    utassert(!ReadIconPack(StrL("SFEICON1xxxx"), entries));
    ByteWriterLE ico;
    ico.Write16(0);
    ico.Write16(1);
    ico.Write16(1);
    ico.Write8x2(16, 16);
    ico.Write16(0);
    ico.Write16(1);
    ico.Write16(32);
    ico.Write32(4);
    ico.Write32(22);
    ico.Write32(0);
    utassert(ValidIcon(ico.AsByteSlice()));
    ByteWriterLE pack;
    pack.d.Append(StrL("SFEICON1"));
    pack.Write32(dimofi(kIconExtensions));
    for (const char* ext : kIconExtensions) {
        pack.Write16((u16)len(Str(ext)));
        pack.Write32(ico.Size());
        pack.d.Append(Str(ext));
        pack.d.Append(ico.AsByteSlice());
    }
    utassert(ReadIconPack(pack.AsByteSlice(), entries));
    utassert(len(entries) == 39);
    Str corrupt = str::Dup(pack.AsByteSlice());
    corrupt.s[18] = '/';
    utassert(!ReadIconPack(corrupt, entries));
    str::Free(corrupt);
    pack.Write8(0);
    utassert(!ReadIconPack(pack.AsByteSlice(), entries));

    TempStr cacheRoot = GetTempFilePathTemp(StrL("file-icons-test"));
    file::Delete(cacheRoot);
    utassert(dir::CreateAll(cacheRoot));
    for (const auto& source : kFileTypeIconPacks) {
        TempStr fixture =
            path::JoinTemp(GetSelfExeDirTemp(), fmt("../../docs/file-icons/packs/%s.sfeicons", Str(source.name)));
        Str bytes = file::ReadFile(fixture);
        if (!len(bytes)) continue;
        utassert(ValidIconPack(bytes, &source, entries));
        utassert(PublishIconPack(&source, entries, cacheRoot));
        utassert(FileTypeIconPackAvailable(source.style, cacheRoot));
        for (const char* ext : kIconExtensions) {
            const auto* asset = IconAsset(source.style, Str(ext));
            utassert(str::Eq(FileTypeDefaultIcon(Str(ext), StrL("reader.exe"), source.style, cacheRoot),
                             fmt("\"%s\",0", IconPath(&source, asset, cacheRoot))));
        }
        Str damaged = str::Dup(bytes);
        damaged.s[len(damaged) - 1] ^= 1;
        utassert(!ValidIconPack(damaged, &source, entries));
        str::Free(damaged);
        const auto* asset = IconAsset(source.style, StrL("pdf"));
        TempStr cachedPdf = IconPath(&source, asset, cacheRoot);
        utassert(file::WriteFile(cachedPdf, StrL("corrupt")));
        utassert(!FileTypeIconPackAvailable(source.style, cacheRoot));
        utassert(str::Eq(FileTypeDefaultIcon(StrL("pdf"), StrL("reader.exe"), source.style, cacheRoot),
                         StrL("\"reader.exe\",-1000")));
        utassert(ValidIconPack(bytes, &source, entries));
        utassert(PublishIconPack(&source, entries, cacheRoot));
        utassert(FileTypeIconPackAvailable(source.style, cacheRoot));
        RemoveIconStaging(PackDir(&source, cacheRoot));
        str::Free(bytes);
    }
    utassert(!FileTypeIconPackAvailable(1, cacheRoot));
    utassert(str::Eq(FileTypeDefaultIcon(StrL("pdf"), StrL("reader.exe"), 1, cacheRoot), StrL("\"reader.exe\",-1000")));
    RemoveDirectoryW(CWStrTemp(cacheRoot));
}
#endif
