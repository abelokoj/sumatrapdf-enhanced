# Annotation Mode

Use the Annotation Mode button to hide the title bar and filename tabs while keeping the tools available. Press Escape or the same button to restore the normal view.

Drag the toolbar grip to the top, bottom, left, or right of the document. The exit button stays at the end of the toolbar. Tools that do not fit remain accessible by scrolling over the toolbar or using its arrow buttons.

In Advanced Settings, `AnnotationToolbarColumns` sets the side toolbar to **1, 2, or 3 columns**. The default is one column. Top and bottom toolbars use one row. Page-number and zoom fields retain their readable width.

For two or three columns, `AnnotationToolbarStyle` offers **Grouped** (default) or **Continuous rail**. Grouped keeps related tools in separate panels; Continuous rail uses one uninterrupted panel. This setting leaves the single-column and horizontal layouts unchanged.

Pinned tools stay in the order you add them. When they do not fit on a side toolbar, scroll vertically to reach them. The Annotation Mode exit button remains available at the end of the toolbar.

`AnnotationPageNumberStyle` sets the stacked page-number display in the one-column toolbar and the default three-column layout. **1A - Short separator** is the default: the current page is above a short line, with the last page below it. **1B - Divided box** uses one tall box with a full divider. **1C - Two boxes** uses separate boxes for the current and last page. All three keep the toolbar's uniform width. With `LiveAppearancePreview` enabled, changes preview immediately; Cancel restores the saved style.

For the three-column side toolbar, `AnnotationThreeColumnPageStyle` offers three choices in Advanced Settings:

- **Current stacked (default)** keeps the current page above the last page, using `AnnotationPageNumberStyle` for its separator or boxes.
- **Full-width page row** places the current and last page side by side across the toolbar, with Previous / Fit Width / Next above and the zoom controls below.
- **Compact horizontal page field** places the current and last page side by side between Previous and Next.

The current page remains editable, including Roman page labels; the last-page count is read-only. This setting applies only to the three-column vertical toolbar in Annotation Mode. It does not change the one-column layout or the two-column horizontal page and zoom rows. Live preview, Cancel, and Save work the same way as the other appearance settings.

# Highlights

Select text and press **A** to highlight it. To highlight a complete rectangular area, hold **Ctrl** while dragging over the page, release the mouse, then press **A** or select Highlight from the selection toolbar or context menu.

Both kinds use the normal highlight color, note, save, delete, and undo controls. New highlights have straight sides and gently rounded corners. Existing PDFs with their own saved highlight appearance retain that appearance until the highlight is edited.
