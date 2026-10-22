import { ptr } from "bun:ffi";
import { join } from "node:path";
import { mkdirSync, writeFileSync } from "node:fs";
import { assemblePdf, pollUntil, runStandalone, tmpPath, writeAppdata } from "./util.ts";
import { launchControlled, killAndWait, waitForTitle } from "./win-automation.ts";
import {
  enumWindows,
  enumChildWindows,
  getWindowPid,
  getClassName,
  getClientRect,
  getWindowText,
  isWindowVisible,
  sendMessage,
  postMessage,
  packCoords,
  captureWindowToPng,
} from "./winapi.ts";

function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`open-document-search: ${message}`);
}
function setText(hwnd: number, value: string) {
  const text = new Uint16Array([...value].map((c) => c.charCodeAt(0)).concat(0));
  sendMessage(hwnd, 0x000c, 0, ptr(text));
}
function popupFor(pid: number) {
  let popup = 0,
    edit = 0,
    list = 0;
  enumWindows((hwnd) => {
    if (getWindowPid(hwnd) !== pid || getClassName(hwnd) !== "SumatraScrollableMenu" || !isWindowVisible(hwnd))
      return true;
    popup = hwnd;
    enumChildWindows(hwnd, (child) => {
      if (getClassName(child) === "Edit") edit = child;
      if (getClassName(child).toLowerCase() === "listbox") list = child;
      return true;
    });
    return false;
  });
  return { popup, edit, list };
}
async function openPicker(frame: number, pid: number) {
  let tabs = 0;
  enumChildWindows(frame, (hwnd) => {
    if (getClassName(hwnd) === "SumatraTabsCtrlClass") tabs = hwnd;
    return true;
  });
  assert(tabs, "tab strip missing");
  const rect = getClientRect(tabs);
  postMessage(tabs, 0x0201, 1, packCoords(rect.right - 8, Math.floor(rect.bottom / 2)));
  postMessage(tabs, 0x0202, 0, packCoords(rect.right - 8, Math.floor(rect.bottom / 2)));
  return pollUntil(
    () => popupFor(pid),
    (p) => !!p.popup && !!p.edit && !!p.list,
    { error: (p) => "searchable document dropdown did not open: " + JSON.stringify({ ...p, rect }) },
  );
}
export async function testit(): Promise<void> {
  const dir = tmpPath(`open-document-search-${process.pid}`);
  mkdirSync(dir, { recursive: true });
  const pdf = assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
  ]);
  const files = Array.from({ length: 22 }, (_, i) => join(dir, `Alpha ${String(i).padStart(2, "0")}.pdf`));
  files[2] = join(dir, "Target & 02.pdf");
  for (const file of files) writeFileSync(file, pdf);
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    const settings = writeAppdata(
      `open-search-${process.pid}-${theme}`,
      `UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReuseInstance = false\nTheme = ${theme}\n`,
    );
    const app = await launchControlled(["-appdata", settings, ...files]);
    try {
      await waitForTitle(app.frame, (t) => t.includes("Alpha 21"));
      await app.client.waitForRenderIdle();
      let ui = await openPicker(app.frame, app.proc.pid!);
      const allCount = Number(sendMessage(ui.list, 0x018b, 0, 0));
      assert(allCount >= files.length, "not all open documents listed");
      setText(ui.edit, "TARGET & 02");
      assert(Number(sendMessage(ui.list, 0x018b, 0, 0)) === 1, "case-insensitive filename search failed");
      const capture = process.env.SUMATRA_TAB_SEARCH_CAPTURES;
      if (capture) {
        mkdirSync(capture, { recursive: true });
        await captureWindowToPng(ui.popup, join(capture, theme.endsWith("Dark") ? "dark.png" : "light.png"));
      }
      postMessage(ui.edit, 0x0100, 13, 0);
      await waitForTitle(app.frame, (t) => t.includes("Target & 02"));
      ui = await openPicker(app.frame, app.proc.pid!);
      assert(getWindowText(ui.edit) === "", "search unexpectedly persisted across openings");
      setText(ui.edit, "no such filename");
      assert(Number(sendMessage(ui.list, 0x018b, 0, 0)) === 0, "empty search result failed");
      postMessage(ui.edit, 0x0100, 13, 0);
      await Bun.sleep(60);
      assert(isWindowVisible(ui.popup), "Enter on no matches closed picker");
      setText(ui.edit, "");
      assert(Number(sendMessage(ui.list, 0x018b, 0, 0)) === allCount, "clearing did not restore documents");
      setText(ui.edit, "Alpha 17");
      const row = new Int32Array(4);
      sendMessage(ui.list, 0x0198, 0, ptr(row));
      const point = packCoords(20, Math.floor((row[1]! + row[3]!) / 2));
      postMessage(ui.list, 0x0201, 1, point);
      postMessage(ui.list, 0x0202, 0, point);
      await waitForTitle(app.frame, (t) => t.includes("Alpha 17"));
      ui = await openPicker(app.frame, app.proc.pid!);
      postMessage(ui.edit, 0x0100, 27, 0);
      await pollUntil(
        () => isWindowVisible(ui.popup),
        (visible) => !visible,
        { error: "Escape did not dismiss picker" },
      );
      assert(getWindowText(app.frame).includes("Alpha 17"), "Escape changed the active document");
      console.log(`${theme}: filtering, keyboard/mouse selection, empty/clear and cancellation passed`);
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
