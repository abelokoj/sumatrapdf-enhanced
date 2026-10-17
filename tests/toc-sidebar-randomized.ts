// Closing/reopening sidebars during resize must place every native panel.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { makeBookmarkedPdf } from "./toc-tree-sent-click.ts";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowToPng,
  clientToScreen,
  countVisibleTreeRows,
  findChildWindow,
  getClientRect,
  getParentWindow,
  getWindowRect,
  getWindowLong,
  GWL_STYLE,
  isWindowVisible,
  moveWindow,
  showWindow,
  SW_MAXIMIZE,
  SW_RESTORE,
} from "./winapi.ts";

const SW_MINIMIZE = 6;
const WS_VISIBLE = 0x10000000;

export async function testit(): Promise<void> {
  const dir = tmpPath("toc-sidebar-randomized");
  mkdirSync(dir, { recursive: true });
  const pdf = process.env.SUMATRA_SIDEBAR_PDF ?? join(dir, "bookmarks.pdf");
  if (!process.env.SUMATRA_SIDEBAR_PDF) writeFileSync(pdf, makeBookmarkedPdf());
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "NoHomeTab = true\nShowToc = false\nShowFavorites = false\nSidebarDx = 369\nUIFontSize = 28\nScrollbars = windows\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir, "-window-pos", "1200x850@20x20", pdf]);
  const history: string[] = [];
  try {
    await client.waitForRenderIdle();
    // A restored session can request the sidebar while its frame is minimized.
    showWindow(frame, SW_MINIMIZE);
    sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
    showWindow(frame, SW_MAXIMIZE);
    await client.waitForRenderIdle();
    const initialTree = findChildWindow(frame, "SysTreeView32");
    const initialPanel = getParentWindow(initialTree);
    const initialRect = getClientRect(initialPanel);
    if (!isWindowVisible(initialTree) || initialRect.right < 50) {
      throw new Error(
        `toc-sidebar-randomized: restored sidebar has invalid geometry: ${JSON.stringify({ initialTree, initialPanel, initialRect })}`,
      );
    }
    showWindow(frame, SW_RESTORE);
    await client.waitForRenderIdle();
    let seed = 0x51debaf;
    const random = (n: number) => {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
      return seed % n;
    };
    for (let i = 0; i < 120; i++) {
      const width = 1000 + random(450);
      const height = 750 + random(280);
      moveWindow(frame, 20, 20, width, height);
      if (i % 7 === 0) {
        sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
        sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
      }
      if (i % 11 === 0) {
        sendCommandSync(frame, cmdId("CmdAnnotationMode"));
        sendCommandSync(frame, cmdId("CmdAnnotationMode"));
      }
      const bursts = 1 + random(5);
      for (let j = 0; j < bursts; j++) sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
      if (i % 3 === 0) sendCommandSync(frame, cmdId("CmdFavoriteToggle"));
      if (i === 5) sendCommandSync(frame, cmdId("CmdDuplicateInNewTab"));
      if (i > 5 && i % 4 === 0) sendCommandSync(frame, cmdId("CmdNextTab"));
      await client.waitForRenderIdle();
      if (i % 20 === 0) console.log(`sidebar stress step ${i}`);
      const response = await client.request(ControlCommand.TestSidebarThumbnails, []);
      const raw = String(response[1] ?? "");
      history.push(`${i}: resize ${width}x${height}, toggles=${bursts}; ${raw}`);
      if (!/top=\d+,1,/.test(raw) && !/bottom=\d+,1,/.test(raw)) {
        const canvasRect = getWindowRect(findCanvas(frame));
        const frameOrigin = clientToScreen(frame, 0, 0);
        if (canvasRect.left - frameOrigin.x > 10) {
          throw new Error(
            `toc-sidebar-randomized: closed sidebar left an empty column at step ${i}: ${JSON.stringify({ canvasRect, frameOrigin, raw })}`,
          );
        }
      }
      for (const name of ["top", "bottom"]) {
        const panel = new RegExp(`${name}=(\\d+),(\\d),(\\w+),`).exec(raw);
        if (!panel) continue;
        const desiredVisible = panel[2] === "1";
        const ownVisible = (getWindowLong(Number(panel[1]), GWL_STYLE) & WS_VISIBLE) !== 0;
        if (desiredVisible !== ownVisible)
          throw new Error(`toc-sidebar-randomized: native visibility differs at step ${i}: ${raw}`);
        if (!desiredVisible) continue;
        const hwnd = Number(panel[1]);
        const rect = getClientRect(hwnd);
        const tree = panel[3] === "bookmarks" ? findChildWindow(hwnd, "SysTreeView32") : 0;
        const blank =
          !isWindowVisible(hwnd) ||
          rect.right < 50 ||
          rect.bottom < 50 ||
          (panel[3] === "bookmarks" && (!tree || !isWindowVisible(tree) || countVisibleTreeRows(tree) === 0));
        if (blank) {
          const details = {
            panel: name,
            raw,
            rect,
            visible: isWindowVisible(hwnd),
            tree,
            treeParent: tree ? getParentWindow(tree) : 0,
            window: getWindowRect(hwnd),
          };
          writeFileSync(join(dir, "history.txt"), history.join("\n"));
          captureWindowToPng(frame, join(dir, "blank-sidebar.png"));
          throw new Error(`toc-sidebar-randomized: blank panel after step ${i}: ${JSON.stringify(details)}`);
        }
      }
    }
    console.log(`toc-sidebar-randomized: 120 resize/open/close/view/tab sequences passed`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
