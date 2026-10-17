/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Crypto.h"
#include "base/Dict.h"
#include "base/File.h"
extern "C" {
#include <mupdf/pdf.h>
}
#include "base/GuessFileType.h"
#include "gui/UIModels.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "EngineMuPDF.h"
#include "AnnotationTransfer.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#include "Settings.h"
#include "FileHistory.h"
#endif

static constexpr int kTransferVersion = 1;
static constexpr int kMaxTransferPages = 10000;
static constexpr int kMaxPageText = 20000;
static constexpr int kMaxTransferAnnots = 100000;
static constexpr i64 kMaxPackageBytes = 256LL * 1024 * 1024;

static void TransferError(Str* error, Str message) {
    if (error) *error = str::Dup(message);
}

static void TransferCheck(fz_context* ctx, HANDLE cancel) {
    if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) fz_throw(ctx, FZ_ERROR_ABORT, "Canceled.");
}

AnnotationTransferPlan::~AnnotationTransferPlan() {
    SecureZeroMemory(targetKey, sizeof(targetKey));
    if (len(targetSnapshot)) file::Delete(targetSnapshot);
    str::Free(packagePath);
    str::Free(targetSnapshot);
    str::Free(targetPath);
    str::Free(sourcePath);
}

static void TransferCryptKey(EngineMupdf* engine, u8 key[32]) {
    AutoUnlockRecursiveMutex lock(&engine->docLock);
    if (engine->pdfdoc->crypt) memcpy(key, pdf_crypt_key(engine->Ctx(), engine->pdfdoc->crypt), 32);
}

static pdf_document* OpenTransferSnapshot(fz_context* ctx, Str path, const u8 key[32]) {
    pdf_document* doc = pdf_open_document(ctx, CStrTemp(path));
    if (doc->crypt) memcpy(pdf_crypt_key(ctx, doc->crypt), key, 32);
    return doc;
}

static bool SameTransferPath(Str a, Str b) {
    if (!len(a) || !len(b)) return false;
    return path::IsSame(a, b) || str::EqI(path::NormalizeTemp(a), path::NormalizeTemp(b));
}

static bool IsTransferAnnot(fz_context* ctx, pdf_obj* obj) {
    if (!pdf_is_dict(ctx, obj)) return false;
    const char* subtype = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
    if (!subtype || !*subtype) return false;
    auto type = pdf_annot_type_from_string(ctx, subtype);
    return type >= PDF_ANNOT_TEXT && type <= PDF_ANNOT_POPUP && type != PDF_ANNOT_LINK;
}

static bool IsTextTransferAnnot(fz_context* ctx, pdf_obj* obj) {
    auto type = pdf_annot_type_from_string(ctx, pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype)));
    return type >= PDF_ANNOT_HIGHLIGHT && type <= PDF_ANNOT_STRIKE_OUT;
}

static TempStr TransferPageKey(Str text) {
    u8 hash[32];
    CalcSHA2Digest(text, hash);
    str::Builder key;
    for (u8 value : hash) key.Append(fmt("%02x", value));
    return ToStrTemp(key);
}

static Str NormalizeTransferText(Str input) {
    str::Builder text;
    bool space = true;
    for (int i = 0; i < len(input) && len(text) < kMaxPageText; i++) {
        unsigned char c = (unsigned char)input.s[i];
        if (c <= ' ') {
            if (!space) text.AppendChar(' ');
            space = true;
        } else {
            text.AppendChar((char)(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c));
            space = false;
        }
    }
    while (len(text) && text[len(text) - 1] == ' ') VecRemoveAt(text, len(text) - 1);
    return str::Dup(ToStr(text));
}

static Str TransferPageText(fz_context* ctx, pdf_document* doc, int pageNo) {
    pdf_page* page = nullptr;
    fz_stext_page* text = nullptr;
    fz_buffer* buffer = nullptr;
    Str result;
    fz_var(page);
    fz_var(text);
    fz_var(buffer);
    fz_var(result);
    fz_try(ctx) {
        page = pdf_load_page(ctx, doc, pageNo - 1);
        fz_stext_options options{};
        options.flags = FZ_STEXT_PRESERVE_WHITESPACE;
        text = fz_new_stext_page_from_page(ctx, (fz_page*)page, &options);
        buffer = fz_new_buffer_from_stext_page(ctx, text);
        unsigned char* data = nullptr;
        size_t size = fz_buffer_storage(ctx, buffer, &data);
        result = NormalizeTransferText(Str((char*)data, (int)std::min(size, (size_t)kMaxPageText)));
    }
    fz_always(ctx) {
        fz_drop_buffer(ctx, buffer);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, (fz_page*)page);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
    return result;
}

static int TransferAnnotCount(fz_context* ctx, pdf_obj* page) {
    int count = 0;
    pdf_obj* list = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    int n = pdf_array_len(ctx, list);
    if (n > kMaxTransferAnnots) fz_throw(ctx, FZ_ERROR_FORMAT, "Too many annotations.");
    for (int i = 0; i < n; i++) count += IsTransferAnnot(ctx, pdf_array_get(ctx, list, i));
    return count;
}

// Remove active content even if it is hidden in an appearance resource.
static void CleanTransferObject(fz_context* ctx, pdf_obj* obj, Vec<int>& visited, int depth, int* budget) {
    if (!obj) return;
    if (depth > 64 || --*budget < 0) fz_throw(ctx, FZ_ERROR_FORMAT, "Annotation resources are too complex.");
    int num = pdf_to_num(ctx, obj);
    if (num) {
        for (int seen : visited)
            if (seen == num) return;
        VecAppend(visited, num);
    }
    if (pdf_is_array(ctx, obj)) {
        for (int i = 0; i < pdf_array_len(ctx, obj); i++)
            CleanTransferObject(ctx, pdf_array_get(ctx, obj, i), visited, depth + 1, budget);
        return;
    }
    if (!pdf_is_dict(ctx, obj)) return;
    static const char* forbidden[] = {"A",
                                      "AA",
                                      "JS",
                                      "JavaScript",
                                      "OpenAction",
                                      "Next",
                                      "Dest",
                                      "URI",
                                      "Launch",
                                      "FS",
                                      "EF",
                                      "EmbeddedFiles",
                                      "RichMediaContent",
                                      "RichMediaSettings",
                                      "AcroForm",
                                      "XFA"};
    for (const char* key : forbidden) pdf_dict_dels(ctx, obj, key);
    for (int i = pdf_dict_len(ctx, obj) - 1; i >= 0; i--) {
        pdf_obj* value = pdf_dict_get_val(ctx, obj, i);
        const char* type = pdf_dict_get_name(ctx, value, PDF_NAME(Type));
        if (str::Eq(Str(type), StrL("Page")) || str::Eq(Str(type), StrL("Pages")) ||
            str::Eq(Str(type), StrL("Catalog"))) {
            pdf_dict_del(ctx, obj, pdf_dict_get_key(ctx, obj, i));
            continue;
        }
        CleanTransferObject(ctx, value, visited, depth + 1, budget);
    }
}

static pdf_obj* CloneTransferAnnot(fz_context* ctx, pdf_document* dst, pdf_graft_map* map, pdf_obj* src) {
    pdf_obj* copy = pdf_new_dict(ctx, dst, 32);
    fz_try(ctx) {
        static const char* keys[] = {"Type",
                                     "Subtype",
                                     "Rect",
                                     "Contents",
                                     "NM",
                                     "M",
                                     "F",
                                     "AP",
                                     "AS",
                                     "Border",
                                     "C",
                                     "CA",
                                     "T",
                                     "CreationDate",
                                     "Subj",
                                     "RC",
                                     "IT",
                                     "RT",
                                     "Open",
                                     "Name",
                                     "State",
                                     "StateModel",
                                     "DA",
                                     "Q",
                                     "DS",
                                     "CL",
                                     "LE",
                                     "L",
                                     "IC",
                                     "BS",
                                     "BE",
                                     "RD",
                                     "Vertices",
                                     "QuadPoints",
                                     "InkList",
                                     "LL",
                                     "LLE",
                                     "Cap",
                                     "CP",
                                     "CO",
                                     "Measure",
                                     "Sy",
                                     "OverlayText",
                                     "Repeat",
                                     "Rotate",
                                     "SumatraTransferId",
                                     "SumatraTransferAnchor",
                                     "SumatraTransferAnchorRect"};
        for (const char* key : keys) {
            pdf_obj* value = pdf_dict_gets(ctx, src, key);
            if (value) pdf_dict_puts_drop(ctx, copy, key, pdf_graft_mapped_object(ctx, map, value));
        }
        Vec<int> visited;
        int budget = kMaxTransferAnnots;
        CleanTransferObject(ctx, copy, visited, 0, &budget);
    }
    fz_catch(ctx) {
        pdf_drop_obj(ctx, copy);
        fz_rethrow(ctx);
    }
    return copy;
}

static void SetTransferId(fz_context* ctx, pdf_obj* obj, Str pageText, int pageNo, int index) {
    const char* existing = pdf_dict_get_text_string(ctx, obj, PDF_NAME(NM));
    Str id = Str(pdf_to_text_string(ctx, pdf_dict_gets(ctx, obj, "SumatraTransferId")));
    if (len(id)) return;
    if (existing && *existing) {
        id = fmt("nm:%s:%s", Str(existing), TransferPageKey(pageText));
    } else {
        size_t size = 0;
        char* raw = pdf_sprint_obj(ctx, nullptr, 0, &size, obj, 1, 1);
        u8 hash[32];
        CalcSHA2Digest(Str(raw, (int)size), hash);
        fz_free(ctx, raw);
        u8 pageHash[32];
        CalcSHA2Digest(pageText, pageHash);
        id = fmt("sha:%02x%02x%02x%02x%02x%02x%02x%02x:%02x%02x%02x%02x:%d:%d", hash[0], hash[1], hash[2], hash[3],
                 hash[4], hash[5], hash[6], hash[7], pageHash[0], pageHash[1], pageHash[2], pageHash[3], pageNo, index);
    }
    pdf_dict_puts_drop(ctx, obj, "SumatraTransferId", pdf_new_text_string(ctx, CStrTemp(id)));
}

static fz_rect SearchTransferAnchor(fz_context* ctx, fz_stext_page* text, const char* anchor, fz_quad* quads,
                                    int* count) {
    int marks[256]{};
    *count = fz_search_stext_page(ctx, text, anchor, marks, quads, 256);
    fz_rect bounds = fz_empty_rect;
    if (*count == 256) {
        *count = 0;
        return bounds;
    }
    int hits = 0;
    for (int i = 0; i < *count; i++) {
        if (marks[i]) hits++;
        bounds = fz_union_rect(bounds, fz_rect_from_quad(quads[i]));
    }
    if (hits > 1) *count = 0;
    return bounds;
}

static void AddTransferAnchors(fz_context* ctx, pdf_document* src, int pageNo, pdf_obj* source, pdf_obj* copy) {
    if (!IsTextTransferAnnot(ctx, source)) return;
    pdf_page* page = nullptr;
    fz_stext_page* text = nullptr;
    char* anchor = nullptr;
    fz_var(page);
    fz_var(text);
    fz_var(anchor);
    fz_try(ctx) {
        page = pdf_load_page(ctx, src, pageNo - 1);
        text = fz_new_stext_page_from_page(ctx, (fz_page*)page, nullptr);
        fz_rect box;
        fz_matrix ctm;
        pdf_page_transform(ctx, page, &box, &ctm);
        fz_rect rect = fz_transform_rect(pdf_dict_get_rect(ctx, source, PDF_NAME(Rect)), ctm);
        anchor = fz_copy_rectangle(ctx, text, rect, 0);
        if (anchor && strlen(anchor) > 2 && strlen(anchor) <= 512) {
            fz_quad quads[256];
            int count;
            fz_rect anchorRect = SearchTransferAnchor(ctx, text, anchor, quads, &count);
            if (count) {
                pdf_dict_puts_drop(ctx, copy, "SumatraTransferAnchor", pdf_new_text_string(ctx, anchor));
                pdf_dict_puts_drop(ctx, copy, "SumatraTransferAnchorRect",
                                   pdf_new_rect(ctx, pdf_get_bound_document(ctx, copy), anchorRect));
            }
        }
    }
    fz_always(ctx) {
        fz_free(ctx, anchor);
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, (fz_page*)page);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
}

static void SaveTransferPdf(fz_context* ctx, pdf_document* doc, Str output, HANDLE cancel) {
    Str temp = str::Dup(
        fmt("%s.transfer-%lu-%lu-%llu.tmp", output, GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount64()));
    bool owned = false;
    fz_var(owned);
    fz_try(ctx) {
        TransferCheck(ctx, cancel);
        HANDLE reserved =
            CreateFileW(CWStrTemp(temp), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (reserved == INVALID_HANDLE_VALUE)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Could not create a temporary output file.");
        CloseHandle(reserved);
        owned = true;
        pdf_write_options opts{};
        opts.do_incremental = 0;
        opts.do_compress = 1;
        opts.do_garbage = 3;
        opts.do_encrypt = doc->crypt ? PDF_ENCRYPT_KEEP : PDF_ENCRYPT_NONE;
        pdf_save_document(ctx, doc, CStrTemp(temp), &opts);
        TransferCheck(ctx, cancel);
        if (!file::RenameReplace(output, temp)) fz_throw(ctx, FZ_ERROR_GENERIC, "Could not save the output file.");
    }
    fz_always(ctx) {
        if (owned) file::Delete(temp);
        str::Free(temp);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
}

bool ExportAnnotationTransfer(EngineBase* engine, Str output, Str* error, HANDLE cancel) {
    if (error) *error = {};
    auto* pdf = AsEngineMupdf(engine);
    if (!pdf || !pdf->pdfdoc || SameTransferPath(engine->FilePath(), output)) {
        TransferError(error, StrL("Choose a separate annotation file for this PDF."));
        return false;
    }
    u8 sourceKey[32]{};
    TransferCryptKey(pdf, sourceKey);
    Str snapshot = str::Dup(GetTempFilePathTemp(StrL("annotation-export")));
    if (!EngineMupdfSaveCopy(engine, snapshot)) {
        SecureZeroMemory(sourceKey, sizeof(sourceKey));
        file::Delete(snapshot);
        str::Free(snapshot);
        TransferError(error, StrL("Could not read the current PDF and its annotations."));
        return false;
    }
    auto* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    pdf_document* src = nullptr;
    pdf_document* dst = nullptr;
    pdf_graft_map* map = nullptr;
    bool ok = false;
    fz_var(src);
    fz_var(dst);
    fz_var(map);
    fz_var(ok);
    fz_try(ctx) {
        src = OpenTransferSnapshot(ctx, snapshot, sourceKey);
        dst = pdf_create_document(ctx);
        map = pdf_new_graft_map(ctx, dst);
        int pages = pdf_count_pages(ctx, src);
        if (pages < 1 || pages > kMaxTransferPages) fz_throw(ctx, FZ_ERROR_FORMAT, "Unsupported page count.");
        pdf_obj* catalog = pdf_dict_get(ctx, pdf_trailer(ctx, dst), PDF_NAME(Root));
        pdf_dict_puts_drop(ctx, catalog, "SumatraAnnotationTransfer", pdf_new_int(ctx, kTransferVersion));
        pdf_dict_puts_drop(ctx, catalog, "SumatraSourceName", pdf_new_text_string(ctx, CStrTemp(engine->FilePath())));
        int total = 0;
        for (int p = 1; p <= pages; p++) {
            TransferCheck(ctx, cancel);
            AutoArenaSavepoint arena;
            pdf_obj* sourcePage = pdf_lookup_page_obj(ctx, src, p - 1);
            fz_rect media = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, sourcePage, PDF_NAME(MediaBox)));
            int rotation = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, sourcePage, PDF_NAME(Rotate)));
            pdf_obj* page = pdf_add_page(ctx, dst, media, rotation, nullptr, nullptr);
            pdf_insert_page(ctx, dst, -1, page);
            pdf_drop_obj(ctx, page);
            page = pdf_lookup_page_obj(ctx, dst, p - 1);
            pdf_obj* crop = pdf_dict_get_inheritable(ctx, sourcePage, PDF_NAME(CropBox));
            if (crop) pdf_dict_put_drop(ctx, page, PDF_NAME(CropBox), pdf_graft_mapped_object(ctx, map, crop));
            pdf_obj* userUnit = pdf_dict_get(ctx, sourcePage, PDF_NAME(UserUnit));
            if (userUnit) pdf_dict_put_drop(ctx, page, PDF_NAME(UserUnit), pdf_graft_mapped_object(ctx, map, userUnit));
            Str pageText = TransferPageText(ctx, src, p);
            pdf_dict_puts_drop(ctx, page, "SumatraSourceText",
                               pdf_new_text_string(ctx, len(pageText) ? CStrTemp(pageText) : ""));
            pdf_obj* annots = pdf_dict_put_array(ctx, page, PDF_NAME(Annots), 8);
            pdf_obj* sourceList = pdf_dict_get(ctx, sourcePage, PDF_NAME(Annots));
            for (int a = 0; a < pdf_array_len(ctx, sourceList); a++) {
                pdf_obj* annot = pdf_array_get(ctx, sourceList, a);
                if (!IsTransferAnnot(ctx, annot)) continue;
                if (++total > kMaxTransferAnnots) fz_throw(ctx, FZ_ERROR_FORMAT, "Too many annotations.");
                pdf_obj* copy = CloneTransferAnnot(ctx, dst, map, annot);
                fz_try(ctx) {
                    SetTransferId(ctx, copy, pageText, p, a);
                    AddTransferAnchors(ctx, src, p, annot, copy);
                    pdf_array_push_drop(ctx, annots, pdf_add_object(ctx, dst, copy));
                }
                fz_always(ctx) {
                    pdf_drop_obj(ctx, copy);
                }
                fz_catch(ctx) {
                    str::Free(pageText);
                    fz_rethrow(ctx);
                }
            }
            str::Free(pageText);
        }
        if (!total) fz_throw(ctx, FZ_ERROR_GENERIC, "This PDF has no transferable annotations.");
        SaveTransferPdf(ctx, dst, output, cancel);
        ok = true;
    }
    fz_always(ctx) {
        SecureZeroMemory(sourceKey, sizeof(sourceKey));
        pdf_drop_graft_map(ctx, map);
        pdf_drop_document(ctx, dst);
        pdf_drop_document(ctx, src);
        file::Delete(snapshot);
        str::Free(snapshot);
    }
    fz_catch(ctx) {
        TransferError(error, Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    return ok;
}

static pdf_document* OpenTransferPackage(fz_context* ctx, Str path) {
    i64 size = file::GetSize(path);
    if (size <= 0 || size > kMaxPackageBytes) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid or oversized annotation file.");
    pdf_document* doc = pdf_open_document(ctx, CStrTemp(path));
    fz_try(ctx) {
        pdf_obj* catalog = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        if (pdf_to_int(ctx, pdf_dict_gets(ctx, catalog, "SumatraAnnotationTransfer")) != kTransferVersion)
            fz_throw(ctx, FZ_ERROR_FORMAT, "Choose an annotation file exported by SumatraPDF Enhanced.");
        int pages = pdf_count_pages(ctx, doc);
        if (pages < 1 || pages > kMaxTransferPages)
            fz_throw(ctx, FZ_ERROR_FORMAT, "Unsupported annotation page count.");
    }
    fz_catch(ctx) {
        pdf_drop_document(ctx, doc);
        fz_rethrow(ctx);
    }
    return doc;
}

static float TransferSimilarity(Str source, Str target) {
    if (len(source) < 12 || len(target) < 12) return 0;
    int words = 0, matched = 0;
    for (int i = 0; i < len(source) && words < 128;) {
        int start = i;
        while (i < len(source) && source.s[i] != ' ') i++;
        int n = i - start;
        i++;
        if (n < 4) continue;
        words++;
        if (str::Contains(target, Str(source.s + start, n))) matched++;
    }
    return words < 3 ? 0.f : (float)matched / words;
}

AnnotationTransferPlan* AnalyzeAnnotationTransfer(EngineBase* engine, Str package, Str* error, HANDLE cancel) {
    if (error) *error = {};
    auto* pdf = AsEngineMupdf(engine);
    if (!pdf || !pdf->pdfdoc) {
        TransferError(error, StrL("Open the revised PDF first."));
        return nullptr;
    }
    auto* plan = new AnnotationTransferPlan();
    TransferCryptKey(pdf, plan->targetKey);
    plan->packagePath = str::Dup(package);
    plan->targetPath = str::Dup(engine->FilePath());
    plan->targetSnapshot = str::Dup(GetTempFilePathTemp(StrL("annotation-target")));
    if (!EngineMupdfSaveCopy(engine, plan->targetSnapshot)) {
        delete plan;
        TransferError(error, StrL("Could not read the revised PDF."));
        return nullptr;
    }
    auto* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    pdf_document* src = nullptr;
    pdf_document* dst = nullptr;
    Vec<Str> texts;
    dict::MapStrToInt pageIndex;
    bool ok = false;
    fz_var(src);
    fz_var(dst);
    fz_var(ok);
    fz_try(ctx) {
        src = OpenTransferPackage(ctx, package);
        dst = OpenTransferSnapshot(ctx, plan->targetSnapshot, plan->targetKey);
        plan->targetPageCount = pdf_count_pages(ctx, dst);
        if (plan->targetPageCount > kMaxTransferPages)
            fz_throw(ctx, FZ_ERROR_FORMAT, "Unsupported revised PDF page count.");
        plan->sourcePath = str::Dup(Str(pdf_to_text_string(
            ctx, pdf_dict_gets(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, src), PDF_NAME(Root)), "SumatraSourceName"))));
        for (int p = 1; p <= plan->targetPageCount; p++) {
            TransferCheck(ctx, cancel);
            AutoArenaSavepoint arena;
            Str text = TransferPageText(ctx, dst, p);
            VecAppend(texts, text);
            Str key = TransferPageKey(text);
            if (!pageIndex.Insert(key, p)) {
                pageIndex.Remove(key, nullptr);
                pageIndex.Insert(key, 0);
            }
        }
        int total = 0;
        for (int p = 1; p <= pdf_count_pages(ctx, src); p++) {
            TransferCheck(ctx, cancel);
            pdf_obj* page = pdf_lookup_page_obj(ctx, src, p - 1);
            AnnotationTransferPage row;
            row.sourcePage = p;
            row.annotationCount = TransferAnnotCount(ctx, page);
            if (!row.annotationCount) continue;
            total += row.annotationCount;
            if (total > kMaxTransferAnnots) fz_throw(ctx, FZ_ERROR_FORMAT, "Too many annotations.");
            Str text = Str(pdf_to_text_string(ctx, pdf_dict_gets(ctx, page, "SumatraSourceText")));
            if (len(text) > kMaxPageText) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page matching data.");
            int exactCount = 0, exactPage = 0;
            if (len(text) >= 12 && len(text) < kMaxPageText - 1 && pageIndex.Get(TransferPageKey(text), &exactPage)) {
                exactCount = exactPage > 0 && str::Eq(text, texts[exactPage - 1]) ? 1 : 2;
            }
            float best = 0, second = 0;
            int bestPage = 0;
            for (int j = 0; !exactCount && len(text) >= 12 && j < len(texts); j++) {
                if (!(j % 32)) TransferCheck(ctx, cancel);
                float score = TransferSimilarity(text, texts[j]);
                if (score > best) {
                    second = best;
                    best = score;
                    bestPage = j + 1;
                } else if (score > second)
                    second = score;
            }
            if (exactCount == 1) {
                row.targetPage = exactPage;
                row.confidence = 1;
                row.textMatched = true;
            } else if (!exactCount && best >= 0.7f && best - second >= 0.15f) {
                row.targetPage = 0;
                row.suggestedPage = bestPage;
                row.confidence = best * 0.9f;
                row.textMatched = true;
            }
            VecAppend(plan->pages, row);
        }
        if (!total) fz_throw(ctx, FZ_ERROR_FORMAT, "The annotation file contains no transferable annotations.");
        ok = true;
    }
    fz_always(ctx) {
        for (Str text : texts) str::Free(text);
        pdf_drop_document(ctx, dst);
        pdf_drop_document(ctx, src);
    }
    fz_catch(ctx) {
        TransferError(error, Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    if (!ok) {
        delete plan;
        return nullptr;
    }
    return plan;
}

static void TransformTransferPoints(fz_context* ctx, pdf_obj* array, fz_matrix transform) {
    int n = pdf_array_len(ctx, array);
    if (n > kMaxTransferAnnots * 2 || n % 2) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid annotation coordinates.");
    for (int i = 0; i < n; i += 2) {
        fz_point point = {pdf_array_get_real(ctx, array, i), pdf_array_get_real(ctx, array, i + 1)};
        if (!isfinite(point.x) || !isfinite(point.y)) fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid annotation coordinates.");
        point = fz_transform_point(point, transform);
        pdf_array_put_real(ctx, array, i, point.x);
        pdf_array_put_real(ctx, array, i + 1, point.y);
    }
}

static bool HasTransferId(fz_context* ctx, pdf_obj* list, pdf_obj* source) {
    Str id = Str(pdf_to_text_string(ctx, pdf_dict_gets(ctx, source, "SumatraTransferId")));
    for (int i = 0; i < pdf_array_len(ctx, list); i++) {
        pdf_obj* annot = pdf_array_get(ctx, list, i);
        if (len(id) && str::Eq(id, Str(pdf_to_text_string(ctx, pdf_dict_gets(ctx, annot, "SumatraTransferId")))))
            return true;
    }
    return false;
}

static fz_matrix TransferPageMatrix(fz_context* ctx, pdf_obj* src, pdf_obj* dst) {
    fz_rect sourceBox, targetBox;
    fz_matrix sourceMatrix, targetMatrix;
    pdf_page_obj_transform(ctx, src, &sourceBox, &sourceMatrix);
    pdf_page_obj_transform(ctx, dst, &targetBox, &targetMatrix);
    sourceBox = fz_transform_rect(sourceBox, sourceMatrix);
    targetBox = fz_transform_rect(targetBox, targetMatrix);
    float sw = sourceBox.x1 - sourceBox.x0, sh = sourceBox.y1 - sourceBox.y0;
    float tw = targetBox.x1 - targetBox.x0, th = targetBox.y1 - targetBox.y0;
    if (!isfinite(sw) || !isfinite(sh) || !isfinite(tw) || !isfinite(th) || sw <= 0 || sh <= 0 || tw <= 0 || th <= 0)
        fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page dimensions.");
    fz_matrix visual = fz_concat(fz_translate(-sourceBox.x0, -sourceBox.y0), fz_scale(tw / sw, th / sh));
    visual = fz_concat(visual, fz_translate(targetBox.x0, targetBox.y0));
    return fz_concat(fz_concat(sourceMatrix, visual), fz_invert_matrix(targetMatrix));
}

static void PlaceTransferAnnot(fz_context* ctx, pdf_document* dst, pdf_obj* copy, pdf_obj* srcPage, pdf_obj* dstPage,
                               int targetPage) {
    fz_matrix transform = TransferPageMatrix(ctx, srcPage, dstPage);
    pdf_dict_put_rect(ctx, copy, PDF_NAME(Rect),
                      fz_transform_rect(pdf_dict_get_rect(ctx, copy, PDF_NAME(Rect)), transform));
    static const char* arrays[] = {"QuadPoints", "Vertices", "L", "CL"};
    for (const char* key : arrays) {
        pdf_obj* points = pdf_dict_gets(ctx, copy, key);
        if (points) TransformTransferPoints(ctx, points, transform);
    }
    pdf_obj* ink = pdf_dict_get(ctx, copy, PDF_NAME(InkList));
    for (int i = 0; i < pdf_array_len(ctx, ink); i++)
        TransformTransferPoints(ctx, pdf_array_get(ctx, ink, i), transform);
    if (!IsTextTransferAnnot(ctx, copy)) return;
    const char* anchor = pdf_to_text_string(ctx, pdf_dict_gets(ctx, copy, "SumatraTransferAnchor"));
    if (!anchor || !*anchor || strlen(anchor) > 512) return;
    pdf_page* page = nullptr;
    fz_stext_page* text = nullptr;
    fz_var(page);
    fz_var(text);
    fz_try(ctx) {
        page = pdf_load_page(ctx, dst, targetPage - 1);
        text = fz_new_stext_page_from_page(ctx, (fz_page*)page, nullptr);
        fz_quad quads[256];
        int count;
        fz_rect rect = SearchTransferAnchor(ctx, text, anchor, quads, &count);
        if (count) {
            fz_rect box;
            fz_matrix ctm;
            pdf_page_transform(ctx, page, &box, &ctm);
            fz_matrix inverse = fz_invert_matrix(ctm);
            fz_rect sourceAnchor = pdf_to_rect(ctx, pdf_dict_gets(ctx, copy, "SumatraTransferAnchorRect"));
            fz_rect sourceBox;
            fz_matrix sourceCtm;
            pdf_page_obj_transform(ctx, srcPage, &sourceBox, &sourceCtm);
            fz_matrix visual = fz_concat(fz_concat(fz_invert_matrix(sourceCtm), transform), ctm);
            sourceAnchor = fz_transform_rect(sourceAnchor, visual);
            if (fabsf((rect.x1 - rect.x0) - (sourceAnchor.x1 - sourceAnchor.x0)) < 0.5f &&
                fabsf((rect.y1 - rect.y0) - (sourceAnchor.y1 - sourceAnchor.y0)) < 0.5f) {
                fz_matrix move = fz_concat(
                    fz_concat(ctm, fz_translate(rect.x0 - sourceAnchor.x0, rect.y0 - sourceAnchor.y0)), inverse);
                pdf_dict_put_rect(ctx, copy, PDF_NAME(Rect),
                                  fz_transform_rect(pdf_dict_get_rect(ctx, copy, PDF_NAME(Rect)), move));
                TransformTransferPoints(ctx, pdf_dict_get(ctx, copy, PDF_NAME(QuadPoints)), move);
            } else {
                pdf_obj* points = pdf_dict_put_array(ctx, copy, PDF_NAME(QuadPoints), count * 8);
                for (int q = 0; q < count; q++) {
                    fz_quad quad = fz_transform_quad(quads[q], inverse);
                    fz_point vertices[] = {quad.ul, quad.ur, quad.ll, quad.lr};
                    for (fz_point pt : vertices) {
                        pdf_array_push_real(ctx, points, pt.x);
                        pdf_array_push_real(ctx, points, pt.y);
                    }
                }
                pdf_dict_put_rect(ctx, copy, PDF_NAME(Rect), fz_transform_rect(rect, inverse));
                pdf_dict_del(ctx, copy, PDF_NAME(AP));
            }
        }
    }
    fz_always(ctx) {
        fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, (fz_page*)page);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
}

bool ImportAnnotationTransfer(AnnotationTransferPlan* plan, Str output, int* imported, int* skipped, Str* error,
                              HANDLE cancel) {
    if (error) *error = {};
    if (imported) *imported = 0;
    if (skipped) *skipped = 0;
    if (!plan || SameTransferPath(output, plan->targetPath) || SameTransferPath(output, plan->packagePath) ||
        SameTransferPath(output, plan->sourcePath) || SameTransferPath(output, plan->targetSnapshot)) {
        TransferError(error, StrL("Save the annotated PDF as a separate file."));
        return false;
    }
    auto* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    pdf_document* src = nullptr;
    pdf_document* dst = nullptr;
    pdf_graft_map* map = nullptr;
    bool ok = false;
    int added = 0, omitted = 0;
    fz_var(src);
    fz_var(dst);
    fz_var(map);
    fz_var(ok);
    fz_var(added);
    fz_var(omitted);
    fz_try(ctx) {
        src = OpenTransferPackage(ctx, plan->packagePath);
        dst = OpenTransferSnapshot(ctx, plan->targetSnapshot, plan->targetKey);
        map = pdf_new_graft_map(ctx, dst);
        for (auto& row : plan->pages) {
            TransferCheck(ctx, cancel);
            AutoArenaSavepoint arena;
            if (row.sourcePage < 1 || row.sourcePage > pdf_count_pages(ctx, src) || row.targetPage < 0 ||
                row.targetPage > pdf_count_pages(ctx, dst))
                fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid page mapping.");
            pdf_obj* sourcePage = pdf_lookup_page_obj(ctx, src, row.sourcePage - 1);
            if (!row.targetPage) {
                omitted += TransferAnnotCount(ctx, sourcePage);
                continue;
            }
            pdf_obj* targetPage = pdf_lookup_page_obj(ctx, dst, row.targetPage - 1);
            pdf_obj* targetList = pdf_dict_get(ctx, targetPage, PDF_NAME(Annots));
            if (!targetList) targetList = pdf_dict_put_array(ctx, targetPage, PDF_NAME(Annots), 8);
            pdf_obj* sourceList = pdf_dict_get(ctx, sourcePage, PDF_NAME(Annots));
            for (int a = 0; a < pdf_array_len(ctx, sourceList); a++) {
                pdf_obj* annot = pdf_array_get(ctx, sourceList, a);
                if (!IsTransferAnnot(ctx, annot)) continue;
                if (HasTransferId(ctx, targetList, annot)) {
                    omitted++;
                    continue;
                }
                pdf_obj* copy = CloneTransferAnnot(ctx, dst, map, annot);
                fz_try(ctx) {
                    PlaceTransferAnnot(ctx, dst, copy, sourcePage, targetPage, row.targetPage);
                    pdf_dict_put(ctx, copy, PDF_NAME(P), targetPage);
                    pdf_array_push_drop(ctx, targetList, pdf_add_object(ctx, dst, copy));
                    added++;
                    if (added > kMaxTransferAnnots) fz_throw(ctx, FZ_ERROR_FORMAT, "Too many annotations.");
                }
                fz_always(ctx) {
                    pdf_drop_obj(ctx, copy);
                }
                fz_catch(ctx) {
                    fz_rethrow(ctx);
                }
            }
        }
        if (!added && !omitted) fz_throw(ctx, FZ_ERROR_GENERIC, "No annotations were selected for import.");
        SaveTransferPdf(ctx, dst, output, cancel);
        ok = true;
    }
    fz_always(ctx) {
        pdf_drop_graft_map(ctx, map);
        pdf_drop_document(ctx, dst);
        pdf_drop_document(ctx, src);
    }
    fz_catch(ctx) {
        TransferError(error, Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    if (ok) {
        if (imported) *imported = added;
        if (skipped) *skipped = omitted;
    }
    return ok;
}

TempStr AnnotationTransferDumpTemp(Str path, Str* error) {
    if (error) *error = {};
    str::Builder result;
    auto* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    pdf_document* doc = nullptr;
    fz_var(doc);
    fz_try(ctx) {
        doc = pdf_open_document(ctx, CStrTemp(path));
        for (int p = 0; p < pdf_count_pages(ctx, doc); p++) {
            pdf_obj* list = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, p), PDF_NAME(Annots));
            for (int i = 0; i < pdf_array_len(ctx, list); i++) {
                pdf_obj* a = pdf_array_get(ctx, list, i);
                fz_rect r = pdf_dict_get_rect(ctx, a, PDF_NAME(Rect));
                result.Append(fmt("page=%d;type=%s;rect=%g,%g,%g,%g", p + 1,
                                  Str(pdf_dict_get_name(ctx, a, PDF_NAME(Subtype))), r.x0, r.y0, r.x1 - r.x0,
                                  r.y1 - r.y0));
                for (const char* key : {"C", "IC"}) {
                    pdf_obj* values = pdf_dict_gets(ctx, a, key);
                    if (!values) continue;
                    result.Append(fmt(";%s=", Str(key)));
                    for (int n = 0; n < pdf_array_len(ctx, values); n++)
                        result.Append(fmt("%s%g", n ? StrL(",") : Str{}, pdf_array_get_real(ctx, values, n)));
                }
                result.Append(fmt(";CA=%g;contents=%s;ink=", pdf_dict_get_real(ctx, a, PDF_NAME(CA)),
                                  Str(pdf_dict_get_text_string(ctx, a, PDF_NAME(Contents)))));
                pdf_obj* ink = pdf_dict_get(ctx, a, PDF_NAME(InkList));
                bool first = true;
                for (int stroke = 0; stroke < pdf_array_len(ctx, ink); stroke++) {
                    pdf_obj* points = pdf_array_get(ctx, ink, stroke);
                    for (int n = 0; n < pdf_array_len(ctx, points); n++) {
                        result.Append(fmt("%s%g", first ? Str{} : StrL(","), pdf_array_get_real(ctx, points, n)));
                        first = false;
                    }
                }
                result.AppendChar('\n');
            }
        }
    }
    fz_always(ctx) {
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        TransferError(error, Str(fz_caught_message(ctx)));
    }
    fz_drop_context(ctx);
    return ToStrTemp(result);
}

#if IS_DEBUG
class TransferTestPassword : public PasswordUI {
  public:
    Str GetPassword(Str, u8*, u8[32], bool* saveKey) override {
        *saveKey = false;
        return str::Dup(StrL("transfer-secret"));
    }
};

static void TransferEncryptedTests() {
    Vec<FileState*> emptyHistory;
    auto* savedHistory = FileHistoryStates();
    FileHistorySetStates(&emptyHistory);
    Str original = str::Dup(GetTempFilePathTemp(StrL("transfer-encrypted")));
    Str package = str::Dup(GetTempFilePathTemp(StrL("transfer-package")));
    Str output = str::Dup(GetTempFilePathTemp(StrL("transfer-result")));
    auto* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    pdf_document* doc = nullptr;
    bool saved = false;
    fz_var(doc);
    fz_var(saved);
    fz_try(ctx) {
        doc = pdf_create_document(ctx);
        pdf_obj* page = pdf_add_page(ctx, doc, fz_rect{0, 0, 612, 792}, 0, nullptr, nullptr);
        pdf_obj* annots = pdf_dict_put_array(ctx, page, PDF_NAME(Annots), 1);
        pdf_obj* annot = pdf_new_dict(ctx, doc, 4);
        pdf_dict_put(ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put(ctx, annot, PDF_NAME(Subtype), PDF_NAME(Square));
        pdf_dict_put_rect(ctx, annot, PDF_NAME(Rect), fz_rect{72, 72, 144, 144});
        pdf_array_push_drop(ctx, annots, annot);
        pdf_insert_page(ctx, doc, -1, page);
        pdf_drop_obj(ctx, page);
        pdf_write_options opts{};
        opts.do_encrypt = PDF_ENCRYPT_AES_256;
        opts.permissions = -1;
        strcpy_s(opts.upwd_utf8, "transfer-secret");
        strcpy_s(opts.opwd_utf8, "transfer-owner");
        pdf_save_document(ctx, doc, CStrTemp(original), &opts);
        saved = true;
    }
    fz_always(ctx) {
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        saved = false;
    }
    utassert(saved);
    TransferTestPassword password;
    EngineBase* engine = saved ? CreateEngineMupdfFromFile(original, FileType::PDF, 96, &password) : nullptr;
    utassert(engine != nullptr);
    Str error;
    bool exported = engine && ExportAnnotationTransfer(engine, package, &error);
    utassert(exported);
    str::Free(error);
    AnnotationTransferPlan* plan = exported ? AnalyzeAnnotationTransfer(engine, package, &error) : nullptr;
    utassert(plan && len(plan->pages) == 1);
    str::Free(error);
    int imported = 0;
    int skipped = 0;
    bool importedOk = false;
    if (plan && len(plan->pages) == 1) {
        plan->pages[0].targetPage = 1;
        importedOk = ImportAnnotationTransfer(plan, output, &imported, &skipped, &error);
    }
    utassert(importedOk && imported == 1 && skipped == 0);
    str::Free(error);
    delete plan;
    if (engine) engine->Release();
    FileHistorySetStates(savedHistory);
    doc = nullptr;
    if (importedOk) {
        fz_try(ctx) {
            doc = pdf_open_document(ctx, CStrTemp(output));
            utassert(pdf_needs_password(ctx, doc));
            utassert(pdf_authenticate_password(ctx, doc, "transfer-secret") != 0);
            utassert(pdf_count_pages(ctx, doc) == 1);
            utassert(TransferAnnotCount(ctx, pdf_lookup_page_obj(ctx, doc, 0)) == 2);
        }
        fz_always(ctx) {
            pdf_drop_document(ctx, doc);
        }
        fz_catch(ctx) {
            utassert(false);
        }
    }
    fz_drop_context(ctx);
    for (Str path : {original, package, output}) {
        file::Delete(path);
        str::Free(path);
    }
}
#endif

void AnnotationTransferTests() {
#if IS_DEBUG
    TransferEncryptedTests();
#endif
#ifdef DEBUG
    Str a = NormalizeTransferText(StrL("  One\n  TWO\tthree  "));
    utassert(str::Eq(a, StrL("one two three")));
    utassert(TransferSimilarity(a, StrL("one two three")) == 0);
    str::Free(a);
    a = NormalizeTransferText(StrL("alpha bravo charlie delta"));
    utassert(TransferSimilarity(a, StrL("alpha bravo charlie delta changed")) == 1);
    utassert(TransferSimilarity(a, StrL("unrelated words here")) == 0);
    str::Free(a);
    utassert(SameTransferPath(StrL("C:/temp/book.pdf"), StrL("c:\\temp\\BOOK.pdf")));
#endif
}
