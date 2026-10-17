// A hidden/redraw-suppressed frame must still apply each panel's own visibility.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { makeBookmarkedPdf } from "./toc-tree-sent-click.ts";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import { findVisibleChildWindow, getParentWindow, getWindowLong, GWL_STYLE, sendMessage } from "./winapi.ts";

const WM_SETREDRAW = 0x000b;
const WS_VISIBLE = 0x10000000;

export async function testit(): Promise<void> {
  const dir = tmpPath("sidebar-hidden-parent");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "bookmarks.pdf");
  writeFileSync(pdf, makeBookmarkedPdf());
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "NoHomeTab = true\nShowToc = true\nShowFavorites = false\nRestoreSession = false\nCheckForUpdates = false\n",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir, pdf]);
  try {
    await client.waitForRenderIdle();
    const tree = findVisibleChildWindow(frame, "SysTreeView32");
    if (!tree) throw new Error("sidebar-hidden-parent: initial bookmarks missing");
    const panel = getParentWindow(tree);
    sendMessage(frame, WM_SETREDRAW, 0, 0);
    let state = "";
    let ownVisible = false;
    try {
      sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
      const response = await client.request(ControlCommand.TestSidebarThumbnails, []);
      state = String(response[1] ?? "");
      ownVisible = (getWindowLong(panel, GWL_STYLE) & WS_VISIBLE) !== 0;
    } finally {
      sendMessage(frame, WM_SETREDRAW, 1, 0);
    }
    if (!/top=\d+,0,/.test(state) || ownVisible) {
      throw new Error(`sidebar-hidden-parent: hidden panel retained WS_VISIBLE=${Number(ownVisible)}; ${state}`);
    }
    sendCommandSync(frame, cmdId("CmdToggleBookmarks"));
    await client.waitForRenderIdle();
    if ((getWindowLong(panel, GWL_STYLE) & WS_VISIBLE) === 0)
      throw new Error("sidebar-hidden-parent: panel did not reopen");
    console.log("sidebar-hidden-parent: close/reopen preserves native visibility while the frame suppresses redraw");
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
