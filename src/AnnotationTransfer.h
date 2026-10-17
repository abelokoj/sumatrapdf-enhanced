/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct AnnotationTransferPage {
    int sourcePage = 0;
    int targetPage = 0;
    int suggestedPage = 0;
    int annotationCount = 0;
    float confidence = 0;
    bool textMatched = false;
};

struct AnnotationTransferPlan {
    Vec<AnnotationTransferPage> pages;
    int targetPageCount = 0;
    Str packagePath;
    Str targetSnapshot;
    Str targetPath;
    Str sourcePath;
    u8 targetKey[32]{};
    ~AnnotationTransferPlan();
};

bool ExportAnnotationTransfer(EngineBase*, Str output, Str* error, HANDLE cancel = nullptr);
AnnotationTransferPlan* AnalyzeAnnotationTransfer(EngineBase*, Str package, Str* error, HANDLE cancel = nullptr);
bool ImportAnnotationTransfer(AnnotationTransferPlan*, Str output, int* imported, int* skipped, Str* error,
                              HANDLE cancel = nullptr);
TempStr AnnotationTransferDumpTemp(Str path, Str* error);
void AnnotationTransferTests();
