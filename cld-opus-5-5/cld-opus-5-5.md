# SumatraPDF Enhanced: review and recommendations (Claude Opus 5.5)

Reviewed: `master` at `7843dce0a` (v0.1.2), 7 October 2026. I covered `src/` (364 files, about 213k lines in top-level `.cpp`), `docs/`, `CHANGELOG.md`, `PrettySumatraPDF_TODO.md`, `agents.md` and CI. Mockups are in `mockups/`. They are rendered from `mockups/generate_mockups.py`, so you can change the palette and regenerate them.

## 1. Verdict

The feature set is already ambitious. You have five pen profiles, three eraser modes, a lasso that can transform and recolor, a laser pointer, annotation recovery, dictionaries, a Learning Hub, AI panels, and a 13-file dark-mode engine. To feel polished and fast, the app does not need more features right now. It needs three things:

1. A real design system: tokens, spacing, type and one set of drawing primitives.
2. An ink pipeline whose cost does not grow with stroke length.
3. One render/cache path with a byte budget.

Do these before adding anything from Samsung Notes. Otherwise each new tool will make the existing inconsistencies worse.

The report is ordered by when to act: section 2 is what to do now to remove bloat, section 3 sets the coding standards every later change should follow, and sections 4 onward cover design, performance and new features.

## 2. Do now: remove bloat

These are cheap, reversible changes. Each one makes the app smaller, faster to build or easier to change, and none adds features. Do them in roughly this order on a `cleanup/debloat` branch, and record the size of `out/rel64/SumatraPDF.exe` before and after. It is 27.2 MB today, and the static build is 33.0 MB.

### 2.1 Put optional features behind build flags

1. **AI integrations (about 4.2k lines, 6 files).** `AIChatPanel`, `AIChatCommon`, `AIClaudeCode`, `AICodexBuild`, `AIGrokBuild` and `AIAntiGravity` are compiled into every build, and `CreateAIChatPanel` runs for every main window (`SumatraPDF.cpp:3397`).
   - Add an `ENHANCED_AI` flag in `BuildConfig.h`, defaulting to on.
   - Compile no-op stubs when it is off.
   - Create the panel only on first show.
   - Consider whether four separate coding-agent backends are needed, or whether one generic "external CLI agent" backend with a configurable command line covers them all. That would delete most of the per-vendor files.
2. **Learning and dictionary features (about 6k+ lines plus data).** These are `Vocabulary*`, `KaikkiDictionary`, `OfflineDictionary`, `DictionarySpeech` and `SelectionTranslate`. `VocabularyMeanings.h` (848 KB) and `VocabularyDecks.h` (168 KB) are compiled in as C arrays.
   - Add an `ENHANCED_LEARNING` flag.
   - Move both tables into a compressed data file loaded on first use, either as an RCDATA resource or a downloadable pack like the Kaikki dictionaries.
   - Compiling large tables as C++ source slows every build and puts the data into the executable image.
3. **A lean build target.** Add `bun cmd/build.ts -rel -lean` (all Enhanced extras off) to CI so the core reader is always shippable on its own, and report its size next to the full build.

### 2.2 Cut embedded assets

- **Fonts.** Three UI font families are embedded: Manrope, Pretendard and Public Sans, 6 files, about 1.2 MB (`SumatraPDF.rc:75-80`). Pretendard alone is about 900 KB.
  - Keep one UI family, or none: Segoe UI Variable ships with Windows 11.
  - Add one serif display face (subset to Latin, about 60 KB) only if you adopt the design direction in section 4.
  - Make extra families an optional download.
- **Themes.** The TODO already limits built-ins to twelve presets. Remove any leftover upstream theme data and code paths that the shipped builds no longer reach.
- **Wordnet data (`src/dictionaries/wordnet-en`, 21 MB).** Make sure it ships only as an optional pack and is never embedded into the main executable or the installer by default.

### 2.3 Remove duplicated code

These are listed in detail in sections 5 and 6. Each one deletes code rather than adding it:

- Merge `RenderCache` and `PageRenderService` into one pipeline.
- Replace the 4 window-corner functions and 38 hand-written rounded-rect paths with one primitive each.
- Use one DPI helper instead of `UiScalePx` + `DpiScale`.
- Replace the 4 hand-built `CreateCompatibleDC` buffers with `GfxCreateWithDoubleBuffer`.
- Replace symbol-font icon glyphs with `SvgIcons.cpp`.
- Route the 19 hard-coded "Segoe UI" font names through `ResolveUiFontName`.

### 2.4 Trim the default UI surface

- Ship about 14 toolbar buttons by default instead of about 49 (`Toolbar.cpp:446-497`). The rest stay available through pinning, the overflow menu and the command palette.
- Move Enhanced extras in `Menu.cpp` (about 293 commands) under a single **Tools** menu.
- Hide rarely used settings behind an "Advanced" category instead of showing them in the main Settings form.

### 2.5 Clean the repository

- Delete or move out of git:
  - `docs/scratch.txt` and `docs/temp-investigate-scroll-6154.md`.
  - The `tmp/` directory.
  - Any ad-hoc test script that has not run in the last two releases. There are 38 tracked `ad-hoc` files, plus many more under `tests/`.
- Keep a short `tests/README.md` that says which scripts are maintained and run in CI.
- `out/` holds 5.1 GB locally. Check that it is ignored by git (`.gitignore`), and add a `bun cmd/build.ts -clean` habit to the release checklist.
- Collapse `PrettySumatraPDF_TODO.md` into a short, current roadmap. The historical decisions inside it belong in `docs/decisions/` as one ADR per decision, so the live file stays readable.

### 2.6 Stop paying for idle work

- No timers while nothing animates. The laser timer runs at 30 ms (`Canvas.cpp:131`) and auto-scroll uses `USER_TIMER_MINIMUM` (`ReadingAutoScroll.cpp:229`); kill both timers the moment their effect ends.
- Cap the shutdown waits (10 s and 90 s, `SumatraPDF.cpp:19410-19479`) and remove the startup `Sleep` retries (`:16537`, `:17234-17267`) unless they are measured to be necessary.
- Give the per-engine dark-mode caches (64 MB each) one global budget, so ten open tabs do not reserve 640 MB.

**Definition of done for section 2:**
- The release exe is smaller, and the lean build exists and passes CI.
- Cold start is unchanged or faster, measured with `PerfLog`.
- No behavior changes for users, except the shorter default toolbar.

## 3. Coding practices to adopt

These rules fit this codebase as it already is: Win32, no STL, its own `src/base` containers, vendored MuPDF with patches. Put them in `agents.md` and a `docs/coding-standards.md`, so human contributors and AI agents follow the same rules.

### 3.1 Structure

- **Keep files and functions small.** No new file over about 2,000 lines and no function over about 80. Split the existing giants by responsibility:
  - `SumatraPDF.cpp` (19.5k): startup, instance IPC, window lifecycle, controller lifecycle, command dispatch.
  - `Canvas.cpp` (7.4k): input routing, paint, overlays (laser, lasso, reading bar), tool state.
  - `Toolbar.cpp` (6.2k): layout, paint, customization, overflow.
  - `HomePage.cpp` (4.7k): layout, cards/list, about.
  - `EngineMupdf.cpp` (10.9k): keep the engine itself, but move the dark-mode hooks into their own file.
- **Give each feature one boundary.** A feature should talk to the rest of the app through a narrow header: commands in, notifications out. It should not reach into `MainWindow` internals. AI, Learning and Read-aloud are the first candidates.
- **Keep Enhanced code separate from upstream code.** New Enhanced code goes in new files, or in `src/enhanced/` once the move is done. Changes to upstream files should be small hooks, so upstream merges stay easy. This is already the TODO's intent; enforce it in review.
- **Data-driven UI.** Toolbars, menus, the command palette and Settings should all come from one command table, holding the ID, label, icon, shortcut, availability check, and whether it appears on the toolbar by default. Today the same facts are repeated in `Toolbar.cpp`, `Menu.cpp`, `CommandPalette.cpp` and `CommandAvailability.cpp`.

### 3.2 Visual code

- **No magic numbers or colors in UI code.** Every color comes from a `Theme*()` token, and every size from `DesignTokens.h` passed through the single DPI helper. Enforce this with a source test (section 5) so violations fail CI.
- **One drawing path.** New UI code uses only the `Gfx` abstraction (Direct2D by default). No new direct GDI+ or GDI drawing outside `src/gui/`.
- **Every custom control is double-buffered and invalidates only what changed.** Never call `InvalidateRect(hwnd, nullptr, TRUE)` on the document canvas.
- **Never measure text while painting.** Measure during layout and cache the result by (string, font, DPI).

### 3.3 Performance rules

- **No heap allocation, file I/O or MuPDF document writes in input hot paths** (pointer move, wheel, timer ticks). Reuse window-owned buffers, and commit edits on pen-up or on a throttle.
- **Never block the UI thread.**
  - No `Sleep`, `WaitForSingleObject(INFINITE)` or synchronous network/disk work on it.
  - Long work goes to a shared worker pool with cancellation tokens, and results come back via `uitask::Post`.
  - Replace ad-hoc `CreateThread` calls feature by feature.
- **Give every cache a byte budget and an eviction rule.** Count limits are not enough.
- **Measure before and after optimizing.** Every performance change states its `PerfLog` numbers in the commit message.

### 3.4 Safety and correctness

- **Make ownership explicit.** Prefer the existing RAII helpers (`AutoFree`, `AutoDelete`, scoped handles) over manual `free`/`delete`.
  - No raw `new` without an owner.
  - Never delete inside a loop over the same container. The eraser loop at `AnnotPlacement.cpp:990/1008` is the pattern to remove: collect first, then delete.
- **Treat files as hostile input.** PDF, EPUB, CHM, downloaded dictionaries and AI responses are all untrusted.
  - Check sizes and bounds, and fail closed.
  - Keep the ASan CI run and add fuzzing (libFuzzer or WinAFL) for the new readers: Avif, Jxl, Webp, Kaikki and LIT.
- **Validate downloads.** The cpdf and dictionary downloads should pin a SHA-256 hash and use HTTPS only. Some of this already exists; make it the rule for every download.
- **Handle errors at boundaries.** Return `bool` or an error code from I/O and parsing. Log through `logf` once, close to the cause, and show the user one clear message rather than failing silently.
- **Keep MuPDF patches disciplined.** Every change under `ext/mupdf` gets its own patch file in `ext/patches/` in the same commit, as `agents.md` already requires; check this in CI.

### 3.5 Tests and review

- **Every bug fix ships with a test** that fails without it: a unit test in a debug build, or a scripted GUI test.
- **Unit-test pure logic:**
  - cache eviction
  - shape recognition
  - stroke smoothing
  - the color models and the color-format parser
  - the dark-mode classifier
  - settings parsing
- **Visual regression.** Screenshot home, reader and Settings in the light, dark and high-contrast themes at 100%, 150% and 200% scale, and diff against golden images in CI.
- **Performance gate in CI.** Time to first page, scrolling 100 pages, zoom steps and an ink stroke of 2,000 points. Fail on regressions over 15%.
- **Static analysis.** Keep CodeQL. Add `/analyze` (MSVC) or clang-tidy with a small, agreed rule set, run with warnings treated as errors on new files only.
- **Small commits with one purpose each.** Commit messages say why. A pull request that touches an upstream file explains why a hook was not enough.

## 4. Design direction (Anthropic-style)

The Anthropic visual language suits a reading tool well. It is warm and calm, typography leads, there is a lot of space, there is a single accent color, and nothing competes with the document.

| Token | Light | Dark | Use |
|---|---|---|---|
| `bg` | `#F5F2EA` | `#1F1E1D` | window, chrome, sidebar |
| `surface` | `#FFFFFF` | `#2A2927` | toolbars, cards, popovers |
| `surface-2` | `#EDE8DD` | `#353330` | tracks, inactive chips |
| `line` | `#DDD6C8` | `#45423D` | 1px hairlines only |
| `ink` | `#1F1E1C` | `#F2EFE8` | primary text |
| `ink-2` | `#6B675F` | `#A39E94` | secondary text, idle icons |
| `accent` | `#C96442` | `#D97757` | one primary action, active tool, focus |
| `accent-soft` | `#F3E1D8` | `#4A3229` | active tool background, selection |
| `canvas` | `#E9E4D8` | `#191817` | area behind pages |

The rules behind the palette:

- **Use only one accent.** The current gold primary and green logo should become a single clay accent. Highlighter yellow belongs to the content, not the chrome.
- **Separate surfaces with hairlines, not shadows.** Allow one soft shadow, and only on floating popovers.
- **Typography.** Use a serif (bundle something like Source Serif 4 or Literata, both OFL) for display text: the home greeting, section titles and dialog titles. Use Segoe UI Variable for everything else. The scale is 11 / 12.5 / 13.5 / 16 / 20 / 40 px at 100%.
- **Spacing scale:** 4, 8, 12, 16, 24, 32. **Radii:** 6 for inputs, 8 for buttons, 12 for toolbar groups, 14–18 for popovers and cards.
- **Icons.** Use 16px outline icons with a 1.6px stroke and round caps, all from `SvgIcons.cpp`. Retire every "Segoe UI Symbol" glyph used as an icon.
- **Motion.** Fades and slides of 120–160 ms with ease-out, for popovers only. Never animate the page.

See `mockups/01-reader-light-annotating.png`, `02-reader-dark-annotating.png`, `04-home-light.png` and `05-home-dark.png`.

### Reader layout (mockup 01/02)

- **Left:** a sidebar toggle, the page field and the zoom field.
- **Centre:** one rounded tool group: Hand, Select, Pen, Highlighter, Eraser, Lasso, Shape, Text, Note.
- **Right:** Undo, Redo, Search, Theme and an overflow menu (`…`).

Everything else should live in the command palette (Ctrl+K) or in the overflow menu. `gToolbarButtons` in `Toolbar.cpp:446-497` currently has about 49 entries. Ship roughly 14 by default and let users pin more; the pin mechanism already exists.

When a pen is active, a favorites strip floats under the tool group. It holds up to 8 presets plus a thickness slider and stays out of the way. A vertical mini-rail on the right holds the Samsung-style extras: ruler, writing pad, voice note and shape mode. The status bar uses quiet language ("Autosaved to recovery copy", "Pen-only mode · Finger scrolls") so the system state is always visible.

### Home (mockup 04/05)

- A serif greeting, then one search field that covers files, commands and full text (Ctrl+K), then one accent "Open file" button and a secondary "Resume …" button.
- Recent files show as cards with a reading-progress bar.
- Remove the multi-badge title fitting in `HomePage.cpp:376-436`; it is fragile once the UI is localized.

## 5. Design-system work (highest leverage for "polish")

The UI audit found the following:

- **About 321 hardcoded color literals.** The worst files are `AnnotEditToolbar.cpp` (30), `Toolbar.cpp` (24), `VocabularyDialog.cpp` (19), `Canvas.cpp` (19), `AnnotPlacement.cpp` (16) and `HomePage.cpp` (13). Move them all onto `Theme*()` tokens, and add a source test that fails on new literals outside `Theme.cpp` and `ChangeColorDialog.cpp`. You already have a similar test for theme color formats.
- **No shared spacing or radius scale.** Each file defines its own `k*Pad`/`k*Radius` constants; HomePage alone has 10. Add a `src/DesignTokens.h` with `Space::S1…S6`, `Radius::Input/Button/Group/Popover` and `Type::Caption…Display`.
- **Two DPI helpers.** `UiScalePx` is used 578 times and `DpiScale` 363 times. Pick one that is aware of per-monitor DPI and the user's UI scale, and wrap every constant in it at the point of use.
- **Hardcoded fonts.** "Segoe UI" appears 19 times in 9 files, for example `VocabularyDialog.cpp:4178`, which also creates an unscaled 36px font. Route all of them through `ResolveUiFontName`.
- **Duplicated drawing code.** There are 38 hand-rolled rounded-rect paths and 4 separate window-corner functions (`DarkMode.cpp:213`, `RefHoverPopup.cpp:72`, `SumatraPDF.cpp:3425`, `ScreenshotCapture.cpp:311`). Replace them with one `GfxFillRoundRect` / `GfxStrokeRoundRect` and one `ApplyWindowCorners(hwnd, kind)`.
- **Mixed graphics backends.** GDI+ appears 439 times in 23 files while D2D is the default backend (`gui/Gfx.cpp:346`). Move new code to the `Gfx` abstraction only, and port the chrome surfaces (toolbar, tabs, home, popovers) to D2D. That gives you consistent antialiasing and is GPU-composited.
- **Inconsistent double buffering.** `GfxCreate` is used without a buffer 15 times, and there are 4 hand-built `CreateCompatibleDC` buffers (`Toolbar.cpp:5768/5873`, `HomePage.cpp:4078`, `OverlayScrollbar.cpp:322/1498`). Use `GfxCreateWithDoubleBuffer` everywhere.
- **Settings dialog.** `SettingsDialog.cpp` is 2,434 lines of imperative layout. Rebuild it from a data table of `{category, label, help, type, getter, setter}`. Add a left category rail (General, Appearance, Reading, Annotation & Pen, Dictionary & Learning, AI, Advanced) and a search box. This also makes it trivial to expose settings that are currently reachable only through the advanced text file.
- **Menus.** There are about 293 commands in `Menu.cpp`. Keep top-level menus short (File, Edit, View, Annotate, Tools, Help) and move the Enhanced extras under Tools. The palette remains the expert path.

## 6. Performance and responsiveness

These are ordered by how much they help.

1. **The ink stroke redraw gets slower as the stroke grows.**
   - The dirty rectangle only ever grows (`AnnotPlacement.cpp:940-944`).
   - Every paint redraws every point (`:1518-1565`), and `UpdateWindow` runs on every pen move (`Canvas.cpp:6211`).
   - Fix: render the in-progress stroke into a cached ARGB layer, add only the new segment each move, and invalidate only that segment's bounds plus the pen width. Present at the display refresh rate rather than per `WM_POINTERUPDATE`.
   - Longer term: use the Windows Ink presenter (`IInkD2DRenderer` / `DCompositionInkTrails` on DirectComposition) for low-latency wet ink with prediction.
2. **The ink hot path allocates memory.**
   - `Canvas.cpp:6194` allocates a history vector per message.
   - `PointerInput.h:163` copies about 10–15 KB on the stack per call.
   - Fix: reuse a buffer that belongs to the window. Keep sub-pixel HIMETRIC coordinates instead of rounding to integer pixels (`AnnotPlacement.cpp:925`).
3. **The eraser is expensive per move.**
   - Each move scans every annotation, re-reads the ink list, calls `pdf_update_annot` and does a full `MainWindowRerender` (`AnnotPlacement.cpp:976-1016`, `Annotation.cpp:2310-2334`).
   - Fix: cull with a per-page spatial index (a bounding-box grid is enough), hide erased segments visually during the drag, and commit once on pen-up.
   - Also fix the delete-while-iterating risk at `AnnotPlacement.cpp:990/1008`.
4. **Every stroke becomes its own annotation** (`AnnotPlacement.cpp:1071`). That bloats the PDF and makes hit-testing slower. Merge strokes made with the same tool and style into one `/Ink` annotation within a 1.5 s and same-page window, the way Acrobat and Samsung Notes behave. Undo should remain per stroke, via the journal.
5. **RenderCache is limited by count, not bytes** (`kMaxBitmapsCached = 128`, `RenderCache.h:16`). At high zoom this can reach gigabytes, and `Add()` silently drops bitmaps (`RenderCache.cpp:374`). Switch to a byte budget, for example `min(1.5 GB, 25% of physical RAM)`, and evict by distance from the viewport.
6. **There are two render pipelines.** `RenderCache` and `PageRenderService` (`PageRenderService.cpp:133`) use different eviction rules. Merge them, using `PageRenderPolicy`.
7. **Dark-mode caches are per engine.** Each engine has its own 64 MB cap (`PdfDarkModeEngineCache.cpp:15`). Make it one global budget, stop recomputing the options hash on every page (`EngineMupdf.cpp:6992`), and classify images on a downscaled copy.
8. **Full-canvas invalidation for overlays.** The laser timer at 30 ms (`Canvas.cpp:131`, `Toolbar.cpp:3882`), lasso drag (`Canvas.cpp:2412`), the reading bar (`ReadingBar.cpp:171`) and 15 calls in `VocabularyDialog` all invalidate the whole canvas. Draw overlays on a separate layered child window or DComp visual so the page bitmap is never re-blitted for a cursor effect.
9. **Auto-scroll** uses `SetTimer(USER_TIMER_MINIMUM)` (`ReadingAutoScroll.cpp:229`). Switch to time-delta stepping driven by `DwmFlush` or a waitable timer.
10. **Shutdown and startup stalls.**
    - Shutdown can wait up to 10 s and 90 s (`SumatraPDF.cpp:19410-19479`). Cap these and use a cancellation flag.
    - At startup, `Sleep(100)` (`:16537`) and the DLL load retries (`:17234-17267`) can sleep for seconds.
11. **Measuring stalls.** `HangDetector` only reports stalls of 3 s or more. Add a 100 ms / 250 ms "jank" tier that writes to `PerfLog`, and add a CI perf smoke test that measures time to first page, scrolling 100 pages and zooming in five steps, failing on regressions over 15%.
12. **Large translation units.** Split `SumatraPDF.cpp` (19.5k lines, about 576 functions) into `AppStartup.cpp`, `InstanceIpc.cpp`, `WindowLifecycle.cpp`, `ControllerLifecycle.cpp` and `CommandDispatch.cpp`. Builds and upstream merges get faster.
13. **No feature flags.** The AI, Vocabulary and Translate modules have none. Add `ENHANCED_AI` / `ENHANCED_LEARNING` build switches and create panels lazily on first show. This keeps a "lean" build possible and shrinks the cold-start working set.

**Targets to adopt:**

| Metric | Target |
|---|---|
| Cold start to first page | ≤ 350 ms on a 100-page PDF |
| Pen-to-photon latency | ≤ 20 ms on 60 Hz, ≤ 12 ms on 120 Hz |
| Frame time while inking or scrolling | ≤ 8 ms |
| Idle CPU | 0% (no timers when nothing animates) |

## 7. Samsung Notes features worth bringing over

This is what the code already has compared with Samsung Notes, and what to add, in priority order.

| # | Feature | Status today | Recommendation |
|---|---|---|---|
| 1 | Pen favorites bar | Only one profile per pen type | Up to 8 saved presets of {type, color, width, opacity}, shown in the floating strip. Tap selects; long-press or right-click edits. Store them in settings. See mockups 01 and 03. |
| 2 | Hold-to-shape | Missing; only Shift-snap on the line tool | After the pen stays still for 450 ms at the end of a stroke, recognise a line, arrow, rectangle, ellipse, triangle or polygon. Use corner detection plus a least-squares fit, then morph the stroke into the shape with a small "Snapped to rectangle" chip and Undo. One more hold keeps the original ink. |
| 3 | Pressure-variable width | Width comes from average pressure, only for Fountain and Brush (`AnnotPlacement.cpp:1498`) | Keep pressure for each point. Render a variable-width outline (the perfect-freehand approach) and store it as `/Ink` plus a custom appearance stream so other readers still see it. This also fixes the mid-stroke width jump and the dropped zero-pressure samples (`:1847`). |
| 4 | Stroke smoothing | A fixed 0.6 low-pass filter on Brush only | 1€ filter while drawing (low lag), then Catmull-Rom or Bézier fitting plus Ramer-Douglas-Peucker thinning on pen-up. Expose a smoothing slider in the pen panel. |
| 5 | Highlighter behind ink, snapping to text | The text highlighter exists; the ink highlighter neither snaps nor layers | Ink highlighter: when the stroke runs roughly along a text line, snap it to that line's quads and produce a `/Highlight` annotation. Always paint highlights before ink, by z-ordering in the appearance or the annotation order. |
| 6 | Eraser size and lasso erase | The radius is fixed at 10 px | Add a size slider (4–60 px) with a cursor ring showing the size, keep the three modes, and add "erase everything inside the lasso". |
| 7 | Better lasso | Move, resize, rotate, recolor, thickness, duplicate | Add copy and paste of the whole group (also to the clipboard as PNG plus PDF), selection across pages, "Convert to text", "Change pen style" and a floating action bar above the selection. |
| 8 | Barrel button and finger scrolling | The barrel button is detected but unused | Default: barrel plus drag = lasso, matching S Pen behavior; make it configurable. In pen-only mode a finger always pans and pinch always zooms. Show that state in the status bar. |
| 9 | Writing pad / zoom pad | On the roadmap | A docked magnifier strip at the bottom of the window. Writing in it lands at the reading position on the page, which advances automatically at the end of a line. This is the feature Samsung Notes users love most for annotating dense PDFs. |
| 10 | Ruler | Missing | A draggable, rotatable straight edge (two-finger rotate, 15° detents). While the pen is within 12 px of it, strokes snap to the edge. Add a protractor later. |
| 11 | Insert blank or grid pages | Missing | "Insert page after" with Blank, Lined, Grid or Dotted options, via MuPDF `pdf_insert_page`. Pairs naturally with Merge PDF. |
| 12 | Voice note anchored to the page | Missing | Record to Opus/WAV, embed it as a `/Sound` or `FileAttachment` annotation with a small chip on the page, and play back with a timeline. Optional: synchronized ink playback. |
| 13 | Handwriting to text | Missing | Windows Ink Analysis (`IInkAnalyzer`) works locally and offline. Lasso the ink, choose "Convert to text", and get a FreeText annotation with the original ink kept as hidden or optional. |
| 14 | Strokes across page boundaries | Strokes are clipped at the page (`AnnotPlacement.cpp:922`) | Split the stroke at the boundary into two linked annotations. |
| 15 | Masking tape | Missing | An opaque, tap-to-reveal rectangle for self-quizzing. Its data model is a Square annotation with a custom flag. This fits the Learning Hub. |

Use mockup 03 as the pen settings popover. It shows the five pen types as large tiles, a live preview, sliders for thickness, opacity and pressure sensitivity, a color row, and "Add to favorites".

## 8. Quality, tests and process

- **Tests.** There are about 500 ad-hoc `.ts` GUI scripts in `tests/`, but few run in CI. Promote a smoke subset (open, ink, erase, undo, save, reopen) to `windows-daily.yml`. Add C++ unit tests for cache eviction, shape recognition, the smoothing filters and `PdfDarkModeImageClassifier`.
- **Visual regression.** Take headless screenshots of the home page, reader and Settings in the light, dark and high-contrast themes at 100%, 150% and 200% scale, and diff them against golden images. Most of the visual problems listed in the changelog, such as duplicate borders and pale corners, would have been caught this way.
- **Accessibility.** Every custom-painted control needs UIA names and roles (`src/uia/` exists, so extend it), a visible 2px accent focus ring, at least 4.5:1 contrast for `ink-2` (the proposed light value `#6B675F` on `#F5F2EA` is about 5.2:1), and 32×32 px hit targets at 100% scale (40×40 in touch mode).
- **Threads.** Each feature starts its own thread (UpdateCheck, ReadAloud, Translate, Vocabulary, and others). Introduce a small shared worker pool with cancellation tokens.
- **Changelog tone.** The changelog is detailed and good. Consider adding a "Known limitations" section for each release.

## 9. Suggested roadmap

| Phase | Weeks | Scope |
|---|---|---|
| 0 Debloat now | 1 | Everything in section 2: build flags, asset cuts, repo cleanup, idle-work fixes; adopt section 3 standards |
| 1 Foundation | 2–3 | Design tokens header, colour-literal lint, one DPI helper, one rounded-rect primitive, double buffering everywhere |
| 2 Fast ink | 3–4 | Incremental wet-ink layer, no allocations in the hot path, sub-pixel points, eraser spatial index plus commit on pen-up, stroke merging |
| 3 Chrome redesign | 5–6 | New toolbar group, overflow menu, home page, Settings rebuilt from data with search; mockups 01–05 |
| 4 Samsung core | 7–9 | Pen favorites, hold-to-shape, pressure-variable width, smoothing, highlighter snap and z-order, eraser size, barrel button |
| 5 Render core | 8–10 (parallel) | Byte-budget cache, merged render pipelines, global dark-mode budget, overlay layer, jank telemetry, perf CI |
| 6 Samsung extended | 11–14 | Writing pad, ruler, insert pages, lasso group actions, handwriting to text, voice notes, masking tape |

## Files in this folder

- `cld-opus-5-5.md`: this report.
- `mockups/01-reader-light-annotating.png/.svg`: the reader with tool group, pen favorites strip, text-snapped highlight, shape-snap chip and Samsung tools rail.
- `mockups/02-reader-dark-annotating.png/.svg`: the same screen in warm dark.
- `mockups/03-pen-settings-panel.png/.svg`: the pen settings popover.
- `mockups/04-home-light.png/.svg` and `05-home-dark.png/.svg`: the home screen.
- `mockups/generate_mockups.py`: regenerates all mockups (`pip install cairosvg`).
