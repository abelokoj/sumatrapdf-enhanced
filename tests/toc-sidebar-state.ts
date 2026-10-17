// Showing a populated bookmarks sidebar must not rebuild its native tree.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  countVisibleTreeRows,
  findVisibleChildWindow,
  getParentWindow,
  isWindowVisible,
  sendMessage,
  treeExpand,
  treeGetNextItem,
  treeGetRoot,
  treeGetSelection,
  TVE_COLLAPSE,
  TVGN_NEXT,
  TVM_SELECTITEM,
  TVGN_CARET,
} from "./winapi.ts";

const TVGN_FIRSTVISIBLE = 0x5;

function bookmarkedPdf(): string {
  const objs = [
    "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R /PageMode /UseOutlines >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
    "<< /Type /Outlines /First 5 0 R /Last 74 0 R /Count 71 >>",
  ];
  for (let i = 0; i < 70; i++) {
    const prev = i ? ` /Prev ${i + 4} 0 R` : "";
    const next = i < 69 ? ` /Next ${i + 6} 0 R` : "";
    const child = i === 0 ? " /First 75 0 R /Last 75 0 R /Count 1" : "";
    objs.push(`<< /Title (Chapter ${i + 1}) /Parent 4 0 R${prev}${next}${child} /Dest [3 0 R /Fit] >>`);
  }
  objs.push("<< /Title (Nested bookmark) /Parent 5 0 R /Dest [3 0 R /Fit] >>");
  return assemblePdf(objs);
}

export async function testit(): Promise<void> {
  const dir = tmpPath("toc-sidebar-state");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "bookmarks.pdf");
  writeFileSync(pdf, bookmarkedPdf(), "latin1");
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "NoHomeTab = true\nShowToc = true\nShowFavorites = false\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const { proc, client, frame } = await launchControlled([
    "-appdata",
    dir,
    "-log-to-file",
    join(dir, "native.log"),
    pdf,
  ]);
  try {
    await client.waitForRenderIdle();
    const tree = await pollUntil(
      () => findVisibleChildWindow(frame, "SysTreeView32"),
      (hwnd) => hwnd !== 0,
    );
    // Complete the first visible panel paint before storing native item handles.
    sendMessage(getParentWindow(tree), 0xf, 0, 0);
    await client.waitForRenderIdle();
    const root = treeGetRoot(tree);
    treeExpand(tree, TVE_COLLAPSE, root);
    let first = root;
    for (let i = 0; i < 45; i++) {
      first = treeGetNextItem(tree, TVGN_NEXT, first);
    }
    sendMessage(tree, TVM_SELECTITEM, TVGN_CARET, root);
    sendMessage(tree, TVM_SELECTITEM, TVGN_FIRSTVISIBLE, first);
    await client.waitForRenderIdle();
    const beforeSelection = treeGetSelection(tree);
    const beforeTop = treeGetNextItem(tree, TVGN_FIRSTVISIBLE);
    const beforeRows = countVisibleTreeRows(tree);
    if (!beforeSelection || beforeRows !== 70) {
      throw new Error("toc-sidebar-state: initial selection or collapsed branch was not established");
    }
    for (let i = 0; i < 3; i++) {
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
      if (
        treeGetRoot(tree) !== root ||
        treeGetSelection(tree) !== beforeSelection ||
        treeGetNextItem(tree, TVGN_FIRSTVISIBLE) !== beforeTop ||
        countVisibleTreeRows(tree) !== beforeRows
      ) {
        throw new Error(
          `toc-sidebar-state: hide/show changed state: ${JSON.stringify({ before: { root, selected: beforeSelection, top: beforeTop, rows: beforeRows }, after: { root: treeGetRoot(tree), selected: treeGetSelection(tree), top: treeGetNextItem(tree, TVGN_FIRSTVISIBLE), rows: countVisibleTreeRows(tree) } }, (_key, value) => (typeof value === "bigint" ? String(value) : value))}`,
        );
      }
    }
    console.log("toc-sidebar-state: selection, scroll, and expansion survive repeated hide/show");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
