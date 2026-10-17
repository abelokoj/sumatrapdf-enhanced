// Bookmark filtering must survive sidebar relayouts, including empty matches.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { makeBookmarkedPdf } from "./toc-tree-sent-click.ts";
import { cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowToPng,
  countVisibleTreeRows,
  findChildWindow,
  findVisibleChildWindow,
  getClientRect,
  getControlText,
  getParentWindow,
  isWindowVisible,
  sendText,
} from "./winapi.ts";

export async function testit(): Promise<void> {
  const dir = tmpPath("toc-filter-layout");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "bookmarks.pdf");
  writeFileSync(pdf, makeBookmarkedPdf());
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    `NoHomeTab = true\nShowToc = true\nShowFavorites = false\nRestoreSession = false\nCheckForUpdates = false\nTheme = Sumatra ${process.env.SUMATRA_TOC_TEST_THEME ?? "Light"}\nInterfaceScale = ${process.env.SUMATRA_TOC_TEST_SCALE ?? "100"}\nUIFontSize = ${process.env.SUMATRA_TOC_TEST_FONT ?? "14"}\n`,
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir, "-window-pos", "1600x1050@20x20", pdf]);
  try {
    await client.waitForRenderIdle();
    const tree = await pollUntil(
      () => findVisibleChildWindow(frame, "SysTreeView32"),
      (hwnd) => hwnd !== 0,
    );
    const filter = findChildWindow(getParentWindow(tree), "Edit");
    if (!filter) throw new Error("toc-filter-layout: missing bookmarks search");
    for (const [text, expected] of [
      ["Three", 1],
      ["no matching bookmark", 0],
      ["", 4],
    ] as const) {
      sendText(filter, text);
      await pollUntil(
        () => countVisibleTreeRows(tree),
        (rows) => rows === expected,
        {
          error: (rows) => `toc-filter-layout: initial '${text}' has ${rows} rows, expected ${expected}`,
        },
      );
      sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
      await pollUntil(
        () => isWindowVisible(tree),
        (visible) => !visible,
      );
      sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
      await pollUntil(
        () => isWindowVisible(tree),
        (visible) => visible,
      );
      await client.waitForRenderIdle();
      const rows = countVisibleTreeRows(tree);
      if (rows !== expected) {
        throw new Error(`toc-filter-layout: '${text}' changed to ${rows} rows after hide/show, expected ${expected}`);
      }
      if (getControlText(filter) !== text) {
        throw new Error(`toc-filter-layout: '${text}' search changed during hide/show`);
      }
    }
    // Reload a different tab's model while the sidebar is hidden, then reopen
    // at the same size: no WM_SIZE is sent to repair the native tree.
    sendCommandSync(frame, cmdId("CmdDuplicateInNewTab"));
    await client.waitForRenderIdle();
    for (let i = 0; i < 12; i++) {
      sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
      await pollUntil(
        () => isWindowVisible(tree),
        (visible) => !visible,
      );
      sendCommandSync(frame, cmdId("CmdNextTab"));
      await client.waitForRenderIdle();
      if (!isWindowVisible(tree)) sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
      await pollUntil(
        () => isWindowVisible(tree),
        (visible) => visible,
      );
      await client.waitForRenderIdle();
      const rect = getClientRect(tree);
      if (countVisibleTreeRows(tree) !== 4 || rect.right <= rect.left || rect.bottom <= rect.top) {
        throw new Error(`toc-filter-layout: blank bookmarks after hidden tab switch ${i + 1}`);
      }
    }
    const captureDir = process.env.SUMATRA_TOC_CAPTURE_DIR;
    if (captureDir) {
      mkdirSync(captureDir, { recursive: true });
      const label = `${process.env.SUMATRA_TOC_TEST_THEME ?? "Light"}-${process.env.SUMATRA_TOC_TEST_SCALE ?? "100"}`;
      if (!captureWindowToPng(getParentWindow(tree), join(captureDir, `${label}-bookmarks.png`))) {
        throw new Error("toc-filter-layout: cannot capture the restored bookmarks panel");
      }
    }
    console.log("toc-filter-layout: filtered and empty bookmark views survive hide/show");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
