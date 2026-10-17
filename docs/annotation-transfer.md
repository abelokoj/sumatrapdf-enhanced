# Move annotations to a revised PDF

Export your annotations from the old document, then import them into the revised document. Highlights, comments, handwritten ink, and shapes retain their supported PDF annotation properties, including colors and opacity.

## Export from the original

1. Open the annotated original PDF.
2. Choose **File → Export annotations for transfer...**.
3. Save the annotation package (`.sannot.pdf`). The package contains the annotations and matching information; it does not change the original PDF.

Export includes the annotations currently in the app, including unsaved changes. Keep the package with the original document if you may need to transfer it again.

## Import into the revised document

1. Open the revised PDF and choose **File → Import annotations from another version...**.
2. Select **Choose annotations...** and open the package.
3. Review the old-page to new-page list. Each row shows the annotation count and match information. Pages with a unique text match can follow inserted or reordered pages.
4. For an uncertain match, choose the intended destination, enter its page number, and select **Apply page**. Select **Skip page**, or enter `0`, to omit that source page. Identical candidate pages are skipped until you choose one.
5. Select **Save annotated copy...** and choose a new PDF filename.
6. Select **Open annotated copy** to inspect the result.

The original and revised PDFs stay unchanged. The result is a separate PDF with editable annotations. If the revised PDF is password-protected, the saved copy retains its password protection. Reimporting the same package into a previously transferred copy does not create a second copy of those transferred annotations.

## Review placement after a revision

Matching is based on document content, not just page numbers. A uniquely matched text anchor can move a highlight with its text. Freehand drawings and shapes use page coordinates and page dimensions, so inspect them when a new edition changes the layout, diagrams, rotation, or cropping. Selecting a destination page does not redraw a diagram around substantially changed content.

A scanned page without extractable text, deleted passage, repeated text, or substantial rewrite may require manual page selection or skipping. Inspect the saved copy before using it as your working document. Flattened marks that have become part of the page image are not PDF annotations and cannot be exported as editable marks.

Annotation packages contain your comments, the original file path and source text used for matching. Share them as you would share the annotated document.
