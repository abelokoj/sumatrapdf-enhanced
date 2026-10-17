// Palette and shortcut help expose native sizing edges and keep their content
// usable when the window is resized independently along either axis.
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { cmdId, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  enumWindows,
  findChildWindow,
  getClassName,
  getControlText,
  getClientRect,
  getFocusedHwnd,
  getParentWindow,
  getWindowLong,
  getWindowPid,
  getWindowRect,
  getWindowText,
  GWL_STYLE,
  WS_THICKFRAME,
  packCoords,
  postMessage,
  sendMessage,
  sendText,
  setWindowPos,
  sleep,
  WM_CLOSE,
} from "./winapi.ts";

async function waitWindow(find: () => number): Promise<number> {
  for (let i = 0; i < 80; i++) {
    const hwnd = find();
    if (hwnd) return hwnd;
    await sleep(50);
  }
  throw new Error("native-window-resize: window did not open");
}

function checkResize(hwnd: number, label: string): void {
  if (!(getWindowLong(hwnd, GWL_STYLE) & WS_THICKFRAME)) {
    throw new Error(`${label}: missing native resize frame`);
  }
  const r = getWindowRect(hwnd);
  const x = Math.round((r.left + r.right) / 2),
    y = Math.round((r.top + r.bottom) / 2);
  const points = [
    [r.left + 1, y, 10],
    [r.right - 2, y, 11],
    [x, r.top + 1, 12],
    [x, r.bottom - 2, 15],
  ];
  for (const [px, py, expected] of points) {
    const got = Number(sendMessage(hwnd, 0x84, 0, packCoords(px!, py!)));
    if (got !== expected) throw new Error(`${label}: edge returned ${got}, expected ${expected}`);
  }
  const w = r.right - r.left,
    h = r.bottom - r.top;
  for (const [width, height] of [
    [w + 100, h],
    [w + 100, Math.max(400, h - 150)],
    [Math.max(650, w - 200), h],
  ]) {
    if (!setWindowPos(hwnd, r.left, r.top, width!, height!)) throw new Error(`${label}: resizing failed`);
    const resized = getWindowRect(hwnd);
    if (resized.right - resized.left !== width || resized.bottom - resized.top !== height) {
      throw new Error(`${label}: independent dimensions did not match requested size`);
    }
    const c = getClientRect(hwnd);
    if (c.right <= 0 || c.bottom <= 0) throw new Error(`${label}: empty client after resize`);
  }
}

export async function testit(): Promise<void> {
  const dir = tmpPath("native-window-resize");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nRememberOpenedFiles = false\n",
  );
  const { proc, client, frame } = await launchControlled(["-appdata", dir]);
  try {
    sendCommandSync(frame, cmdId("CmdCommandPalette"));
    const palette = await waitWindow(() => {
      const focus = getFocusedHwnd(frame);
      const parent = focus && getClassName(focus) === "Edit" ? getParentWindow(focus) : 0;
      return parent && parent !== frame ? parent : 0;
    });
    const query = findChildWindow(palette, "Edit");
    sendText(query, "> zoom");
    await sleep(100);
    const focusBefore = getFocusedHwnd(frame);
    const queryBefore = getControlText(query);
    checkResize(palette, "Command Palette");
    const edit = findChildWindow(palette, "Edit");
    if (!edit) throw new Error("palette query disappeared after resize");
    if (focusBefore === edit && getFocusedHwnd(frame) !== edit)
      throw new Error("palette query lost focus after resize");
    if (focusBefore !== edit)
      console.log("palette focus assertion not exercised: background query was not focused before resizing");
    if (getControlText(edit) !== queryBefore)
      throw new Error(
        `palette query changed after resize before=${JSON.stringify(queryBefore)} after=${JSON.stringify(getControlText(edit))}`,
      );
    if (queryBefore !== "> zoom") throw new Error(`palette query setup failed: ${JSON.stringify(queryBefore)}`);
    const paletteRect = getWindowRect(palette),
      editRect = getWindowRect(edit);
    if (
      editRect.left < paletteRect.left ||
      editRect.right > paletteRect.right ||
      editRect.bottom > paletteRect.bottom
    ) {
      throw new Error("palette query escaped its resized window");
    }
    postMessage(palette, WM_CLOSE, 0, 0);
    await sleep(150);
    sendCommandSync(frame, cmdId("CmdToggleKeyboardHelp"));
    const help = await waitWindow(() => {
      let found = 0;
      enumWindows((hwnd) => {
        if (getWindowPid(hwnd) === proc.pid && getWindowText(hwnd) === "Keyboard Shortcuts") found = hwnd;
        return true;
      });
      return found;
    });
    checkResize(help, "Keyboard Shortcuts");
    console.log(
      "native-window-resize: native edges and independent geometry passed; actual pointer dragging remains manual",
    );
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
