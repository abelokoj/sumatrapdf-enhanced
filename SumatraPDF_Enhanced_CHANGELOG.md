# SumatraPDF Enhanced changelog

## Testing release 2026-10-23.7

- Restore reliable reference and document contents-page hover previews in reading and hand-tool modes.
- Keep the hover deadline and rendering request while moving inside one reference, and retain known references through temporary rendering hit-test misses.
- Restore the thin red outline in light/dark themes and avoid competing one-line internal-link tooltips.
- Publish Windows x64 installer and portable ZIP packages. See [release notes](docs/releases/2026-10-23.7.md) for validation and limitations.

## Local test build 2026-10-23.1

- Keep one hover deadline and render request while the pointer moves within the same reference or contents-page link; leaving or changing the reference still cancels/replaces it.
- Add a continuous-movement regression alongside the existing color, hand-tool and red-outline checks.

## Local test build 2026-10-22.8

- Give black and colored internal paper/equation references the same hover-preview path, including cursor-only discovery after a missed initial mouse hit.
- Suppress the competing one-line reference-description tooltip when a preview is available; retain descriptions for external links and when previews are disabled.
- Cover black linked papers/equations, colored papers and unlinked black citations, plus the original thesis references supplied for diagnosis.

## Local test build 2026-10-22.7

- Restore reference previews while the hand tool is idle; keep dragging and panning behavior unchanged.
- Keep valid previews alive when rendering temporarily locks link detection, while retaining normal dismissal outside the source reference.
- Restore a 0.6 pt red outline around reference previews in light and dark themes, scaled for display DPI. High-contrast mode retains the system edge color.
- Add a native mouse/timer regression for reading and hand tools, destination selection and the red outline.

## Testing release 2026-10-22.6

- Add filename search to the open-document dropdown, with live case-insensitive filtering, keyboard/mouse selection, and a no-matches state.
- Preserve the existing tab order, scrolling, fonts, scaling and theme colors.
- See [release notes](docs/releases/2026-10-22.6.md) for behavior and validation limits.

## Testing release 2026-10-21.5

- Fix Markdown content-only zoom and synchronize wheel, toolbar, default and remembered zoom.
- Add editable selection highlight presets for PDFs and fixed-page documents.
- Add all-downloaded-and-online dictionary aggregation with source labels, cancellation and persisted selection.
- See [release notes](docs/releases/2026-10-21.5.md) for the full changes since 2026-10-11.4 and validation limits.


## Pen tools icon selection - 2026-10-18

- Replace the Pen tools button with approved option 6 (Restrained monochrome): silver nib, neutral fitting, small blue/cyan collar accent, and short rounded point. Use separate light/dark SVG artwork and refresh it when the toolbar theme changes.
- Fix faint native rendering by replacing unsupported SVG gradient references with explicit silver fills and a cyan/blue collar accent. Native MuPDF rendering reproduced the black body before the change and passed the silver-fill check afterward.
- Retain all seven theme pairs, earlier comparison sheets, selection notes, and scalable option 6 assets in [the design reference folder](docs/design/pen-tools-options-2026-10-18/SELECTION.md).


## Implemented Features, UI Smoothness & Markdown Enhancements — 2026-10-17

- **Command Palette Header & Draggable Frame**: Added a standard captioned title bar with dark-mode styling and a close button to the Command Palette window (`CommandPalette.cpp`), enabling users to drag, reposition, and close the palette easily.
- **Markdown Editing Toolbar & Shortcuts (PrivacyNotes.app Method)**: Added an "Edit Markdown" toolbar button that surfaces when a Markdown file is open. Clicking it presents a 13-icon formatting toolbar (`B`, `I`, `U`, `H`, `List`, `Link`, `[ ]`, `Color`, `Align`, `Code`, `Highlight`, `Callout`, `Strike`) with selection wrapping, placeholder insertion, and standard keyboard shortcuts (Ctrl+B, Ctrl+I, Ctrl+U, Ctrl+K, Ctrl+S) in `TextViewWnd.cpp` and `Toolbar.cpp`.
- **Ctrl + Mouse Wheel Zoom in Markdown Documents**: Re-enabled WebView2 zoom control in `WebView.cpp` and routed Ctrl+wheel zooming in `BrowserDocView.cpp` for instant zoom responsiveness in Markdown documents.
- **Ultra-Smooth Pen, Drawing & Annotation Tools**: Re-engineered ink annotation drawing in `AnnotPlacement.cpp` to achieve zero-latency "pen on paper" smoothness, utilizing cardinal spline curve smoothing (`DrawCurve` with tension 0.5f and anti-aliased round caps) and immediate `UpdateWindow` paint dispatch to eliminate stroke lag and jagged polylines.
- **Tab Bar Overflow Alignment & 2pt Top Padding**: Perfectly aligned the three right-hand tab overflow buttons (`<`, `>`, `v`), standardized their rendered Pixmap dimensions, and introduced a 2pt top padding across tab bars to prevent text and tabs from abutting the top border (`TabsCtrl.cpp`, `Tabs.cpp`).
- **Sleek Filled Arrow Heads**: Updated all shapes with arrows (lines, dimension lines, two-ended arrows) to feature sleek, proportionally sized, filled arrow heads rather than oversized or open arrow heads across both MuPDF appearance streams and in-app drawing (`pdf-appearance.c`, `Annotation.cpp`).
- **Consistent Application Icon Across All Popup Windows**: Ensured all dialogs and popup windows (Theme/Color, Shapes [2D / Axes], Command Palette, Font settings, etc.) consistently display the SumatraPDF application icon in their title bar caption headers (`ScaledWindowCaption.h`, `VirtHost.cpp`, `WindowBase.cpp`).
- **Configurable Polygon Sides & Star Shape**: Added side count selection for the Polygon annotation tool and introduced a full-featured Star shape tool (`CmdCreateAnnotStar`) with configurable point/side count (persisted via `polygonSides` and `starSides` in `Settings.h` / `Settings.cpp`, with visual UI pickers in `Toolbar.cpp` and vertex generation in `AnnotPlacement.cpp`).
- **Lasso Selection Rotation Handle & Instant Smooth Transformations**: Redesigned the lasso tool's rotation handle with a 3x larger circular badge and curved rotation arrow badge (`Canvas.cpp`). Eliminated moving/resizing lag by rendering live preview ink strokes during active transformations, invoking synchronous canvas updates on mouse move, and immediately rerendering the target page on transform commit.
- **Partial Shape Erasing**: Enhanced the eraser tool so that drawn shapes (rectangles, ellipses, polygons, stars, math curves) are no longer erased only as single monolithic units; they can now be partially erased segment-by-segment just like freehand ink strokes (`AnnotPlacement.cpp`).
- **Shape Color Selection Crash Resolution**: Fixed a crash occurring when selecting shape colors: guarded `VBox`, `HBox`, and `Wrap` layouts against null child pointers; verified and safeguarded shape fill panel creation for shapes without fills (such as mathematical curves and axes); added null checks across `OnShapeBorderColor`, `OnShapeFillSwatch`, `ShapeFillColorPicked`, and thickness sliders; bounded SVG icon indexing in `PinnedShapeSvg` and `BuildShapesHoverMenu`; and hardened MuPDF appearance generation against missing parent pages and invalid stream dictionaries.
- **App-Wide Smoothness & Binary Optimization**: Verified link-time whole-program code generation and dead-stripping (`/OPT:REF`, `/OPT:ICF`) to maintain an ultra-compact binary footprint (~28.1 MB executable, ~10.2 MB core library) with zero UI freezing across the application.

## Implemented release-number migration — 2026-10-16

- Assign canonical date tags `enhanced-2026-10-01.1`, `.2`, `.3` and `enhanced-2026-10-11.4` to the four historical Enhanced releases at their original source commits. Retain legacy Enhanced source tags as aliases; official upstream source tags are unchanged.
- Rename download assets and update public release headings, badges and cross-links. Preserve original EXE/ZIP hashes and signatures. Replace external checksums/manifests and add per-release migration records documenting both historical and canonical identities; embedded historical versions remain unchanged.
- Implement calendar version generation, real-date validation, four-component Windows product/file metadata and canonical installer/portable names. Set the unpublished local build to `2026-10-16.5`.
- Show the Enhanced version in About and diagnostics, with a separate upstream SumatraPDF3.7 attribution. Apply matching Windows versions to the rendering library and shell extensions.
- Update the updater to accept calendar versions while retaining legacy semantic-version migration, strict download/digest/architecture/product identity checks and four-field resource validation. Existing old installed updaters require manual installation of the first calendar build.
- Validate global counters across paginated published app releases, including previews and maintenance; drafts and optional asset-only releases do not consume a number. Packaging tests mock GitHub; their publication messages do not indicate a real release.
- Debug and Release builds passed without warnings/errors;105 focused native assertions, generator and isolated publisher/packaging tests passed. Inspected Light/Dark About captures and checked exact Release/installer payload version fields.
- Provide a local x64 test installer. No source commit/push or new public release occurred; ARM64 was not rebuilt. [Evidence and installer](C:/Users/abelo/Documents/Codex/2026-09-30/referenced-chatgpt-conversation-this-is-an/artifacts/version-migration-2026-10-16/results.json).

## Adopted release numbering — 2026-10-16

- Adopt `YYYY-MM-DD.N` for displayed Enhanced versions and future download filenames; for example, `2026-10-23.14`. Use the actual publication date in America/New_York.
- `N` is the running count of all published releases, including previews, feature updates and maintenance fixes. It never resets by day or year. All architecture/package files for one release share the same number; unpublished local test builds do not consume a public release number.
- Use `YYYY.MM.DD.N` for Windows numeric version metadata, for example `2026.10.23.14`. Describe Major, Minor or Maintenance scope in release notes rather than adding another version component.
- Rename the four existing GitHub release titles and their version notices: Preview 03 to `2026-10-01.1 Preview`, v0.1.0 to `2026-10-01.2`, v0.1.1 to `2026-10-01.3`, and v0.1.2 to `2026-10-11.4`. Preserve existing tags, download assets, URLs and historical binary contents. The next published release uses its actual date and counter `5`.
- Supersede the earlier bullet/colon formats, short-year proposal, daily counter and separate maintenance suffix.
- Initial adoption status: titles/notes were renamed first; the subsequent implementation and canonical-tag migration are recorded above. Retain a distinct “Based on SumatraPDF 3.7” upstream-source attribution rather than presenting 3.7 as the Enhanced product version.
- Record the adopted rule and pending implementation in [PrettySumatraPDF_TODO.md](SumatraPDF_Enhanced_TODO.md).

## v0.1.0 — 2026-10-01

- Update the repository homepage README with Enhanced features, x64/ARM64 download links, upstream version/date, credits and clear separation of shipped versus deferred work.

- Maintainer accepted Preview 03; fast-forward its complete source history into `master`, now the main Enhanced branch.
- Publish an accepted release tagged `enhanced-v0.1.0`, with x64 and ARM64 portable ZIPs and checksums. Preserve identical compiled app binaries from the successful hosted preview builds.
- Include original SumatraPDF 3.7 source-snapshot version, upstream commit and 30 September 2026 commit date in release notes and package metadata.
- Carry forward all shipped features, known limitations and deferred tasks; add no new application-code changes in this promotion.

## Requested / deferred — 2026-10-01

- Add deferred UI-011 for Cupertino-inspired icons; update READ-003 to 25%–600% with keyboard/mouse zoom beyond the picker range. Include the upstream snapshot date in every release: 30 September 2026, 09:55:54 UTC for a0d8bca.

- Extend VOC-013 with word-wrapped answer options and other dictionary/vocabulary/practice text, variable-height rows and resize/font-size reflow. Deferred.

- Record deferred PEN-007: fix blinking laser strokes and expose a temporary-stroke lifetime in seconds.

- Record deferred PEN-006, VOC-014 and ANNO-001: illustrated pen-type controls, selected-text Dictionary/Meaning popup action and bundled/system font selection for free-text PDF annotations.

- Record deferred HOME-001: homepage overview of shipped Enhanced features, highlighting verified additions beyond upstream and distinguishing improvements to existing reader capabilities.

- Record deferred UI-008–UI-010 and VOC-010–VOC-013: dictionary title icon, complete Settings font scaling, guided tutorials, animated correct/incorrect feedback, project-hosted dictionary packs, themed rounded learning UI and lettered quiz options. App behavior is unchanged.

- Confirm GitHub-hosted Windows runners as the maintainer-selected build environment; workflow setup remains pending.

- Record READ-003: compact toolbar/Settings zoom choices from 25% to 1200%, retaining mouse zoom beyond that range. Implementation is deferred; current app behavior is unchanged.

## Preview 03 — 2026-10-01

### Distribution

- GitHub-hosted x64 and ARM64 Release builds passed in run 36825783482. Ship both native architecture ZIPs with upstream version/commit/date metadata. ARM64 device testing remains pending.

- Add the upstream-supported native ARM64 release mode and a GitHub-hosted x64/ARM64 build matrix with architecture-checked portable artifacts. Skip the inherited upstream publishing job on this fork.

- State the original SumatraPDF version and exact upstream commit in every release. Preview 03 uses the upstream 3.7 source snapshot at `a0d8bcaed0412ce803d9c213845e710fcbe3c7a5`.

- Package the first public Windows x64 portable preview with original licenses, offline dictionaries, full release notes and SHA-256 checksums; tag `enhanced-v0.1.0-preview.3`. This is a locally built preview, with manual UI/stylus acceptance pending.

### Appearance and navigation

- Bundle Manrope, Pretendard Std and Public Sans in Regular/SemiBold with SIL OFL notices; add an Appearance font selector alongside System. Fonts are private executable resources, not installed system-wide. PDF content fonts are unchanged.
- Apply interface font sizing/family to native popup menus and add independent UI icon sizing, recent-thumbnail count and minimum tab width. Tabs scroll when they exceed the available width. Settings resizes/scrolls at large font sizes.
- Replace core toolbar icons with attributed, rounded Lucide SVGs; improve muted icon contrast and theme refresh of document controls.
- Regenerate undersized recent thumbnails at higher resolution using bounded background work and keep existing previews while replacements render.
- Add an editable toolbar zoom percentage; default zoom above 100% advances in 25-percentage-point steps. Fit commands and explicit advanced zoom overrides remain available.

### Pen and annotations

- Pin annotation presets, including the same pen with different colors and widths; save favorites in application settings and remove them by right-click.
- Default pen thickness to 0.1–16 pt in 0.1 pt increments; expose custom minimum, maximum and step. Preserve fractional PDF border widths through save/reopen and annotation editing.
- Keep pen profile metadata in saved PDF ink so reopened annotations can be edited/erased, including on another Enhanced installation. Existing native PDF markup remains editable through upstream tools.
- Process coalesced stylus samples in order; reduce preview repaint area and repaint once per input batch. Fix rotated-page preview/eraser scaling.
- Enable configurable equation/citation hover previews for PDF destinations and supported numeric/author-year references; clear lookup state when the document changes.

### Offline dictionary and vocabulary

- Bundle the same eleven wmkeyboard study packs and 3,890 unique original pack meanings (all definition senses), preserving source hashes, originals and attribution. Prefer WM meanings during offline lookup; include licensed WordNet English as a fallback for ordinary words outside the study lists.
- Add selected-word Shift+D lookup in a native, themed dialog, direct word entry, explicit catalog downloads and validated local TSV/StarDict/WM JSON/gzip import. Lookup and practice do not send selected text online.
- Save definitions, dictionary identifiers, reading source/page/context, learned status and deck membership in a local vocabulary library. Add search, word commands, custom lists and attributed backup/import/export with bounded validation, atomic saves and protection for corrupt stores.
- Add a Home learning hub and dictionary action, eleven named study decks, word of the day, due review, study ahead and six activities: flashcards, meaning choice, word choice, spelling, word scramble and matching pairs. Persist SM-2/Leitner scheduling and review progress.
- Exercise exact wmkeyboard definition priority, actual bundled WordNet lookup, malformed dictionary parsing, vocabulary save/reopen/import/export, duplicate deck membership, quiz answers and scheduling through headless tests.

### Validation and limits

- Added headless regression coverage for zoom stops and saved/reopened ink profile, fractional width, editing and erasing without removing another stroke.
- The complete Preview 03 Debug and Release x64 builds passed with zero warnings and zero errors. All 104,861 headless checks passed, including the real bundled dictionary lookups and vocabulary persistence/scheduling tests. The earlier twelve-theme source check also passed. Previous-build symbols were preserved under a separate preview filename to resolve a locked PDB. Manual UI and hardware acceptance remains with the maintainer; no app GUI was launched for this work.
- Full per-point pressure/tilt, textured nibs, lasso and handwriting cleanup remain roadmap work. Scanned/unresolvable external citations cannot provide an automatic local preview. Physical pen latency has not been measured.

## Preview 02 — 2026-09-30

- Start from official SumatraPDF upstream `a0d8bcaed0412ce803d9c213845e710fcbe3c7a5`; fork `abelokoj/sumatrapdf`. Keep upstream reader/annotation integrations while porting Pretty's presentation into native Windows controls.
- Brand the app SumatraPDF Enhanced; add Pretty-style rounded toolbar groups, welcome hero, search, recent-document cards, Resume last and right-side day/night/document-inversion actions.
- Ship the twelve Pretty theme presets; correct the Purple Dark surface color and add a source regression check for supported theme colors.
- Expose interface/sidebar text size and home thumbnail size in main Settings; add +/- and Ctrl+wheel thumbnail sizing.
- Add expandable/collapsible pen settings with ballpoint, fountain, brush, pencil and highlighter profiles, color, thickness, stroke/highlight-only erasing and touch suppression while writing.
- Add temporary laser pointer solid trail, hollow trail and single-dot styles with preset/custom colors. Laser marks fade and are never saved as PDF annotations.
- Retain optional upstream WebView2 integrations; the native interface does not require the Microsoft Edge browser. Optional browser panels still need the separate WebView2 runtime.
- Support the installed Visual Studio 2022 Build Tools through the common build entry point. Debug and Release builds compiled successfully; hardware/visual acceptance remains pending.
- Commit `19a5c255c` on `feature/up-003-native-pretty-ui` was pushed to GitHub at the maintainer's request.

## Development plan — 2026-09-30

- Preserve the original feature roadmap, Samsung Notes parity priorities and upstream merge hygiene in `PrettySumatraPDF_TODO.md`.
- Document repository setup, task/branch naming, one-feature branches, CI/manual/release build strategy, artifact conventions, PR acceptance and first sprint. Recommended Windows workflow files remain explicit roadmap tasks.

## October 1, 2026: Private development record

Added DATA-001 for a user-selected storage folder for application data and notes. Prepared four release downloads, removed private records from public downloads and branch/tag history, and retained the local TODO. Hosted installer publication is pending.


Published the four v0.1.0 downloads from hosted build 36832302329, source 4e29def69. Validated package architectures and excluded private records. Generated blue, violet, coral and green logo previews; the application logo remains unchanged. Local compilation reached linking but could not write SumatraPDF.pdb; hosted builds passed for both architectures.

